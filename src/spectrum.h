#pragma once

#include <QList>
#include <vector>

// Turns the sound being played into one level per frequency band, for the visualizer:
// low bands first, each level 0..1. Fed with mono samples as they arrive, read once per
// frame shown on the keyboard.
class Spectrum {
public:
    // 4096 samples at 48 kHz: bins 11.7 Hz apart, so the lowest bands do not all move
    // together, for a window of 85 ms.
    static constexpr int FftSize = 4096;
    static constexpr double LowHz = 40, HighHz = 16000;
    // Music carries far more energy in the bass: left as is, only the leftmost columns ever
    // lit (measured with real music on the GS76). Each band is raised by this much per
    // octave above the pivot, which evened the bars out on that music.
    static constexpr double TiltDbPerOctave = 4.5, TiltPivotHz = 100;
    // A band this far under its reference reads 0.
    static constexpr double RangeDb = 12;
    // A band's reference is its own recent peak, but never more than this under the loudest
    // band's: every column lives with the music, and a weak band still shows as weaker.
    // (With one reference for all, the mids stayed low on real music.)
    static constexpr double BandHeadroomDb = 12;
    // A band forgets its peaks slowly (0.6 dB a second): bars drop between the beats.
    // Forgetting them as fast as the loudest band's left every column near its top.
    static constexpr double BandRefDecayDbPerFrame = 0.02;
    // The reference follows the loudest band, so quiet and loud music both fill the
    // keyboard, but never falls below this (dBFS): background noise stays dark.
    static constexpr double FloorRefDb = -30;
    static constexpr double RefDecayDbPerFrame = 0.2;
    // The most a level drops in one frame: bars rise at once and fall smoothly.
    static constexpr double FallPerFrame = 0.12;

    Spectrum(int sampleRate, int bands);
    // Keeps the last FftSize samples.
    void push(const float *mono, int count);
    QList<double> frame();
    // Forgets what was heard: a stream that starts again must not replay the old sound.
    void reset();
    // The band's lower edge; band `bands` gives HighHz.
    double bandLowHz(int band) const;

private:
    int m_sampleRate;
    int m_bands;
    std::vector<float> m_ring;
    int m_next = 0;
    std::vector<float> m_window;
    double m_refDb = FloorRefDb;
    std::vector<double> m_bandRefDb; // each band's own recent peak
    QList<double> m_levels;
};
