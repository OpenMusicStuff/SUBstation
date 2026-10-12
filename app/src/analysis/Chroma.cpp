#include "analysis/Chroma.h"

#include "analysis/Fft.h"

#include <algorithm>
#include <cmath>

namespace sub::app::analysis {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

Chroma::Chroma() {
    clear();
}

std::size_t Chroma::fftSize(double sampleRate) {
    std::size_t n = 1024;
    while (sampleRate / double(n) > kMaxBinHz && n < (std::size_t(1) << 16))
        n *= 2;
    return n;
}

void Chroma::clear() {
    for (auto& channel : levels_)
        channel.fill(kFloorDb);
    for (auto& samples : samples_)
        std::fill(samples.begin(), samples.end(), 0.0f);
    fresh_[0] = fresh_[1] = false;
}

void Chroma::resize(double sampleRate) {
    sampleRate_ = sampleRate;
    const std::size_t n = fftSize(sampleRate);
    window_ = hann(n);
    double energy = 0.0;
    for (const double w : window_)
        energy += w * w;
    // Parseval: a sine of amplitude A puts n * sum(w^2) * A^2 / 4 into the positive half of the spectrum.
    scale_ = 4.0 / (double(n) * energy);
    for (auto& samples : samples_)
        samples.assign(n, 0.0f);
}

void Chroma::feed(Channel channel, const float* samples, std::size_t count, double sampleRate) {
    if (sampleRate <= 0.0)
        return;
    if (sampleRate != sampleRate_)
        resize(sampleRate);
    if (count == 0)
        return;
    std::vector<float>& buffer = samples_[channel];
    const std::size_t size = buffer.size();
    if (count > size) {
        samples += count - size;
        count = size;
    }
    std::move(buffer.begin() + std::ptrdiff_t(count), buffer.end(), buffer.begin());
    for (std::size_t i = 0; i < count; ++i)
        buffer[size - count + i] = std::isfinite(samples[i]) ? samples[i] : 0.0f;
    fresh_[channel] = true;
}

bool Chroma::update(double lowHz, double highHz, double dtSeconds) {
    bool changed = false;
    const double fall = kFallDbPerSecond * std::max(0.0, dtSeconds);
    for (int ch = 0; ch < 2; ++ch) {
        std::array<double, kClasses>& shown = levels_[ch];
        std::array<double, kClasses> now{};
        now.fill(kFloorDb);
        if (fresh_[ch] && !window_.empty()) {
            fresh_[ch] = false;
            const std::vector<float>& samples = samples_[ch];
            const std::size_t n = samples.size();
            std::vector<double> windowed(n);
            for (std::size_t i = 0; i < n; ++i)
                windowed[i] = double(samples[i]) * window_[i];
            const std::vector<Complex> bins = rfft(windowed.data(), n, n);
            std::array<double, kClasses> power{};
            const double perBin = sampleRate_ / double(n);
            const std::size_t first = std::size_t(std::max(1.0, std::ceil(lowHz / perBin)));
            const std::size_t last = std::min(bins.size() - 1, std::size_t(std::floor(highHz / perBin)));
            for (std::size_t k = first; k <= last; ++k) {
                // the pitch, in semitones above a C; shared between the two classes either side of it
                const double pitch = 12.0 * std::log2(double(k) * perBin / 440.0) + 9.0;
                const double below = std::floor(pitch), into = pitch - below;
                const int a = ((int(below) % 12) + 12) % 12, b = (a + 1) % 12;
                const double p = std::norm(bins[k]);
                const double toA = std::cos(0.5 * kPi * into);
                power[std::size_t(a)] += p * toA * toA;
                power[std::size_t(b)] += p * (1.0 - toA * toA);
            }
            for (int c = 0; c < kClasses; ++c)
                now[std::size_t(c)] = std::max(kFloorDb, 10.0 * std::log10(std::max(power[std::size_t(c)] * scale_, 1e-30)));
        }
        for (int c = 0; c < kClasses; ++c) {
            double& level = shown[std::size_t(c)];
            const double next = std::max(now[std::size_t(c)], std::max(kFloorDb, level - fall));
            if (next != level) {
                level = next;
                changed = true;
            }
        }
    }
    return changed;
}

double Chroma::levelDb(Channel channel, int pitchClass) const {
    if (pitchClass < 0 || pitchClass >= kClasses)
        return kFloorDb;
    return levels_[channel][std::size_t(pitchClass)];
}

bool Chroma::above(double db) const {
    for (const auto& channel : levels_)
        for (const double level : channel)
            if (level > db)
                return true;
    return false;
}

}  // namespace sub::app::analysis
