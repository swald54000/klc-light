#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

class Layout;

// One step of a color cycle: fade to this color over this many controller time units.
struct Step {
    QColor color;
    int duration = 0;
};

// How a wave crosses the keyboard: toward or away from an origin, at a speed given by its
// size. The origin is a named point (left, right, top, bottom, center) or a key id.
struct Wave {
    QString origin = "left";
    QString flow = "toward"; // toward: the wave runs to the origin; away: from it
    int size = 68;           // 1..100
    bool operator==(const Wave &o) const { return origin == o.origin && flow == o.flow && size == o.size; }
    bool operator!=(const Wave &o) const { return !(*this == o); }
};

// The wave an origin of the captured preset ("aqua-0".."aqua-5") looked like.
Wave waveFromAqua(const QString &origin);
// A wave leaving an edge is the same, to the eye, as one running to the opposite edge.
Wave normalized(const Wave &wave);
QJsonObject waveToJson(const Wave &wave);
// Reads holder["wave"], else an older holder["origin"]; normalized.
Wave waveFromJson(const QJsonObject &holder);
QStringList validateWave(const Wave &wave, const QString &where, const Layout &layout);

// An animated effect on a group of keys.
struct EffectLayer {
    QString name;
    QString type;    // "wave" or "pulse"
    QStringList keys;
    QColor start;    // Cycle start; the first step's color unless given.
    QList<Step> steps;
    int closing = 0; // Duration back to the start color; 0 means the first step's.
    Wave wave;       // wave only
};

// What the whole keyboard shows under the per-key colors and effects.
struct BaseLayer {
    QString type = "static"; // static, breathing, flashing, rainbow or wave
    QColor color = Qt::black;
    QList<Step> steps;       // wave only
    Wave wave;               // wave only
};

struct Reactive {
    bool enabled = false;
    QColor color = Qt::white;
    int duration = 30;
};

struct Profile {
    int version = 1;
    int brightness = 100; // percent, 1..100
    int speed = 100;      // percent, 25..400
    BaseLayer base;
    QMap<QString, QColor> keys;
    QList<EffectLayer> effects;
    Reactive reactive;
};

// The firmware limits this version of the app works within.
constexpr int kMaxSteps = 16;
constexpr int kMaxAnimatedSlots = 6;

QJsonObject toJson(const Profile &profile);
std::optional<Profile> profileFromJson(const QJsonObject &object, QStringList *errors);
// Problems that would make the profile unsafe or impossible to send; empty when valid.
QStringList validate(const Profile &profile, const Layout &layout);
int animatedSlots(const Profile &profile);

// JSON helpers shared with the group-based format.
QString colorName(const QColor &color);
std::optional<QColor> parseColor(const QJsonValue &value, const QString &where, QStringList *errors);
QJsonArray stepsToJson(const QList<Step> &steps);
QList<Step> stepsFromJson(const QJsonValue &value, const QString &where, QStringList *errors);
