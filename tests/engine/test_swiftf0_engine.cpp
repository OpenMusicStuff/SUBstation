// SwiftF0 as the engine runs it (builtin/SwiftF0.h): the port giving what swift-f0 0.3.0 gives under ONNX Runtime
// (a reference recorded from it on a signal made here the same way), frame for frame; streamed at host rates, each
// frame's pitch and its centre's time, final 176 ms after it; silence unvoiced.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "builtin/SwiftF0.h"
#include "harness/Signal.h"
#include "harness/Test.h"

using namespace subtest;
namespace sf = sub::swiftf0;

namespace {

// 2 s at 16 kHz: a voice-like tone gliding from 180 to 300 Hz with a vibrato, 12 harmonics falling as 1/h, silent
// from 0.9 to 1.1 s, a little noise from an LCG; worked out in double, as the reference was.
std::vector<float> testSignal() {
    std::vector<float> x(32000);
    double phase = 0.0;
    uint64_t state = 12345;
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = static_cast<double>(i) / 16000.0;
        const double f = 180.0 * std::pow(2.0, t * std::log2(300.0 / 180.0) / 2) * std::pow(2.0, 0.3 / 12 * std::sin(2 * kPi * 5 * t));
        double s = 0.0;
        for (int h = 1; h <= 12; ++h) s += std::sin(h * phase) / h;
        state = (state * 1103515245ull + 12345ull) % 2147483648ull;
        const double noise = (static_cast<double>(state) / 2147483648.0 - 0.5) * 0.002;
        x[i] = static_cast<float>((t >= 0.9 && t < 1.1 ? 0.0 : 0.25 * s) + noise);
        phase += 2 * kPi * f / 16000.0;
        if (phase > 2 * kPi * 64) phase -= 2 * kPi * 64;
    }
    return x;
}

struct Reference {
    double pitch, confidence;
};

// swift-f0 0.3.0 (ONNX Runtime 1.31.0) on testSignal(): pitch (Hz), confidence for each frame (silence gated)
const Reference kReference[] = {
#include "swiftf0_reference.inc"
};

}  // namespace

TEST_CASE("swiftf0: the port gives what swift-f0 does") {
    const std::vector<float> x = testSignal();
    const sf::Analysis a = sf::analyse16k(x.data(), static_cast<int64_t>(x.size()));
    REQUIRE(a.frames == static_cast<int64_t>(std::size(kReference)));
    double worstCents = 0.0, worstConfidence = 0.0;
    int voiced = 0;
    for (int64_t i = 0; i + 7 < a.frames; ++i) {  // (the last 7: swift-f0 pads the end otherwise)
        const Reference& r = kReference[i];
        worstConfidence = std::max(worstConfidence, std::abs(a.confidence[i] - r.confidence));
        CHECK_EQ(a.confidence[i] >= sf::kVoiced, r.confidence >= sf::kVoiced);
        if (r.confidence >= sf::kVoiced) {
            ++voiced;
            worstCents = std::max(worstCents, std::abs(1200.0 * std::log2(a.pitch[i] / r.pitch)));
        }
    }
    CHECK(voiced > 90);
    CHECK(worstCents < 0.01);
    CHECK(worstConfidence < 1e-4);
}

TEST_CASE("swiftf0: streamed at host rates") {
    // steady tones: every frame past the first voiced, within a few cents (SwiftF0's own accuracy, before
    // GigaTune's refinement); frame k's centre at k * hostHop(); final delay() samples after it at the most
    for (const double rate : {44100.0, 48000.0, 96000.0})
    for (const double f0 : {110.0, 220.0, 440.0, 880.0}) {
        INFO(std::to_string(rate) + " Hz rate, " + std::to_string(f0) + " Hz");
        sf::Detector d;
        d.prepare(rate);
        CHECK(std::abs(d.hostHop() - 256.0 * rate / 16000.0) < 1e-9);
        CHECK(d.delay() > 0.176 * rate && d.delay() < 0.18 * rate);
        int frames = 0;
        double worst = 0.0;
        for (int64_t i = 0; i < static_cast<int64_t>(1.0 * rate); ++i) {
            double s = 0.0;
            for (int h = 1; h * f0 < 0.4 * rate && h <= 30; ++h) s += std::sin(2 * kPi * h * f0 * i / rate) / h;
            if (!d.push(static_cast<float>(0.3 * s))) continue;
            const sf::Frame& f = d.frame();
            // final no earlier than its lookahead and no later than delay() after its centre
            const double centre = f.index * d.hostHop();
            CHECK(i >= centre + sf::kLookahead * rate / 16000.0 - 1);
            CHECK(i <= centre + d.delay());
            if (centre < 0.1 * rate) continue;
            ++frames;
            CHECK(f.confidence >= sf::kVoiced);
            worst = std::max(worst, std::abs(1200.0 * std::log2(f.pitch / f0)));
        }
        CHECK(frames > 40);
        CHECK(worst < 5.0);
    }
}

TEST_CASE("swiftf0: silence is unvoiced") {
    sf::Detector d;
    d.prepare(48000.0);
    int frames = 0;
    for (int i = 0; i < 48000; ++i)
        if (d.push(0.f)) {
            ++frames;
            CHECK_EQ(d.frame().confidence, 0.0);
        }
    CHECK(frames > 40);
    d.reset();  // and again from the start
    int again = 0;
    for (int i = 0; i < 24000; ++i) again += d.push(0.f) ? 1 : 0;
    CHECK(again > 10);
}
