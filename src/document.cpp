#include "document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include "layout.h"

int Document::groupOf(const QString &keyId) const {
    for (int i = 0; i < groups.size(); ++i)
        if (groups[i].keys.contains(keyId))
            return i;
    return -1;
}

Profile lighting(const Document &doc) {
    Profile profile;
    profile.version = 1;
    profile.brightness = doc.brightness;
    profile.speed = doc.speed;
    profile.base = doc.base;
    profile.reactive = doc.reactive;
    for (const Group &group : doc.groups) {
        if (!group.animated()) {
            for (const QString &id : group.keys)
                profile.keys.insert(id, group.color);
        } else {
            profile.effects.append({group.name, group.behavior, group.keys, group.start, group.steps,
                                    group.closing, group.wave});
        }
    }
    return profile;
}

Document fromLighting(const Profile &profile, const QString &name) {
    Document doc;
    doc.name = name;
    doc.brightness = profile.brightness;
    doc.speed = profile.speed;
    doc.base = profile.base;
    doc.reactive = profile.reactive;

    QSet<QString> animatedKeys;
    QList<Group> animated;
    for (const EffectLayer &effect : profile.effects) {
        Group group;
        group.name = effect.name;
        group.behavior = effect.type;
        group.keys = effect.keys;
        group.start = effect.start;
        group.steps = effect.steps;
        group.closing = effect.closing;
        group.wave = effect.wave;
        animated.append(group);
        for (const QString &id : effect.keys)
            animatedKeys.insert(id);
    }

    // Painted keys in layout order, then any the layout does not draw (so nothing is lost).
    QStringList order;
    for (const Key &key : Layout::gs76().keys())
        order.append(key.id);
    for (auto it = profile.keys.begin(); it != profile.keys.end(); ++it)
        if (!order.contains(it.key()))
            order.append(it.key());

    QHash<QString, int> byColor;
    for (const QString &id : order) {
        if (!profile.keys.contains(id) || animatedKeys.contains(id))
            continue;
        const QColor color = profile.keys.value(id);
        const QString hex = color.name();
        if (!byColor.contains(hex)) {
            Group group;
            group.name = QCoreApplication::translate("Document", "Group %1").arg(hex);
            group.color = color;
            byColor.insert(hex, doc.groups.size());
            doc.groups.append(group);
        }
        doc.groups[byColor.value(hex)].keys.append(id);
    }

    // v1 does not require effect names to be unique or filled in; groups do.
    QSet<QString> taken;
    for (const Group &group : doc.groups)
        taken.insert(group.name);
    for (Group &group : animated) {
        QString base = group.name;
        if (base.trimmed().isEmpty())
            base = QCoreApplication::translate("Document", "Group %1").arg(doc.groups.size() + 1);
        QString unique = base;
        for (int n = 2; taken.contains(unique); ++n)
            unique = QString("%1 %2").arg(base).arg(n);
        group.name = unique;
        taken.insert(unique);
        doc.groups.append(group);
    }
    return doc;
}

QJsonObject toJson(const Document &doc) {
    // Base, speed and reactive are written exactly as in version 1.
    QJsonObject object = toJson(lighting(doc));
    object.remove("keys");
    object.remove("effects");
    object["version"] = 3;
    object["name"] = doc.name;

    QJsonArray groups;
    for (const Group &group : doc.groups) {
        QJsonObject g{{"name", group.name}, {"behavior", group.behavior},
                      {"keys", QJsonArray::fromStringList(group.keys)}};
        if (!group.animated()) {
            g["color"] = colorName(group.color);
        } else {
            g["steps"] = stepsToJson(group.steps);
            if (group.behavior == "wave")
                g["wave"] = waveToJson(group.wave);
            if (group.closing > 0)
                g["closing"] = group.closing;
            if (group.steps.isEmpty() || group.start != group.steps.first().color)
                g["start"] = colorName(group.start);
        }
        groups.append(g);
    }
    object["groups"] = groups;
    return object;
}

std::optional<Document> documentFromJson(const QJsonObject &object, QStringList *errors) {
    QStringList local;
    QStringList &problems = errors ? *errors : local;
    const int version = object.value("version").toInt(-1);

    if (version == 1) {
        const auto profile = profileFromJson(object, &problems);
        if (!profile)
            return std::nullopt;
        return fromLighting(*profile, QString());
    }
    if (version != 2 && version != 3) {
        problems.append(QString("version: %1 is not supported").arg(object.value("version").toVariant().toString()));
        return std::nullopt;
    }

    // Base, speed and reactive share version 1's reading; the groups are read here.
    QJsonObject shell = object;
    shell["version"] = 1;
    shell.remove("keys");
    shell.remove("effects");
    const auto common = profileFromJson(shell, &problems);
    if (!common)
        return std::nullopt;

    const qsizetype before = problems.size();
    Document doc = fromLighting(*common, object.value("name").toString());
    const QJsonArray groups = object.value("groups").toArray();
    for (int i = 0; i < groups.size(); ++i) {
        const QJsonObject g = groups.at(i).toObject();
        const QString where = QString("group %1").arg(i + 1);
        Group group;
        group.name = g.value("name").toString();
        group.behavior = g.value("behavior").toString();
        // The power key is lit by F1 and belongs to no group; one left with no key goes.
        bool dropped = false;
        for (const QJsonValue &key : g.value("keys").toArray()) {
            if (Layout::gs76().lightSource(key.toString()) != key.toString())
                dropped = true;
            else
                group.keys.append(key.toString());
        }
        if (dropped && group.keys.isEmpty())
            continue;
        if (!group.animated()) {
            if (g.contains("color"))
                if (const auto color = parseColor(g.value("color"), where, &problems))
                    group.color = *color;
        } else {
            group.steps = stepsFromJson(g.value("steps"), where, &problems);
            if (group.behavior == "wave")
                group.wave = waveFromJson(g);
            group.closing = g.value("closing").toInt(0);
            if (g.contains("start")) {
                if (const auto color = parseColor(g.value("start"), where, &problems))
                    group.start = *color;
            } else if (!group.steps.isEmpty()) {
                group.start = group.steps.first().color;
            }
        }
        doc.groups.append(group);
    }
    if (problems.size() > before)
        return std::nullopt;
    return doc;
}

QStringList validate(const Document &doc, const Layout &layout) {
    QStringList errors;
    QSet<QString> names, claimed;
    for (int i = 0; i < doc.groups.size(); ++i) {
        const Group &group = doc.groups[i];
        if (group.name.trimmed().isEmpty())
            errors.append(QString("group %1: the name is empty").arg(i + 1));
        else if (names.contains(group.name))
            errors.append(QString("group %1: the name is already used").arg(group.name));
        names.insert(group.name);
        for (const QString &key : group.keys) {
            if (claimed.contains(key))
                errors.append(QString("group %1: %2 already belongs to another group").arg(group.name, key));
            claimed.insert(key);
        }
    }
    // Unknown keys, steps, wave origins and the animated-slot limit are the lighting's rules.
    errors += validate(lighting(doc), layout);
    return errors;
}

std::optional<Document> readDocument(const QString &path, QStringList *errors) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errors->append(QString("%1: %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!json.isObject()) {
        errors->append(QString("%1: not a JSON object (%2)").arg(path, parseError.errorString()));
        return std::nullopt;
    }
    QStringList problems;
    auto doc = documentFromJson(json.object(), &problems);
    if (doc)
        problems += validate(*doc, Layout::gs76());
    for (const QString &problem : problems)
        errors->append(QString("%1: %2").arg(path, problem));
    return problems.isEmpty() ? doc : std::nullopt;
}

bool saveDocument(const Document &doc, const QString &path, QString *error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QString("cannot create %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    // QSaveFile writes a temporary file beside the target and renames it on commit.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson(doc)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}
