#include "spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

constexpr double kNoPeakDb = -1000; // a band that has not been heard yet

// In-place radix-2 FFT; the size is a power of two.
void fft(std::vector<std::complex<float>> &a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float angle = float(-2 * M_PI / double(len));
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

} // namespace

Spectrum::Spectrum(int sampleRate, int bands)
    : m_sampleRate(sampleRate), m_bands(bands), m_ring(FftSize, 0.0f), m_window(FftSize), m_bandRefDb(size_t(bands), kNoPeakDb),
      m_levels(bands, 0.0) {
    for (int i = 0; i < FftSize; ++i)
        m_window[i] = float(0.5 - 0.5 * std::cos(2 * M_PI * i / double(FftSize - 1))); // Hann
}

void Spectrum::push(const float *mono, int count) {
    for (int i = 0; i < count; ++i) {
        m_ring[m_next] = mono[i];
        m_next = (m_next + 1) % FftSize;
    }
}

void Spectrum::reset() {
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    m_next = 0;
    m_refDb = FloorRefDb;
    std::fill(m_bandRefDb.begin(), m_bandRefDb.end(), kNoPeakDb);
    std::fill(m_levels.begin(), m_levels.end(), 0.0);
}

double Spectrum::bandLowHz(int band) const {
    return LowHz * std::pow(HighHz / LowHz, double(band) / m_bands);
}

QList<double> Spectrum::frame() {
    std::vector<std::complex<float>> bins(FftSize);
    for (int i = 0; i < FftSize; ++i)
        bins[i] = m_ring[(m_next + i) % FftSize] * m_window[i];
    fft(bins);

    // A full-scale sine reads 0 dBFS: the Hann window halves its peak of FftSize / 2.
    const double fullScale = FftSize / 4.0;
    const double binHz = double(m_sampleRate) / FftSize;
    QList<double> db(m_bands);
    double loudest = -1000;
    for (int b = 0; b < m_bands; ++b) {
        const double low = bandLowHz(b), high = bandLowHz(b + 1);
        int first = int(std::ceil(low / binHz)), last = int(std::ceil(high / binHz)) - 1;
        if (last < first) // A band narrower than a bin takes the bin nearest its centre.
            first = last = int(std::lround(std::sqrt(low * high) / binHz));
        first = std::clamp(first, 1, FftSize / 2 - 1);
        last = std::clamp(last, first, FftSize / 2 - 1);
        double peak = 0;
        for (int i = first; i <= last; ++i)
            peak = std::max(peak, double(std::abs(bins[i])));
        db[b] = 20 * std::log10(std::max(peak / fullScale, 1e-12)) + TiltDbPerOctave * std::log2(std::sqrt(low * high) / TiltPivotHz);
        loudest = std::max(loudest, db[b]);
    }

    m_refDb = std::max({loudest, m_refDb - RefDecayDbPerFrame, FloorRefDb});
    for (int b = 0; b < m_bands; ++b) {
        m_bandRefDb[size_t(b)] = std::max(db[b], m_bandRefDb[size_t(b)] - BandRefDecayDbPerFrame);
        const double ref = std::max(m_bandRefDb[size_t(b)], m_refDb - BandHeadroomDb);
        const double raw = std::clamp((db[b] - (ref - RangeDb)) / RangeDb, 0.0, 1.0);
        m_levels[b] = raw >= m_levels[b] ? raw : std::max(raw, m_levels[b] - FallPerFrame);
    }
    return m_levels;
}
