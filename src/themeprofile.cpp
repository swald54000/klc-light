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
            home + "/.config/omarchy/themes", "/usr/share/omarchy/themes"};
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

QStringList installedThemes(const ThemePaths &paths) {
    QStringList themes;
    for (const QString &root : {paths.userThemes, paths.stockThemes})
        themes += QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    themes.removeDuplicates();
    themes.sort();
    return themes;
}

std::optional<Profile> themeLighting(const ThemePaths &paths, const QString &theme, QStringList *errors) {
    const QStringList roots = {paths.userThemes + "/" + theme, paths.stockThemes + "/" + theme};
    for (const QString &root : roots) {
        const QString path = root + "/keyboard.klc.json";
        if (QFileInfo::exists(path))
            return readProfile(path, errors);
    }
    for (const QString &root : roots) {
        const QString path = root + "/keyboard.effect", effect = readTrimmed(path);
        if (effect.isEmpty())
            continue;
        if (auto profile = profileFromLegacy(effect, QString()))
            return profile;
        errors->append(QString("%1: unknown effect \"%2\"").arg(path, effect));
        return std::nullopt;
    }
    for (const QString &root : roots) {
        const QString path = root + "/keyboard.rgb", rgb = readTrimmed(path);
        if (rgb.isEmpty())
            continue;
        if (auto profile = profileFromLegacy(QString(), rgb))
            return profile;
        errors->append(QString("%1: invalid color \"%2\"").arg(path, rgb));
        return std::nullopt;
    }
    return std::nullopt;
}
