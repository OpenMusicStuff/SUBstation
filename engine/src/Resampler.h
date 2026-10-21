#pragma once
// Sample-rate conversion for decoding (AudioSource::decode(): a file at another
// rate than the engine's), a stream of frames at a time. Not real-time: it
// allocates as it goes. (Re-Pitch is another thing: Warp.h's renderResampled().)
//
// A polyphase windowed-sinc filter (docs/engine/warp.md, "Resampling a file to
// the engine's rate"): each output frame sits at its exact place in the input
// (frame k at k * inRate / outRate), and is the sum of the input frames around
// it weighed by a sinc low-pass, cut off at the lower rate's Nyquist frequency,
// under a Kaiser window (kZeroCrossings of the sinc at the lower rate on each
// side; β for kStopbandDb of attenuation). The transition band lies around
// that Nyquist frequency, about 9 % of it each way: 44.1 kHz is flat to 20 kHz,
// and nothing above 24.1 kHz (48 kHz played at 44.1) folds back below 20 kHz.
// Each phase's taps are normalised to sum to 1, so a constant stays that
// constant (to within float rounding).
//
// The phases are the rates' own when there are at most kMaxPhases of them
// (out / gcd(in, out): 160 for 44.1 to 48 kHz, 147 back), so every frame's taps
// are exact; for rates whose ratio needs more, a table of phases is
// interpolated linearly. Frames before the input's start and after its end
// count as silence. The output is the same whatever the chunks the input comes
// in (each frame's sum is made in one fixed order), and has ceil(frames *
// outRate / inRate) frames: the input's duration.

#include <cstdint>
#include <functional>
#include <vector>

namespace sub {

class Resampler {
public:
    static constexpr int kZeroCrossings = 36;
    static constexpr double kStopbandDb = 100.0;
    static constexpr int64_t kMaxPhases = 4096;

    // Frames out, interleaved (`channels` of them each), as they are ready.
    using Sink = std::function<void(const float* interleaved, int64_t frames)>;

    // Throws std::invalid_argument for no channels or a rate of 0.
    Resampler(uint32_t channels, uint32_t inRate, uint32_t outRate);

    // `frames` more input frames, interleaved; the output frames they complete go to `out`.
    void push(const float* interleaved, int64_t frames, const Sink& out);
    // The input ended: the rest of the output goes to `out`.
    void finish(const Sink& out);

    // How many frames `inputFrames` at `inRate` become at `outRate`.
    static int64_t outputFrames(int64_t inputFrames, uint32_t inRate, uint32_t outRate);

    // The filter (for tests): taps per output frame, and phases.
    int taps() const { return taps_; }
    int64_t phases() const { return phases_; }

private:
    void produce(int64_t until, const Sink& out);  // output frames up to `until` (whose taps are all held)
    const float* row(int64_t phase, float* scratch) const;  // the taps of an output frame at `phase`

    uint32_t channels_;
    int64_t up_;    // out / gcd: the output frame k sits at input frame k * down_ / up_
    int64_t down_;  // in / gcd
    int half_;      // taps on each side of an output frame's place
    int taps_;      // 2 * half_ (a multiple of 8)
    bool exact_;    // a phase per k mod up_ (else phases_ + 1 rows interpolated)
    int64_t phases_;
    std::vector<float> table_;  // rows of taps_

    std::vector<std::vector<float>> held_;  // input frames held, by channel, from frame base_
    int64_t base_ = 0;
    int64_t pushed_ = 0;  // input frames pushed in all
    int64_t next_ = 0;    // the next output frame,
    int64_t at_ = 0;      // the input frame at or just before its place (next_ * down_ / up_),
    int64_t phase_ = 0;   // and how far past it, in phases ((next_ * down_) % up_)
    int64_t step_ = 0;    // down_ / up_: input frames from one output frame's place to the next's,
    int64_t stepPhase_ = 0;  // and phases (down_ % up_)
    std::vector<float> outBuffer_;
    std::vector<float> scratch_;
};

}  // namespace sub
