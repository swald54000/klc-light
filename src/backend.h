#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <optional>

#include "device.h"
#include "keylabels.h"
#include "preview.h"
#include "profile.h"
#include "themeprofile.h"

// Carried from the sending thread back to the editor.
Q_DECLARE_METATYPE(ApplyResult)

// The editor's state: the active theme's profile, the key selection, undo history, and
// applying (save, then one send to the keyboard on a worker thread).
//
// Layers, as the left column lists them: 0 = base, 1 = per-key colors, 2.. = effects,
// last = reactive.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY profileChanged)
    Q_PROPERTY(int currentLayer READ currentLayer WRITE setCurrentLayer NOTIFY currentLayerChanged)
    Q_PROPERTY(QString currentKind READ currentKind NOTIFY currentLayerChanged)
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
    Q_PROPERTY(QString themeName READ themeName NOTIFY themeChanged)
    Q_PROPERTY(QString profilePath READ profilePath NOTIFY themeChanged)
    Q_PROPERTY(bool experimentalReactive READ experimentalReactive WRITE setExperimentalReactive NOTIFY profileChanged)
    Q_PROPERTY(QStringList savedColors READ savedColors NOTIFY savedColorsChanged)

public:
    using Sender = std::function<ApplyResult(const QList<Packet> &)>;

    Backend(ThemePaths paths, KeyLabels labels, Sender sender, QString colorsPath, QObject *parent = nullptr);
    ~Backend() override;

    QVariantList keys() const { return m_keys; }
    QVariantList layers() const;
    int currentLayer() const { return m_currentLayer; }
    void setCurrentLayer(int layer);
    QString currentKind() const;
    QVariantMap current() const;
    QStringList selection() const { return m_selection; }
    bool dirty() const { return m_dirty; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int revision() const { return m_revision; }
    int brightness() const { return m_profile.brightness; }
    void setBrightness(int value);
    int speed() const { return m_profile.speed; }
    void setSpeed(int value);
    bool busy() const { return m_busy; }
    int cooldownMs() const { return m_cooldownMs; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    QString themeName() const { return m_themeName; }
    QString profilePath() const { return m_savePath; }
    bool experimentalReactive() const { return m_experimentalReactive; }
    void setExperimentalReactive(bool enabled);
    QStringList savedColors() const { return m_savedColors; }
    const Profile &profile() const { return m_profile; }

    Q_INVOKABLE void select(const QStringList &ids, bool add);
    Q_INVOKABLE void selectGroup(const QString &name);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void setColor(const QColor &color);
    Q_INVOKABLE void setBaseType(const QString &type);
    Q_INVOKABLE void addEffect(const QString &type);
    Q_INVOKABLE void removeLayer(int layer);
    Q_INVOKABLE void assignSelection();
    Q_INVOKABLE void setStepColor(int index, const QColor &color);
    Q_INVOKABLE void setStepDuration(int index, int duration);
    Q_INVOKABLE void addStep();
    Q_INVOKABLE void removeStep(int index);
    Q_INVOKABLE void setOrigin(const QString &origin);
    Q_INVOKABLE void setReactiveEnabled(bool enabled);
    Q_INVOKABLE void setReactiveDuration(int duration);
    Q_INVOKABLE void setSavedColor(int index, const QColor &color);
    Q_INVOKABLE QColor previewColor(const QString &id, qint64 ms) const;
    Q_INVOKABLE void apply();
    // Everything edited between these two calls is one undo step (press, drag, release).
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void reloadTheme();
    Q_INVOKABLE void dismissError();

signals:
    void profileChanged();
    void currentLayerChanged();
    void selectionChanged();
    void busyChanged();
    void cooldownChanged();
    void errorChanged();
    void themeChanged();
    void savedColorsChanged();
    void applyFinished(const ApplyResult &result);

private:
    // Every edit goes through here: snapshot for undo, apply the change, refresh.
    void edit(const std::function<void(Profile &)> &change);
    void changed();
    void setError(const QString &error);
    void loadProfile();
    void watchTheme();
    void onApplyFinished(const ApplyResult &result);
    EffectLayer *currentEffect();
    const EffectLayer *currentEffect() const;
    QList<Step> *currentSteps();
    void loadSavedColors();
    void storeSavedColors() const;

    ThemePaths m_paths;
    KeyLabels m_labels;
    Sender m_sender;
    QString m_colorsPath;
    QVariantList m_keys;

    Profile m_profile;
    std::optional<Preview> m_preview;
    QList<Profile> m_undo, m_redo;
    bool m_inGesture = false;       // between beginGesture() and endGesture()
    bool m_gestureRecorded = false; // the gesture already pushed its undo step
    QStringList m_selection;
    int m_currentLayer = 0;
    int m_revision = 0;
    bool m_dirty = false;
    bool m_busy = false;
    bool m_experimentalReactive = false;
    int m_cooldownMs = 0;
    QString m_error, m_notice, m_themeName, m_savePath;
    QStringList m_savedColors;
    QFileSystemWatcher m_watcher;
    QTimer m_cooldown;
};
