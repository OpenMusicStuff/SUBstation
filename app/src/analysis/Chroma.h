#pragma once
// Hue's chromagram: how loud each of the twelve pitch classes is in what a
// device's displays stream (its input and its output), for its editor's wheel.
// Each channel keeps its latest samples; update() takes a Hann-windowed FFT of
// them (fine enough to part semitones from 160 Hz up: bins of 6 Hz at most)
// and shares each bin's power between the two pitch classes nearest it, over
// the band the device processes. Levels rise at once and fall smoothly. Plain
// maths on the UI thread.

#include <array>
#include <cstddef>
#include <vector>

namespace sub::app::analysis {

class Chroma {
public:
    enum Channel { Input = 0, Output = 1 };
    static constexpr int kClasses = 12;          // C, C#, ... B
    static constexpr double kFloorDb = -90.0;    // dBFS: a full-scale sine in one class reads 0
    static constexpr double kFallDbPerSecond = 30.0;
    static constexpr double kMaxBinHz = 6.0;     // the FFT is long enough for bins this fine

    Chroma();

    // The FFT's length for `sampleRate`: the shortest power of two whose bins are kMaxBinHz or finer.
    static std::size_t fftSize(double sampleRate);
    // A channel's samples since the last update (none: it falls back).
    void feed(Channel channel, const float* samples, std::size_t count, double sampleRate);
    // Measures again over `lowHz`..`highHz`, `dtSeconds` after the last: whether anything changed.
    bool update(double lowHz, double highHz, double dtSeconds);
    // A pitch class's level now (dB, kFloorDb at the least).
    double levelDb(Channel channel, int pitchClass) const;
    const std::array<double, kClasses>& levels(Channel channel) const { return levels_[channel]; }
    // Whether any class of either channel is over `db`.
    bool above(double db) const;
    double sampleRate() const { return sampleRate_; }
    void clear();

private:
    void resize(double sampleRate);

    double sampleRate_ = 0.0;
    std::vector<double> window_;
    double scale_ = 0.0;                 // power -> a sine's squared amplitude
    std::vector<float> samples_[2];
    bool fresh_[2] = {false, false};     // samples came since the last update
    std::array<double, kClasses> levels_[2]{};
};

}  // namespace sub::app::analysis
