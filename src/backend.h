#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <optional>

#include "device.h"
#include "document.h"
#include "keylabels.h"
#include "library.h"
#include "preview.h"
#include "themeprofile.h"

// Carried from the sending thread back to the editor.
Q_DECLARE_METATYPE(ApplyResult)

// The editor's state: the open profile (a Document of named key groups), the key selection,
// undo history, and applying (save to the library, then one send to the keyboard on a worker
// thread).
//
// What the right panel edits, currentKind: "base" (the whole keyboard), "group" (the open
// group, currentGroup), "selection" (free keys, not one group: offer to make a group of them)
// or "reactive".
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY profileChanged)
    Q_PROPERTY(QString currentKind READ currentKind NOTIFY currentChanged)
    Q_PROPERTY(int currentGroup READ currentGroup NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap baseSummary READ baseSummary NOTIFY profileChanged)
    Q_PROPERTY(QStringList groupKeys READ groupKeys NOTIFY currentChanged)
    Q_PROPERTY(int animatedCount READ animatedCount NOTIFY profileChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY profileChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY profileChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY profileChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY profileChanged)
    Q_PROPERTY(int revision READ revision NOTIFY profileChanged)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY profileChanged)
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY profileChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int cooldownMs READ cooldownMs NOTIFY cooldownChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY errorChanged)
    Q_PROPERTY(bool experimentalReactive READ experimentalReactive WRITE setExperimentalReactive NOTIFY profileChanged)
    Q_PROPERTY(QStringList savedColors READ savedColors NOTIFY savedColorsChanged)
    // The next click on a key sets the open wave's origin instead of selecting it.
    Q_PROPERTY(bool picking READ picking NOTIFY pickingChanged)
    // The library, read afresh from disk each time: a theme hook can change it while the
    // editor is open.
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY libraryChanged)
    Q_PROPERTY(QStringList unreadableProfiles READ unreadableProfiles NOTIFY libraryChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY libraryChanged)
    Q_PROPERTY(QString appliedName READ appliedName NOTIFY libraryChanged)
    Q_PROPERTY(QString linkedTheme READ linkedTheme NOTIFY libraryChanged)
    Q_PROPERTY(QStringList themes READ themes CONSTANT)

public:
    using Sender = std::function<ApplyResult(const QList<Packet> &)>;

    Backend(ThemePaths paths, QString libraryDir, KeyLabels labels, Sender sender, QString colorsPath,
            QObject *parent = nullptr);
    ~Backend() override;

    QVariantList keys() const { return m_keys; }
    QVariantList groups() const; // [{name, behavior, count, color}]
    QString currentKind() const;
    int currentGroup() const { return m_kind == Kind::Group ? m_group : -1; }
    QVariantMap baseSummary() const; // {type, color}: shown whichever panel is open
    QStringList groupKeys() const;
    int animatedCount() const; // controller slots in use, the animated base included
    QVariantMap current() const;
    QStringList selection() const { return m_selection; }
    bool dirty() const { return m_dirty; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int revision() const { return m_revision; }
    int brightness() const { return m_doc.brightness; }
    void setBrightness(int value);
    int speed() const { return m_doc.speed; }
    void setSpeed(int value);
    bool busy() const { return m_busy; }
    int cooldownMs() const { return m_cooldownMs; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    bool experimentalReactive() const { return m_experimentalReactive; }
    void setExperimentalReactive(bool enabled);
    bool picking() const { return m_picking; }
    QStringList savedColors() const { return m_savedColors; }
    const Document &document() const { return m_doc; }
    QStringList profiles() const { return fresh().names(); }
    QStringList unreadableProfiles() const; // listed in profiles, but cannot be opened
    QString profileName() const { return m_doc.name; }
    QString appliedName() const { return fresh().applied(); }
    QString linkedTheme() const { return fresh().themeOf(m_doc.name); }
    QStringList themes() const { return installedThemes(m_paths); }

    Q_INVOKABLE void openBase();
    Q_INVOKABLE void openGroup(int index); // clears the selection
    Q_INVOKABLE void openReactive();
    // A click on one key opens its group; anything else is a free selection, unless every
    // key is in the open group.
    Q_INVOKABLE void select(const QStringList &ids, bool add);
    Q_INVOKABLE void selectGroup(const QString &name);
    Q_INVOKABLE void clearSelection();
    // From the selection; "" or the reason it was refused.
    Q_INVOKABLE QString createGroup(const QString &name);
    Q_INVOKABLE void addSelectionToGroup(int index);
    Q_INVOKABLE void removeSelectionFromGroup(); // from the open group
    Q_INVOKABLE void deleteGroup(int index);
    Q_INVOKABLE QString renameGroup(int index, const QString &name); // "" or the reason
    Q_INVOKABLE QString setBehavior(const QString &behavior);       // open group; "" or the reason
    Q_INVOKABLE QString defaultGroupName() const; // label of the single selected key, else "Group N"
    Q_INVOKABLE void setColor(const QColor &color);
    Q_INVOKABLE void setBaseType(const QString &type);
    Q_INVOKABLE void setStepColor(int index, const QColor &color);
    Q_INVOKABLE void setStepDuration(int index, int duration);
    Q_INVOKABLE void addStep();
    Q_INVOKABLE void removeStep(int index);
    // left, right, up, down, center-in, center-out, key-in, key-out, for the open wave.
    Q_INVOKABLE void setWaveDirection(const QString &direction);
    Q_INVOKABLE void setWaveSize(int size); // 1..100
    Q_INVOKABLE void pickWaveOrigin();
    Q_INVOKABLE void cancelPick();
    // True when picking: the key becomes the open wave's origin and picking ends.
    Q_INVOKABLE bool pickKey(const QString &id);
    Q_INVOKABLE void setReactiveEnabled(bool enabled);
    Q_INVOKABLE void setReactiveDuration(int duration);
    Q_INVOKABLE void setSavedColor(int index, const QColor &color);
    Q_INVOKABLE QColor previewColor(const QString &id, qint64 ms) const;
    Q_INVOKABLE void apply();
    // Profiles. Opening one discards unsaved changes (the QML asks first) and resets undo.
    // The new/duplicate/rename calls return "" or the reason they were refused.
    Q_INVOKABLE bool openProfile(const QString &name);
    Q_INVOKABLE QString newProfile(const QString &name);       // empty, saved, opened
    Q_INVOKABLE QString duplicateProfile(const QString &name); // the open document, saved, opened
    Q_INVOKABLE QString renameProfile(const QString &name);    // the open profile
    Q_INVOKABLE void deleteProfile();                          // the open one; opens the next
    // Links the open profile to the theme ("" unlinks); returns the profile the theme was
    // linked to before, if another one. Not an edit: no undo step, not dirty.
    Q_INVOKABLE QString linkTheme(const QString &theme);
    Q_INVOKABLE bool save(); // to the library, without sending
    // except: the name being renamed, free to take again (in another case, say).
    Q_INVOKABLE QString nameError(const QString &name, const QString &except = QString()) const;
    // Everything edited between these two calls is one undo step (press, drag, release).
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void dismissError();

signals:
    void profileChanged();
    void currentChanged();
    void selectionChanged();
    void busyChanged();
    void cooldownChanged();
    void errorChanged();
    void savedColorsChanged();
    void libraryChanged();
    void pickingChanged();
    void applyFinished(const ApplyResult &result);

private:
    enum class Kind { Base, Group, Selection, Reactive };

    // Every edit goes through here: snapshot for undo, apply the change, refresh.
    void edit(const std::function<void(Document &)> &change);
    void changed();
    void setCurrent(Kind kind, int group = -1);
    void setSelection(const QStringList &ids);
    void setError(const QString &error);
    void open();
    // Makes doc the open profile: no history, no selection, not dirty.
    void openDocument(const Document &doc);
    // The library as it is on disk now, never the copy read at startup: every state write
    // rewrites the whole state.json from what its Library holds.
    Library fresh() const { return Library(m_library.dir()); }
    void onApplyFinished(const ApplyResult &result);
    // The open group's steps (or the base's), in doc; nullptr when there are none to edit.
    QList<Step> *stepsIn(Document &doc) const;
    void loadSavedColors();
    void storeSavedColors() const;

    ThemePaths m_paths;
    Library m_library;
    KeyLabels m_labels;
    Sender m_sender;
    QString m_colorsPath;
    QVariantList m_keys;

    Document m_doc;
    std::optional<Preview> m_preview;
    QList<Document> m_undo, m_redo;
    bool m_inGesture = false;       // between beginGesture() and endGesture()
    // The wave the panel shows (an animated wave base or wave group), null otherwise.
    Wave *openWave(Document &doc) const;
    QVariantMap waveSummary(const Wave &wave) const;
    bool m_picking = false;         // between pickWaveOrigin() and a key click or cancelPick()
    bool m_gestureRecorded = false; // the gesture already pushed its undo step
    QStringList m_selection;
    Kind m_kind = Kind::Base;
    int m_group = -1;
    int m_revision = 0;
    bool m_dirty = false;
    bool m_busy = false;
    bool m_experimentalReactive = false;
    int m_cooldownMs = 0;
    QString m_error, m_notice;
    QString m_sending; // the profile being sent, applied once the send works
    QStringList m_savedColors;
    QTimer m_cooldown;
};
