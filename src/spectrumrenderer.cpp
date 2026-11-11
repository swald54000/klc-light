#include "spectrumrenderer.h"

#include <algorithm>
#include <cmath>

#include "layout.h"
#include "preview.h"

SpectrumRenderer::SpectrumRenderer(const Layout &layout) : m_layout(layout), m_hids(streamHids(layout)) {
    const QList<Key> keys = layout.keys();
    if (keys.isEmpty())
        return;
    double minX = keys.first().x, maxX = minX, minY = keys.first().y, maxY = minY;
    for (const Key &key : keys) {
        minX = std::min(minX, key.x);
        maxX = std::max(maxX, key.x);
        minY = std::min(minY, key.y);
        maxY = std::max(maxY, key.y);
    }
    // A key belongs to the column and the row its centre falls in, wide and tall keys included.
    auto bin = [](double value, double min, double max, int count) {
        return max > min ? std::min(count - 1, int((value - min) / (max - min) * count)) : 0;
    };
    for (const Key &key : keys) {
        Cell cell;
        cell.column = bin(key.x, minX, maxX, Columns);
        cell.row = bin(key.y, minY, maxY, Rows);
        m_cells.insert(key.hid, cell);
        m_hidOfId.insert(key.id, key.hid);
    }
}

void SpectrumRenderer::setProfile(const Profile &profile) {
    // The color at the start of each cycle: an animated profile gives its colors, not its motion.
    const Preview preview(profile, m_layout);
    for (auto it = m_hidOfId.begin(); it != m_hidOfId.end(); ++it)
        m_cells[it.value()].color = preview.colorAt(it.key(), 0);
}

Frame SpectrumRenderer::render(const QList<double> &levels) const {
    Frame frame;
    frame.reserve(m_hids.size());
    for (quint8 hid : m_hids) {
        QColor color = Qt::black;
        const auto cell = m_cells.constFind(hid);
        if (cell != m_cells.constEnd()) {
            const double height = std::clamp(levels.value(cell->column), 0.0, 1.0) * Rows;
            const int fromBottom = Rows - 1 - cell->row;
            // The bar's top row is dimmed by how far the bar reaches into it.
            const double lit = std::clamp(height - fromBottom, 0.0, 1.0);
            if (lit >= 1.0)
                color = cell->color;
            else if (lit > 0.0)
                color = QColor(qRound(cell->color.red() * lit), qRound(cell->color.green() * lit), qRound(cell->color.blue() * lit));
        }
        frame.append({hid, color});
    }
    return frame;
}

int SpectrumRenderer::columnOf(const QString &keyId) const {
    const auto hid = m_hidOfId.constFind(keyId);
    return hid == m_hidOfId.constEnd() ? -1 : m_cells.value(*hid).column;
}

int SpectrumRenderer::rowOf(const QString &keyId) const {
    const auto hid = m_hidOfId.constFind(keyId);
    return hid == m_hidOfId.constEnd() ? -1 : m_cells.value(*hid).row;
}
