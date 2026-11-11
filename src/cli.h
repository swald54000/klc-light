#pragma once

#include <QStringList>
#include <QTextStream>
#include <functional>

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
    // The visualizer's socket; empty to never look for it. While the visualizer runs it
    // owns the keyboard: `apply` and `off` only record what to show and tell it.
    QString visualizerSocket = {};
    // Run `systemctl --user <args>`: the exit code, and what it printed. Unset: no service manager.
    std::function<int(const QStringList &)> systemctl = {};
    std::function<QString(const QStringList &)> systemctlOutput = {};
};

constexpr char kVisualizerUnit[] = "klc-light-visualizer.service";

// What `klc-light restore` would put on the keyboard, for the visualizer: its colors while
// it runs, and what it gives back when it stops.
struct ResolvedProfile {
    std::optional<Profile> profile; // the applied profile, the active theme's if that one is gone; none if neither
    bool off = false;               // the keyboard was turned off: black is what goes back
    QString name;
    QStringList errors;
};
ResolvedProfile resolveProfile(const QString &libraryDir, const ThemePaths &paths);

// `klc-light apply [<name> | <file.json>] [--theme] [--check] [--json]`, `off`, `restore`, `list`,
// `visualizer on|off [--json]`.
// Exit 0 on success or when there is nothing to apply, 2 when the keyboard's controller is
// missing from the USB bus, 1 on any other error.
int runCli(const QStringList &args, CliEnv &env);
