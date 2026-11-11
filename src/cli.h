#pragma once

#include <QStringList>
#include <QTextStream>

#include "profile.h"
#include "themeprofile.h"
#include <optional>

class Device;

struct CliEnv {
    ThemePaths paths;
    QString libraryDir; // ~/.config/klc-light
    Device *device;
    QTextStream *out;
    QTextStream *err;
    // KLC_LIGHT_FAKE_DEVICE=1: print the packets ("F <hex>" or "O <hex>" per line) instead
    // of sending them.
    bool fake = false;
};

// What `klc-light restore` would put on the keyboard, for the visualizer: its colors while
// it runs, and what it gives back when it stops.
struct ResolvedProfile {
    std::optional<Profile> profile; // the applied profile, the active theme's if that one is gone; none if neither
    bool off = false;               // the keyboard was turned off: black is what goes back
    QString name;
    QStringList errors;
};
ResolvedProfile resolveProfile(const QString &libraryDir, const ThemePaths &paths);

// `klc-light apply [<name> | <file.json>] [--theme] [--check] [--json]`, `off`, `restore`, `list`.
// Exit 0 on success or when there is nothing to apply, 2 when the keyboard's controller is
// missing from the USB bus, 1 on any other error.
int runCli(const QStringList &args, CliEnv &env);
