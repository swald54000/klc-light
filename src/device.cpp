#include "device.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLockFile>

#include <csignal>

namespace {

// Holds SIGINT, SIGTERM and SIGHUP back while the controller is attached: a process killed
// between attach and detach would leave the keyboard attached and half-configured. They are
// delivered when this goes out of scope, once the controller is detached again.
class SignalHold {
public:
    SignalHold() {
        sigset_t set;
        sigemptyset(&set);
        for (int sig : {SIGINT, SIGTERM, SIGHUP})
            sigaddset(&set, sig);
        m_held = pthread_sigmask(SIG_BLOCK, &set, &m_previous) == 0;
    }
    ~SignalHold() {
        if (m_held)
            pthread_sigmask(SIG_SETMASK, &m_previous, nullptr);
    }
    SignalHold(const SignalHold &) = delete;
    SignalHold &operator=(const SignalHold &) = delete;

private:
    sigset_t m_previous;
    bool m_held = false;
};

} // namespace

QString keyboardAbsentMessage() {
    return QCoreApplication::translate("Device", "The keyboard's lighting controller is not on the USB bus (error -71 after a restart). "
                                                 "Shut the computer down completely, then start it again: a restart is not enough.");
}

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
    return applyLatest([&packets] { return BuildResult{packets, {}}; });
}

ApplyResult Device::applyLatest(const std::function<BuildResult()> &build) {
    // One send at a time across the editor, the theme hooks and the visualizer.
    if (const ApplyResult locked = acquire(); locked.error != ApplyError::None)
        return locked;

    const BuildResult built = build();
    if (!built.errors.isEmpty() || built.packets.isEmpty()) {
        release();
        if (!built.errors.isEmpty())
            return {ApplyError::Invalid, built.errors.join("\n")};
        return {};
    }

    const SignalHold hold;
    ApplyResult result = attach();
    if (result.error != ApplyError::None)
        return result;
    result = send(built.packets);
    // Always detach, even after a failure, so the keyboard is never left half-configured.
    detach(false);
    return result;
}

ApplyResult Device::begin() {
    if (m_attached)
        return {};
    if (const ApplyResult locked = acquire(); locked.error != ApplyError::None)
        return locked;
    return attach();
}

ApplyResult Device::acquire() {
    auto lock = std::make_unique<QLockFile>(m_runtimeDir + "/klc-light.lock");
    if (!lock->tryLock(m_lockWaitMs))
        return {ApplyError::Locked, QCoreApplication::translate("Device", "Another lighting change is still being sent.")};
    m_lock = std::move(lock);

    // Wait out the interval rather than skipping: when themes change quickly, the last
    // theme's lighting must still reach the keyboard.
    const qint64 elapsed = m_nowMs() - lastSend();
    if (elapsed >= 0 && elapsed < MinIntervalMs)
        m_sleepMs(int(MinIntervalMs - elapsed));
    return {};
}

ApplyResult Device::attach() {
    QString error;
    if (!m_binder.bind(&error)) {
        // A bind that timed out may still finish as root; detaching is harmless when the
        // keyboard is not bound, and keeps it from being left attached.
        QString unbindError;
        m_binder.unbind(&unbindError);
        release();
        // msi-kbd-usb reports the controller itself missing from the bus with its USB id.
        if (error.contains("1038:113a"))
            return {ApplyError::KeyboardAbsent, keyboardAbsentMessage()};
        const bool missing = error.contains("not found") || error.contains("missing");
        return {missing ? ApplyError::BinderMissing : ApplyError::BindFailed,
                QCoreApplication::translate("Device", "Could not attach the keyboard: %1").arg(error)};
    }
    if (!m_transport.waitAndOpen(OpenTimeoutMs)) {
        QString unbindError;
        m_binder.unbind(&unbindError);
        release();
        return {ApplyError::DeviceMissing,
                QCoreApplication::translate("Device", "The keyboard did not appear after attaching it (USB error -71 is likely). "
                                                      "Power-cycle its USB port or shut the computer down completely.")};
    }
    m_attached = true;
    recordSend();
    return {};
}

ApplyResult Device::send(const QList<Packet> &packets, int gapMs) {
    if (!m_attached)
        return {ApplyError::SendFailed, QCoreApplication::translate("Device", "Sending to the keyboard failed: %1").arg("not attached")};
    for (const Packet &packet : packets) {
        const bool ok = packet.kind == Packet::Feature ? m_transport.sendFeature(packet.data)
                                                       : m_transport.sendOutput(packet.data);
        m_sleepMs(gapMs);
        if (!ok)
            return {ApplyError::SendFailed,
                    QCoreApplication::translate("Device", "Sending to the keyboard failed: %1").arg(m_transport.lastError())};
    }
    return {};
}

void Device::end() { detach(true); }

void Device::detach(bool recordEnd) {
    if (!m_attached)
        return;
    m_transport.close();
    QString unbindError;
    m_binder.unbind(&unbindError);
    m_attached = false;
    if (recordEnd)
        recordSend();
    release();
}

void Device::release() { m_lock.reset(); }

Device::~Device() { end(); }
