#pragma once

#include <QList>
#include <vector>

// Turns the sound being played into one level per frequency band, for the visualizer:
// low bands first, each level 0..1. Fed with mono samples as they arrive, read once per
// frame shown on the keyboard.
class Spectrum {
public:
    static constexpr int FftSize = 2048;
    static constexpr double LowHz = 40, HighHz = 16000;
    // A band this far under the reference reads 0.
    static constexpr double RangeDb = 30;
    // The reference follows the loudest band, so quiet and loud music both fill the
    // keyboard, but never falls below this (dBFS): background noise stays dark.
    static constexpr double FloorRefDb = -30;
    static constexpr double RefDecayDbPerFrame = 0.2;
    // The most a level drops in one frame: bars rise at once and fall smoothly.
    static constexpr double FallPerFrame = 0.08;

    Spectrum(int sampleRate, int bands);
    // Keeps the last FftSize samples.
    void push(const float *mono, int count);
    QList<double> frame();
    // The band's lower edge; band `bands` gives HighHz.
    double bandLowHz(int band) const;

private:
    int m_sampleRate;
    int m_bands;
    std::vector<float> m_ring;
    int m_next = 0;
    std::vector<float> m_window;
    double m_refDb = FloorRefDb;
    QList<double> m_levels;
};
