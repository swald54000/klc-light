#pragma once

#include <QColor>
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

// An animated effect on a group of keys.
struct EffectLayer {
    QString name;
    QString type;    // "wave" or "pulse"
    QStringList keys;
    QColor start;    // Cycle start; the first step's color unless given.
    QList<Step> steps;
    int closing = 0; // Duration back to the start color; 0 means the first step's.
    QString origin;  // Wave origin taken from the captured preset: "aqua-0".."aqua-5".
};

// What the whole keyboard shows under the per-key colors and effects.
struct BaseLayer {
    QString type = "static"; // static, breathing, flashing, rainbow or wave
    QColor color = Qt::black;
    QList<Step> steps;       // wave only
    QString origin;          // wave only
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
