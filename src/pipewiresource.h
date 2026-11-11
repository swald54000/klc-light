#pragma once

#include <mutex>
#include <vector>

#include "audiosource.h"

struct pw_thread_loop;
struct pw_stream;

// What the default audio output is playing, through PipeWire: a capture stream on the
// output's monitor, which follows the default output when it changes. PipeWire's own
// thread fills a one-second buffer that read() empties.
class PipewireSource : public AudioSource {
public:
    static constexpr int Rate = 48000;

    PipewireSource();
    ~PipewireSource() override;
    bool start(QString *error) override;
    void stop() override;
    int sampleRate() const override { return Rate; }
    int read(float *out, int max) override;

    // Called on PipeWire's thread.
    void process();

private:
    pw_thread_loop *m_loop = nullptr;
    pw_stream *m_stream = nullptr;
    std::mutex m_mutex;
    std::vector<float> m_ring; // Rate samples
    size_t m_head = 0;         // next write
    size_t m_count = 0;        // samples waiting to be read
};
