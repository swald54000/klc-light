#include "hidapitransport.h"

#include <QElapsedTimer>
#include <QThread>

#include <hidapi.h>

namespace {
constexpr unsigned short kVendor = 0x1038, kProduct = 0x113a;
}

HidapiTransport::HidapiTransport() { hid_init(); }

HidapiTransport::~HidapiTransport() {
    close();
    hid_exit();
}

bool HidapiTransport::waitAndOpen(int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    // After binding, the hidraw node takes a moment to appear.
    while (true) {
        hid_device_info *devices = hid_enumerate(kVendor, kProduct);
        const bool present = devices != nullptr;
        hid_free_enumeration(devices);
        if (present && (m_device = hid_open(kVendor, kProduct, nullptr)))
            return true;
        if (timer.elapsed() >= timeoutMs)
            break;
        QThread::msleep(100);
    }
    m_error = "keyboard 1038:113a not found";
    return false;
}

bool HidapiTransport::sendFeature(const QByteArray &data) {
    if (hid_send_feature_report(m_device, reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size())) < 0) {
        m_error = QString::fromWCharArray(hid_error(m_device));
        return false;
    }
    return true;
}

bool HidapiTransport::sendOutput(const QByteArray &data) {
    if (hid_write(m_device, reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size())) < 0) {
        m_error = QString::fromWCharArray(hid_error(m_device));
        return false;
    }
    return true;
}

void HidapiTransport::close() {
    if (m_device) {
        hid_close(m_device);
        m_device = nullptr;
    }
}
