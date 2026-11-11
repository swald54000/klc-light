#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <functional>
#include <memory>

#include "protocol.h"

class QLockFile;

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

// Attaches the keyboard to the kernel USB driver for sending and detaches it after. The
// KLC shows a burst as soon as it is sent, attached or not (docs/origins.md, 2026-11-11);
// detached is kept as its resting state because that is the one validated over restarts.
class Binder {
public:
    virtual ~Binder() = default;
    virtual bool bind(QString *error) = 0;
    virtual bool unbind(QString *error) = 0;
};

// KeyboardAbsent: the lighting controller is not on the USB bus at all. On this laptop that
// happens after a warm restart (USB error -71) and only a full power-off clears it.
enum class ApplyError { None, Locked, Invalid, BinderMissing, BindFailed, KeyboardAbsent, DeviceMissing, SendFailed };

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

    // A session keeps the lock and the attached, open controller from begin() to end(), for
    // the visualizer's stream. begin() waits for the lock and the interval like a send; when
    // it fails, the controller is detached and the lock released.
    ApplyResult begin();
    // Each packet once, `gapMs` after each; stops at the first one refused.
    ApplyResult send(const QList<Packet> &packets, int gapMs = PacketGapMs);
    // Closes, detaches and releases the lock; the interval before the next attach starts
    // here. Does nothing when no session is open.
    void end();
    bool begun() const { return m_attached; }
    // Counts the interval from now. After a suspend the clock says the last send is long
    // past, yet the controller has only just come back with the computer.
    void restartInterval() { recordSend(); }
    void setLockWaitMs(int ms) { m_lockWaitMs = ms; }

    ~Device();

private:
    qint64 lastSend() const;
    void recordSend() const;
    ApplyResult acquire(); // the lock, then the interval
    ApplyResult attach();  // bind and open; on failure everything is undone
    // A send's interval runs from its attach, as measured; a session's from its end too.
    void detach(bool recordEnd);
    void release();

    std::unique_ptr<QLockFile> m_lock;
    bool m_attached = false;
    int m_lockWaitMs = LockWaitMs;

    Transport &m_transport;
    Binder &m_binder;
    QString m_runtimeDir;
    std::function<void(int)> m_sleepMs;
    std::function<qint64()> m_nowMs;
};

// What every command says when the controller is missing from the USB bus.
QString keyboardAbsentMessage();

// Carried by signals: the editor's send result, the visualizer's failure.
Q_DECLARE_METATYPE(ApplyResult)
