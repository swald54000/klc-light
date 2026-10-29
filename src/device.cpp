#include "device.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLockFile>

Device::Device(Transport &transport, Binder &binder, QString runtimeDir, std::function<void(int)> sleepMs,
               std::function<qint64()> nowMs)
    : m_transport(transport), m_binder(binder), m_runtimeDir(std::move(runtimeDir)), m_sleepMs(std::move(sleepMs)),
      m_nowMs(nowMs ? std::move(nowMs) : [] { return QDateTime::currentMSecsSinceEpoch(); }) {}

qint64 Device::lastSend() const {
    QFile file(m_runtimeDir + "/klc-light.last");
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return file.readAll().trimmed().toLongLong();
}

void Device::recordSend() const {
    QFile file(m_runtimeDir + "/klc-light.last");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QByteArray::number(m_nowMs()));
}

ApplyResult Device::apply(const QList<Packet> &packets) {
    // One send at a time across the editor and the theme hooks.
    QLockFile lock(m_runtimeDir + "/klc-light.lock");
    if (!lock.tryLock(LockWaitMs))
        return {ApplyError::Locked, QCoreApplication::translate("Device", "Another lighting change is still being sent.")};

    // Wait out the interval rather than skipping: when themes change quickly, the last
    // theme's lighting must still reach the keyboard.
    const qint64 elapsed = m_nowMs() - lastSend();
    if (elapsed >= 0 && elapsed < MinIntervalMs)
        m_sleepMs(int(MinIntervalMs - elapsed));

    QString error;
    if (!m_binder.bind(&error)) {
        const bool missing = error.contains("not found") || error.contains("missing");
        return {missing ? ApplyError::BinderMissing : ApplyError::BindFailed,
                QCoreApplication::translate("Device", "Could not attach the keyboard: %1").arg(error)};
    }

    ApplyResult result;
    if (!m_transport.waitAndOpen(OpenTimeoutMs)) {
        result = {ApplyError::DeviceMissing,
                  QCoreApplication::translate("Device", "The keyboard did not appear after attaching it (USB error -71 is likely). "
                                                        "Power-cycle its USB port or shut the computer down completely.")};
    } else {
        recordSend();
        for (const Packet &packet : packets) {
            const bool ok = packet.kind == Packet::Feature ? m_transport.sendFeature(packet.data)
                                                           : m_transport.sendOutput(packet.data);
            if (!ok) {
                result = {ApplyError::SendFailed,
                          QCoreApplication::translate("Device", "Sending to the keyboard failed: %1").arg(m_transport.lastError())};
                break;
            }
        }
        m_transport.close();
    }

    // Always detach, even after a failure, so the keyboard is never left half-configured.
    QString unbindError;
    m_binder.unbind(&unbindError);
    return result;
}
