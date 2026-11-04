#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "profile.h"

class Layout;

// A set of keys that share one look: a plain color or an animation.
struct Group {
    QString name;
    QString behavior = "static"; // static, pulse, wave
    QStringList keys;
    QColor color = Qt::white;    // static
    QColor start;                // pulse, wave (as EffectLayer)
    QList<Step> steps;
    int closing = 0;
    Wave wave;                   // wave
    bool animated() const { return behavior != "static"; }
};

// The editable profile: the same lighting as a Profile, organised in named key groups.
struct Document {
    QString name;
    int brightness = 100, speed = 100;
    BaseLayer base;
    QList<Group> groups;
    Reactive reactive;
    int groupOf(const QString &keyId) const; // index, -1 if none
};

// The version 1 profile that sends this document's lighting.
Profile lighting(const Document &doc);
Document fromLighting(const Profile &profile, const QString &name);
QJsonObject toJson(const Document &doc); // "version": 3
// Reads version 1 (converted), 2 or 3.
std::optional<Document> documentFromJson(const QJsonObject &object, QStringList *errors);
QStringList validate(const Document &doc, const Layout &layout);
std::optional<Document> readDocument(const QString &path, QStringList *errors);
bool saveDocument(const Document &doc, const QString &path, QString *error);
