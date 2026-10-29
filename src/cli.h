#pragma once

#include <QStringList>
#include <QTextStream>

#include "themeprofile.h"

class Device;

struct CliEnv {
    ThemePaths paths;
    Device *device;
    QTextStream *out;
    QTextStream *err;
};

// `klc-light apply [profile.json] [--check] [--json] [--experimental-reactive]`.
// Exit 0 on success or when there is nothing to apply, 1 on any error.
int runCli(const QStringList &args, CliEnv &env);
