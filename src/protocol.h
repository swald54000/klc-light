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

// The "ares" and "tron" effects of msi-kbd-effect, on the captured aqua key mapping.
Profile builtinProfile(const QString &name);
// A profile from the older theme files: keyboard.effect names a built-in effect,
// keyboard.rgb holds one color for every key.
std::optional<Profile> profileFromLegacy(const QString &effectName, const QString &rgbHex);
