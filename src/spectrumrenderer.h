#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "profile.h"
#include "protocol.h"

class Layout;

// Draws band levels as bars on the keyboard, in the applied profile's colors: one band per
// column of keys, low frequencies on the left, each bar rising from the bottom row.
class SpectrumRenderer {
public:
    // 18 columns leave every column at least three keys on the GS76; 20 leave one a single key.
    static constexpr int Columns = 18, Rows = 6;

    explicit SpectrumRenderer(const Layout &layout);
    void setProfile(const Profile &profile);
    // One entry per streamHids() code, in that order. `levels` holds Columns values 0..1.
    Frame render(const QList<double> &levels) const;
    int columnOf(const QString &keyId) const; // 0 left .. 17 right; -1 if the key is not shown
    int rowOf(const QString &keyId) const;    // 0 top .. 5 bottom; -1 if the key is not shown

private:
    struct Cell {
        int column = -1, row = -1;
        QColor color = Qt::black; // what the profile shows on the key
    };
    const Layout &m_layout;
    QList<quint8> m_hids;
    QHash<quint8, Cell> m_cells; // visible keys only
    QHash<QString, quint8> m_hidOfId;
};
