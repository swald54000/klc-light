#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

#include "effects.h"
#include "profile.h"
#include "protocol.h"
#include <optional>

class Layout;
struct Key;

// Computes what each key shows at a given time, from the same color cycles the keyboard
// runs, so the editor can animate the profile without sending anything.
class Preview {
public:
    // Length of one controller time unit. An estimate until compared with the keyboard.
    static constexpr int UnitMs = 10;

    Preview(const Profile &profile, const Layout &layout);

    QColor colorAt(const QString &keyId, qint64 ms) const;

private:
    struct Animated {
        EffectDef def;
        std::optional<WaveFields> wave;
    };

    QColor cycleColor(const EffectDef &def, qint64 ms) const;
    // How far ahead (toward) or behind (away) of the wave's point this key runs, as the
    // controller computes it: distance along the wave's axes over the propagation speed.
    double delayMs(const Key &key, const WaveFields &wave) const;

    const Layout &m_layout;
    QHash<QString, QColor> m_static;
    QHash<QString, int> m_slotOfKey;
    QList<Animated> m_animated;
    QColor m_base;
    QColor m_power;
    int m_baseSlot = -1;
};
