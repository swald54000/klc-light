#pragma once

#include "device.h"
#include "protocol.h"

class Layout;

// The visualizer's hold on the keyboard: attaches it once, puts every key in host-stream
// mode, shows frames, and puts the profile back before detaching. After a refused send it
// detaches and sends nothing more: repeated sends have hung this controller before.
class StreamSession {
public:
    // GG waits this long after each packet that sets the keys up.
    static constexpr int EnterGapMs = 30;

    StreamSession(Device &device, const Layout &layout);
    ApplyResult start();
    // Nothing is sent when the frame is the one already shown.
    ApplyResult show(const Frame &frame);
    // Sends `restore` (the profile's packets; none to send nothing) and detaches.
    ApplyResult stop(const QList<Packet> &restore);
    bool running() const { return m_running; }

private:
    ApplyResult fail(const ApplyResult &result);

    Device &m_device;
    const Layout &m_layout;
    bool m_running = false;
    Frame m_shown;
};
