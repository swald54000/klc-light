#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <optional>

#include "profile.h"

// One entry of the controller's transition table: for `duration` time units (10 ms),
// add these slopes, in 1/16 color level, to each channel once per unit.
struct Transition {
    qint8 dr = 0, dg = 0, db = 0;
    quint16 duration = 0;
    bool operator==(const Transition &o) const {
        return dr == o.dr && dg == o.dg && db == o.db && duration == o.duration;
    }
};

// A color cycle: start color, then fade to each step's color, then back to the start
// over `closing` time units. Without a wave every linked key runs it in step (a pulse).
struct EffectDef {
    QColor start;
    QList<Step> steps;
    int closing = 0;
    std::optional<Wave> wave;
};

EffectDef baseEffect(const BaseLayer &base);
EffectDef layerEffect(const EffectLayer &layer);
// The user's "ares" and "tron" effects, the waves palette or the pulse palette.
EffectDef builtinPalette(const QString &name, bool pulse);
EffectDef scaled(const EffectDef &def, int brightness, int speed);
QList<Transition> transitions(const EffectDef &def, QString *error);
