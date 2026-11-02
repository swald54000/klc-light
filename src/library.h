#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

#include "document.h"
#include "themeprofile.h"

struct LibraryEntry {
    QString name;
    QString path;
    bool readable = true;
};

// The user's named profiles, kept as <dir>/profiles/<file>.klc.json, plus <dir>/state.json:
// which profile is applied and which Omarchy theme each one is linked to (one theme, one profile).
class Library {
public:
    explicit Library(QString dir); // ~/.config/klc-light
    QString dir() const { return m_dir; }
    QList<LibraryEntry> entries() const; // sorted by name
    QStringList names() const;
    std::optional<Document> load(const QString &name, QStringList *errors) const;
    bool save(const Document &doc, QString *error); // create or overwrite by doc.name
    bool rename(const QString &from, const QString &to, QString *error);
    bool remove(const QString &name, QString *error);
    QString nameError(const QString &name, const QString &except = {}) const; // empty if usable

    QString applied() const { return m_applied; }
    void setApplied(const QString &name);
    QString profileForTheme(const QString &theme) const { return m_themes.value(theme); }
    QString themeOf(const QString &profile) const;
    // Links the profile to the theme (empty theme unlinks); returns the profile the theme was
    // linked to before, empty if none.
    QString link(const QString &profile, const QString &theme);

    // First run: one profile per themed keyboard setup, linked to its theme. Only when
    // <dir>/profiles does not exist yet; returns whether it ran.
    bool migrate(const ThemePaths &paths);

    static QString fileNameFor(const QString &name, const QStringList &takenFileNames);

private:
    QString profilesDir() const { return m_dir + "/profiles"; }
    void writeState();
    void unlink(const QString &profile);
    std::optional<LibraryEntry> find(const QString &name) const;

    QString m_dir;
    QString m_applied;
    QMap<QString, QString> m_themes; // theme -> profile
};
