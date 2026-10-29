#pragma once

#include <QColor>
#include <QList>
#include <QString>

#include "profile.h"

// One entry of the controller's transition table: add these deltas, in units of two
// color levels, to each channel over `duration` time units.
struct Transition {
    qint8 dr = 0, dg = 0, db = 0;
    quint16 duration = 0;
    bool operator==(const Transition &o) const {
        return dr == o.dr && dg == o.dg && db == o.db && duration == o.duration;
    }
};

// A color cycle: start color, then fade to each step's color, then back to the start
// over `closing` time units.
struct EffectDef {
    QColor start;
    QList<Step> steps;
    int closing = 0;
    QString origin;
};

EffectDef baseEffect(const BaseLayer &base);
EffectDef layerEffect(const EffectLayer &layer);
// The user's "ares" and "tron" effects, the waves palette or the pulse palette.
EffectDef builtinPalette(const QString &name, bool pulse);
EffectDef scaled(const EffectDef &def, int brightness, int speed);
QList<Transition> transitions(const EffectDef &def, QString *error);
