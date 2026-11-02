#pragma once

#include <QStringList>
#include <QTextStream>

#include "themeprofile.h"

class Device;

struct CliEnv {
    ThemePaths paths;
    QString libraryDir; // ~/.config/klc-light
    Device *device;
    QTextStream *out;
    QTextStream *err;
};

// `klc-light apply [<name> | <file.json>] [--theme] [--check] [--json] [--experimental-reactive]`.
// Exit 0 on success or when there is nothing to apply, 2 when the keyboard's controller is
// missing from the USB bus, 1 on any other error.
int runCli(const QStringList &args, CliEnv &env);
