#pragma once

#include <QString>

// Where the visualizer gets the sound being played; faked in tests.
class AudioSource {
public:
    virtual ~AudioSource() = default;
    virtual bool start(QString *error) = 0;
    virtual void stop() = 0;
    virtual int sampleRate() const = 0;
    // Never blocks: copies up to `max` mono samples captured since the last call, oldest
    // first, and returns how many.
    virtual int read(float *out, int max) = 0;
};
