#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// The four lighting regions of the KLC; each takes one 42-key packet.
enum class Region { Alphanum, Enter, Modifiers, Numpad };

quint8 regionCode(Region region);

struct Key {
    QString id;   // Linux input name of the physical key, e.g. "KEY_Q".
    quint8 hid;   // USB HID usage the controller addresses the key by.
    Region region;
    double x, y;  // Centre in SteelSeries GG units; y grows upwards.
    double width; // In key widths.
    bool visible = true; // False for codes the controller has but the keyboard does not show.
};

// The GS76 French ISO keyboard as the controller sees it.
class Layout {
public:
    static const Layout &gs76();

    // The keys drawn and selectable in the editor.
    QList<Key> keys() const;
    // Every code the profile can address, hidden ones included.
    QList<Key> allKeys() const { return m_keys; }
    const Key *byId(const QString &id) const;
    const Key *byHid(quint8 hid) const;
    // Every code the controller has in the region, in packet order (42 per region).
    QList<quint8> regionHids(Region region) const;
    // The leading codes the captured "aqua" preset sends for the region.
    QList<quint8> aquaHids(Region region) const;
    // Key ids of a selection group: wasd, function, digits, numpad, modifiers, all.
    QStringList group(const QString &name) const;

private:
    Layout();

    QList<Key> m_keys;
    QHash<QString, int> m_byId;
    QHash<quint8, int> m_byHid;
};
