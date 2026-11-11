#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <optional>

#include "profile.h"

class Layout;

struct Packet {
    enum Kind { Feature, Output } kind;
    QByteArray data; // Report id first, sent as-is.
};

struct BuildResult {
    QList<Packet> packets;
    QStringList errors;
};

// Every packet that shows the profile on the KLC, in send order. Nothing is sent if
// `errors` is not empty.
BuildResult buildPackets(const Profile &profile, const Layout &layout);

// GG's host-stream mode (docs/origins.md, 2026-11-11): keys in mask 2 take their color
// from frames the computer sends, for as long as it sends them.
struct KeyColor {
    quint8 hid;
    QColor color;
    bool operator==(const KeyColor &o) const { return hid == o.hid && color == o.color; }
};
using Frame = QList<KeyColor>;
// The most keys one frame packet may carry: 113 in one packet are not shown, 57 are.
constexpr int kStreamKeysPerPacket = 57;
// Every code the stream addresses, in the order of the enter packets.
QList<quint8> streamHids(const Layout &layout);
// Four region packets with every key black in mask 2, then the output report 0D 00 02.
// That report goes out once: sent after a frame, it clears it.
QList<Packet> streamEnterPackets(const Layout &layout);
// Feature reports 0C 00 <n> 00 followed by n × [hid R G B], unused bytes FF.
QList<Packet> streamFramePackets(const Frame &frame);

// What a wave tells the controller (effect packet 0x8C..0x95, 0x9A), as SteelSeries GG
// encodes it: a point in the controller's units, the axes the distance is measured along
// (x only: horizontal, y only: vertical, both: radial), the propagation speed and whether
// the wave runs away from the point rather than toward it.
struct WaveFields {
    int x = 0, y = 0;
    bool alongX = false, alongY = false;
    int speed = 0;
    bool away = false;
};

WaveFields waveFields(const Wave &wave, const Layout &layout);
// Size 1..100 to GG's propagation speed 31..1001, on a log scale (68 → 326).
int waveSpeed(int size);

// The "ares" and "tron" effects of msi-kbd-effect, on the captured aqua key mapping.
Profile builtinProfile(const QString &name);
// A profile from the older theme files: keyboard.effect names a built-in effect,
// keyboard.rgb holds one color for every key.
std::optional<Profile> profileFromLegacy(const QString &effectName, const QString &rgbHex);
