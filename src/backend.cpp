#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

#include "layout.h"
#include "protocol.h"

namespace {

constexpr int kUndoLimit = 100;
constexpr int kSavedColorSlots = 8;

const QList<Step> defaultSteps(const QString &type) {
    if (type == "pulse")
        return {{QColor("#FFFFFF"), 60}, {QColor("#000000"), 60}};
    return {{QColor("#00E5FF"), 40}, {QColor("#0A27D3"), 40}};
}

} // namespace

Backend::Backend(ThemePaths paths, KeyLabels labels, Sender sender, QString colorsPath, QObject *parent)
    : QObject(parent), m_paths(std::move(paths)), m_labels(std::move(labels)), m_sender(std::move(sender)),
      m_colorsPath(std::move(colorsPath)) {
    qRegisterMetaType<ApplyResult>();
    // GG's positions are approximate around the navigation keys, so neighbours can sit
    // closer than a key width. Each key gets at most its nominal width and stops halfway
    // to its neighbours on the same row.
    constexpr double unit = 35, gap = 2;
    const QList<Key> keys = Layout::gs76().keys();
    for (const Key &key : keys) {
        double left = key.x - key.width * unit / 2, right = key.x + key.width * unit / 2;
        for (const Key &other : keys) {
            if (other.id == key.id || other.y != key.y)
                continue;
            const double middle = (key.x + other.x) / 2;
            if (other.x > key.x)
                right = std::min(right, std::max(middle, other.x - other.width * unit / 2) - gap / 2);
            else
                left = std::max(left, std::min(middle, other.x + other.width * unit / 2) + gap / 2);
        }
        m_keys.append(QVariantMap{{"id", key.id}, {"label", m_labels.label(key)}, {"x", key.x}, {"y", key.y},
                                  {"width", key.width}, {"left", left}, {"right", right}});
    }

    m_cooldown.setInterval(100);
    connect(&m_cooldown, &QTimer::timeout, this, [this] {
        m_cooldownMs = std::max(0, m_cooldownMs - 100);
        if (m_cooldownMs == 0)
            m_cooldown.stop();
        emit cooldownChanged();
    });
    connect(this, &Backend::applyFinished, this, &Backend::onApplyFinished, Qt::QueuedConnection);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Backend::reloadTheme);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Backend::reloadTheme);

    loadSavedColors();
    loadProfile();
    watchTheme();
}

Backend::~Backend() {
    // A send in flight finishes on its own thread; wait for it rather than cutting the
    // keyboard off mid-send.
    QElapsedTimer timer;
    timer.start();
    while (m_busy && timer.elapsed() < 20000) {
        QThread::msleep(20);
        QCoreApplication::processEvents();
    }
}

void Backend::loadProfile() {
    const Resolved resolved = resolveProfile(m_paths);
    m_themeName = resolved.themeName;
    m_savePath = resolved.savePath;
    m_profile = resolved.profile.value_or(Profile());
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    setError(resolved.errors.join("\n"));
    emit themeChanged();
    changed();
}

void Backend::watchTheme() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);
    // A theme switch replaces theme.name, so watch its directory too and re-arm each time.
    for (const QString &path : {m_paths.themeName, QFileInfo(m_paths.themeName).absolutePath()})
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);
}

void Backend::reloadTheme() {
    watchTheme();
    QFile file(m_paths.themeName);
    const QString name = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
    if (name.isEmpty() || name == m_themeName)
        return;
    if (m_dirty) {
        m_notice = tr("The theme changed to %1. Your changes still belong to %2; apply or undo them, then reopen.")
                       .arg(name, m_themeName);
        emit errorChanged();
        return;
    }
    m_notice.clear();
    loadProfile();
}

void Backend::changed() {
    m_preview.emplace(m_profile, Layout::gs76());
    ++m_revision;
    emit profileChanged();
}

void Backend::edit(const std::function<void(Profile &)> &change) {
    Profile next = m_profile;
    change(next);
    m_undo.append(m_profile);
    if (m_undo.size() > kUndoLimit)
        m_undo.removeFirst();
    m_redo.clear();
    m_profile = next;
    m_dirty = true;
    changed();
}

void Backend::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::dismissError() {
    m_error.clear();
    m_notice.clear();
    emit errorChanged();
}

QVariantList Backend::layers() const {
    QVariantList list;
    list.append(QVariantMap{{"kind", "base"}, {"name", tr("Base")}, {"type", m_profile.base.type}});
    list.append(QVariantMap{{"kind", "keys"}, {"name", tr("Keys")}, {"count", int(m_profile.keys.size())}});
    for (const EffectLayer &effect : m_profile.effects)
        list.append(QVariantMap{{"kind", "effect"}, {"name", effect.name}, {"type", effect.type},
                                {"count", int(effect.keys.size())}});
    list.append(QVariantMap{{"kind", "reactive"}, {"name", tr("Reactive")}, {"enabled", m_profile.reactive.enabled}});
    return list;
}

QString Backend::currentKind() const {
    if (m_currentLayer == 0)
        return "base";
    if (m_currentLayer == 1)
        return "keys";
    if (m_currentLayer - 2 < m_profile.effects.size())
        return "effect";
    return "reactive";
}

void Backend::setCurrentLayer(int layer) {
    layer = std::clamp(layer, 0, int(m_profile.effects.size()) + 2);
    if (layer == m_currentLayer)
        return;
    m_currentLayer = layer;
    emit currentLayerChanged();
    emit profileChanged();
}

EffectLayer *Backend::currentEffect() {
    const int index = m_currentLayer - 2;
    return index >= 0 && index < m_profile.effects.size() ? &m_profile.effects[index] : nullptr;
}

const EffectLayer *Backend::currentEffect() const {
    const int index = m_currentLayer - 2;
    return index >= 0 && index < m_profile.effects.size() ? &m_profile.effects[index] : nullptr;
}

QVariantMap Backend::current() const {
    auto stepsList = [](const QList<Step> &steps) {
        QVariantList list;
        for (const Step &step : steps)
            list.append(QVariantMap{{"color", step.color}, {"duration", step.duration}});
        return list;
    };
    const QString kind = currentKind();
    if (kind == "base")
        return {{"type", m_profile.base.type}, {"color", m_profile.base.color},
                {"steps", stepsList(m_profile.base.steps)}, {"origin", m_profile.base.origin}};
    if (kind == "keys") {
        QColor color = Qt::white;
        if (!m_selection.isEmpty() && m_profile.keys.contains(m_selection.first()))
            color = m_profile.keys.value(m_selection.first());
        return {{"color", color}};
    }
    if (const EffectLayer *effect = currentEffect())
        return {{"type", effect->type}, {"name", effect->name}, {"steps", stepsList(effect->steps)},
                {"origin", effect->origin}, {"count", int(effect->keys.size())}};
    return {{"enabled", m_profile.reactive.enabled}, {"color", m_profile.reactive.color},
            {"duration", m_profile.reactive.duration}};
}

void Backend::select(const QStringList &ids, bool add) {
    QStringList next = add ? m_selection : QStringList();
    for (const QString &id : ids)
        if (!next.contains(id) && Layout::gs76().byId(id))
            next.append(id);
    m_selection = next;
    emit selectionChanged();
    emit profileChanged();
}

void Backend::selectGroup(const QString &name) { select(Layout::gs76().group(name), false); }

void Backend::clearSelection() { select({}, false); }

void Backend::setColor(const QColor &color) {
    const QString kind = currentKind();
    if (kind == "base")
        edit([&](Profile &p) { p.base.color = color; });
    else if (kind == "keys" && !m_selection.isEmpty())
        edit([&](Profile &p) {
            for (const QString &id : m_selection)
                p.keys.insert(id, color);
        });
    else if (kind == "reactive")
        edit([&](Profile &p) { p.reactive.color = color; });
}

void Backend::setBaseType(const QString &type) {
    // As in Mystic Light, an effect picked here lights the whole keyboard: effect layers
    // would otherwise hide it on every key they cover. Ctrl+Z brings them back.
    const int removed = type == "static" ? 0 : int(m_profile.effects.size());
    edit([&](Profile &p) {
        p.base.type = type;
        if (type == "wave" && p.base.steps.isEmpty()) {
            p.base.steps = defaultSteps("wave");
            p.base.origin = "aqua-1";
        }
        // A black base would make breathing and flashing invisible.
        if (p.base.color == QColor(Qt::black) && type != "static")
            p.base.color = QColor("#00E5FF");
        if (removed > 0)
            p.effects.clear();
    });
    if (removed > 0) {
        setCurrentLayer(0);
        m_notice = tr("%n effect layer(s) removed so the effect shows on the whole keyboard (Ctrl+Z to undo).", "", removed);
        emit errorChanged();
    }
}

void Backend::addEffect(const QString &type) {
    const QStringList keys = m_selection;
    edit([&](Profile &p) {
        // A key belongs to one effect: take the selection away from the others.
        for (EffectLayer &other : p.effects)
            for (const QString &id : keys)
                other.keys.removeAll(id);
        EffectLayer effect;
        effect.type = type;
        effect.name = type == "pulse" ? tr("Pulse %1").arg(p.effects.size() + 1) : tr("Wave %1").arg(p.effects.size() + 1);
        effect.keys = keys;
        effect.steps = defaultSteps(type);
        effect.start = effect.steps.first().color;
        effect.origin = type == "pulse" ? "aqua-0" : "aqua-1";
        p.effects.append(effect);
    });
    setCurrentLayer(int(m_profile.effects.size()) + 1);
}

void Backend::removeLayer(int layer) {
    const int index = layer - 2;
    if (index < 0 || index >= m_profile.effects.size())
        return;
    edit([&](Profile &p) { p.effects.removeAt(index); });
    setCurrentLayer(std::min(m_currentLayer, int(m_profile.effects.size()) + 1));
}

void Backend::assignSelection() {
    const int index = m_currentLayer - 2;
    if (index < 0 || index >= m_profile.effects.size())
        return;
    const QStringList keys = m_selection;
    edit([&](Profile &p) {
        for (EffectLayer &other : p.effects)
            for (const QString &id : keys)
                other.keys.removeAll(id);
        p.effects[index].keys += keys;
    });
}

QList<Step> *Backend::currentSteps() {
    if (currentKind() == "base")
        return &m_profile.base.steps;
    if (EffectLayer *effect = currentEffect())
        return &effect->steps;
    return nullptr;
}

void Backend::setStepColor(int index, const QColor &color) {
    const int layer = m_currentLayer;
    if (!currentSteps() || index < 0 || index >= currentSteps()->size())
        return;
    edit([&](Profile &p) {
        QList<Step> &steps = layer == 0 ? p.base.steps : p.effects[layer - 2].steps;
        steps[index].color = color;
        if (layer >= 2 && index == 0)
            p.effects[layer - 2].start = color;
    });
}

void Backend::setStepDuration(int index, int duration) {
    const int layer = m_currentLayer;
    if (!currentSteps() || index < 0 || index >= currentSteps()->size())
        return;
    edit([&](Profile &p) {
        QList<Step> &steps = layer == 0 ? p.base.steps : p.effects[layer - 2].steps;
        steps[index].duration = std::clamp(duration, 1, 65535);
    });
}

void Backend::addStep() {
    const int layer = m_currentLayer;
    if (!currentSteps() || currentSteps()->size() + 1 >= kMaxSteps)
        return;
    edit([&](Profile &p) {
        QList<Step> &steps = layer == 0 ? p.base.steps : p.effects[layer - 2].steps;
        steps.append(steps.isEmpty() ? Step{Qt::white, 40} : steps.last());
    });
}

void Backend::removeStep(int index) {
    const int layer = m_currentLayer;
    if (!currentSteps() || currentSteps()->size() <= 1 || index < 0 || index >= currentSteps()->size())
        return;
    edit([&](Profile &p) {
        QList<Step> &steps = layer == 0 ? p.base.steps : p.effects[layer - 2].steps;
        steps.removeAt(index);
        if (layer >= 2)
            p.effects[layer - 2].start = steps.first().color;
    });
}

void Backend::setOrigin(const QString &origin) {
    const int layer = m_currentLayer;
    edit([&](Profile &p) {
        if (layer == 0)
            p.base.origin = origin;
        else if (layer >= 2 && layer - 2 < p.effects.size())
            p.effects[layer - 2].origin = origin;
    });
}

void Backend::setBrightness(int value) {
    value = std::clamp(value, 1, 100);
    if (value != m_profile.brightness)
        edit([&](Profile &p) { p.brightness = value; });
}

void Backend::setSpeed(int value) {
    value = std::clamp(value, 25, 400);
    if (value != m_profile.speed)
        edit([&](Profile &p) { p.speed = value; });
}

void Backend::setReactiveEnabled(bool enabled) {
    edit([&](Profile &p) { p.reactive.enabled = enabled; });
}

void Backend::setReactiveDuration(int duration) {
    edit([&](Profile &p) { p.reactive.duration = std::clamp(duration, 0, 2000); });
}

void Backend::setExperimentalReactive(bool enabled) {
    if (enabled == m_experimentalReactive)
        return;
    m_experimentalReactive = enabled;
    emit profileChanged();
}

QColor Backend::previewColor(const QString &id, qint64 ms) const {
    return m_preview ? m_preview->colorAt(id, ms) : QColor(Qt::black);
}

void Backend::apply() {
    if (m_busy || m_cooldownMs > 0)
        return;
    const BuildResult built = buildPackets(m_profile, Layout::gs76(), m_experimentalReactive);
    if (!built.errors.isEmpty()) {
        setError(built.errors.join("\n"));
        return;
    }
    QString saveError;
    if (!saveProfile(m_profile, m_savePath, &saveError)) {
        setError(tr("Could not save %1: %2").arg(m_savePath, saveError));
        return;
    }
    m_dirty = false;
    setError(QString());
    emit profileChanged();

    m_busy = true;
    emit busyChanged();
    const QList<Packet> packets = built.packets;
    QThread *thread = QThread::create([this, packets] { emit applyFinished(m_sender(packets)); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Backend::onApplyFinished(const ApplyResult &result) {
    m_busy = false;
    emit busyChanged();
    if (result.error != ApplyError::None)
        setError(result.message);
    m_cooldownMs = Device::MinIntervalMs;
    m_cooldown.start();
    emit cooldownChanged();
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_profile);
    m_profile = m_undo.takeLast();
    m_dirty = true;
    setCurrentLayer(std::min(m_currentLayer, int(m_profile.effects.size()) + 2));
    changed();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_profile);
    m_profile = m_redo.takeLast();
    m_dirty = true;
    changed();
}

void Backend::loadSavedColors() {
    m_savedColors = QStringList(kSavedColorSlots, QString());
    QFile file(m_colorsPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (int i = 0; i < std::min(int(array.size()), kSavedColorSlots); ++i)
        m_savedColors[i] = array.at(i).toString();
}

void Backend::storeSavedColors() const {
    QDir().mkpath(QFileInfo(m_colorsPath).absolutePath());
    QFile file(m_colorsPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(QJsonArray::fromStringList(m_savedColors)).toJson(QJsonDocument::Compact));
}

void Backend::setSavedColor(int index, const QColor &color) {
    if (index < 0 || index >= kSavedColorSlots)
        return;
    m_savedColors[index] = color.name(QColor::HexRgb).toUpper();
    storeSavedColors();
    emit savedColorsChanged();
}
