#include "profile.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include "layout.h"

namespace {

const QStringList kBaseTypes{"static", "breathing", "flashing", "rainbow", "wave"};
const QStringList kEffectTypes{"wave", "pulse"};

} // namespace

QString colorName(const QColor &color) { return color.name(QColor::HexRgb).toUpper(); }

// Only "#RRGGBB": QColor would also take names and short forms, which a hand-edited
// profile should not rely on.
std::optional<QColor> parseColor(const QJsonValue &value, const QString &where, QStringList *errors) {
    static const QRegularExpression pattern("^#[0-9A-Fa-f]{6}$");
    const QString text = value.toString();
    if (!value.isString() || !pattern.match(text).hasMatch()) {
        errors->append(QString("%1: invalid color %2, expected #RRGGBB").arg(where, value.isString() ? text : "(not a string)"));
        return std::nullopt;
    }
    return QColor(text);
}

QJsonArray stepsToJson(const QList<Step> &steps) {
    QJsonArray array;
    for (const Step &step : steps)
        array.append(QJsonArray{colorName(step.color), step.duration});
    return array;
}

QList<Step> stepsFromJson(const QJsonValue &value, const QString &where, QStringList *errors) {
    QList<Step> steps;
    if (!value.isArray()) {
        errors->append(QString("%1: steps must be a list of [color, duration]").arg(where));
        return steps;
    }
    const QJsonArray array = value.toArray();
    for (int i = 0; i < array.size(); ++i) {
        const QJsonArray pair = array.at(i).toArray();
        const QString here = QString("%1 step %2").arg(where).arg(i + 1);
        if (pair.size() != 2 || !pair.at(1).isDouble()) {
            errors->append(QString("%1: expected [color, duration]").arg(here));
            continue;
        }
        if (const auto color = parseColor(pair.at(0), here, errors))
            steps.append({*color, pair.at(1).toInt()});
    }
    return steps;
}

int animatedSlots(const Profile &profile) {
    return profile.effects.size() + (profile.base.type == "static" ? 0 : 1);
}

Wave waveFromAqua(const QString &origin) {
    if (origin == "aqua-1" || origin == "aqua-4")
        return {"right", "toward", 68};
    if (origin == "aqua-3")
        return {"left", "toward", 68};
    return {"center", "toward", 68};
}

Wave normalized(const Wave &wave) {
    static const QHash<QString, QString> opposite{{"left", "right"}, {"right", "left"}, {"top", "bottom"}, {"bottom", "top"}};
    if (wave.flow != "away" || !opposite.contains(wave.origin))
        return wave;
    return {opposite.value(wave.origin), "toward", wave.size};
}

QJsonObject waveToJson(const Wave &wave) {
    return {{"origin", wave.origin}, {"flow", wave.flow}, {"size", wave.size}};
}

Wave waveFromJson(const QJsonObject &holder) {
    if (holder.value("wave").isObject()) {
        const QJsonObject object = holder.value("wave").toObject();
        const Wave defaults;
        return normalized({object.value("origin").toString(defaults.origin), object.value("flow").toString(defaults.flow),
                           object.value("size").toInt(defaults.size)});
    }
    if (holder.contains("origin"))
        return waveFromAqua(holder.value("origin").toString());
    return {};
}

QStringList validateWave(const Wave &wave, const QString &where, const Layout &layout) {
    static const QStringList points{"left", "right", "top", "bottom", "center"};
    QStringList errors;
    if (!points.contains(wave.origin)) {
        const Key *key = layout.byId(wave.origin);
        if (wave.origin.startsWith("KEY_") || wave.origin.startsWith("HID_")) {
            if (!key || !key->visible)
                errors.append(QString("%1: wave origin: unknown key \"%2\"").arg(where, wave.origin));
        } else {
            errors.append(QString("%1: wave origin: unknown \"%2\"").arg(where, wave.origin));
        }
    }
    if (wave.flow != "toward" && wave.flow != "away")
        errors.append(QString("%1: wave flow: \"%2\" is not toward or away").arg(where, wave.flow));
    if (wave.size < 1 || wave.size > 100)
        errors.append(QString("%1: wave size: %2 out of range 1..100").arg(where).arg(wave.size));
    return errors;
}

QJsonObject toJson(const Profile &profile) {
    QJsonObject base{{"type", profile.base.type}};
    if (profile.base.type == "wave") {
        base["steps"] = stepsToJson(profile.base.steps);
        base["wave"] = waveToJson(profile.base.wave);
    } else if (profile.base.type != "rainbow") {
        base["color"] = colorName(profile.base.color);
    }

    QJsonObject keys;
    for (auto it = profile.keys.begin(); it != profile.keys.end(); ++it)
        keys[it.key()] = colorName(it.value());

    QJsonArray effects;
    for (const EffectLayer &effect : profile.effects) {
        QJsonObject object{{"name", effect.name}, {"type", effect.type},
                           {"keys", QJsonArray::fromStringList(effect.keys)},
                           {"steps", stepsToJson(effect.steps)}};
        if (effect.type == "wave")
            object["wave"] = waveToJson(effect.wave);
        if (effect.closing > 0)
            object["closing"] = effect.closing;
        if (effect.steps.isEmpty() || effect.start != effect.steps.first().color)
            object["start"] = colorName(effect.start);
        effects.append(object);
    }

    QJsonObject object{
        {"version", profile.version}, {"brightness", profile.brightness}, {"speed", profile.speed},
        {"base", base},
        {"reactive", QJsonObject{{"enabled", profile.reactive.enabled},
                                 {"color", colorName(profile.reactive.color)},
                                 {"duration", profile.reactive.duration}}},
    };
    if (!keys.isEmpty())
        object["keys"] = keys;
    if (!effects.isEmpty())
        object["effects"] = effects;
    return object;
}

std::optional<Profile> profileFromJson(const QJsonObject &object, QStringList *errors) {
    QStringList local;
    QStringList &problems = errors ? *errors : local;
    const qsizetype before = problems.size();
    Profile profile;

    profile.version = object.value("version").toInt(-1);
    if (profile.version != 1) {
        problems.append(QString("version: %1 is not supported, expected 1").arg(object.value("version").toVariant().toString()));
        return std::nullopt;
    }
    profile.brightness = object.value("brightness").toInt(100);
    profile.speed = object.value("speed").toInt(100);

    if (!object.value("base").isObject()) {
        problems.append("base: missing");
    } else {
        const QJsonObject base = object.value("base").toObject();
        profile.base.type = base.value("type").toString();
        if (base.contains("color"))
            if (const auto color = parseColor(base.value("color"), "base", &problems))
                profile.base.color = *color;
        if (profile.base.type == "wave") {
            profile.base.steps = stepsFromJson(base.value("steps"), "base", &problems);
            profile.base.wave = waveFromJson(base);
        }
    }

    const QJsonObject keys = object.value("keys").toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it)
        if (const auto color = parseColor(it.value(), it.key(), &problems))
            profile.keys.insert(it.key(), *color);

    const QJsonArray effects = object.value("effects").toArray();
    for (int i = 0; i < effects.size(); ++i) {
        const QJsonObject e = effects.at(i).toObject();
        const QString where = QString("effect %1").arg(i + 1);
        EffectLayer effect;
        effect.name = e.value("name").toString();
        effect.type = e.value("type").toString();
        for (const QJsonValue &key : e.value("keys").toArray())
            effect.keys.append(key.toString());
        effect.steps = stepsFromJson(e.value("steps"), where, &problems);
        effect.wave = waveFromJson(e);
        effect.closing = e.value("closing").toInt(0);
        if (e.contains("start")) {
            if (const auto color = parseColor(e.value("start"), where, &problems))
                effect.start = *color;
        } else if (!effect.steps.isEmpty()) {
            effect.start = effect.steps.first().color;
        }
        profile.effects.append(effect);
    }

    const QJsonObject reactive = object.value("reactive").toObject();
    profile.reactive.enabled = reactive.value("enabled").toBool(false);
    profile.reactive.duration = reactive.value("duration").toInt(30);
    if (reactive.contains("color"))
        if (const auto color = parseColor(reactive.value("color"), "reactive", &problems))
            profile.reactive.color = *color;

    if (problems.size() > before)
        return std::nullopt;
    return profile;
}

QStringList validate(const Profile &profile, const Layout &layout) {
    QStringList errors;
    auto checkSteps = [&](const QList<Step> &steps, const QString &where) {
        if (steps.isEmpty())
            errors.append(QString("%1: needs at least one step").arg(where));
        // The closing transition back to the start color takes one more slot.
        if (steps.size() + 1 > kMaxSteps)
            errors.append(QString("%1: %2 steps, at most %3 transitions per effect")
                              .arg(where).arg(steps.size()).arg(kMaxSteps));
        for (const Step &step : steps)
            if (step.duration < 1 || step.duration > 65535)
                errors.append(QString("%1: duration %2 out of range 1..65535").arg(where).arg(step.duration));
    };

    if (profile.version != 1)
        errors.append(QString("version: %1 is not supported, expected 1").arg(profile.version));
    if (profile.brightness < 1 || profile.brightness > 100)
        errors.append(QString("brightness: %1 out of range 1..100").arg(profile.brightness));
    if (profile.speed < 25 || profile.speed > 400)
        errors.append(QString("speed: %1 out of range 25..400").arg(profile.speed));
    if (!kBaseTypes.contains(profile.base.type))
        errors.append(QString("base: unknown type \"%1\"").arg(profile.base.type));
    if (profile.base.type == "wave") {
        checkSteps(profile.base.steps, "base");
        errors += validateWave(profile.base.wave, "base", layout);
    }

    for (auto it = profile.keys.begin(); it != profile.keys.end(); ++it)
        if (!layout.byId(it.key()))
            errors.append(QString("keys: unknown key %1").arg(it.key()));

    QSet<QString> claimed;
    for (int i = 0; i < profile.effects.size(); ++i) {
        const EffectLayer &effect = profile.effects[i];
        const QString where = QString("effect %1").arg(effect.name.isEmpty() ? QString::number(i + 1) : effect.name);
        if (!kEffectTypes.contains(effect.type))
            errors.append(QString("%1: unknown type \"%2\"").arg(where, effect.type));
        if (effect.type == "wave")
            errors += validateWave(effect.wave, where, layout);
        checkSteps(effect.steps, where);
        for (const QString &key : effect.keys) {
            if (!layout.byId(key))
                errors.append(QString("%1: unknown key %2").arg(where, key));
            else if (claimed.contains(key))
                errors.append(QString("%1: %2 already belongs to another effect").arg(where, key));
            claimed.insert(key);
        }
    }
    if (animatedSlots(profile) > kMaxAnimatedSlots)
        errors.append(QString("effects: %1 animated effects, at most %2 in this version")
                          .arg(animatedSlots(profile)).arg(kMaxAnimatedSlots));
    return errors;
}
