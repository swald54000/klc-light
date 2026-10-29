#include "themeprofile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include "layout.h"
#include "protocol.h"

namespace {

QString readTrimmed(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll()).trimmed();
}

} // namespace

ThemePaths ThemePaths::standard() {
    const QString home = QDir::homePath();
    return {home + "/.local/state/omarchy/current/theme", home + "/.local/state/omarchy/current/theme.name",
            home + "/.config/omarchy/themes"};
}

std::optional<Profile> readProfile(const QString &path, QStringList *errors) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errors->append(QString("%1: %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        errors->append(QString("%1: not a JSON object (%2)").arg(path, parseError.errorString()));
        return std::nullopt;
    }
    QStringList problems;
    auto profile = profileFromJson(document.object(), &problems);
    if (profile)
        problems += validate(*profile, Layout::gs76());
    for (const QString &problem : problems)
        errors->append(QString("%1: %2").arg(path, problem));
    return problems.isEmpty() ? profile : std::nullopt;
}

Resolved resolveProfile(const ThemePaths &paths) {
    Resolved resolved;
    resolved.themeName = readTrimmed(paths.themeName);
    if (resolved.themeName.isEmpty()) {
        resolved.errors.append(QString("no active Omarchy theme: %1 is missing").arg(paths.themeName));
        return resolved;
    }
    resolved.savePath = paths.userThemes + "/" + resolved.themeName + "/keyboard.klc.json";

    for (const QString &path : {resolved.savePath, paths.stateTheme + "/keyboard.klc.json"}) {
        if (!QFileInfo::exists(path))
            continue;
        resolved.source = path;
        resolved.profile = readProfile(path, &resolved.errors);
        return resolved;
    }

    const QString effectPath = paths.stateTheme + "/keyboard.effect", rgbPath = paths.stateTheme + "/keyboard.rgb";
    const QString effect = readTrimmed(effectPath), rgb = readTrimmed(rgbPath);
    if (!effect.isEmpty()) {
        if ((resolved.profile = profileFromLegacy(effect, QString()))) {
            resolved.source = effectPath;
            return resolved;
        }
        resolved.errors.append(QString("%1: unknown effect \"%2\"").arg(effectPath, effect));
    }
    if (!rgb.isEmpty()) {
        if ((resolved.profile = profileFromLegacy(QString(), rgb)))
            resolved.source = rgbPath;
        else
            resolved.errors.append(QString("%1: invalid color \"%2\"").arg(rgbPath, rgb));
    }
    return resolved;
}

bool saveProfile(const Profile &profile, const QString &path, QString *error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QString("cannot create %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    // QSaveFile writes a temporary file beside the target and renames it on commit.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson(profile)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}
