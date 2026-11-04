#include "protocol.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include "effects.h"
#include "layout.h"

namespace {

constexpr int kFeatureSize = 524;
constexpr int kOutputSize = 64;
constexpr int kFragmentsPerRegion = 42;

enum : quint8 { kMaskEffect = 0, kMaskSteady = 1, kMaskReactive = 8 };

// The power key is in none of msi-perkeyrgb's packets nor the captured preset; it is sent
// as a twelfth element of the 11-key packet, which the controller accepts.
constexpr quint8 kPowerHid = 0x66;

// The captured "aqua" preset of msi-perkeyrgb, the only effect packets validated on this
// firmware; new effects reuse its packets and change only the color cycle.
const QList<QByteArray> &aquaPackets() {
    static const QList<QByteArray> packets = [] {
        QList<QByteArray> list;
        QFile file(":/presets/aqua.json");
        if (file.open(QIODevice::ReadOnly))
            for (const QJsonValue &hex : QJsonDocument::fromJson(file.readAll()).array())
                list.append(QByteArray::fromHex(hex.toString().toLatin1()));
        return list;
    }();
    return packets;
}

QByteArray aquaEffectPacket(int id) {
    for (const QByteArray &packet : aquaPackets())
        if (quint8(packet.at(0)) == 0x0b && quint8(packet.at(2)) == id)
            return packet;
    return {};
}


QColor scaledColor(const QColor &color, int brightness) {
    return scaled(EffectDef{color, {}, 1, {}}, brightness, 100).start;
}

QByteArray fragment(const QColor &color, const Reactive *reactive, quint8 effect, quint8 mask, quint8 hid) {
    QByteArray f(12, '\0');
    f[0] = char(color.red());
    f[1] = char(color.green());
    f[2] = char(color.blue());
    if (reactive) {
        f[3] = char(reactive->color.red());
        f[4] = char(reactive->color.green());
        f[5] = char(reactive->color.blue());
        f[6] = char(reactive->duration & 0xFF);
        f[7] = char((reactive->duration >> 8) & 0xFF);
    }
    f[8] = char(effect);
    f[9] = char(mask);
    f[11] = char(hid);
    return f;
}

// Each channel nibble-swapped over two bytes, as the controller stores colors (0x52 -> 20 05).
QByteArray encodeColor(int value) {
    QByteArray bytes(2, '\0');
    bytes[0] = char((value & 0x0F) << 4);
    bytes[1] = char((value & 0xF0) >> 4);
    return bytes;
}

void putU16(QByteArray &packet, int at, int value) {
    packet[at] = char(value & 0xFF);
    packet[at + 1] = char((value >> 8) & 0xFF);
}

QByteArray effectPacket(const QByteArray &templatePacket, int slot, const EffectDef &def,
                        const QList<Transition> &table, const Layout &layout) {
    QByteArray packet = templatePacket;
    packet[2] = char(slot);
    if (def.wave) {
        const WaveFields f = waveFields(*def.wave, layout);
        putU16(packet, 0x8c, f.x);
        putU16(packet, 0x8e, f.y);
        putU16(packet, 0x90, f.alongX ? 1 : 0);
        putU16(packet, 0x92, f.alongY ? 1 : 0);
        putU16(packet, 0x94, f.speed);
        packet[0x9a] = char(f.away ? 1 : 0);
    }
    QByteArray entries(16 * 8, '\0');
    int total = 0;
    for (int i = 0; i < table.size(); ++i) {
        const Transition &t = table[i];
        char *e = entries.data() + i * 8;
        e[0] = char(i == 0 ? slot : i);
        e[2] = char(t.dr);
        e[3] = char(t.dg);
        e[4] = char(t.db);
        e[6] = char(t.duration & 0xFF);
        e[7] = char(t.duration >> 8);
        total += t.duration;
    }
    packet.replace(0x02, entries.size(), entries);
    packet.replace(0x84, 6, encodeColor(def.start.red()) + encodeColor(def.start.green()) + encodeColor(def.start.blue()));
    packet[0x96] = char(table.size());
    packet[0x98] = char(total & 0xFF);
    packet[0x99] = char((total >> 8) & 0xFF);
    return packet;
}

// Byte 2 counts the key elements that follow (GG's `length`): 42, 11, 24 and 36 in the
// captured packets, 12 once the power key rides with the 11.
QByteArray regionPacket(const QList<QByteArray> &fragments, bool staticTrailer) {
    QByteArray packet(4, '\0');
    packet[0] = 0x0e;
    packet[2] = char(fragments.size());
    for (const QByteArray &f : fragments)
        packet += f;
    packet += QByteArray(12 * (kFragmentsPerRegion - fragments.size()), '\0');
    // msi-perkeyrgb's static packets end in 08 39; the captured preset's end in zeros.
    // Both are kept as validated.
    QByteArray trailer(16, '\0');
    if (staticTrailer) {
        trailer[14] = 0x08;
        trailer[15] = 0x39;
    }
    return packet + trailer;
}

Packet outputRefresh() {
    QByteArray data(kOutputSize, '\0');
    data[0] = 0x09;
    return {Packet::Output, data};
}

} // namespace

int waveSpeed(int size) {
    return int(std::lround(31.0 * std::pow(1001.0 / 31.0, (std::clamp(size, 1, 100) - 1) / 99.0)));
}

WaveFields waveFields(const Wave &wave, const Layout &layout) {
    // The controller's span (docs/origins.md): x 0..4294, y 0..1075 upward.
    constexpr int right = 4294, top = 1075, midX = 2147, midY = 538;
    const Wave w = normalized(wave);
    WaveFields f;
    f.speed = waveSpeed(w.size);
    f.away = w.flow == "away";
    if (w.origin == "left" || w.origin == "right") {
        f.x = w.origin == "left" ? 0 : right;
        f.y = midY;
        f.alongX = true;
    } else if (w.origin == "top" || w.origin == "bottom") {
        f.x = midX;
        f.y = w.origin == "bottom" ? 0 : top;
        f.alongY = true;
        // The board is a quarter as tall as wide: slowed in proportion, a vertical wave
        // shows as many bands as a horizontal one of the same size.
        f.speed = std::max(1, int(std::lround(f.speed * double(top) / right)));
    } else {
        const Key *key = layout.byId(w.origin);
        f.x = key && key->cx >= 0 ? key->cx : midX;
        f.y = key && key->cy >= 0 ? key->cy : midY;
        f.alongX = f.alongY = true;
    }
    return f;
}

BuildResult buildPackets(const Profile &profile, const Layout &layout, bool allowReactive) {
    BuildResult result;
    result.errors = validate(profile, layout);
    if (!result.errors.isEmpty())
        return result;

    const Reactive *reactive = allowReactive && profile.reactive.enabled ? &profile.reactive : nullptr;
    const quint8 steadyMask = reactive ? kMaskReactive : kMaskSteady;
    auto keyColor = [&](quint8 hid) {
        const Key *key = layout.byHid(hid);
        const QColor color = key && profile.keys.contains(key->id) ? profile.keys.value(key->id) : profile.base.color;
        return scaledColor(color, profile.brightness);
    };

    // Animated slots: the base first when it moves, then each effect layer in order.
    QList<EffectDef> animated;
    QHash<quint8, int> slotOfHid;
    if (profile.base.type != "static") {
        animated.append(scaled(baseEffect(profile.base), profile.brightness, profile.speed));
        for (const Key &key : layout.allKeys())
            slotOfHid.insert(key.hid, 0);
    }
    for (const EffectLayer &layer : profile.effects) {
        for (const QString &id : layer.keys)
            slotOfHid.insert(layout.byId(id)->hid, animated.size());
        animated.append(scaled(layerEffect(layer), profile.brightness, profile.speed));
    }
    // A painted key keeps its color over an animated base; effect layers still win.
    if (profile.base.type != "static")
        for (auto it = profile.keys.begin(); it != profile.keys.end(); ++it) {
            const quint8 hid = layout.byId(it.key())->hid;
            if (slotOfHid.value(hid, -1) == 0)
                slotOfHid.remove(hid);
        }

    const QList<Region> regions{Region::Alphanum, Region::Enter, Region::Modifiers, Region::Numpad};

    if (animated.isEmpty()) {
        for (Region region : regions) {
            QList<QByteArray> fragments;
            QSet<quint8> seen;
            // The region lists repeat some codes as padding; msi-perkeyrgb writes each once,
            // with a code-0 filler past the counted elements. Only real keys are written here,
            // so byte 2 counts exactly the elements the controller reads.
            for (quint8 hid : layout.regionHids(region))
                if (hid != 0 && !seen.contains(hid)) {
                    seen.insert(hid);
                    fragments.append(fragment(keyColor(hid), reactive, 0, steadyMask, hid));
                }
            if (region == Region::Enter)
                fragments.append(fragment(keyColor(kPowerHid), reactive, 0, steadyMask, kPowerHid));
            result.packets.append({Packet::Feature, regionPacket(fragments, true)});
        }
        result.packets.append(outputRefresh());
        return result;
    }

    auto region = [&](Region r) {
        QList<QByteArray> fragments;
        QList<quint8> hids = layout.aquaHids(r);
        if (r == Region::Enter)
            hids.append(kPowerHid);
        for (quint8 hid : hids) {
            if (slotOfHid.contains(hid))
                fragments.append(fragment(Qt::black, nullptr, quint8(slotOfHid.value(hid)), kMaskEffect, hid));
            else
                fragments.append(fragment(keyColor(hid), reactive, 0, steadyMask, hid));
        }
        return Packet{Packet::Feature, regionPacket(fragments, false)};
    };

    result.packets.append(region(Region::Enter));
    result.packets.append(region(Region::Modifiers));
    // The order the captured preset sends its effects in.
    for (int slot : {4, 3, 2, 1, 0, 5}) {
        if (slot >= animated.size())
            continue;
        const EffectDef &def = animated[slot];
        QString error;
        const QList<Transition> table = transitions(def, &error);
        // The captured pulse for in-step cycles, a captured wave for waves.
        const QByteArray templatePacket = aquaEffectPacket(def.wave ? 3 : 0);
        if (!error.isEmpty() || templatePacket.isEmpty()) {
            const bool base = profile.base.type != "static" && slot == 0;
            const QString name = base ? QString("base") : QString("effect %1").arg(slot + (profile.base.type != "static" ? 0 : 1));
            result.errors.append(QString("%1: %2").arg(name, error.isEmpty() ? "unknown origin" : error));
            continue;
        }
        result.packets.append({Packet::Feature, effectPacket(templatePacket, slot, def, table, layout)});
    }
    result.packets.append(region(Region::Alphanum));
    result.packets.append(region(Region::Numpad));
    for (const QByteArray &packet : aquaPackets())
        if (quint8(packet.at(0)) == 0x09)
            result.packets.append({Packet::Feature, packet});
    result.packets.append(outputRefresh());

    for (const Packet &packet : result.packets) {
        const bool refresh = quint8(packet.data.at(0)) == 0x09;
        if (packet.data.size() != (refresh ? kOutputSize : kFeatureSize))
            result.errors.append(QString("internal: packet of %1 bytes").arg(packet.data.size()));
    }
    if (!result.errors.isEmpty())
        result.packets.clear();
    return result;
}

Profile builtinProfile(const QString &name) {
    const Layout &layout = Layout::gs76();
    Profile profile;
    profile.base.color = Qt::black;
    QList<QStringList> keysOfSlot(6);
    for (const QByteArray &packet : aquaPackets()) {
        if (quint8(packet.at(0)) != 0x0e)
            continue;
        for (int i = 0; i < kFragmentsPerRegion; ++i) {
            const char *f = packet.constData() + 4 + i * 12;
            const quint8 hid = quint8(f[11]), effect = quint8(f[8]), mask = quint8(f[9]);
            const Key *key = layout.byHid(hid);
            if (hid == 0 || !key || mask != kMaskEffect)
                continue;
            keysOfSlot[effect].append(key->id);
        }
    }
    for (int slot = 0; slot < 6; ++slot) {
        const EffectDef def = builtinPalette(name, slot == 0);
        EffectLayer layer;
        layer.name = QString("%1 %2").arg(name).arg(slot);
        layer.type = slot == 0 ? "pulse" : "wave";
        layer.keys = keysOfSlot[slot];
        layer.start = def.start;
        layer.steps = def.steps;
        layer.closing = def.closing;
        layer.wave = waveFromAqua(QString("aqua-%1").arg(slot));
        profile.effects.append(layer);
    }
    return profile;
}

std::optional<Profile> profileFromLegacy(const QString &effectName, const QString &rgbHex) {
    if (effectName == "ares" || effectName == "tron")
        return builtinProfile(effectName);
    static const QRegularExpression hex("^#?([0-9A-Fa-f]{6})$");
    const auto match = hex.match(rgbHex.trimmed());
    if (effectName.isEmpty() && match.hasMatch()) {
        Profile profile;
        profile.base.color = QColor("#" + match.captured(1));
        return profile;
    }
    return std::nullopt;
}
