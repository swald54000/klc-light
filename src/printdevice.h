#pragma once

#include <QTextStream>

#include "device.h"

// KLC_LIGHT_FAKE_DEVICE=1 for the visualizer: the packets are printed ("F <hex>" or
// "O <hex>" per line) instead of sent, and nothing is attached.
class PrintTransport : public Transport {
public:
    explicit PrintTransport(QTextStream &out) : m_out(out) {}
    bool waitAndOpen(int) override { return true; }
    bool sendFeature(const QByteArray &data) override { return print("F ", data); }
    bool sendOutput(const QByteArray &data) override { return print("O ", data); }
    void close() override {}
    QString lastError() const override { return {}; }

private:
    bool print(const char *kind, const QByteArray &data) {
        m_out << kind << data.toHex() << "\n";
        m_out.flush();
        return true;
    }
    QTextStream &m_out;
};

class NullBinder : public Binder {
public:
    bool bind(QString *) override { return true; }
    bool unbind(QString *) override { return true; }
};
