// Decoding's sample-rate conversion (Resampler.h, docs/engine/warp.md#decoding):
// a constant stays that constant; a tone in the passband comes out as that tone
// at its place in time (nothing late, nothing early), to within 100 dB or so; a
// tone above the lower rate's Nyquist frequency, and the images of one below it,
// are gone; the output is the input's duration, and the same to the bit
// whatever the chunks the input came in; rates whose ratio needs more phases
// than kept interpolate between them as well. A file at another rate decodes
// through it (AudioSource::load()).

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "AudioSource.h"
#include "Resampler.h"
#include "harness/Fixtures.h"

using namespace subtest;

namespace {

// The whole of `input` (mono) at `inRate` resampled to `outRate`, pushed `chunk` frames at a time (0: at once).
std::vector<float> resample(const std::vector<float>& input, uint32_t inRate, uint32_t outRate, size_t chunk = 0) {
    sub::Resampler resampler(1, inRate, outRate);
    std::vector<float> out;
    const auto take = [&](const float* frames, int64_t n) { out.insert(out.end(), frames, frames + n); };
    if (chunk == 0) chunk = std::max<size_t>(input.size(), 1);
    for (size_t at = 0; at < input.size(); at += chunk) {
        resampler.push(input.data() + at, static_cast<int64_t>(std::min(chunk, input.size() - at)), take);
    }
    resampler.finish(take);
    return out;
}

std::vector<float> tone(double hz, uint32_t rate, double seconds, double amplitude = 0.5) {
    std::vector<float> samples(static_cast<size_t>(seconds * rate));
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    }
    return samples;
}

// The largest difference from what `want` says frame i should be, over the frames away from the
// ends (where the input's edge, silence beyond it, is heard).
template <typename Want>
double worstError(const std::vector<float>& got, uint32_t rate, Want want) {
    double worst = 0.0;
    const auto margin = static_cast<size_t>(rate / 20);  // 50 ms
    for (size_t i = margin; i + margin < got.size(); ++i) worst = std::max(worst, std::abs(got[i] - want(i)));
    return worst;
}

double rms(const std::vector<float>& got, uint32_t rate) {
    const auto margin = static_cast<size_t>(rate / 20);
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = margin; i + margin < got.size(); ++i, ++n) sum += static_cast<double>(got[i]) * got[i];
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

double db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-30)); }

}  // namespace

TEST_CASE("resampler: as long as the input lasts") {
    CHECK_EQ(sub::Resampler::outputFrames(44100, 44100, 48000), int64_t{48000});
    CHECK_EQ(sub::Resampler::outputFrames(48000, 48000, 44100), int64_t{44100});
    CHECK_EQ(sub::Resampler::outputFrames(1, 44100, 48000), int64_t{2});  // (frame 1 sits at 0.919: inside)
    CHECK_EQ(sub::Resampler::outputFrames(2, 44100, 48000), int64_t{3});
    CHECK_EQ(sub::Resampler::outputFrames(100, 48000, 48000), int64_t{100});
    CHECK_EQ(sub::Resampler::outputFrames(0, 44100, 48000), int64_t{0});
    for (const auto& [in, out] : {std::pair{44100u, 48000u}, {48000u, 44100u}, {22050u, 48000u}, {96000u, 44100u},
                                 {44100u, 47999u}}) {
        INFO(std::to_string(in) + " to " + std::to_string(out));
        for (const size_t frames : {size_t{1}, size_t{37}, size_t{12345}}) {
            CHECK_EQ(static_cast<int64_t>(resample(std::vector<float>(frames, 0.25f), in, out).size()),
                     sub::Resampler::outputFrames(static_cast<int64_t>(frames), in, out));
        }
    }
}

TEST_CASE("resampler: a constant stays that constant") {
    for (const auto& [in, out] : {std::pair{44100u, 48000u}, {48000u, 44100u}, {22050u, 48000u}, {96000u, 44100u},
                                 {8000u, 192000u}, {44100u, 47999u}}) {
        INFO(std::to_string(in) + " to " + std::to_string(out));
        const std::vector<float> got = resample(std::vector<float>(in, 0.5f), in, out);
        CHECK(worstError(got, out, [](size_t) { return 0.5; }) < 1e-6);
    }
}

TEST_CASE("resampler: a tone in the passband comes out as it was, where it was") {
    // (44.1 kHz's passband reaches 20 kHz; a frame k out sits at k * in / out in.)
    for (const auto& [in, out] : {std::pair{44100u, 48000u}, {48000u, 44100u}, {44100u, 96000u}, {96000u, 44100u},
                                 {22050u, 48000u}, {44100u, 47999u}}) {
        for (const double hz : {1000.0, 9000.0, 19500.0 * std::min(in, out) / 44100.0}) {
            INFO(std::to_string(in) + " to " + std::to_string(out) + ", " + std::to_string(hz) + " Hz");
            const std::vector<float> got = resample(tone(hz, in, 1.0), in, out);
            const double error = worstError(got, out, [&](size_t i) {
                return 0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / out);
            });
            CHECK(db(error / 0.5) < -98.0);  // (100 to 122 dB under it)
        }
    }
}

TEST_CASE("resampler: nothing above the lower rate's Nyquist folds back, nor are images made") {
    // Down: tones past the stopband's edge (about 1.09 times the output's Nyquist) are gone.
    for (const double hz : {24500.0, 30000.0, 45000.0}) {
        INFO(std::to_string(hz) + " Hz, 96 to 44.1 kHz");
        CHECK(db(rms(resample(tone(hz, 96000, 1.0), 96000, 44100), 44100) / 0.5) < -110.0);  // (116 to 128 dB)
    }
    // 48 to 44.1 kHz: 20 kHz passes; 23 kHz (in the transition band) is attenuated, and what is left
    // of it folds back to 21.1 kHz, above 20 kHz.
    const double sine = 0.5 / std::sqrt(2.0);  // (a sine's RMS)
    CHECK(std::abs(db(rms(resample(tone(20000.0, 48000, 1.0), 48000, 44100), 44100) / sine)) < 0.05);
    CHECK(db(rms(resample(tone(23000.0, 48000, 1.0), 48000, 44100), 44100) / sine) < -20.0);
    // Up: a tone's image (at 44.1 kHz less it) isn't there: what comes out is the tone alone.
    const std::vector<float> got = resample(tone(15000.0, 44100, 1.0), 44100, 96000);
    const double error = worstError(got, 96000, [&](size_t i) {
        return 0.5 * std::sin(2.0 * std::numbers::pi * 15000.0 * static_cast<double>(i) / 96000);
    });
    CHECK(db(error / 0.5) < -98.0);
}

TEST_CASE("resampler: the same to the bit whatever the chunks") {
    std::vector<float> noise(30000);
    uint32_t seed = 1;
    for (float& s : noise) {
        seed = seed * 1664525u + 1013904223u;
        s = static_cast<float>(seed >> 8) / static_cast<float>(1 << 24) - 0.5f;
    }
    for (const auto& [in, out] : {std::pair{44100u, 48000u}, {96000u, 44100u}, {44100u, 47999u}}) {
        INFO(std::to_string(in) + " to " + std::to_string(out));
        const std::vector<float> whole = resample(noise, in, out);
        for (const size_t chunk : {size_t{1}, size_t{7}, size_t{4096}, size_t{16384}}) {
            CHECK(resample(noise, in, out, chunk) == whole);
        }
    }
}

TEST_CASE("resampler: stereo frames keep their channels apart") {
    sub::Resampler resampler(2, 44100, 48000);
    std::vector<float> interleaved;
    for (int i = 0; i < 44100; ++i) {
        interleaved.push_back(0.5f);
        interleaved.push_back(-0.25f);
    }
    std::vector<float> got;
    const auto take = [&](const float* frames, int64_t n) { got.insert(got.end(), frames, frames + 2 * n); };
    resampler.push(interleaved.data(), 44100, take);
    resampler.finish(take);
    REQUIRE(got.size() == 2 * 48000u);
    for (size_t i = 2400; i < 48000 - 2400; ++i) {
        CHECK_NEAR(got[2 * i], 0.5, 1e-6);
        CHECK_NEAR(got[2 * i + 1], -0.25, 1e-6);
    }
}

TEST_CASE("resampler: rates whose ratio needs many phases interpolate between some") {
    const sub::Resampler exact(1, 44100, 48000);
    CHECK_EQ(exact.phases(), int64_t{160});
    const sub::Resampler interpolated(1, 44100, 47999);
    CHECK(interpolated.phases() < 47999);
    CHECK(interpolated.phases() <= sub::Resampler::kMaxPhases);
    CHECK_EQ(exact.taps() % 8, 0);
    // Down, the kernel spans as many of the lower rate's periods: more input frames.
    CHECK(sub::Resampler(1, 96000, 44100).taps() > exact.taps());
    CHECK_THROWS_AS(sub::Resampler(1, 0, 48000), std::invalid_argument);
}

TEST_CASE("resampler: a file at another rate decodes through it") {
    const std::string path = makeWav(tone(1000.0, 44100, 1.5), 1, 44100);
    const auto native = sub::AudioSource::load(path, 44100);  // (the file's samples as they are)
    const std::vector<float> samples(native->channelData(0), native->channelData(0) + native->frames());
    const auto source = sub::AudioSource::load(path, kSampleRate);
    CHECK_EQ(source->frames(), sub::Resampler::outputFrames(static_cast<int64_t>(samples.size()), 44100, kSampleRate));
    CHECK_EQ(source->fileSampleRate(), 44100u);
    const std::vector<float> want = resample(samples, 44100, kSampleRate);
    REQUIRE(want.size() == static_cast<size_t>(source->frames()));
    CHECK(std::equal(want.begin(), want.end(), source->channelData(0)));
}
