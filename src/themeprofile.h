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
    QString stockThemes; // /usr/share/omarchy/themes
    static ThemePaths standard();
};

std::optional<Profile> readProfile(const QString &path, QStringList *errors);
// Writes beside the target and renames, so a crash never leaves half a profile.
bool saveProfile(const Profile &profile, const QString &path, QString *error);

// Every theme the user or Omarchy ships, sorted.
QStringList installedThemes(const ThemePaths &paths);
// One theme's lighting, wherever it comes from: keyboard.klc.json, else keyboard.effect, else
// keyboard.rgb, the user's copy before the stock one. Nothing when the theme has none.
std::optional<Profile> themeLighting(const ThemePaths &paths, const QString &theme, QStringList *errors);
