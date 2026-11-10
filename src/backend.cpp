#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
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
    for (const Key &key : Layout::gs76().keys()) {
        QVariantMap entry{{"id", key.id}, {"label", m_labels.label(key)}, {"x", key.x}, {"y", key.y},
                          {"left", key.x - key.w / 2}, {"right", key.x + key.w / 2},
                          {"top", key.y - key.h / 2}, {"bottom", key.y + key.h / 2}};
        if (Layout::gs76().lightSource(key.id) != key.id)
            entry.insert("note", tr("Lit with F1 (same circuit)"));
        m_keys.append(entry);
    }

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
        doc->name = Library::defaultName();
    }
    openDocument(*doc);
    setError(errors.join("\n"));
}

void Backend::openDocument(const Document &doc) {
    m_doc = doc;
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    setSelection({});
    m_kind = Kind::Base;
    m_group = -1;
    changed();
    emit libraryChanged();
}

void Backend::changed() {
    // Undo, redo or a removal may take the open group away.
    if (m_kind == Kind::Group && (m_group < 0 || m_group >= m_doc.groups.size())) {
        m_kind = Kind::Base;
        m_group = -1;
    }
    // Picking an origin only makes sense while a wave is open.
    if (Document copy = m_doc; m_picking && !openWave(copy))
        cancelPick();
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
    cancelPick();
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

QVariantMap Backend::baseSummary() const {
    const QColor color = !m_doc.base.steps.isEmpty() && m_doc.base.type == "wave" ? m_doc.base.steps.first().color
                                                                                  : m_doc.base.color;
    return {{"type", m_doc.base.type}, {"color", color}};
}

QStringList Backend::groupKeys() const {
    return m_kind == Kind::Group ? m_doc.groups.value(m_group).keys : QStringList();
}

int Backend::animatedCount() const { return animatedSlots(lighting(m_doc)); }

QVariantMap Backend::current() const {
    switch (m_kind) {
    case Kind::Base: {
        QVariantMap map{{"type", m_doc.base.type}, {"color", m_doc.base.color},
                        {"steps", stepsList(m_doc.base.steps)}};
        if (m_doc.base.type == "wave")
            map.insert(waveSummary(m_doc.base.wave));
        return map;
    }
    case Kind::Group: {
        const Group &group = m_doc.groups[m_group];
        QVariantMap map{{"name", group.name}, {"behavior", group.behavior}, {"type", group.behavior},
                        {"color", swatch(group)}, {"steps", stepsList(group.steps)},
                        {"count", int(group.keys.size())}};
        if (group.behavior == "wave")
            map.insert(waveSummary(group.wave));
        return map;
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
    // Clicking the power key picks F1, whose output lights it.
    for (const QString &shown : ids) {
        const QString id = Layout::gs76().lightSource(shown);
        if (!next.contains(id) && Layout::gs76().byId(id))
            next.append(id);
    }
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
        group.wave = Wave{};
    }
    group.behavior = behavior;
    if (animatedSlots(lighting(next)) > kMaxAnimatedSlots)
        return tr("At most 18 animated groups (controller limit)");
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
            d.base.wave = Wave{};
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

Wave *Backend::openWave(Document &doc) const {
    if (m_kind == Kind::Base && doc.base.type == "wave")
        return &doc.base.wave;
    if (m_kind == Kind::Group && m_group >= 0 && m_group < doc.groups.size() && doc.groups[m_group].behavior == "wave")
        return &doc.groups[m_group].wave;
    return nullptr;
}

QVariantMap Backend::waveSummary(const Wave &wave) const {
    const bool key = wave.origin.startsWith("KEY_");
    const bool away = wave.flow == "away";
    QString direction;
    if (key)
        direction = away ? "key-out" : "key-in";
    else if (wave.origin == "center")
        direction = away ? "center-out" : "center-in";
    else
        direction = QHash<QString, QString>{{"left", "left"}, {"right", "right"}, {"top", "up"}, {"bottom", "down"}}
                        .value(wave.origin, "left");
    return {{"direction", direction}, {"waveKey", key ? wave.origin : QString()}, {"waveSize", wave.size}};
}

void Backend::setWaveDirection(const QString &direction) {
    static const QHash<QString, QPair<QString, QString>> fixed{
        {"left", {"left", "toward"}},     {"right", {"right", "toward"}},       {"up", {"top", "toward"}},
        {"down", {"bottom", "toward"}},   {"center-in", {"center", "toward"}}, {"center-out", {"center", "away"}}};
    if (direction != "key-in" && direction != "key-out")
        cancelPick();
    edit([&](Document &d) {
        Wave *wave = openWave(d);
        if (!wave)
            return;
        if (fixed.contains(direction)) {
            wave->origin = fixed.value(direction).first;
            wave->flow = fixed.value(direction).second;
        } else if (direction == "key-in" || direction == "key-out") {
            // Switching between the two keeps the key; coming from a direction starts at G,
            // in the middle of the keyboard, until a key is picked.
            if (!wave->origin.startsWith("KEY_"))
                wave->origin = "KEY_G";
            wave->flow = direction == "key-in" ? "toward" : "away";
        }
    });
}

void Backend::setWaveSize(int size) {
    size = std::clamp(size, 1, 100);
    edit([&](Document &d) {
        if (Wave *wave = openWave(d))
            wave->size = size;
    });
}

void Backend::pickWaveOrigin() {
    Document copy = m_doc;
    if (m_picking || !openWave(copy))
        return;
    m_picking = true;
    emit pickingChanged();
}

void Backend::cancelPick() {
    if (!m_picking)
        return;
    m_picking = false;
    emit pickingChanged();
}

bool Backend::pickKey(const QString &id) {
    if (!m_picking)
        return false;
    cancelPick();
    const Key *key = Layout::gs76().byId(id);
    if (key && key->visible)
        edit([&](Document &d) {
            if (Wave *wave = openWave(d))
                wave->origin = id;
        });
    return true;
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
    edit([&](Document &d) { d.reactive.duration = std::clamp(duration, 50, 2000); });
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
        built = buildPackets(lighting(m_doc), Layout::gs76());
        errors = built.errors;
    }
    if (!errors.isEmpty()) {
        setError(errors.join("\n"));
        return;
    }
    if (!save())
        return;
    m_sending = m_doc.name;
    setError(QString());

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
        Library library = fresh();
        // The keyboard is lit again: clear a `klc-light off` for the bar widget.
        library.setOff(false);
        for (const QString &name : library.names())
            if (name.compare(m_sending, Qt::CaseInsensitive) == 0) {
                library.setApplied(name);
                break;
            }
        emit libraryChanged();
    }
    m_cooldownMs = Device::MinIntervalMs;
    m_cooldown.start();
    emit cooldownChanged();
}

bool Backend::save() {
    QString error;
    if (!fresh().save(m_doc, &error)) {
        setError(tr("Could not save %1: %2").arg(m_doc.name, error));
        return false;
    }
    m_dirty = false;
    emit profileChanged();
    emit libraryChanged();
    return true;
}

QString Backend::nameError(const QString &name, const QString &except) const { return fresh().nameError(name, except); }

QStringList Backend::unreadableProfiles() const {
    QStringList names;
    for (const LibraryEntry &entry : fresh().entries())
        if (!entry.readable)
            names.append(entry.name);
    return names;
}

bool Backend::openProfile(const QString &name) {
    if (m_busy) {
        setError(tr("Wait for the keyboard to finish"));
        return false;
    }
    QStringList errors;
    const auto doc = fresh().load(name, &errors);
    if (!doc) {
        setError(errors.isEmpty() ? tr("Could not open %1").arg(name) : errors.join("\n"));
        return false;
    }
    setError(QString());
    openDocument(*doc);
    return true;
}

QString Backend::newProfile(const QString &name) {
    if (m_busy) {
        setError(tr("Wait for the keyboard to finish"));
        return m_error;
    }
    if (const QString problem = fresh().nameError(name); !problem.isEmpty())
        return problem;
    Document doc;
    doc.name = name.trimmed();
    doc.base.color = Qt::black;
    QString error;
    if (!fresh().save(doc, &error)) {
        setError(tr("Could not save %1: %2").arg(doc.name, error));
        return error;
    }
    setError(QString());
    openDocument(doc);
    return {};
}

QString Backend::duplicateProfile(const QString &name) {
    if (m_busy) {
        setError(tr("Wait for the keyboard to finish"));
        return m_error;
    }
    if (const QString problem = fresh().nameError(name); !problem.isEmpty())
        return problem;
    Document doc = m_doc;
    doc.name = name.trimmed();
    QString error;
    if (!fresh().save(doc, &error)) {
        setError(tr("Could not save %1: %2").arg(doc.name, error));
        return error;
    }
    setError(QString());
    openDocument(doc);
    return {};
}

QString Backend::renameProfile(const QString &name) {
    if (m_busy) {
        setError(tr("Wait for the keyboard to finish"));
        return m_error;
    }
    Library library = fresh();
    const QString to = name.trimmed();
    if (const QString problem = library.nameError(to, m_doc.name); !problem.isEmpty())
        return problem;
    QString error;
    if (library.names().contains(m_doc.name)) {
        if (!library.rename(m_doc.name, to, &error)) {
            setError(tr("Could not rename %1: %2").arg(m_doc.name, error));
            return error;
        }
        m_doc.name = to;
    } else {
        // Never saved: it only exists in the editor.
        m_doc.name = to;
        if (!save())
            return m_error;
    }
    emit profileChanged();
    emit libraryChanged();
    return {};
}

void Backend::deleteProfile() {
    if (m_busy) {
        setError(tr("Wait for the keyboard to finish"));
        return;
    }
    Library library = fresh();
    const QStringList before = library.names();
    const int index = int(before.indexOf(m_doc.name));
    QString error;
    if (index >= 0 && !library.remove(m_doc.name, &error)) {
        setError(tr("Could not delete %1: %2").arg(m_doc.name, error));
        return;
    }
    // The one that took its place in the list, else the one before it.
    const QList<LibraryEntry> entries = library.entries();
    QList<LibraryEntry> readable;
    for (const LibraryEntry &entry : entries)
        if (entry.readable)
            readable.append(entry);
    std::optional<Document> next;
    QStringList errors;
    for (int i = 0; i < readable.size() && !next; ++i) {
        const LibraryEntry &entry = readable[std::min(std::max(index, 0) + i, int(readable.size()) - 1)];
        next = library.load(entry.name, &errors);
    }
    if (!next) {
        Document doc;
        doc.name = Library::defaultName();
        doc.base.color = Qt::black;
        if (!library.save(doc, &error)) {
            setError(tr("Could not save %1: %2").arg(doc.name, error));
            return;
        }
        next = doc;
    }
    setError(QString());
    openDocument(*next);
}

QString Backend::linkTheme(const QString &theme) {
    if (!fresh().names().contains(m_doc.name) && !save())
        return {};
    Library library = fresh();
    const QString previous = library.link(m_doc.name, theme.trimmed());
    emit libraryChanged();
    return previous == m_doc.name ? QString() : previous;
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
    // Written beside the file and renamed, so a crash never leaves it cut short.
    QSaveFile file(m_colorsPath);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(QJsonArray::fromStringList(m_savedColors)).toJson(QJsonDocument::Compact)) < 0 ||
        !file.commit())
        qWarning("klc-light: cannot write %s: %s", qPrintable(m_colorsPath), qPrintable(file.errorString()));
}

void Backend::setSavedColor(int index, const QColor &color) {
    if (index < 0 || index >= kSavedColorSlots)
        return;
    m_savedColors[index] = color.name(QColor::HexRgb).toUpper();
    storeSavedColors();
    emit savedColorsChanged();
}
