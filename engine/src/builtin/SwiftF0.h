#pragma once
// SwiftF0 (Lars Nieradzik, swift-f0 0.3.0, MIT: third_party/swift-f0), run by the engine itself, a frame at a time:
// a pitch tracker that is a small neural network (14 386 parameters) reading three log-frequency spectra of the
// audio at 16 kHz. GigaTune's High Quality mode hears the pitch with it (builtin/GigaTuneDesign.cpp).
//
// - The host rate's audio is resampled to 16 kHz (a Kaiser-windowed sinc, 16 zero crossings either side, cut off
//   at 94 % of 8 kHz): 16 kHz sample m is the host's at m * rate / 16000.
// - Every 256 samples (16 ms) a frame, centred there: magnitude spectra of 512, 1024 and 2048 samples (periodic
//   Hann windows, KFR's FFT), each through its 128-band filterbank, + 1e-8, log. The audio before the start is
//   silence, as swift-f0's batch analysis pads it.
// - The network's convolutions see three frames of time at most (one either side), so each runs as the columns
//   come: a column of its output for each of its input, one frame late. Its pitch head is final six frames after
//   a frame's spectra, its confidence seven: a frame is final 7 * 256 + 1024 = 2816 samples (176 ms) after its
//   centre. Streamed so, the frames are those of swift-f0's batch analysis (SwiftF0.detect) of the same audio.
// - The pitch: the most likely of 95 log-spaced classes (46.875 Hz to 2093.75 Hz) and its neighbours, averaged in
//   log frequency by a softmax of their scores. The confidence: a sigmoid, 0 where the frame's 256 samples peak
//   under 1e-3 (digital silence, which the network never saw). Voiced from 0.5.
//
// Real-time: prepare() allocates, the rest doesn't.

#include <cstdint>
#include <memory>

namespace sub::swiftf0 {

inline constexpr int kRate = 16000;
inline constexpr int kHop = 256;
inline constexpr int kLookahead = 7 * kHop + 1024;  // 16 kHz samples past a frame's centre before it is final
inline constexpr double kVoiced = 0.5;               // the confidence a voiced frame has at least

struct Frame {
    int64_t index = 0;        // its centre: 16 kHz sample index * kHop (the host's at index * kHop * rate / 16000)
    double pitch = 0.0;       // Hz
    double confidence = 0.0;  // 0..1
};

class Detector {
public:
    Detector();
    ~Detector();
    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;

    void prepare(double hostRate);  // not real-time
    void reset() noexcept;

    // Real-time: the next host-rate sample. True when a frame became final with it: frame() is that frame.
    bool push(float x) noexcept;
    const Frame& frame() const noexcept;

    // Host samples per frame, and how many host samples after a frame's centre it is final at the most.
    double hostHop() const noexcept;
    int delay() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// For tests: the frames of a signal at 16 kHz analysed whole: what swift-f0's SwiftF0.detect gives for it (but
// for the last 7 frames, which its padding at the end makes a little different).
struct Analysis {
    std::unique_ptr<double[]> pitch, confidence;
    int64_t frames = 0;
};
Analysis analyse16k(const float* audio, int64_t samples);

}  // namespace sub::swiftf0
