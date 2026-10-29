#pragma once

#include <QString>
#include <QStringList>
#include <optional>

#include "profile.h"

// Where Omarchy keeps the active theme and the user's theme overrides.
struct ThemePaths {
    QString stateTheme; // ~/.local/state/omarchy/current/theme (the assembled theme)
    QString themeName;  // ~/.local/state/omarchy/current/theme.name
    QString userThemes; // ~/.config/omarchy/themes
    static ThemePaths standard();
};

struct Resolved {
    std::optional<Profile> profile;
    QString source;   // File the profile came from; empty when there is none.
    QString savePath; // Where the editor saves this theme's profile.
    QString themeName;
    QStringList errors;
};

// The active theme's lighting: its keyboard.klc.json (the user's override first, then the
// assembled theme), else keyboard.effect, else keyboard.rgb, else nothing.
Resolved resolveProfile(const ThemePaths &paths);
std::optional<Profile> readProfile(const QString &path, QStringList *errors);
// Writes beside the target and renames, so a crash never leaves half a profile.
bool saveProfile(const Profile &profile, const QString &path, QString *error);
