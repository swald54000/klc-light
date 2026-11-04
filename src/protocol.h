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
// `errors` is not empty. Reactive lighting stays off unless explicitly allowed, since it
// has not been validated on this keyboard's firmware.
BuildResult buildPackets(const Profile &profile, const Layout &layout, bool allowReactive = false);

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
