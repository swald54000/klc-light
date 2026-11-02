#include "library.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace {

const QString kSuffix = ".klc.json";

QString baseName(const QString &fileName) { return fileName.chopped(kSuffix.size()); }

QString capitalized(QString theme) {
    if (!theme.isEmpty())
        theme[0] = theme[0].toUpper();
    return theme;
}

} // namespace

Library::Library(QString dir) : m_dir(std::move(dir)) {
    QFile file(m_dir + "/state.json");
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) {
        file.close();
        QString bad = m_dir + "/state.json.bad";
        for (int n = 2; QFileInfo::exists(bad); ++n)
            bad = m_dir + "/state.json.bad." + QString::number(n);
        if (!file.rename(bad)) {
            // Keep the corrupt bytes before the next write replaces state.json.
            qWarning("klc-light: cannot move %s aside: %s", qPrintable(m_dir + "/state.json"), qPrintable(file.errorString()));
            copyAside(bad, bytes);
        }
        return;
    }
    const QJsonObject state = document.object(), themes = state.value("themes").toObject();
    m_applied = state.value("applied").toString();
    for (auto it = themes.begin(); it != themes.end(); ++it)
        if (it.value().isString() && !it.value().toString().isEmpty())
            m_themes.insert(it.key(), it.value().toString());
}

void Library::copyAside(const QString &path, const QByteArray &bytes) {
    QFile copy(path);
    if (!copy.open(QIODevice::WriteOnly) || copy.write(bytes) < 0)
        qWarning("klc-light: cannot keep a copy of the corrupt state in %s", qPrintable(path));
}

void Library::writeState() {
    QJsonObject themes;
    for (auto it = m_themes.begin(); it != m_themes.end(); ++it)
        themes.insert(it.key(), it.value());
    QDir().mkpath(m_dir);
    QSaveFile file(m_dir + "/state.json");
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(QJsonObject{{"applied", m_applied}, {"themes", themes}}).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit())
        qWarning("klc-light: cannot write %s: %s", qPrintable(m_dir + "/state.json"), qPrintable(file.errorString()));
}

QList<LibraryEntry> Library::entries() const {
    QList<LibraryEntry> list;
    const QDir dir(profilesDir());
    for (const QString &fileName : dir.entryList({"*" + kSuffix}, QDir::Files)) {
        LibraryEntry entry;
        entry.path = dir.filePath(fileName);
        QStringList errors;
        const auto doc = readDocument(entry.path, &errors);
        entry.readable = doc.has_value();
        entry.name = doc && !doc->name.isEmpty() ? doc->name : baseName(fileName);
        list.append(entry);
    }
    std::sort(list.begin(), list.end(), [](const LibraryEntry &a, const LibraryEntry &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return list;
}

QStringList Library::names() const {
    QStringList names;
    for (const LibraryEntry &entry : entries())
        names.append(entry.name);
    return names;
}

std::optional<LibraryEntry> Library::find(const QString &name) const {
    for (const LibraryEntry &entry : entries())
        if (entry.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return entry;
    return std::nullopt;
}

std::optional<Document> Library::load(const QString &name, QStringList *errors) const {
    const auto entry = find(name);
    if (!entry) {
        errors->append(QString("no profile named \"%1\"").arg(name));
        return std::nullopt;
    }
    return readDocument(entry->path, errors);
}

QString Library::fileNameFor(const QString &name, const QStringList &takenFileNames) {
    QString base;
    for (const QChar c : name.toLower())
        base += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : QChar('-');
    if (base.isEmpty())
        base = "profile";
    QString candidate = base;
    for (int n = 2; takenFileNames.contains(candidate); ++n)
        candidate = base + "-" + QString::number(n);
    return candidate;
}

bool Library::save(const Document &saved, QString *error) {
    Document doc = saved;
    doc.name = doc.name.trimmed();
    QString path;
    if (const auto existing = find(doc.name)) {
        if (!existing->readable) {
            *error = QString("\"%1\" exists but cannot be read; not overwriting it").arg(doc.name);
            return false;
        }
        path = existing->path;
    } else {
        QStringList taken;
        for (const QString &fileName : QDir(profilesDir()).entryList({"*" + kSuffix}, QDir::Files))
            taken.append(baseName(fileName));
        path = profilesDir() + "/" + fileNameFor(doc.name, taken) + kSuffix;
    }
    return saveDocument(doc, path, error);
}

QString Library::nameError(const QString &name, const QString &except) const {
    if (name.trimmed().isEmpty())
        return QCoreApplication::translate("Library", "The name is empty.");
    if (name.trimmed().compare(except.trimmed(), Qt::CaseInsensitive) != 0)
        for (const QString &existing : names())
            if (existing.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
                return QCoreApplication::translate("Library", "A profile named \"%1\" already exists.").arg(existing);
    return QString();
}

bool Library::rename(const QString &fromName, const QString &toName, QString *error) {
    const QString to = toName.trimmed();
    const auto entry = find(fromName);
    if (!entry) {
        *error = QString("no profile named \"%1\"").arg(fromName);
        return false;
    }
    const QString from = entry->name;
    if (from == to)
        return true;
    if (const QString problem = nameError(to, from); !problem.isEmpty()) {
        *error = problem;
        return false;
    }
    QStringList errors;
    auto doc = load(from, &errors);
    if (!doc) {
        *error = errors.join("; ");
        return false;
    }
    const QString oldPath = entry->path;
    doc->name = to;
    QStringList taken;
    for (const QString &fileName : QDir(profilesDir()).entryList({"*" + kSuffix}, QDir::Files))
        taken.append(baseName(fileName));
    taken.removeAll(baseName(QFileInfo(oldPath).fileName()));
    const QString newPath = profilesDir() + "/" + fileNameFor(to, taken) + kSuffix;
    if (!saveDocument(*doc, newPath, error))
        return false;
    if (newPath != oldPath)
        QFile::remove(oldPath);
    bool changed = false;
    if (m_applied == from) {
        m_applied = to;
        changed = true;
    }
    for (auto it = m_themes.begin(); it != m_themes.end(); ++it)
        if (it.value() == from) {
            it.value() = to;
            changed = true;
        }
    if (changed)
        writeState();
    return true;
}

bool Library::remove(const QString &name, QString *error) {
    const auto entry = find(name);
    if (!entry) {
        *error = QString("no profile named \"%1\"").arg(name);
        return false;
    }
    if (!QFile::remove(entry->path)) {
        *error = QString("cannot remove %1").arg(entry->path);
        return false;
    }
    bool changed = false;
    if (m_applied == name) {
        m_applied.clear();
        changed = true;
    }
    for (auto it = m_themes.begin(); it != m_themes.end();) {
        if (it.value() == name) {
            it = m_themes.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        writeState();
    return true;
}

void Library::setApplied(const QString &name) {
    if (m_applied == name)
        return;
    m_applied = name;
    writeState();
}

QString Library::themeOf(const QString &profile) const {
    for (auto it = m_themes.begin(); it != m_themes.end(); ++it)
        if (it.value() == profile)
            return it.key();
    return QString();
}

void Library::unlink(const QString &profile) {
    for (auto it = m_themes.begin(); it != m_themes.end();) {
        if (it.value() == profile)
            it = m_themes.erase(it);
        else
            ++it;
    }
}

QString Library::link(const QString &profile, const QString &theme) {
    const QString previous = m_themes.value(theme);
    unlink(profile);
    if (!theme.isEmpty())
        m_themes.insert(theme, profile);
    writeState();
    return previous == profile ? QString() : previous;
}

bool Library::migrate(const ThemePaths &paths) {
    if (QFileInfo::exists(profilesDir()))
        return false;
    if (!QDir().mkpath(profilesDir()))
        qWarning("klc-light: migration: cannot create %s", qPrintable(profilesDir()));
    QFile themeName(paths.themeName);
    const QString active = themeName.open(QIODevice::ReadOnly) ? QString::fromUtf8(themeName.readAll()).trimmed() : QString();
    bool any = false;
    for (const QString &theme : installedThemes(paths)) {
        QStringList errors;
        const auto profile = themeLighting(paths, theme, &errors);
        for (const QString &problem : errors)
            qWarning("klc-light: migration: theme %s: %s", qPrintable(theme), qPrintable(problem));
        if (!profile)
            continue;
        QString name = capitalized(theme), error;
        for (int n = 2; !nameError(name).isEmpty(); ++n)
            name = capitalized(theme) + " " + QString::number(n);
        if (save(fromLighting(*profile, name), &error)) {
            link(name, theme);
            any = true;
        } else {
            qWarning("klc-light: migration: theme %s: cannot save profile: %s", qPrintable(theme), qPrintable(error));
        }
    }
    if (!any) {
        QString error;
        if (save(fromLighting(Profile(), "Default"), &error))
            setApplied("Default");
        else
            qWarning("klc-light: migration: cannot save Default: %s", qPrintable(error));
        return true;
    }
    setApplied(profileForTheme(active));
    return true;
}
