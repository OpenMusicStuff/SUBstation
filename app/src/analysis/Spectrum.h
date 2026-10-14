#pragma once
// Spectra of what devices' displays stream, for their editors' analyzers: the
// Delay's input behind its filter curve, the EQ's input and output behind its
// bands. Each keeps the latest samples, takes a Hann-windowed FFT of them as
// more come (a display refresh at a time), and smooths the levels over time.
// Plain maths on the UI thread: the editors' items draw the result.

#include <cstddef>
#include <vector>

namespace sub::app::analysis {

// Refreshes without samples the spectra hold still for before they fall back
// (about 0.25 s at 60 a second). The audio comes in blocks: at 60 refreshes a
// second some refreshes get none while it plays, which isn't silence.
inline constexpr int kHoldRefreshes = 15;

// The Delay's: the input's latest kFftSize samples, each bin's level in dB
// rising at once and falling kFallDb per refresh.
class FallingSpectrum {
public:
    static constexpr int kFftSize = 4096;
    static constexpr double kFloorDb = -90.0;
    static constexpr double kFallDb = 1.0;  // per display refresh (~60 a second)

    FallingSpectrum();

    // The input's samples since the last call: the spectrum follows them (and
    // falls back without any). Whether it changed.
    bool add(const float* samples, std::size_t count, double sampleRate);
    double sampleRate() const { return sampleRate_; }
    // dB per bin (kFftSize / 2 + 1).
    const std::vector<float>& levels() const { return spectrum_; }
    // The spectrum in dB under each of `columns` columns across `low`..`high` Hz
    // (log): the loudest bin under a column, or (low down, where a bin spans
    // columns) interpolated between bins.
    std::vector<double> columns(int columns, double low, double high) const;

private:
    double sampleRate_ = 48000.0;
    std::vector<float> samples_;
    std::vector<double> window_;
    double scale_ = 0.0;
    std::vector<float> spectrum_;
    int quiet_ = 0;  // refreshes since samples last came
};

// The EQ's analyzer: spectra of the input and the output, a Hann-windowed FFT
// of the latest samples, in two stages, timed by what the samples that came
// cover rather than per refresh. Each bin's power is averaged briefly
// (kAverageSeconds: averaging the power, not the dB, steadies what partials
// beating against each other make each frame's levels do); the level shown
// follows that in dB, rising quickly (kRiseSeconds) and falling as Pro-Q's
// does (kFallSeconds): fast from up high, slowing as it nears where it goes
// (the floor, once the sound stops). Shown tilted kTilt dB/octave so music
// looks about level.
class EqAnalyzer {
public:
    enum Channel { Input = 0, Output = 1 };
    static constexpr int kFftSize = 8192;
    static constexpr double kFloorDb = -96.0;  // dBFS (after the tilt) at the bottom of the graph
    static constexpr double kCeilDb = 6.0;     // and at the top
    static constexpr double kTilt = 4.5;       // dB/octave around 1 kHz, as Pro-Q's
    static constexpr double kAverageSeconds = 0.05;  // the power's average
    static constexpr double kRiseSeconds = 0.03;     // the level's (in dB), rising and falling
    static constexpr double kFallSeconds = 0.15;
    static constexpr double kRefreshSeconds = 1.0 / 60;  // a refresh without samples (silence) is this long
    static constexpr double kTiltFade = 18.0;  // dB above the floor over which the tilt comes in
    static constexpr double kSilentLevel = 1e-5;     // samples this quiet (-100 dBFS) or quieter are silence
    static constexpr double kSilenceSeconds = 0.15;  // this long of it (or of nothing coming) is no audio

    EqAnalyzer();

    double sampleRate() const { return sampleRate_; }
    // A channel's samples since the last refresh (none: it falls back).
    void feed(Channel channel, const float* samples, std::size_t count, double sampleRate);
    // Whether it shows anything above the floor.
    bool live(Channel channel) const;
    // Whether audio comes: something above kSilentLevel within the last kSilenceSeconds.
    bool hearing(Channel channel) const { return silentSeconds_[channel] < kSilenceSeconds; }
    const std::vector<float>& levels(Channel channel) const { return levels_[channel]; }
    // The spectrum in dB (tilted) at each of the graph's column frequencies:
    // the loudest bin between a column and the next, or (low down, where a bin
    // spans columns) between bins. Tilted, but not near the floor: silence (or
    // a spectrum falling back to it) stays flat instead of the tilt lifting its
    // high end into view.
    std::vector<double> columns(Channel channel, const std::vector<double>& freqs) const;

private:
    double sampleRate_ = 48000.0;
    std::vector<double> window_;
    double scale_ = 0.0;
    std::vector<float> samples_[2];
    std::vector<double> power_[2];  // per bin, averaged (kAverageSeconds)
    std::vector<float> levels_[2];  // the same in dB
    int quiet_[2] = {0, 0};  // refreshes since samples last came
    double silentSeconds_[2] = {1e9, 1e9};  // since a sample above kSilentLevel came
};

}  // namespace sub::app::analysis
