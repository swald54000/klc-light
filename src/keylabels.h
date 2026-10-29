#pragma once

#include <QHash>
#include <QString>

#include "layout.h"

// What each key shows under the active keyboard layout, so an AZERTY keyboard is drawn
// as AZERTY while profiles keep layout-independent key ids.
class KeyLabels {
public:
    static KeyLabels fromRmlvo(const QString &layout, const QString &variant);
    // Asks Hyprland for the configured layout; falls back to "us".
    static KeyLabels current();

    QString label(const Key &key) const;

private:
    QHash<QString, QString> m_labels;
};
