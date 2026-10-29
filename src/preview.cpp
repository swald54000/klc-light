#include "preview.h"

#include <limits>

#include "layout.h"

Preview::Preview(const Profile &profile, const Layout &layout) : m_layout(layout) {
    m_base = scaled(EffectDef{profile.base.color, {}, 1, {}}, profile.brightness, 100).start;
    for (auto it = profile.keys.begin(); it != profile.keys.end(); ++it)
        m_static.insert(it.key(), scaled(EffectDef{it.value(), {}, 1, {}}, profile.brightness, 100).start);

    auto leftmost = [&layout](const QStringList &ids) {
        double x = std::numeric_limits<double>::max();
        for (const QString &id : ids)
            if (const Key *key = layout.byId(id); key && key->visible)
                x = std::min(x, key->x);
        return x == std::numeric_limits<double>::max() ? 0.0 : x;
    };

    if (profile.base.type != "static") {
        m_baseSlot = 0;
        const bool wave = profile.base.type == "wave" || profile.base.type == "rainbow";
        m_animated.append({scaled(baseEffect(profile.base), profile.brightness, profile.speed), wave,
                           leftmost(layout.group("all"))});
    }
    for (const EffectLayer &layer : profile.effects) {
        for (const QString &id : layer.keys)
            m_slotOfKey.insert(id, m_animated.size());
        m_animated.append({scaled(layerEffect(layer), profile.brightness, profile.speed), layer.type == "wave",
                           leftmost(layer.keys)});
    }
}

QColor Preview::cycleColor(const EffectDef &def, qint64 ms) const {
    QList<QColor> points{def.start};
    QList<int> durations;
    for (const Step &step : def.steps) {
        points.append(step.color);
        durations.append(step.duration);
    }
    points.append(def.start);
    durations.append(def.closing);

    qint64 total = 0;
    for (int d : durations)
        total += qint64(d) * UnitMs;
    if (total <= 0)
        return def.start;
    qint64 t = ms % total;
    if (t < 0)
        t += total;
    for (int i = 0; i < durations.size(); ++i) {
        const qint64 length = qint64(durations[i]) * UnitMs;
        if (t < length) {
            const double f = double(t) / double(length);
            const QColor &a = points[i], &b = points[i + 1];
            return QColor(qRound(a.red() + (b.red() - a.red()) * f), qRound(a.green() + (b.green() - a.green()) * f),
                          qRound(a.blue() + (b.blue() - a.blue()) * f));
        }
        t -= length;
    }
    return def.start;
}

QColor Preview::colorAt(const QString &keyId, qint64 ms) const {
    int slot = m_slotOfKey.value(keyId, -1);
    if (slot < 0 && m_static.contains(keyId))
        return m_static.value(keyId);
    if (slot < 0)
        slot = m_baseSlot;
    if (slot < 0)
        return m_base;
    const Animated &animated = m_animated[slot];
    qint64 delay = 0;
    if (animated.wave)
        if (const Key *key = m_layout.byId(keyId))
            delay = qint64((key->x - animated.leftmost) * WaveMsPerUnit);
    return cycleColor(animated.def, ms - delay);
}
