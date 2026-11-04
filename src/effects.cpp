#include "effects.h"

#include <algorithm>
#include <cfenv>
#include <cstdlib>
#include <cmath>

namespace {

QColor rgb(int r, int g, int b) { return QColor(r, g, b); }

// Python's round(): halves go to the even neighbour, which the controller tables the
// user validated were computed with.
int roundHalfEven(double value) {
    std::fesetround(FE_TONEAREST);
    return int(std::nearbyint(value));
}

} // namespace

EffectDef baseEffect(const BaseLayer &base) {
    const QColor c = base.color, black = Qt::black;
    if (base.type == "breathing")
        return {c, {{c, 40}, {black, 80}}, 80, "aqua-0"};
    if (base.type == "flashing")
        return {c, {{c, 20}, {black, 2}, {black, 20}}, 2, "aqua-0"};
    if (base.type == "rainbow")
        return {rgb(255, 0, 0),
                {{rgb(255, 255, 0), 40}, {rgb(0, 255, 0), 40}, {rgb(0, 255, 255), 40},
                 {rgb(0, 0, 255), 40}, {rgb(255, 0, 255), 40}},
                40, "aqua-1"};
    if (base.type == "wave")
        return {base.steps.isEmpty() ? c : base.steps.first().color, base.steps,
                base.steps.isEmpty() ? 1 : base.steps.first().duration, base.origin};
    return {c, {}, 0, QString()};
}

EffectDef layerEffect(const EffectLayer &layer) {
    const int closing = layer.closing > 0 ? layer.closing : layer.steps.isEmpty() ? 1 : layer.steps.first().duration;
    return {layer.start, layer.steps, closing, layer.origin};
}

// Copied from EFFECTS in ~/.local/share/msi-perkeyrgb-setup/msi-kbd-effect, so the themes
// that use them look exactly as they did.
EffectDef builtinPalette(const QString &name, bool pulse) {
    if (name == "tron") {
        if (pulse)
            return {rgb(0x00, 0x3C, 0xFF), {{rgb(0x00, 0x3C, 0xFF), 60}, {rgb(0x00, 0xDC, 0xFF), 120}}, 120, "aqua-0"};
        return {rgb(0x00, 0xE5, 0xFF),
                {{rgb(0x00, 0xE5, 0xFF), 33}, {rgb(0x00, 0x6D, 0xFF), 40}, {rgb(0x0A, 0x27, 0xD3), 50},
                 {rgb(0x82, 0xE9, 0xFF), 40}, {rgb(0x82, 0xE9, 0xFF), 60}},
                40, "aqua-1"};
    }
    if (name == "ares") {
        if (pulse)
            return {rgb(0x50, 0x00, 0x00), {{rgb(0x50, 0x00, 0x00), 30}, {rgb(0xFF, 0x10, 0x10), 60}}, 60, "aqua-0"};
        return {rgb(0xFF, 0x00, 0x00),
                {{rgb(0xFF, 0x00, 0x00), 14}, {rgb(0xC8, 0x00, 0x1E), 16}, {rgb(0x8C, 0x00, 0x00), 18},
                 {rgb(0xFF, 0x2A, 0x00), 12}, {rgb(0xFF, 0x6A, 0x10), 12}, {rgb(0xFF, 0xAA, 0x00), 10},
                 {rgb(0xFF, 0xD0, 0xA0), 6}, {rgb(0xFF, 0xD0, 0xA0), 4}, {rgb(0xFF, 0x00, 0x2A), 12}},
                14, "aqua-1"};
    }
    return {};
}

EffectDef scaled(const EffectDef &def, int brightness, int speed) {
    auto color = [brightness](const QColor &c) {
        return QColor(roundHalfEven(c.red() * brightness / 100.0), roundHalfEven(c.green() * brightness / 100.0),
                      roundHalfEven(c.blue() * brightness / 100.0));
    };
    auto duration = [speed](int d) { return std::max(1, roundHalfEven(d * 100.0 / speed)); };
    EffectDef result{color(def.start), {}, duration(def.closing), def.origin};
    for (const Step &step : def.steps)
        result.steps.append({color(step.color), duration(step.duration)});
    return result;
}

QList<Transition> transitions(const EffectDef &def, QString *error) {
    QList<QColor> targets;
    QList<int> durations;
    for (const Step &step : def.steps) {
        targets.append(step.color);
        durations.append(step.duration);
    }
    targets.append(def.start);
    durations.append(def.closing);

    QList<Transition> result;
    if (durations.size() > kMaxSteps) {
        if (error)
            *error = QString("%1 transitions, at most %2").arg(durations.size()).arg(kMaxSteps);
        return result;
    }
    // The controller holds colors in 8.4 fixed point and adds each slope once per time unit.
    int value[3] = {def.start.red() * 16, def.start.green() * 16, def.start.blue() * 16};
    int total = 0;
    for (int i = 0; i < durations.size(); ++i) {
        if (durations[i] < 1 || durations[i] > 65535) {
            if (error)
                *error = QString("transition %1: duration %2 out of range 1..65535").arg(i + 1).arg(durations[i]);
            return {};
        }
        const int target[3] = {targets[i].red() * 16, targets[i].green() * 16, targets[i].blue() * 16};
        // A slope fits a signed byte: a step too short for its color change is lengthened
        // (a full swing needs 33 units) rather than left short of its color.
        int duration = durations[i];
        for (int c = 0; c < 3; ++c)
            duration = std::max(duration, (std::abs(target[c] - value[c]) + 126) / 127);
        // Integer division rounds toward zero, so a step never overshoots its color: the
        // controller would wrap the channel instead of stopping at 0 or 255.
        int slope[3];
        for (int c = 0; c < 3; ++c) {
            slope[c] = (target[c] - value[c]) / duration;
            value[c] += slope[c] * duration;
        }
        total += duration;
        result.append({qint8(slope[0]), qint8(slope[1]), qint8(slope[2]), quint16(duration)});
    }
    // The packet stores the cycle's total duration in 16 bits too.
    if (total > 65535) {
        if (error)
            *error = QString("cycle lasts %1 units, at most 65535").arg(total);
        return {};
    }
    return result;
}
