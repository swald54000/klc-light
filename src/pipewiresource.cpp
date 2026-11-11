#include "pipewiresource.h"

#include <cerrno>
#include <cstring>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

namespace {

void onProcess(void *data) { static_cast<PipewireSource *>(data)->process(); }

pw_stream_events streamEvents() {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = onProcess;
    return events;
}
const pw_stream_events kEvents = streamEvents();

} // namespace

PipewireSource::PipewireSource() : m_ring(Rate, 0.0f) {}

PipewireSource::~PipewireSource() { stop(); }

bool PipewireSource::start(QString *error) {
    if (m_loop)
        return true;
    auto fail = [&](const char *what) {
        if (error)
            *error = QString("%1: %2").arg(what, QString::fromLocal8Bit(std::strerror(errno)));
        stop();
        return false;
    };

    pw_init(nullptr, nullptr);
    m_loop = pw_thread_loop_new("klc-light-audio", nullptr);
    if (!m_loop)
        return fail("PipeWire loop");

    // stream.capture.sink asks for the monitor of the output; with no target named, the
    // session manager links it to the default output and moves it when that changes.
    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                             PW_KEY_MEDIA_ROLE, "Music", PW_KEY_STREAM_CAPTURE_SINK, "true",
                                             PW_KEY_NODE_NAME, "klc-light-visualizer", nullptr);
    m_stream = pw_stream_new_simple(pw_thread_loop_get_loop(m_loop), "klc-light visualizer", props, &kEvents, this);
    if (!m_stream)
        return fail("PipeWire is not available");

    uint8_t buffer[1024];
    spa_pod_builder builder;
    spa_pod_builder_init(&builder, buffer, sizeof(buffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = Rate;
    info.channels = 1;
    info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};

    const int connected = pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                                            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
    if (connected < 0) {
        errno = -connected;
        return fail("PipeWire capture");
    }
    if (pw_thread_loop_start(m_loop) < 0)
        return fail("PipeWire thread");
    return true;
}

void PipewireSource::stop() {
    if (m_loop)
        pw_thread_loop_stop(m_loop);
    if (m_stream)
        pw_stream_destroy(m_stream);
    if (m_loop)
        pw_thread_loop_destroy(m_loop);
    m_stream = nullptr;
    m_loop = nullptr;
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_count = 0;
}

void PipewireSource::process() {
    pw_buffer *b = pw_stream_dequeue_buffer(m_stream);
    if (!b)
        return;
    const spa_data &d = b->buffer->datas[0];
    if (d.data && d.chunk) {
        const uint32_t offset = d.chunk->offset % d.maxsize;
        const uint32_t size = d.chunk->size < d.maxsize - offset ? d.chunk->size : d.maxsize - offset;
        const float *samples = reinterpret_cast<const float *>(static_cast<const uint8_t *>(d.data) + offset);
        const size_t count = size / sizeof(float);
        const std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < count; ++i) {
            m_ring[m_head] = samples[i];
            m_head = (m_head + 1) % m_ring.size();
        }
        // When nobody reads fast enough, the oldest samples are lost.
        m_count = m_count + count < m_ring.size() ? m_count + count : m_ring.size();
    }
    pw_stream_queue_buffer(m_stream, b);
}

int PipewireSource::read(float *out, int max) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const size_t count = m_count < size_t(max) ? m_count : size_t(max);
    size_t from = (m_head + m_ring.size() - m_count) % m_ring.size();
    for (size_t i = 0; i < count; ++i) {
        out[i] = m_ring[from];
        from = (from + 1) % m_ring.size();
    }
    m_count -= count;
    return int(count);
}
