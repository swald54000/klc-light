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
const QStringList kBehaviors{"static", "pulse", "wave"};

QVariantList stepsList(const QList<Step> &steps) {
    QVariantList list;
    for (const Step &step : steps)
        list.append(QVariantMap{{"color", step.color}, {"duration", step.duration}});
    return list;
}

// What a group's swatch shows: its color, or the first color of its animation.
QColor swatch(const Group &group) {
    if (!group.animated())
        return group.color;
    return group.steps.isEmpty() ? group.start : group.steps.first().color;
}

// Takes the keys out of every group; a group left empty is removed.
void takeKeys(Document &doc, const QStringList &keys) {
    for (Group &group : doc.groups)
        for (const QString &id : keys)
            group.keys.removeAll(id);
    doc.groups.removeIf([](const Group &group) { return group.keys.isEmpty(); });
}

int indexOf(const Document &doc, const QString &name) {
    for (int i = 0; i < doc.groups.size(); ++i)
        if (doc.groups[i].name == name)
            return i;
    return -1;
}

} // namespace

Backend::Backend(ThemePaths paths, QString libraryDir, KeyLabels labels, Sender sender, QString colorsPath,
                 QObject *parent)
    : QObject(parent), m_paths(std::move(paths)), m_library(std::move(libraryDir)), m_labels(std::move(labels)),
      m_sender(std::move(sender)), m_colorsPath(std::move(colorsPath)) {
    qRegisterMetaType<ApplyResult>();
    for (const Key &key : Layout::gs76().keys())
        m_keys.append(QVariantMap{{"id", key.id}, {"label", m_labels.label(key)}, {"x", key.x}, {"y", key.y},
                                  {"left", key.x - key.w / 2}, {"right", key.x + key.w / 2},
                                  {"top", key.y - key.h / 2}, {"bottom", key.y + key.h / 2}});

    m_cooldown.setInterval(100);
    connect(&m_cooldown, &QTimer::timeout, this, [this] {
        m_cooldownMs = std::max(0, m_cooldownMs - 100);
        if (m_cooldownMs == 0)
            m_cooldown.stop();
        emit cooldownChanged();
    });
    connect(this, &Backend::applyFinished, this, &Backend::onApplyFinished, Qt::QueuedConnection);

    loadSavedColors();
    open();
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

// The applied profile, else the first readable one, else an empty one (saved on Apply).
void Backend::open() {
    m_library.migrate(m_paths);
    QStringList errors;
    std::optional<Document> doc;
    if (!m_library.applied().isEmpty())
        doc = m_library.load(m_library.applied(), &errors);
    for (const LibraryEntry &entry : m_library.entries()) {
        if (doc)
            break;
        if (entry.readable)
            doc = m_library.load(entry.name, &errors);
    }
    if (!doc) {
        doc = Document();
        doc->name = "Default";
    }
    m_doc = *doc;
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    setError(errors.join("\n"));
    changed();
}

void Backend::changed() {
    // Undo, redo or a removal may take the open group away.
    if (m_kind == Kind::Group && (m_group < 0 || m_group >= m_doc.groups.size())) {
        m_kind = Kind::Base;
        m_group = -1;
    }
    m_preview.emplace(lighting(m_doc), Layout::gs76());
    ++m_revision;
    emit profileChanged();
    emit currentChanged();
}

void Backend::edit(const std::function<void(Document &)> &change) {
    Document next = m_doc;
    change(next);
    // Re-picking the current value is not an edit.
    if (toJson(next) == toJson(m_doc))
        return;
    // A gesture records the state from before its first change, once.
    if (!m_inGesture || !m_gestureRecorded) {
        m_undo.append(m_doc);
        if (m_undo.size() > kUndoLimit)
            m_undo.removeFirst();
        m_gestureRecorded = m_inGesture;
    }
    m_redo.clear();
    m_doc = next;
    m_dirty = true;
    changed();
}

void Backend::setCurrent(Kind kind, int group) {
    if (kind != Kind::Group)
        group = -1;
    if (kind == m_kind && group == m_group)
        return;
    m_kind = kind;
    m_group = group;
    emit currentChanged();
    emit profileChanged();
}

void Backend::setSelection(const QStringList &ids) {
    if (ids == m_selection)
        return;
    m_selection = ids;
    emit selectionChanged();
    emit profileChanged();
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

QVariantList Backend::groups() const {
    QVariantList list;
    for (const Group &group : m_doc.groups)
        list.append(QVariantMap{{"name", group.name}, {"behavior", group.behavior},
                                {"count", int(group.keys.size())}, {"color", swatch(group)}});
    return list;
}

QString Backend::currentKind() const {
    switch (m_kind) {
    case Kind::Group: return "group";
    case Kind::Selection: return "selection";
    case Kind::Reactive: return "reactive";
    case Kind::Base: break;
    }
    return "base";
}

QStringList Backend::groupKeys() const {
    return m_kind == Kind::Group ? m_doc.groups.value(m_group).keys : QStringList();
}

int Backend::animatedCount() const { return animatedSlots(lighting(m_doc)); }

QVariantMap Backend::current() const {
    switch (m_kind) {
    case Kind::Base:
        return {{"type", m_doc.base.type}, {"color", m_doc.base.color},
                {"steps", stepsList(m_doc.base.steps)}, {"origin", m_doc.base.origin}};
    case Kind::Group: {
        const Group &group = m_doc.groups[m_group];
        return {{"name", group.name}, {"behavior", group.behavior}, {"type", group.behavior},
                {"color", swatch(group)}, {"steps", stepsList(group.steps)}, {"origin", group.origin},
                {"count", int(group.keys.size())}};
    }
    case Kind::Selection:
        return {{"count", int(m_selection.size())}, {"name", defaultGroupName()}};
    case Kind::Reactive:
        break;
    }
    return {{"enabled", m_doc.reactive.enabled}, {"color", m_doc.reactive.color},
            {"duration", m_doc.reactive.duration}};
}

void Backend::openBase() {
    setSelection({});
    setCurrent(Kind::Base);
}

void Backend::openGroup(int index) {
    if (index < 0 || index >= m_doc.groups.size())
        return;
    setSelection({});
    setCurrent(Kind::Group, index);
}

void Backend::openReactive() {
    setSelection({});
    setCurrent(Kind::Reactive);
}

void Backend::select(const QStringList &ids, bool add) {
    QStringList next = add ? m_selection : QStringList();
    for (const QString &id : ids)
        if (!next.contains(id) && Layout::gs76().byId(id))
            next.append(id);
    setSelection(next);

    if (next.isEmpty()) {
        if (m_kind == Kind::Selection)
            setCurrent(Kind::Base);
        return;
    }
    // Within the open group, the group stays open (the keys to take out of it, say).
    if (m_kind == Kind::Group &&
        std::all_of(next.begin(), next.end(), [&](const QString &id) { return m_doc.groupOf(id) == m_group; }))
        return;
    // A plain click on a grouped key opens its group.
    const int group = !add && next.size() == 1 ? m_doc.groupOf(next.first()) : -1;
    if (group >= 0)
        setCurrent(Kind::Group, group);
    else
        setCurrent(Kind::Selection);
}

void Backend::selectGroup(const QString &name) { select(Layout::gs76().group(name), false); }

// Escape, or a click beside the keys: back to the base.
void Backend::clearSelection() { openBase(); }

QString Backend::defaultGroupName() const {
    QString name;
    if (m_selection.size() == 1)
        if (const Key *key = Layout::gs76().byId(m_selection.first()))
            name = m_labels.label(*key).trimmed();
    if (!name.isEmpty()) {
        const QString base = name;
        for (int n = 2; indexOf(m_doc, name) >= 0; ++n)
            name = QString("%1 %2").arg(base).arg(n);
        return name;
    }
    for (int n = m_doc.groups.size() + 1;; ++n) {
        name = tr("Group %1").arg(n);
        if (indexOf(m_doc, name) < 0)
            return name;
    }
}

// Why a name cannot be a group's (other than the group at `except`); empty if it can.
static QString groupNameError(const Document &doc, const QString &name, int except) {
    if (name.isEmpty())
        return Backend::tr("The name is empty.");
    const int existing = indexOf(doc, name);
    if (existing >= 0 && existing != except)
        return Backend::tr("A group named \"%1\" already exists.").arg(name);
    return QString();
}

QString Backend::createGroup(const QString &name) {
    const QString trimmed = name.trimmed();
    if (m_selection.isEmpty())
        return tr("Select keys first.");
    if (const QString problem = groupNameError(m_doc, trimmed, -1); !problem.isEmpty())
        return problem;
    const QStringList keys = m_selection;
    edit([&](Document &d) {
        // A key belongs to one group: take the selection away from the others.
        takeKeys(d, keys);
        Group group;
        group.name = trimmed;
        group.keys = keys;
        d.groups.append(group);
    });
    setSelection({});
    setCurrent(Kind::Group, int(m_doc.groups.size()) - 1);
    return QString();
}

void Backend::addSelectionToGroup(int index) {
    if (index < 0 || index >= m_doc.groups.size() || m_selection.isEmpty())
        return;
    const QString name = m_doc.groups[index].name;
    const QStringList keys = m_selection;
    edit([&](Document &d) {
        takeKeys(d, keys);
        // The target may have been emptied and removed above: it gets the keys back.
        const int target = indexOf(d, name);
        if (target >= 0) {
            d.groups[target].keys += keys;
        } else {
            Group group = m_doc.groups[index];
            group.keys = keys;
            d.groups.insert(std::min(index, int(d.groups.size())), group);
        }
    });
    setSelection({});
    setCurrent(Kind::Group, indexOf(m_doc, name));
}

void Backend::removeSelectionFromGroup() {
    if (m_kind != Kind::Group)
        return;
    QStringList keys;
    for (const QString &id : m_selection)
        if (m_doc.groupOf(id) == m_group)
            keys.append(id);
    if (keys.isEmpty())
        return;
    // Emptied, the group is removed: back to the base, not on to the next group.
    if (keys.size() == m_doc.groups[m_group].keys.size()) {
        m_kind = Kind::Base;
        m_group = -1;
    }
    edit([&](Document &d) { takeKeys(d, keys); });
    setSelection({});
}

void Backend::deleteGroup(int index) {
    if (index < 0 || index >= m_doc.groups.size())
        return;
    if (m_kind == Kind::Group && m_group == index) {
        m_kind = Kind::Base;
        m_group = -1;
        m_selection.clear();
        emit selectionChanged();
    } else if (m_kind == Kind::Group && m_group > index) {
        --m_group;
    }
    edit([&](Document &d) { d.groups.removeAt(index); });
}

QString Backend::renameGroup(int index, const QString &name) {
    if (index < 0 || index >= m_doc.groups.size())
        return QString();
    const QString trimmed = name.trimmed();
    if (const QString problem = groupNameError(m_doc, trimmed, index); !problem.isEmpty())
        return problem;
    edit([&](Document &d) { d.groups[index].name = trimmed; });
    return QString();
}

QString Backend::setBehavior(const QString &behavior) {
    if (m_kind != Kind::Group || !kBehaviors.contains(behavior))
        return QString();
    Document next = m_doc;
    Group &group = next.groups[m_group];
    if (group.behavior == behavior)
        return QString();
    if (behavior == "static") {
        group.color = swatch(group);
    } else if (!group.animated()) {
        // The animation starts from the group's color, in one step.
        group.steps = {{group.color, behavior == "pulse" ? 60 : 40}};
        group.start = group.color;
        group.closing = 0;
        group.origin = behavior == "pulse" ? "aqua-0" : "aqua-1";
    }
    group.behavior = behavior;
    if (animatedSlots(lighting(next)) > kMaxAnimatedSlots)
        return tr("At most 6 animated groups (controller limit)");
    edit([&](Document &d) { d = next; });
    return QString();
}

void Backend::setColor(const QColor &color) {
    switch (m_kind) {
    case Kind::Base:
        edit([&](Document &d) { d.base.color = color; });
        break;
    case Kind::Group:
        if (!m_doc.groups[m_group].animated())
            edit([&](Document &d) { d.groups[m_group].color = color; });
        break;
    case Kind::Reactive:
        edit([&](Document &d) { d.reactive.color = color; });
        break;
    case Kind::Selection:
        break;
    }
}

void Backend::setBaseType(const QString &type) {
    // As in Mystic Light, an effect picked here lights the whole keyboard: animated groups
    // would otherwise hide it on every key they cover. Ctrl+Z brings them back.
    const int removed = type == "static" ? 0 : int(lighting(m_doc).effects.size());
    edit([&](Document &d) {
        d.base.type = type;
        if (type == "wave" && d.base.steps.isEmpty()) {
            d.base.steps = {{QColor("#00E5FF"), 40}, {QColor("#0A27D3"), 40}};
            d.base.origin = "aqua-1";
        }
        // A black base would make breathing and flashing invisible.
        if (d.base.color == QColor(Qt::black) && type != "static")
            d.base.color = QColor("#00E5FF");
        if (removed > 0)
            d.groups.removeIf([](const Group &group) { return group.animated(); });
    });
    if (removed > 0) {
        openBase();
        m_notice = tr("%n effect layer(s) removed so the effect shows on the whole keyboard (Ctrl+Z to undo).", "", removed);
        emit errorChanged();
    }
}

QList<Step> *Backend::stepsIn(Document &doc) const {
    if (m_kind == Kind::Base)
        return &doc.base.steps;
    if (m_kind == Kind::Group && doc.groups.value(m_group).animated())
        return &doc.groups[m_group].steps;
    return nullptr;
}

void Backend::setStepColor(int index, const QColor &color) {
    edit([&](Document &d) {
        QList<Step> *steps = stepsIn(d);
        if (!steps || index < 0 || index >= steps->size())
            return;
        (*steps)[index].color = color;
        if (m_kind == Kind::Group && index == 0)
            d.groups[m_group].start = color;
    });
}

void Backend::setStepDuration(int index, int duration) {
    edit([&](Document &d) {
        QList<Step> *steps = stepsIn(d);
        if (steps && index >= 0 && index < steps->size())
            (*steps)[index].duration = std::clamp(duration, 1, 65535);
    });
}

void Backend::addStep() {
    edit([&](Document &d) {
        QList<Step> *steps = stepsIn(d);
        if (steps && steps->size() + 1 < kMaxSteps)
            steps->append(steps->isEmpty() ? Step{Qt::white, 40} : steps->last());
    });
}

void Backend::removeStep(int index) {
    edit([&](Document &d) {
        QList<Step> *steps = stepsIn(d);
        if (!steps || steps->size() <= 1 || index < 0 || index >= steps->size())
            return;
        steps->removeAt(index);
        if (m_kind == Kind::Group)
            d.groups[m_group].start = steps->first().color;
    });
}

void Backend::setOrigin(const QString &origin) {
    edit([&](Document &d) {
        if (m_kind == Kind::Base)
            d.base.origin = origin;
        else if (m_kind == Kind::Group && d.groups[m_group].animated())
            d.groups[m_group].origin = origin;
    });
}

void Backend::setBrightness(int value) {
    value = std::clamp(value, 1, 100);
    if (value != m_doc.brightness)
        edit([&](Document &d) { d.brightness = value; });
}

void Backend::setSpeed(int value) {
    value = std::clamp(value, 25, 400);
    if (value != m_doc.speed)
        edit([&](Document &d) { d.speed = value; });
}

void Backend::setReactiveEnabled(bool enabled) {
    edit([&](Document &d) { d.reactive.enabled = enabled; });
}

void Backend::setReactiveDuration(int duration) {
    edit([&](Document &d) { d.reactive.duration = std::clamp(duration, 0, 2000); });
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
    QStringList errors = validate(m_doc, Layout::gs76());
    BuildResult built;
    if (errors.isEmpty()) {
        built = buildPackets(lighting(m_doc), Layout::gs76(), m_experimentalReactive);
        errors = built.errors;
    }
    if (!errors.isEmpty()) {
        setError(errors.join("\n"));
        return;
    }
    QString saveError;
    if (!m_library.save(m_doc, &saveError)) {
        setError(tr("Could not save %1: %2").arg(m_doc.name, saveError));
        return;
    }
    m_sending = m_doc.name;
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
    if (result.error != ApplyError::None) {
        setError(result.message);
    } else {
        // What the keyboard shows, and what `klc-light apply` sends again at boot. Read
        // state.json afresh: a theme hook may have changed it since the editor opened.
        Library(m_library.dir()).setApplied(m_sending);
    }
    m_cooldownMs = Device::MinIntervalMs;
    m_cooldown.start();
    emit cooldownChanged();
}

void Backend::beginGesture() {
    m_inGesture = true;
    m_gestureRecorded = false;
}

void Backend::endGesture() {
    m_inGesture = false;
    m_gestureRecorded = false;
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_doc);
    m_doc = m_undo.takeLast();
    m_dirty = true;
    changed();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_doc);
    m_doc = m_redo.takeLast();
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
