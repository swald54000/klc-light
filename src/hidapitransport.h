#pragma once

#include "device.h"

struct hid_device_;

// The keyboard through hidapi's hidraw backend, the same calls msi-perkeyrgb makes.
class HidapiTransport : public Transport {
public:
    HidapiTransport();
    ~HidapiTransport() override;
    bool waitAndOpen(int timeoutMs) override;
    bool sendFeature(const QByteArray &data) override;
    bool sendOutput(const QByteArray &data) override;
    void close() override;
    QString lastError() const override { return m_error; }

private:
    hid_device_ *m_device = nullptr;
    QString m_error;
};
