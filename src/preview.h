#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

#include "effects.h"
#include "profile.h"

class Layout;

// Computes what each key shows at a given time, from the same color cycles the keyboard
// runs, so the editor can animate the profile without sending anything.
class Preview {
public:
    // Length of one controller time unit. An estimate until compared with the keyboard.
    static constexpr int UnitMs = 10;
    // Waves travel left to right this many milliseconds per layout pixel (about 70 ms per
    // key); an approximation of the firmware's wave origins.
    static constexpr double WaveMsPerUnit = 1.34;

    Preview(const Profile &profile, const Layout &layout);

    QColor colorAt(const QString &keyId, qint64 ms) const;

private:
    struct Animated {
        EffectDef def;
        bool wave = false;
        double leftmost = 0;
    };

    QColor cycleColor(const EffectDef &def, qint64 ms) const;

    const Layout &m_layout;
    QHash<QString, QColor> m_static;
    QHash<QString, int> m_slotOfKey;
    QList<Animated> m_animated;
    QColor m_base;
    int m_baseSlot = -1;
};
