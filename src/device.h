#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>

#include "protocol.h"

// The HID connection to the keyboard; faked in tests.
class Transport {
public:
    virtual ~Transport() = default;
    // Waits for the keyboard's hidraw node to appear after binding, then opens it.
    virtual bool waitAndOpen(int timeoutMs) = 0;
    virtual bool sendFeature(const QByteArray &data) = 0;
    virtual bool sendOutput(const QByteArray &data) = 0;
    virtual void close() = 0;
    virtual QString lastError() const = 0;
};

// Attaches the keyboard to the kernel USB driver for sending and detaches it after: the
// KLC only shows new lighting once detached.
class Binder {
public:
    virtual ~Binder() = default;
    virtual bool bind(QString *error) = 0;
    virtual bool unbind(QString *error) = 0;
};

enum class ApplyError { None, Locked, Invalid, BinderMissing, BindFailed, DeviceMissing, SendFailed };

struct ApplyResult {
    ApplyError error = ApplyError::None;
    QString message;
};

// The only code that talks to the keyboard. Each apply sends every packet exactly once:
// repeated or retried sends have hung this controller before.
class Device {
public:
    static constexpr int MinIntervalMs = 5000;
    static constexpr int OpenTimeoutMs = 3000;
    static constexpr int LockWaitMs = 15000;
    // msi-perkeyrgb waits this long after every packet: the controller misbehaves when
    // packets arrive too fast.
    static constexpr int PacketGapMs = 10;

    Device(Transport &transport, Binder &binder, QString runtimeDir, std::function<void(int)> sleepMs,
           std::function<qint64()> nowMs = {});

    ApplyResult apply(const QList<Packet> &packets);
    // Builds the packets only once it is this send's turn (after the lock and the
    // interval), so quick theme switches end with the latest theme on the keyboard.
    // Build errors come back as ApplyError::Invalid; no packets means nothing to do.
    ApplyResult applyLatest(const std::function<BuildResult()> &build);

private:
    qint64 lastSend() const;
    void recordSend() const;

    Transport &m_transport;
    Binder &m_binder;
    QString m_runtimeDir;
    std::function<void(int)> m_sleepMs;
    std::function<qint64()> m_nowMs;
};
