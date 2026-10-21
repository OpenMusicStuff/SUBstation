// Streaming long files from disk (docs/engine/streaming.md): what a streamed
// source reads (a WAV or AIFF as it is, a cache file otherwise), its peaks, the
// pool of pages, the disk threads, and that playing a streamed source sounds
// the same as playing it from memory, live and offline, whatever the disk.
//
// With SUBSTATION_TEST_STREAMING set (CTest's engine_streamed), every engine
// the tests make streams every file it loads (threshold 0), from a cache folder
// of the run's own: every test that plays a file plays it streamed, and must
// come out as it does from memory.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

#include "CacheFile.h"
#include "Engine.h"
#include "StreamCache.h"
#include "StreamScheduler.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "platform/Paths.h"

using namespace subtest;

namespace {

// --- Every engine streaming (CTest's engine_streamed) ------------------------------

struct StreamEverything {
    std::filesystem::path folder;
    StreamEverything() {
        if (!std::getenv("SUBSTATION_TEST_STREAMING")) return;
        std::random_device random;
        folder = std::filesystem::temp_directory_path() / ("substation-streaming-" + std::to_string(random()));
        sub::StreamingSettings settings;
        settings.enabled = true;
        settings.thresholdSeconds = 0.0;
        settings.cacheFolder = sub::platform::fromPath(folder);
        sub::Engine::setDefaultStreaming(settings);
    }
    ~StreamEverything() {
        std::error_code ignored;
        if (!folder.empty()) std::filesystem::remove_all(folder, ignored);
    }
};
const StreamEverything streamEverything;

// --- Files ------------------------------------------------------------------------

std::string utf8(const std::filesystem::path& path) { return sub::platform::fromPath(path); }

// Interleaved samples in [-1, 1) as `bits`-bit integers or 32-bit floats, in a WAV file
// (WAVE_FORMAT_EXTENSIBLE if asked).
std::string writeWavAs(const std::filesystem::path& path, const Samples& interleaved, int channels, int rate, int bits,
                       bool isFloat, bool extensible = false) {
    std::ofstream out(path, std::ios::binary);
    const auto frames = static_cast<uint32_t>(interleaved.size() / static_cast<size_t>(channels));
    const uint32_t bytes = static_cast<uint32_t>(bits / 8);
    const uint32_t dataBytes = frames * static_cast<uint32_t>(channels) * bytes;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t fmtBytes = extensible ? 40 : 16;
    out.write("RIFF", 4);
    u32(4 + 8 + fmtBytes + 8 + dataBytes);
    out.write("WAVEfmt ", 8);
    u32(fmtBytes);
    u16(extensible ? 0xFFFE : (isFloat ? 3 : 1));
    u16(static_cast<uint16_t>(channels));
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate) * channels * bytes);
    u16(static_cast<uint16_t>(channels * bytes));
    u16(static_cast<uint16_t>(bits));
    if (extensible) {
        u16(22);
        u16(static_cast<uint16_t>(bits));
        u32(channels == 1 ? 0x4 : 0x3);
        const unsigned char guid[16] = {static_cast<unsigned char>(isFloat ? 3 : 1), 0, 0, 0, 0, 0, 0x10, 0,
                                        0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        out.write(reinterpret_cast<const char*>(guid), 16);
    }
    out.write("data", 4);
    u32(dataBytes);
    for (const float s : interleaved) {
        if (isFloat) {
            out.write(reinterpret_cast<const char*>(&s), 4);
            continue;
        }
        const double scale = bits == 16 ? 32768.0 : 8388608.0;
        const double v = std::clamp(std::round(static_cast<double>(s) * scale), -scale, scale - 1);
        const auto x = static_cast<int32_t>(v);
        for (uint32_t b = 0; b < bytes; ++b) out.put(static_cast<char>((x >> (8 * b)) & 0xFF));
    }
    return utf8(path);
}

// The same as an AIFF (or AIFF-C, compressed as `compression` says: "NONE", "sowt", "fl32").
std::string writeAiff(const std::filesystem::path& path, const Samples& interleaved, int channels, int rate, int bits,
                      const char* compression = nullptr) {
    std::ofstream out(path, std::ios::binary);
    const auto frames = static_cast<uint32_t>(interleaved.size() / static_cast<size_t>(channels));
    const bool isFloat = compression && std::strcmp(compression, "fl32") == 0;
    const bool little = compression && std::strcmp(compression, "sowt") == 0;
    const uint32_t bytes = static_cast<uint32_t>(bits / 8);
    const uint32_t dataBytes = frames * static_cast<uint32_t>(channels) * bytes;
    auto be32 = [&](uint32_t v) {
        for (int b = 3; b >= 0; --b) out.put(static_cast<char>((v >> (8 * b)) & 0xFF));
    };
    auto be16 = [&](uint16_t v) {
        out.put(static_cast<char>(v >> 8));
        out.put(static_cast<char>(v & 0xFF));
    };
    const uint32_t commBytes = compression ? 18 + 4 + 2 : 18;  // (an empty name: a count and a pad byte)
    out.write("FORM", 4);
    be32(4 + 8 + commBytes + 8 + 8 + dataBytes);
    out.write(compression ? "AIFC" : "AIFF", 4);
    out.write("COMM", 4);
    be32(commBytes);
    be16(static_cast<uint16_t>(channels));
    be32(frames);
    be16(static_cast<uint16_t>(bits));
    // The rate as an 80-bit extended float.
    const int exponent = static_cast<int>(std::floor(std::log2(rate)));
    be16(static_cast<uint16_t>(16383 + exponent));
    const uint64_t mantissa = static_cast<uint64_t>(rate) << (63 - exponent);
    be32(static_cast<uint32_t>(mantissa >> 32));
    be32(static_cast<uint32_t>(mantissa));
    if (compression) {
        out.write(compression, 4);
        out.put(0);
        out.put(0);
    }
    out.write("SSND", 4);
    be32(8 + dataBytes);
    be32(0);
    be32(0);
    for (const float s : interleaved) {
        if (isFloat) {
            uint32_t v;
            std::memcpy(&v, &s, 4);
            be32(v);
            continue;
        }
        const double scale = bits == 16 ? 32768.0 : 8388608.0;
        const auto x = static_cast<int32_t>(std::clamp(std::round(static_cast<double>(s) * scale), -scale, scale - 1));
        for (uint32_t b = 0; b < bytes; ++b) {
            const uint32_t shift = little ? 8 * b : 8 * (bytes - 1 - b);
            out.put(static_cast<char>((x >> shift) & 0xFF));
        }
    }
    return utf8(path);
}

// Noise in both channels (a ramp in the second), `seconds` long at `rate`.
Samples testSignal(double seconds, int channels, int rate = kSampleRate, unsigned seed = 7) {
    const auto frames = static_cast<size_t>(seconds * rate);
    const Samples a = noise(frames, seed, 0.6f);
    if (channels == 1) return a;
    Samples b(frames);
    for (size_t i = 0; i < frames; ++i) b[i] = static_cast<float>(static_cast<double>(i % 997) / 997.0 - 0.5);
    return interleave({a, b});
}

// A cache of the test's own, its folder in the test's folder.
std::shared_ptr<sub::StreamCache> testCache() {
    auto cache = sub::StreamCache::create();
    cache->setFolder(utf8(tempDir() / "streaming"));
    return cache;
}

// Every sample of every channel of two sources the same, to the bit; and their peaks.
void checkSameSource(const sub::AudioSource& got, const sub::AudioSource& want) {
    REQUIRE(got.channels() == want.channels());
    REQUIRE(got.frames() == want.frames());
    CHECK_EQ(got.sampleRate(), want.sampleRate());
    for (uint32_t c = 0; c < want.channels(); ++c) {
        INFO("channel " + std::to_string(c));
        CHECK_ARRAY_EQUAL(sourceChannel(got, c), sourceChannel(want, c));
    }
    for (int level = 0; level < sub::AudioSource::kNumPeakLevels; ++level) {
        INFO("peaks, level " + std::to_string(level));
        REQUIRE(got.numPeaks(level) == want.numPeaks(level));
        const size_t n = static_cast<size_t>(want.numPeaks(level)) * want.channels() * 2;
        CHECK_ARRAY_EQUAL(Samples(got.peaks(level), got.peaks(level) + n), Samples(want.peaks(level), want.peaks(level) + n));
    }
}

// --- Engines --------------------------------------------------------------------

sub::StreamingSettings streamingEverything() {
    sub::StreamingSettings settings;
    settings.enabled = true;
    settings.thresholdSeconds = 0.0;
    settings.cacheFolder = utf8(tempDir() / "streaming");
    return settings;
}

sub::StreamingSettings inMemory() { return {}; }

// A project of clips of every kind, on an engine streaming (or not) as `settings` say.
struct Project {
    sub::Engine engine;
    std::vector<uint32_t> tracks;

    explicit Project(const sub::StreamingSettings& settings, const std::vector<std::string>& files) {
        engine.setStreaming(settings);
        for (const std::string& file : files) engine.loadSource(file);
        const auto add = [&](std::vector<sub::ClipDesc> clips) {
            const uint32_t track = engine.addTrack();
            engine.setTrackClips(track, clips);
            tracks.push_back(track);
        };
        const std::string& stereo = files[0];
        const std::string& mono = files[1];
        // As they are: offsets, gain, pan, fades; mono and stereo.
        sub::ClipDesc a = clip(stereo, 0.0, 3.0, 0.25, 0.8f);
        a.pan = -0.3f;
        a.fadeInSec = 0.05;
        a.fadeOutSec = 0.4;
        sub::ClipDesc b = clip(mono, 6.5, 2.0, 1.1, 1.f);
        add({a, b});
        // Re-Pitch, faster and slower.
        sub::ClipDesc fast = clip(stereo, 1.0, 2.5, 0.3);
        fast.warp = true;
        fast.segmentBpm = 80.0;  // at 120 BPM: 1.5 times as fast
        fast.warpMode = sub::WarpMode::RePitch;
        sub::ClipDesc slow = clip(mono, 7.0, 1.5, 0.0);
        slow.warp = true;
        slow.segmentBpm = 160.0;
        slow.warpMode = sub::WarpMode::RePitch;
        add({fast, slow});
        // Stretched, and transposed.
        sub::ClipDesc stretched = clip(stereo, 0.5, 2.0, 0.7);
        stretched.warp = true;
        stretched.segmentBpm = 100.0;
        stretched.warpMode = sub::WarpMode::Standard;
        sub::ClipDesc transposed = clip(mono, 6.0, 1.5, 0.2);
        transposed.transpose = 3.0;
        transposed.warpMode = sub::WarpMode::Formants;
        add({stretched, transposed});
        engine.setClipFadeMs(4.0);
    }
};

// Plays `snap` live, `blocks` blocks of `block` frames; interleaved stereo. `between` runs after each block.
Samples playLive(sub::Renderer& renderer, sub::SharedState& shared, const sub::RenderSnapshot& snap, int blocks,
                 int block = 256, const std::function<void(int)>& between = {}) {
    Samples out;
    std::vector<float> left(static_cast<size_t>(block)), right(left.size());
    float* outputs[2] = {left.data(), right.data()};
    sub::AudioIO io;
    io.outputs = outputs;
    io.numOutputs = 2;
    io.frames = static_cast<uint32_t>(block);
    for (int b = 0; b < blocks; ++b) {
        renderer.processLive(snap, shared, io);
        for (int i = 0; i < block; ++i) {
            out.push_back(left[static_cast<size_t>(i)]);
            out.push_back(right[static_cast<size_t>(i)]);
        }
        if (between) between(b);
    }
    return out;
}

// A disk that reads nothing (from page `fromPage` on) until let go of (the read hook waits), then at once.
// Declared after the scheduler whose reads it holds, so it lets them go before the scheduler's threads end.
struct Gate {
    std::shared_ptr<std::atomic<bool>> opened = std::make_shared<std::atomic<bool>>(false);
    sub::StreamCache::ReadHook hook(int64_t fromPage = 0) const {
        return [opened = opened, fromPage](const std::string&, int64_t first, int64_t count) {
            if (first + count <= fromPage) return;
            while (!opened->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
    }
    void open() const { opened->store(true); }
    ~Gate() { open(); }
};

// A snapshot of one track playing `clips` into the master (Direct clips; one voice per stretch configuration).
std::shared_ptr<sub::RenderSnapshot> snapshotOf(std::vector<sub::ClipRender> clips) {
    auto snap = std::make_shared<sub::RenderSnapshot>();
    snap->sampleRate = kSampleRate;
    snap->tempoMap = std::make_shared<const sub::TempoMap>(120.0);
    sub::TrackRender track;
    track.id = 1;
    track.params = std::make_shared<sub::TrackParams>();
    track.buffers = std::make_shared<sub::TrackBuffers>(sub::Renderer::kMaxBlock);
    for (const sub::ClipRender& clip : clips) track.maxClipLength = std::max(track.maxClipLength, clip.length);
    track.clips = std::move(clips);
    track.outgoing = {0};
    snap->tracks.push_back(std::move(track));
    sub::EdgeRender output;
    output.state = std::make_shared<sub::EdgeState>(sub::Renderer::kMaxBlock);
    snap->edges.push_back(std::move(output));
    snap->masterInputs = {0};
    for (int c = 0; c < sub::kNumStretchConfigs; ++c)
        snap->warpVoices[static_cast<size_t>(c)] = {std::make_shared<sub::WarpVoice>(static_cast<sub::StretchConfig>(c), kSampleRate)};
    return snap;
}

sub::ClipRender directClip(std::shared_ptr<const sub::AudioSource> source, int64_t start, int64_t length,
                           int64_t sourceOffset = 0) {
    sub::ClipRender clip;
    clip.source = std::move(source);
    clip.start = start;
    clip.length = length;
    clip.sourceOffset = sourceOffset;
    clip.key = 3;
    return clip;
}

}  // namespace

TEST_CASE("with SUBSTATION_TEST_STREAMING, every engine streams every file it plays") {
    if (streamEverything.folder.empty()) SKIP("SUBSTATION_TEST_STREAMING not set");
    sub::Engine engine;
    CHECK(engine.streaming().enabled);
    CHECK(!engine.loadSource(clickWav(0.5))->resident());
}

// --- What is read from disk ------------------------------------------------------

TEST_CASE("a WAV at the engine's rate streams as it is: the frames and peaks load() has") {
    struct Kind {
        int channels, bits;
        bool isFloat, extensible;
    };
    for (const Kind kind : {Kind{1, 16, false, false}, Kind{2, 16, false, false}, Kind{2, 24, false, false},
                            Kind{1, 24, false, true}, Kind{2, 32, true, false}, Kind{2, 32, true, true}}) {
        INFO(std::to_string(kind.channels) + " channels, " + std::to_string(kind.bits) + " bits" +
             (kind.extensible ? ", extensible" : ""));
        // Three pages and a bit: the last page partly in the file.
        const double seconds = 3.3 * static_cast<double>(sub::kStreamPageFrames) / kSampleRate;
        const std::string path = writeWavAs(tempDir() / ("as-is-" + std::to_string(kind.channels) + "-" +
                                                         std::to_string(kind.bits) + (kind.extensible ? "x" : "") + ".wav"),
                                            testSignal(seconds, kind.channels), kind.channels, kSampleRate, kind.bits,
                                            kind.isFloat, kind.extensible);
        const auto cache = testCache();
        const auto streamed = cache->open(path, kSampleRate);
        REQUIRE(streamed);
        CHECK(!streamed->resident());
        CHECK(streamed->channelData(0) == nullptr);
        REQUIRE(streamed->stream());
        CHECK(!streamed->stream()->layout().paged);
        CHECK(sub::platform::fileKey(streamed->stream()->layout().path) == sub::platform::fileKey(path));  // the file itself
        checkSameSource(*streamed, *sub::AudioSource::load(path, kSampleRate));
        // And its frames as the renderer reads them, live (all there) and offline: both sides.
        const sub::SourceCursor cursor(*streamed);
        Samples left(5000), right(5000), wantL(5000), wantR(5000);
        const auto resident = sub::AudioSource::load(path, kSampleRate);
        const sub::SourceCursor memory(*resident);
        for (const int64_t at : {int64_t{-100}, int64_t{32000}, streamed->frames() - 300}) {
            streamed->stream()->fetchBlocking(at, 5000, left.data(), right.data());
            memory.fetch(at, 5000, wantL.data(), wantR.data());
            CHECK_ARRAY_EQUAL(left, wantL);
            CHECK_ARRAY_EQUAL(right, wantR);
            CHECK_EQ(cursor.fetch(at, 5000, left.data(), right.data()), memory.fetch(at, 5000, wantL.data(), wantR.data()));
            CHECK_ARRAY_EQUAL(left, wantL);
        }
        CHECK_EQ(streamed->stream()->missedFrames(), uint64_t{0});
    }
}

TEST_CASE("an AIFF streams as it is too, big-endian or not") {
    struct Kind {
        int channels, bits;
        const char* compression;
    };
    for (const Kind kind : {Kind{2, 16, nullptr}, Kind{1, 24, nullptr}, Kind{2, 24, "NONE"}, Kind{2, 16, "sowt"},
                            Kind{1, 32, "fl32"}}) {
        INFO(std::to_string(kind.bits) + " bits, " + (kind.compression ? kind.compression : "AIFF"));
        const std::string path =
            writeAiff(tempDir() / ("as-is-" + std::to_string(kind.bits) + (kind.compression ? kind.compression : "") + ".aiff"),
                      testSignal(1.5, kind.channels), kind.channels, kSampleRate, kind.bits, kind.compression);
        uint32_t rate = 0;
        const auto layout = sub::asIsLayout(path, rate);
        REQUIRE(layout);
        CHECK_EQ(rate, uint32_t{kSampleRate});
        const auto cache = testCache();
        const auto streamed = cache->open(path, kSampleRate);
        checkSameSource(*streamed, *sub::AudioSource::load(path, kSampleRate));
    }
}

TEST_CASE("another rate, or another kind of file, streams from a cache file made once") {
    // At 44.1 kHz (resampled), with three channels (mixed down), and an MP3: floats, the decoder's own.
    const std::string resampled = writeWavAs(tempDir() / "at44.wav", testSignal(2.0, 2, 44100), 2, 44100, 16, false);
    const std::string threeChannels = [] {
        const Samples a = testSignal(1.0, 1, kSampleRate, 1), b = testSignal(1.0, 1, kSampleRate, 2),
                      c = testSignal(1.0, 1, kSampleRate, 3);
        return writeWavAs(tempDir() / "three.wav", interleave({a, b, c}), 3, kSampleRate, 24, false);
    }();
    std::string mp3;
    {
        sub::Engine engine;
        engine.setStreaming(inMemory());
        clipTrack(engine, makeWav(testSignal(1.0, 2), 2));
        mp3 = utf8(tempDir() / "mix.mp3");
        engine.exportFile(mp3, 0.0, 2.0, sub::ExportFormat{sub::ExportFormat::Kind::Mp3, 24, 192});
    }
    for (const std::string& path : {resampled, threeChannels, mp3}) {
        INFO(path);
        const auto cache = testCache();
        const auto streamed = cache->open(path, kSampleRate);
        REQUIRE(streamed->stream());
        const sub::StreamLayout& layout = streamed->stream()->layout();
        CHECK(layout.paged);
        CHECK(layout.encoding == sub::SampleEncoding::Float32);
        CHECK(std::filesystem::exists(sub::platform::toPath(layout.path)));
        checkSameSource(*streamed, *sub::AudioSource::load(path, kSampleRate));
        CHECK_EQ(streamed->fileSampleRate(), sub::AudioSource::load(path, kSampleRate)->fileSampleRate());
    }

    // Once made, it is read, not decoded again: what the cache file holds is what plays.
    const auto cache = testCache();
    const std::string cachePath = cache->open(resampled, kSampleRate)->stream()->layout().path;
    {
        std::fstream file(sub::platform::toPath(cachePath), std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(4096);  // the first page's first sample
        const float marked = 0.125f;
        file.write(reinterpret_cast<const char*>(&marked), 4);
    }
    float first = 0.f;
    testCache()->open(resampled, kSampleRate)->read(0, 0, 1, &first);
    CHECK_EQ(first, 0.125f);
}

TEST_CASE("a cache file is made again when its file changes, or at another rate") {
    const auto path = tempDir() / "changes.wav";
    writeWavAs(path, testSignal(1.0, 2, 44100, 1), 2, 44100, 16, false);
    const auto cache = testCache();
    const std::string before = cache->open(utf8(path), kSampleRate)->stream()->layout().path;
    const std::string other = cache->open(utf8(path), 96000)->stream()->layout().path;
    CHECK(before != other);  // (another rate: another file)
    // Written again (another length, so another stamp, whatever the clock's resolution).
    writeWavAs(path, testSignal(1.5, 2, 44100, 2), 2, 44100, 16, false);
    const auto streamed = cache->open(utf8(path), kSampleRate);
    CHECK(streamed->stream()->layout().path != before);
    checkSameSource(*streamed, *sub::AudioSource::load(utf8(path), kSampleRate));
}

TEST_CASE("a long file decoded into memory takes its peaks from its peak file, which a stream of it shares") {
    const auto cache = testCache();
    // At 44.1 kHz (resampled): past kPeakFileFrames at the engine's rate.
    const std::string path = writeWavAs(tempDir() / "long.wav", testSignal(22.0, 1, 44100, 3), 1, 44100, 16, false);
    std::string peakPath = cache->folder();
    sub::platform::appendName(peakPath,
                              std::string_view(sub::cacheFileName(path, *sub::platform::stamp(path), kSampleRate)));
    peakPath += sub::kPeakFileExtension;
    const auto made = sub::AudioSource::load(path, kSampleRate);  // (its peaks made from its frames)
    REQUIRE(made->frames() >= sub::StreamCache::kPeakFileFrames);
    const auto first = cache->loadResident(path, kSampleRate);
    CHECK(first->resident());
    checkSameSource(*first, *made);
    REQUIRE(std::filesystem::exists(sub::platform::toPath(peakPath)));

    // The next time they are read from it, not made again: a peak changed there shows.
    auto changed = sub::readPeakFile(peakPath, 1, made->frames());
    REQUIRE(changed);
    (*changed)[0][1] = 0.75f;  // (the first peak's maximum)
    REQUIRE(sub::writePeakFile(peakPath, 1, made->frames(), *changed));
    CHECK_EQ(cache->loadResident(path, kSampleRate)->peaks(0)[1], 0.75f);

    // A stream of it makes and reads the same file: the same peaks, to the bit.
    REQUIRE(sub::writePeakFile(peakPath, 1, made->frames(), made->peakLevels()));
    checkSameSource(*cache->open(path, kSampleRate), *made);
    checkSameSource(*cache->loadResident(path, kSampleRate), *made);

    // A short file's peaks are made as it is decoded: no peak file.
    const std::string shortPath = writeWavAs(tempDir() / "short.wav", testSignal(1.0, 1, 44100, 4), 1, 44100, 16, false);
    std::string shortPeaks = cache->folder();
    sub::platform::appendName(shortPeaks, std::string_view(sub::cacheFileName(shortPath, *sub::platform::stamp(shortPath),
                                                                              kSampleRate)));
    shortPeaks += sub::kPeakFileExtension;
    checkSameSource(*cache->loadResident(shortPath, kSampleRate), *sub::AudioSource::load(shortPath, kSampleRate));
    CHECK(!std::filesystem::exists(sub::platform::toPath(shortPeaks)));
}

TEST_CASE("cache files keep the decoder's integers where they hold every sample exactly") {
    // A big-endian WAV (RIFX) isn't read as it is, but its samples are 16-bit integers.
    const Samples signal = testSignal(1.0, 2);
    const auto path = tempDir() / "rifx.wav";
    {
        std::ofstream out(path, std::ios::binary);
        auto be32 = [&](uint32_t v) {
            for (int b = 3; b >= 0; --b) out.put(static_cast<char>((v >> (8 * b)) & 0xFF));
        };
        auto be16 = [&](uint16_t v) {
            out.put(static_cast<char>(v >> 8));
            out.put(static_cast<char>(v & 0xFF));
        };
        const auto dataBytes = static_cast<uint32_t>(signal.size() * 2);
        out.write("RIFX", 4);
        be32(36 + dataBytes);
        out.write("WAVEfmt ", 8);
        be32(16);
        be16(1);
        be16(2);
        be32(kSampleRate);
        be32(kSampleRate * 4);
        be16(4);
        be16(16);
        out.write("data", 4);
        be32(dataBytes);
        for (const float s : signal)
            be16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(std::round(s * 32768.0), -32768.0, 32767.0))));
    }
    const auto cache = testCache();
    const auto streamed = cache->open(utf8(path), kSampleRate);
    CHECK(streamed->stream()->layout().paged);
    CHECK(streamed->stream()->layout().encoding == sub::SampleEncoding::Int16);
    checkSameSource(*streamed, *sub::AudioSource::load(utf8(path), kSampleRate));

    // A writer of integers refuses a sample they can't hold exactly (the caller writes floats then).
    sub::CacheFileWriter exact(utf8(tempDir() / "exact.subcache"), 1, kSampleRate, sub::SampleEncoding::Int16);
    const float fine[3] = {0.5f, -1.f, 0.f};
    CHECK(exact.write(fine, 3));
    sub::CacheFileWriter inexact(utf8(tempDir() / "inexact.subcache"), 1, kSampleRate, sub::SampleEncoding::Int16);
    std::vector<float> page(static_cast<size_t>(sub::kStreamPageFrames), 0.25f);
    page[10] = 0.1f;  // (no 16-bit integer)
    CHECK(!inexact.write(page.data(), static_cast<int64_t>(page.size())));
}

TEST_CASE("the cache folder keeps to its budget: the least recently used go, never one in use") {
    const auto cache = testCache();
    std::vector<std::shared_ptr<sub::AudioSource>> open;
    std::vector<std::string> paths;
    for (int i = 0; i < 3; ++i) {
        const std::string path = writeWavAs(tempDir() / ("budget" + std::to_string(i) + ".wav"),
                                            testSignal(2.0, 2, 44100, static_cast<unsigned>(i)), 2, 44100, 16, false);
        open.push_back(cache->open(path, kSampleRate));
        paths.push_back(open.back()->stream()->layout().path);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));  // (their last uses apart)
    }
    open.erase(open.begin(), open.begin() + 2);  // the first two closed, the last in use
    cache->setFolderBudget(1);  // (nothing fits)
    CHECK(!std::filesystem::exists(sub::platform::toPath(paths[0])));
    CHECK(!std::filesystem::exists(sub::platform::toPath(paths[1])));
    CHECK(std::filesystem::exists(sub::platform::toPath(paths[2])));  // in use
}

// --- The pool --------------------------------------------------------------------

TEST_CASE("a missing page is silence, faded at its edges, and counted") {
    const std::string path = writeWavAs(tempDir() / "long.wav", full(static_cast<size_t>(10 * sub::kStreamPageFrames), 0.5f),
                                        1, kSampleRate, 16, false);
    const auto cache = testCache();
    const auto source = cache->open(path, kSampleRate);
    const sub::StreamedAudio& stream = *source->stream();
    // The head (2 s: pages 0 to 2) is in memory, the rest not yet.
    CHECK(stream.loaded(0));
    CHECK(stream.loaded(2));
    CHECK(!stream.loaded(3));
    const int64_t edge = 3 * sub::kStreamPageFrames;
    Samples left(1000), right(1000);
    const int64_t there = stream.fetch(edge - 500, 1000, left.data(), right.data());
    CHECK_EQ(there, int64_t{500});
    CHECK_EQ(stream.missedFrames(), uint64_t{500});
    CHECK_EQ(stream.takeUrgent(), int64_t{3});  // (for the disk threads)
    CHECK_EQ(left[0], 0.5f);
    CHECK_EQ(left[400], 0.5f);
    // The last kMissFade frames before it fall to nothing.
    CHECK(left[499] < 0.5f / 32);
    CHECK(left[499] > 0.f);
    CHECK(left[460] < 0.5f && left[460] > left[499]);
    CHECK_EQ(left[500], 0.f);
    CHECK_EQ(left[999], 0.f);
    CHECK_ARRAY_EQUAL(left, right);  // (mono: both sides)
    // Expected misses (the playhead dragged) are counted apart.
    stream.fetch(edge, 100, left.data(), right.data(), true);
    CHECK_EQ(stream.missedFrames(), uint64_t{500});
    CHECK_EQ(stream.expectedMisses(), uint64_t{100});
    // What the cache reports, once.
    const auto misses = cache->takeMisses();
    REQUIRE(misses.size() == 1);
    CHECK_EQ(misses[0].frames, uint64_t{500});
    CHECK(!misses[0].unreadable);
    CHECK(cache->takeMisses().empty());
    // Read (waiting for the disk), it is all there.
    stream.fetchBlocking(edge - 500, 1000, left.data(), right.data());
    CHECK_ARRAY_EQUAL(left, full(1000, 0.5f));
    CHECK(stream.loaded(3));
}

TEST_CASE("the pool lets the least recently wanted pages go, never pinned ones, and reuses them once the audio thread is done") {
    const std::string path = writeWavAs(tempDir() / "pool.wav", testSignal(60 * sub::kStreamPageFrames / double(kSampleRate), 2),
                                        2, kSampleRate, 16, false);
    const auto cache = testCache();
    cache->setPoolBytes(16 * sub::StreamCache::kPageBytes);  // the smallest pool: 16 pages
    std::atomic<uint64_t> begun{5}, finished{5};
    cache->setAudioEpochs(&begun, &finished);
    const auto source = cache->open(path, kSampleRate);
    const std::shared_ptr<sub::StreamedAudio> stream = source->sharedStream();
    sub::platform::AlignedBuffer buffer;
    // Pages 10 to 25 fill the pool.
    for (int64_t k = 10; k < 26; ++k) REQUIRE(cache->load(*stream, k, 1, false, buffer));
    for (int64_t k = 10; k < 26; ++k) CHECK(stream->loaded(k));
    // A pass wants 18 to 25 now: 10 to 17 are the ones to go, the oldest first.
    std::vector<sub::StreamCache::Want> wanted;
    for (int64_t k = 18; k < 26; ++k) wanted.push_back({stream, k, 0});
    CHECK(cache->plan(wanted).empty());  // (all in memory)
    // An audio callback under way: what is let go can't be reused yet.
    begun = 6;
    REQUIRE(cache->load(*stream, 30, 1, false, buffer));
    CHECK(stream->loaded(30));
    int64_t gone = 0;
    for (int64_t k = 10; k < 18; ++k) gone += stream->loaded(k) ? 0 : 1;
    CHECK_EQ(gone, int64_t{1});
    for (int64_t k = 18; k < 26; ++k) CHECK(stream->loaded(k));  // wanted now
    for (int64_t k = 0; k < 3; ++k) CHECK(stream->loaded(k));    // the head: pinned
    const size_t allocated = cache->stats().allocatedPages;
    // The callback done: the page let go is reused rather than another made.
    finished = 6;
    REQUIRE(cache->load(*stream, 31, 1, false, buffer));
    CHECK(cache->stats().allocatedPages <= allocated);
    CHECK(cache->stats().evicted >= 2);
    // A lease keeps a page as it is, even let go of: it isn't reused while held.
    const sub::StreamLease held = stream->lease(18);
    REQUIRE(held);
    const float before = held.channel(0)[7];
    cache->plan({});  // (a pass that wants nothing: any page but the pinned ones may go)
    for (int64_t k = 32; k < 60; ++k) cache->load(*stream, k, 1, false, buffer);
    CHECK(!stream->loaded(18));  // let go of...
    CHECK_EQ(held.channel(0)[7], before);  // ...but not reused
}

TEST_CASE("readers off the audio thread lease pages safely while the cache lets them go and frees them") {
    const std::string path = writeWavAs(tempDir() / "leases.wav", testSignal(40 * sub::kStreamPageFrames / double(kSampleRate), 2),
                                        2, kSampleRate, 16, false);
    const auto resident = sub::AudioSource::load(path, kSampleRate);
    const auto cache = testCache();  // (no audio thread: a page let go of is free at once, unless leased)
    const auto source = cache->open(path, kSampleRate);
    std::atomic<bool> stop{false};
    std::atomic<int> wrong{0};
    std::vector<std::thread> readers;
    for (uint32_t t = 0; t < 4; ++t) {
        readers.emplace_back([&, t] {
            std::vector<float> got(4096);
            std::minstd_rand random(t + 1);
            while (!stop) {
                const auto start = static_cast<int64_t>(random() % uint64_t(source->frames() - 4096));
                const auto c = static_cast<uint32_t>(random() % 2);
                if (!source->read(c, start, 4096, got.data()) ||
                    !std::equal(got.begin(), got.end(), resident->channelData(c) + start))
                    ++wrong;
            }
        });
    }
    // Meanwhile passes that want nothing (any page may go), and the pool shrunk and grown again (the
    // pages over it freed).
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    for (int i = 0; std::chrono::steady_clock::now() < until; ++i) {
        cache->plan({});
        cache->setPoolBytes((i % 2 ? 16 : 24) * sub::StreamCache::kPageBytes);
    }
    stop = true;
    for (auto& reader : readers) reader.join();
    CHECK_EQ(wrong.load(), 0);
    CHECK(cache->stats().evicted > 0);
}

// --- Playing --------------------------------------------------------------------

TEST_CASE("offline renders are the same streamed as from memory, to the bit, whatever the disk") {
    const std::string stereo = makeWav(testSignal(9.0, 2), 2);
    const std::string mono = makeWav(testSignal(6.0, 1, kSampleRate, 11), 1);
    Project memory(inMemory(), {stereo, mono});
    Project streamed(streamingEverything(), {stereo, mono});
    REQUIRE(!streamed.engine.cachedSource(stereo)->resident());
    REQUIRE(memory.engine.cachedSource(stereo)->resident());
    const int64_t frames = 6 * kSampleRate;
    const Samples want = memory.engine.renderOffline(0.0, frames);
    CHECK(rms(want) > 0.05);
    CHECK_ARRAY_EQUAL(streamed.engine.renderOffline(0.0, frames), want);
    // From elsewhere, with the loop on.
    memory.engine.setLoop(true, 3.0, 9.0);
    streamed.engine.setLoop(true, 3.0, 9.0);
    CHECK_ARRAY_EQUAL(streamed.engine.renderOffline(5.0, frames, true), memory.engine.renderOffline(5.0, frames, true));
    // A track's signal (freezing).
    for (size_t i = 0; i < memory.tracks.size(); ++i) {
        CHECK_ARRAY_EQUAL(streamed.engine.renderTrackOffline(streamed.tracks[i], 0.0, frames),
                          memory.engine.renderTrackOffline(memory.tracks[i], 0.0, frames));
    }
    // A slow disk changes nothing offline: it is waited for.
    Project slow(streamingEverything(), {stereo, mono});
    slow.engine.setStreamReadHook([](const std::string&, int64_t, int64_t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    });
    CHECK_ARRAY_EQUAL(slow.engine.renderOffline(0.0, frames), want);
}

TEST_CASE("live, streamed clips play what they play from memory once their pages are in") {
    const std::string path = makeWav(testSignal(8.0, 2), 2);
    const auto resident = sub::AudioSource::load(path, kSampleRate);
    const auto cache = testCache();
    const auto streamed = cache->open(path, kSampleRate);
    const auto clips = [](std::shared_ptr<const sub::AudioSource> source) {
        sub::ClipRender direct = directClip(source, 0, 3 * kSampleRate, kSampleRate);  // from 1 s into the file
        sub::ClipRender fast = directClip(source, 3 * kSampleRate, 2 * kSampleRate, 2 * kSampleRate);
        fast.playback = sub::ClipRender::Playback::Resample;
        fast.rate = 1.37;
        fast.key = 4;
        sub::ClipRender stretched = directClip(source, 5 * kSampleRate, 2 * kSampleRate, kSampleRate / 2);
        stretched.playback = sub::ClipRender::Playback::Stretch;
        stretched.rate = 0.8;
        stretched.key = 5;
        return std::vector<sub::ClipRender>{direct, fast, stretched};
    };
    const auto want = snapshotOf(clips(resident));
    const auto snap = snapshotOf(clips(streamed));
    sub::SharedState shared;
    sub::StreamScheduler scheduler(cache, shared);
    scheduler.setHorizon(8.0);  // (all of it: no real time here)
    scheduler.setSnapshot(snap);
    scheduler.waitIdle();
    sub::Renderer live, reference;
    for (sub::Renderer* r : {&live, &reference}) {
        r->prepare(kSampleRate);
        r->setPosition(0);
        r->setPlaying(true);
    }
    const int blocks = 7 * kSampleRate / 256;
    const Samples out = playLive(live, shared, *snap, blocks);
    sub::SharedState spare;
    CHECK_ARRAY_EQUAL(out, playLive(reference, spare, *want, blocks));
    CHECK(rms(out) > 0.05);
    CHECK_EQ(streamed->stream()->missedFrames(), uint64_t{0});
}

TEST_CASE("live, a disk slower than playback: what comes late is silence, faded, and counted; the rest as from memory") {
    const std::string path = makeWav(full(static_cast<size_t>(8 * kSampleRate), 0.5f), 1);
    const auto cache = testCache();
    const auto streamed = cache->open(path, kSampleRate);
    const auto snap = snapshotOf({directClip(streamed, 0, 8 * kSampleRate)});
    sub::SharedState shared;
    sub::StreamScheduler scheduler(cache, shared, 1);
    const Gate gate;  // (after the scheduler: let go of before its threads end)
    scheduler.setHorizon(1.0);
    cache->setReadHook(gate.hook());
    scheduler.setSnapshot(snap);
    sub::Renderer live;
    live.prepare(kSampleRate);
    live.setPosition(0);
    live.setPlaying(true);
    // Up to 4 s the disk reads nothing; then it keeps up again.
    const int blocks = 6 * kSampleRate / 256;
    const Samples out = playLive(live, shared, *snap, blocks, 256, [&](int b) {
        if (b == 4 * kSampleRate / 256) gate.open();
        if (b >= 4 * kSampleRate / 256) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    });
    const Samples left = channel(out, 0);
    // The head (its first 2 s) was in memory; after it the disk came late.
    for (size_t i = 0; i < static_cast<size_t>(2 * kSampleRate) - 64; ++i) REQUIRE(left[i] == 0.5f);
    CHECK_EQ(left[static_cast<size_t>(3 * kSampleRate)], 0.f);
    CHECK(streamed->stream()->missedFrames() >= static_cast<uint64_t>(kSampleRate));
    // Once it has caught up, as from memory.
    CHECK_EQ(left.back(), 0.5f);
    // Every frame as from memory, silent, or between the two at a miss's edges: never anything else.
    int64_t silent = 0, faded = 0, runs = 0;
    for (size_t i = 0; i < left.size(); ++i) {
        const float v = left[i];
        REQUIRE(v >= 0.f);
        REQUIRE(v <= 0.5f);
        if (v == 0.f) {
            ++silent;
            if (i == 0 || left[i - 1] != 0.f) ++runs;
        } else if (v != 0.5f) {
            ++faded;
        }
    }
    CHECK(faded > 0);
    CHECK(faded <= 2 * sub::StreamedAudio::kMissFade * runs);
    CHECK(static_cast<uint64_t>(silent) <= streamed->stream()->missedFrames());
    cache->setReadHook(nullptr);
}

#if SUBTEST_TIMED
TEST_CASE("live, the audio thread never waits for the disk") {
    const std::string path = makeWav(testSignal(6.0, 2), 2);
    uint64_t missed = 0;
    // The longest block, with a fast disk, and with one whose reads never finish while it plays.
    const auto longest = [&](bool stalled) {
        const auto cache = testCache();
        const auto streamed = cache->open(path, kSampleRate);
        const auto snap = snapshotOf({directClip(streamed, 0, 6 * kSampleRate)});
        sub::SharedState shared;
        sub::StreamScheduler scheduler(cache, shared, 2);
        const Gate gate;
        if (stalled) cache->setReadHook(gate.hook(3));  // (past the head)
        scheduler.setSnapshot(snap);
        sub::Renderer live;
        live.prepare(kSampleRate);
        live.setPosition(0);
        live.setPlaying(true);
        std::vector<float> left(256), right(256);
        float* outputs[2] = {left.data(), right.data()};
        sub::AudioIO io;
        io.outputs = outputs;
        io.numOutputs = 2;
        io.frames = 256;
        double worst = 0.0;
        for (int b = 0; b < 600; ++b) {
            const auto started = std::chrono::steady_clock::now();
            live.processLive(*snap, shared, io);
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        cache->setReadHook(nullptr);
        missed = streamed->stream()->missedFrames();
        return worst;
    };
    const double fast = longest(false);
    const double stalled = longest(true);
    INFO("fast " + std::to_string(fast) + " ms, stalled " + std::to_string(stalled) + " ms");
    CHECK(missed > 0);  // (it played on past the head, missing)
    CHECK(stalled < 20.0);
    CHECK(stalled < fast + 10.0);
}
#endif

TEST_CASE("playback starting where pages aren't in memory waits for them, up to 150 ms") {
    const std::string path = makeWav(full(static_cast<size_t>(20 * kSampleRate), 0.5f), 1);
    const auto cache = testCache();
    const auto streamed = cache->open(path, kSampleRate);
    const auto snap = snapshotOf({directClip(streamed, 0, 20 * kSampleRate)});
    constexpr int kBlocks = 80;  // (20480 frames, 256 at a time)
    const int64_t preRoll = std::llround(sub::Renderer::kPreRollSeconds * kSampleRate);
    for (const bool comesInTime : {true, false}) {
        INFO(comesInTime ? "the read comes in time" : "the read comes late");
        const int64_t from = (comesInTime ? 6 : 13) * kSampleRate;  // (not a clip's start: nothing there is in memory)
        sub::SharedState shared;
        sub::StreamScheduler scheduler(cache, shared, 1);
        const Gate gate;
        cache->setReadHook(gate.hook());
        scheduler.setHorizon(1.0);
        sub::Renderer live;
        live.prepare(kSampleRate);
        live.syncTempo(*snap);
        live.setPosition(from);
        live.publishTransport(shared);
        scheduler.setSnapshot(snap);
        const uint64_t missedBefore = streamed->stream()->missedFrames();
        shared.commands.push({sub::TransportCommand::Type::Play});
        // The disk reads nothing for 10 blocks (in time) or for the whole of it (late).
        const Samples out = playLive(live, shared, *snap, kBlocks, 256, [&](int b) {
            if (comesInTime && b == 10) gate.open();
            if (gate.opened->load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        });
        const int64_t moved = live.position() - from;
        CHECK(moved > 0);
        CHECK(moved <= int64_t{kBlocks} * 256 - 10 * 256);  // it waited first
        if (comesInTime) {
            // It waited for the read, then played everything from there: nothing missed.
            CHECK_EQ(streamed->stream()->missedFrames(), missedBefore);
            const auto sounding = above(channel(out, 0), 0.25);
            REQUIRE(!sounding.empty());
            CHECK_EQ(static_cast<int64_t>(channel(out, 0).size()) - sounding.front(), moved);
        } else {
            // It waited 150 ms (of blocks), then went on, missing what hadn't come.
            CHECK(moved >= int64_t{kBlocks} * 256 - preRoll - 256);
            CHECK(moved <= int64_t{kBlocks} * 256 - preRoll + 256);
            CHECK(streamed->stream()->missedFrames() > missedBefore);
        }
        cache->setReadHook(nullptr);
    }
}

TEST_CASE("a file that becomes unreadable plays as silence and is reported, never a crash") {
    const auto path = tempDir() / "goes.wav";
    writeWavAs(path, full(static_cast<size_t>(8 * kSampleRate), 0.5f), 1, kSampleRate, 16, false);
    const auto cache = testCache();
    const auto source = cache->open(utf8(path), kSampleRate);
    std::filesystem::resize_file(path, 4096);  // (cut short: its pages past the head can't be read)
    float sample = 1.f;
    CHECK(!source->read(0, 5 * kSampleRate, 1, &sample));
    CHECK_EQ(sample, 0.f);
    CHECK(source->stream()->unreadable());
    const auto misses = cache->takeMisses();
    REQUIRE(misses.size() == 1);
    CHECK(misses[0].unreadable);
    CHECK(sub::platform::fileKey(misses[0].path) == sub::platform::fileKey(utf8(path)));
    // The head is still in memory.
    CHECK(source->read(0, 100, 1, &sample));
    CHECK_EQ(sample, 0.5f);
}

TEST_CASE("the warp cache renders a streamed clip to a file streamed back: the same frames") {
    const std::string path = makeWav(testSignal(3.0, 2), 2);
    const auto cache = testCache();
    const auto streamed = cache->open(path, kSampleRate);
    const auto resident = sub::AudioSource::load(path, kSampleRate);
    const auto stretched = [](std::shared_ptr<const sub::AudioSource> source) {
        sub::ClipRender clip = directClip(std::move(source), 0, 2 * kSampleRate, kSampleRate / 3);
        clip.playback = sub::ClipRender::Playback::Stretch;
        clip.rate = 0.9;
        return clip;
    };
    sub::WarpCache warps(sub::WarpCache::kDefaultBudget, std::chrono::milliseconds(0));
    warps.setStreamCache(cache);
    const auto renderOf = [&](const sub::ClipRender& clip) {
        warps.beginBuild();
        auto render = warps.request(clip, kSampleRate);
        warps.endBuild();
        warps.waitIdle();
        return render;
    };
    const auto fromDisk = renderOf(stretched(streamed));
    const auto inMemory = renderOf(stretched(resident));
    REQUIRE(fromDisk->ready());
    REQUIRE(inMemory->ready());
    REQUIRE(fromDisk->audio());
    CHECK(!fromDisk->audio()->resident());
    CHECK(fromDisk->left() == nullptr);
    CHECK(inMemory->audio()->resident());
    const std::string file = fromDisk->audio()->stream()->layout().path;
    CHECK(std::filesystem::exists(sub::platform::toPath(file)));
    CHECK_ARRAY_EQUAL(sourceChannel(*fromDisk->audio(), 0), Samples(inMemory->left(), inMemory->left() + inMemory->length()));
    CHECK_ARRAY_EQUAL(sourceChannel(*fromDisk->audio(), 1), Samples(inMemory->right(), inMemory->right() + inMemory->length()));
}

// --- The engine ---------------------------------------------------------------------

TEST_CASE("an engine streams the files its settings say, and decodes the rest into memory") {
    const std::string shortFile = makeWav(testSignal(1.0, 2), 2);
    const std::string longFile = makeWav(testSignal(3.0, 2), 2);
    sub::Engine engine;
    sub::StreamingSettings settings = streamingEverything();
    settings.thresholdSeconds = 2.0;
    engine.setStreaming(settings);
    CHECK(engine.loadSource(shortFile)->resident());
    const auto streamed = engine.loadSource(longFile);
    CHECK(!streamed->resident());
    CHECK(engine.loadSource(longFile) == streamed);  // cached
    // A device's sample is in memory whatever its length.
    CHECK(engine.loadResidentSource(longFile)->resident());
    CHECK(engine.loadResidentSource(shortFile) == engine.loadSource(shortFile));
    // Switched off: loading it again decodes it into memory, in its place.
    settings.enabled = false;
    engine.setStreaming(settings);
    const auto again = engine.loadSource(longFile);
    CHECK(again->resident());
    CHECK(engine.cachedSource(longFile) == again);
    CHECK(engine.streaming() == settings);
}

TEST_CASE("an engine keeps the head of each file and the start of each clip in memory, and lets clips' go") {
    const std::string path = makeWav(testSignal(40.0, 1), 1);
    sub::Engine engine;
    engine.setClipFadeMs(0);
    engine.setStreaming(streamingEverything());
    const auto source = engine.loadSource(path);
    REQUIRE(!source->resident());
    const sub::StreamedAudio& stream = *source->stream();
    const uint32_t track = engine.addTrack();
    // A clip from 20 s into the file.
    engine.setTrackClips(track, {clip(path, 4.0, 5.0, 20.0)});
    engine.waitForStreams();
    const int64_t clipPage = (20 * kSampleRate) >> sub::kStreamPageShift;
    CHECK(stream.loaded(0));         // the head
    CHECK(stream.loaded(clipPage));  // the clip's start
    CHECK(stream.loaded(clipPage + 2));
    const size_t pinned = engine.streamingStats().pinnedPages;
    CHECK(pinned >= 6);
    // The clip gone, its start isn't pinned any more (the head still is).
    engine.setTrackClips(track, {});
    engine.waitForStreams();
    CHECK(engine.streamingStats().pinnedPages < pinned);
    CHECK(stream.loaded(0));
    // Played from its start offline: there from the first frame.
    engine.setTrackClips(track, {clip(path, 0.0, 5.0, 20.0)});
    const Samples out = engine.renderOffline(0.0, 1000);
    Samples want(1000);
    source->read(0, 20 * kSampleRate, 1000, want.data());
    CHECK_ALLCLOSE(channel(out, 0), want, 1e-6, 1e-7);
    CHECK(engine.takeStreamMisses().empty());
}
