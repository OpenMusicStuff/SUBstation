#pragma once
// Warping: playing a clip at another speed (to follow the project tempo) and/or
// another pitch.
//
//  * Re-Pitch clips are resampled, like a turntable: faster is higher.
//    Stateless, so it needs nothing but the source.
//  * Every other mode uses a phase-vocoder time stretcher (Signalsmith Stretch,
//    MIT), which keeps speed and pitch independent. Stretchers carry state from
//    block to block, so they live in WarpVoices: allocated on the edit side and
//    handed to the audio thread through the snapshot, like everything else.
//    Live playback reads a stretched clip's whole render instead once the
//    background has made it (WarpCache.h).

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sub {

// A warped clip's source position along it where the tempo changes under it:
// (timeline samples from the clip's start, source frames from its sourceOffset),
// sorted by both, straight from one to the next and on past the ends at the end
// segments' slopes. Exact at the tempo map's points, a few hundred samples apart
// through its ramps (EngineSnapshot.cpp, warpCurve()).
struct WarpPoint {
    int64_t at = 0;
    double source = 0.0;
    friend bool operator==(const WarpPoint&, const WarpPoint&) = default;
};
using WarpCurve = std::vector<WarpPoint>;

namespace warpcurve {

// The segment `t` is on: the index of its first point (at least two points).
inline size_t segment(const WarpCurve& curve, double t) noexcept {
    const auto after = std::upper_bound(curve.begin() + 1, curve.end() - 1, t,
                                        [](double v, const WarpPoint& p) { return v < static_cast<double>(p.at); });
    return static_cast<size_t>(after - curve.begin()) - 1;
}
inline double slope(const WarpCurve& curve, size_t i) noexcept {
    const double span = static_cast<double>(curve[i + 1].at - curve[i].at);
    return span > 0.0 ? (curve[i + 1].source - curve[i].source) / span : 0.0;
}
inline double sourceAt(const WarpCurve& curve, double t) noexcept {
    const size_t i = segment(curve, t);
    return curve[i].source + (t - static_cast<double>(curve[i].at)) * slope(curve, i);
}
inline double rateAt(const WarpCurve& curve, double t) noexcept { return slope(curve, segment(curve, t)); }
// The inverse: the time (from the clip's start) its source reaches `source`.
inline double timeOf(const WarpCurve& curve, double source) noexcept {
    const auto after = std::upper_bound(curve.begin() + 1, curve.end() - 1, source,
                                        [](double v, const WarpPoint& p) { return v < p.source; });
    const size_t i = static_cast<size_t>(after - curve.begin()) - 1;
    const double s = slope(curve, i);
    return static_cast<double>(curve[i].at) + (s > 0.0 ? (source - curve[i].source) / s : 0.0);
}

}  // namespace warpcurve


struct ClipRender;
class WarpRender;

// Warp modes; the order matches the app layer's list (kWarpModes in
// app/src/model/Clip.h).
//  Transients: short stretch blocks, tight attacks (drums).
//  Standard:   the stretcher's default blocks; good all-round.
//  Smooth:     long blocks: smooth tones, pads and noise, softer attacks.
//  Formants:   Standard, plus keeping the formants in place when transposing.
//  RePitch:    no stretching; resampled, so speed and pitch change together.
enum class WarpMode : uint8_t { Transients, Standard, Smooth, Formants, RePitch };

// Stretcher block sizes. Each warp mode (except Re-Pitch) maps to one, and
// voices are pooled per configuration because reconfiguring allocates.
enum class StretchConfig : uint8_t { Transient, Standard, Smooth };
inline constexpr int kNumStretchConfigs = 3;

StretchConfig stretchConfigFor(WarpMode mode) noexcept;

// A stretch configuration's analysis block and the interval between blocks, in
// seconds.
struct StretchTiming {
    double blockSeconds;
    double intervalSeconds;
};
StretchTiming stretchTiming(StretchConfig config) noexcept;

// Sets a stereo Signalsmith stretcher up for `config` at `sampleRate`: the
// clips' voices and the Sampler's stretchers alike. Split computation spreads
// each block's work over the interval after it (an interval of latency, which
// their alignment compensates like the rest).
template <typename Stretcher>
void configureStretcher(Stretcher& stretcher, StretchConfig config, double sampleRate) {
    const StretchTiming timing = stretchTiming(config);
    stretcher.configure(2, static_cast<int>(sampleRate * timing.blockSeconds),
                        static_cast<int>(sampleRate * timing.intervalSeconds), true);
}

// The seed every stretcher starts from: it randomises some phases (when
// stretching a lot), and offline renders should come out the same every time.
inline constexpr long kStretchSeed = 0x6115;

// One stretcher, bound to whichever clip is using it. Constructing it allocates;
// render() is real-time safe.
class WarpVoice {
public:
    WarpVoice(StretchConfig config, double sampleRate);
    ~WarpVoice();
    WarpVoice(const WarpVoice&) = delete;
    WarpVoice& operator=(const WarpVoice&) = delete;

    StretchConfig config() const noexcept { return config_; }

    // Writes (does not add) `frames` frames of `clip`, starting at timeline sample
    // `position`, to outL/outR. `continuing` says the voice played this clip in
    // the previous block; it then carries on seamlessly if the source position
    // still lines up (e.g. across a tempo change), and otherwise re-seeks so the
    // first frame is exactly aligned.
    void render(const ClipRender& clip, int64_t position, int frames, bool continuing, float* outL,
                float* outR) noexcept;

    // Whether render() would carry on (not re-seek) playing `clip` from
    // timeline sample `position`, were it continuing: the source position its
    // output has reached lines up.
    bool carriesOn(const ClipRender& clip, int64_t position) const noexcept;

    // The voice takes over from `render` (which was playing `clip` up to
    // `position`) with a crossfade of kFadeSeconds: the render read along the
    // clip's mapping from `position` on, fading out as the voice fades in.
    // Real-time safe: it copies what it needs (nothing of the render is kept).
    // Call before the render() that plays `position`, which mixes it in.
    void fadeFrom(const WarpRender& render, const ClipRender& clip, int64_t position) noexcept;
    static constexpr double kFadeSeconds = 0.01;

    // Bookkeeping for the renderer's voice allocation.
    uint64_t key = 0;       // ClipRender::key of the clip it last played
    uint64_t lastUsed = 0;  // renderer block counter

private:
    struct State;
    StretchConfig config_;
    std::unique_ptr<State> state_;
};

// The voices a renderer may use, per stretch configuration.
using WarpVoiceSet = std::array<std::vector<std::shared_ptr<WarpVoice>>, kNumStretchConfigs>;

// Re-Pitch playback: band-limited (windowed-sinc) resampling of the source.
// Writes (does not add) `frames` frames starting at timeline sample `position`.
void renderResampled(const ClipRender& clip, int64_t position, int frames, float* outL, float* outR) noexcept;

}  // namespace sub
