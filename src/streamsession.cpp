#include "streamsession.h"

#include <QCoreApplication>

#include "layout.h"

StreamSession::StreamSession(Device &device, const Layout &layout) : m_device(device), m_layout(layout) {}

ApplyResult StreamSession::start() {
    if (m_running)
        return {};
    if (const ApplyResult begun = m_device.begin(); begun.error != ApplyError::None)
        return begun;
    m_running = true;
    if (const ApplyResult entered = m_device.send(streamEnterPackets(m_layout), EnterGapMs); entered.error != ApplyError::None)
        return fail(entered);
    // The enter packets leave every key black.
    m_shown.clear();
    for (quint8 hid : streamHids(m_layout))
        m_shown.append({hid, Qt::black});
    return {};
}

ApplyResult StreamSession::show(const Frame &frame) {
    if (!m_running)
        return {ApplyError::SendFailed, QCoreApplication::translate("Device", "Sending to the keyboard failed: %1").arg("the stream has stopped")};
    if (frame == m_shown)
        return {};
    if (const ApplyResult sent = m_device.send(streamFramePackets(frame)); sent.error != ApplyError::None)
        return fail(sent);
    m_shown = frame;
    return {};
}

ApplyResult StreamSession::stop(const QList<Packet> &restore) {
    if (!m_running)
        return {};
    const ApplyResult sent = restore.isEmpty() ? ApplyResult{} : m_device.send(restore);
    m_device.end();
    m_running = false;
    return sent;
}

ApplyResult StreamSession::fail(const ApplyResult &result) {
    m_device.end();
    m_running = false;
    return result;
}
