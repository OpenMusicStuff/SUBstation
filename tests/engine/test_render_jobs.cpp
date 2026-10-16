// Renders in the background (RenderJob): an export or a track's render on a
// thread of its own, with its progress, cancelled (its file gone), one at a
// time, rendering the project as it was when it started.

#include <algorithm>
#include <cmath>
#include <chrono>
#include <memory>
#include <thread>

#include "Engine.h"
#include "AudioFileWriter.h"
#include "AudioSource.h"
#include "RenderJob.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"

using namespace subtest;

namespace {

struct JobEngine {
    sub::Engine engine;
    JobEngine() { engine.setClipFadeMs(0); }
};

constexpr double kTwoHours = 2 * 60 * 60 * 2.0;  // in beats

// The progress seen until the job is done.
std::vector<double> waitDone(sub::RenderJob& job, double timeout = 20.0) {
    std::vector<double> seen;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    while (!job.done()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);  // (the render never ended)
        seen.push_back(job.progress());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return seen;
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code ignored;
    return std::filesystem::exists(path, ignored);
}

}  // namespace

TEST_CASE("an export in the background is the same export") {
    JobEngine e;
    auto& engine = e.engine;
    clipTrack(engine, rampWav());
    const auto now = tempDir() / "now.wav", later = tempDir() / "later.wav";
    engine.exportWav(now.string(), 0.0, 2.0, 24);

    const auto job = engine.startExport(later.string(), 0.0, 2.0, 24);
    CHECK_EQ(job->path(), later.string());
    CHECK(engine.isRendering());
    const std::vector<double> seen = waitDone(*job);
    CHECK(std::is_sorted(seen.begin(), seen.end()));
    CHECK_EQ(job->progress(), 1.0);
    CHECK(engine.isRendering());  // until it is finished
    CHECK(job->finish() == std::optional<int64_t>(2 * kBeat));
    CHECK(!engine.isRendering());
    CHECK(readBytes(later) == readBytes(now));
    CHECK(job->finish() == std::optional<int64_t>(2 * kBeat));  // (again: the same)
}

TEST_CASE("a track rendered in the background is the same render") {
    JobEngine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, dcWav(), 0.0, 0.5);
    const auto now = tempDir() / "now.wav", later = tempDir() / "later.wav";
    const int64_t written = engine.renderTrackToWav(track, now.string(), 0.0, 2.0, 1.0);
    const auto job = engine.startTrackRender(track, later.string(), 0.0, 2.0, 1.0);
    waitDone(*job);
    CHECK(job->finish() == std::optional<int64_t>(written));
    CHECK_EQ(written, 2 * kBeat);  // (no tail: silent after the clip)
    CHECK(readBytes(later) == readBytes(now));
}

TEST_CASE("several tracks rendered in the background: their files, progress, cancelling") {
    JobEngine e;
    auto& engine = e.engine;
    const uint32_t a = clipTrack(engine, dcWav(), 0.0, 0.5);
    const uint32_t b = clipTrack(engine, rampWav());
    const auto pathA = tempDir() / "a.wav", pathB = tempDir() / "b.wav";
    const auto job = engine.startTracksRender({{a, pathA.string()}, {b, pathB.string()}}, 0.0, 2.0, 1.0);
    CHECK(job->paths() == (std::vector<std::string>{pathA.string(), pathB.string()}));
    const std::vector<double> seen = waitDone(*job);
    CHECK(std::is_sorted(seen.begin(), seen.end()));
    CHECK_EQ(job->progress(), 1.0);
    CHECK(job->finish() == std::optional<int64_t>(2 * kBeat));
    CHECK(job->written() == (std::vector<int64_t>{2 * kBeat, 2 * kBeat}));
    CHECK(readBytes(pathA) != readBytes(pathB));
    const auto alone = tempDir() / "alone.wav";
    engine.renderTrackToWav(b, alone.string(), 0.0, 2.0, 1.0);
    CHECK(readBytes(pathB) == readBytes(alone));

    const auto longA = tempDir() / "long-a.wav", longB = tempDir() / "long-b.wav";
    const auto cancelled = engine.startTracksRender({{a, longA.string()}, {b, longB.string()}}, 0.0, kTwoHours, 0.0);
    CHECK(fileExists(longA) && fileExists(longB));  // created at once
    cancelled->cancel();
    CHECK(!cancelled->finish().has_value());
    CHECK(cancelled->written().empty());
    CHECK(!fileExists(longA) && !fileExists(longB));
    CHECK_THROWS_AS(engine.startTracksRender({}, 0.0, 1.0, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(engine.startTracksRender({{a, longA.string()}, {12345, longB.string()}}, 0.0, 1.0, 0.0),
                    std::invalid_argument);
    CHECK(!fileExists(longA) && !fileExists(longB));
}

TEST_CASE("a track's render hands over its samples as decoding its file gives them") {
    JobEngine e;
    auto& engine = e.engine;
    const uint32_t a = clipTrack(engine, rampWav());
    const uint32_t b = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(b), "synth", -1);
    engine.setTrackNotes(b, {{1.5, 0.5, 60, 100}});  // (its release rings past the range: a tail)
    const auto pathA = tempDir() / "kept-a.wav", pathB = tempDir() / "kept-b.wav";
    const auto job = engine.startTracksRender({{a, pathA.string()}, {b, pathB.string()}}, 0.0, 2.0, 1.0);
    job->finish();
    REQUIRE(job->sources().size() == 2);
    for (size_t i = 0; i < 2; ++i) {
        const sub::AudioSource& kept = *job->sources()[i];
        const auto decoded = sub::AudioSource::load(job->paths()[i], kSampleRate);
        CHECK_EQ(kept.path(), job->paths()[i]);
        CHECK_EQ(kept.frames(), job->written()[i]);
        CHECK_EQ(kept.frames(), decoded->frames());
        CHECK_EQ(kept.channels(), decoded->channels());
        CHECK_EQ(kept.sampleRate(), decoded->sampleRate());
        CHECK_EQ(kept.fileSampleRate(), decoded->fileSampleRate());
        const auto samples = static_cast<size_t>(kept.frames()) * kept.channels();
        CHECK(std::equal(kept.data(), kept.data() + samples, decoded->data()));
        for (int level = 0; level < kept.numPeakLevels(); ++level) {
            const auto peaks = static_cast<size_t>(kept.numPeaks(level)) * kept.channels() * 2;
            CHECK_EQ(kept.numPeaks(level), decoded->numPeaks(level));
            CHECK(std::equal(kept.peaks(level), kept.peaks(level) + peaks, decoded->peaks(level)));
        }
    }
    CHECK(job->sources()[1]->frames() > 2 * kBeat);
    // An export keeps none.
    const auto exported = engine.startExport((tempDir() / "kept-mix.wav").string(), 0.0, 2.0, 24);
    exported->finish();
    CHECK(exported->sources().empty());
}

TEST_CASE("a cancelled render leaves no file") {
    JobEngine e;
    auto& engine = e.engine;
    clipTrack(engine, dcWav());
    const auto target = tempDir() / "long.wav";
    auto job = engine.startExport(target.string(), 0.0, kTwoHours, 16);
    CHECK(fileExists(target));  // created at once
    job->cancel();
    CHECK(job->cancelled());
    CHECK(!job->finish().has_value());
    CHECK(!fileExists(target));
    CHECK(!engine.isRendering());
    // The engine renders again as before.
    CHECK_APPROX(at(engine.renderOffline(0.0, kBeat), 100, 0), 0.5);

    const uint32_t track = engine.addTrack();
    job = engine.startTrackRender(track, (tempDir() / "track.wav").string(), 0.0, kTwoHours, 0.0);
    job->cancel();
    CHECK(!job->finish().has_value());
    CHECK(!fileExists(tempDir() / "track.wav"));
}

TEST_CASE("one render at a time, and what waits for it") {
    JobEngine e;
    auto& engine = e.engine;
    clipTrack(engine, dcWav());
    const auto job = engine.startExport((tempDir() / "a.wav").string(), 0.0, 2.0, 24);
    CHECK_THROWS_MATCHING(engine.startExport((tempDir() / "b.wav").string(), 0.0, 2.0, 24), std::runtime_error,
                          "Wait for the render");
    CHECK(!fileExists(tempDir() / "b.wav"));
    CHECK_THROWS_MATCHING(engine.renderOffline(0.0, 100), std::runtime_error, "Wait for the render");
    CHECK_THROWS_MATCHING(engine.setAudioThreads(engine.audioThreads() != 1 ? 1 : 2), std::runtime_error,
                          "Wait for the render");
    engine.takeMeters();  // what the UI polls doesn't wait for it
    engine.idle();
    CHECK(job->finish() == std::optional<int64_t>(2 * kBeat));
    engine.setAudioThreads(1);
    CHECK_APPROX(at(engine.renderOffline(0.0, 100), 50, 0), 0.5);
}

TEST_CASE("a render hears the project as it was when it started") {
    JobEngine e;
    auto& engine = e.engine;
    const uint32_t track = clipTrack(engine, dcWav());
    const uint32_t utility = engine.addBuiltinProcessor(engine.trackChain(track), "utility", -1);
    const auto mix = tempDir() / "mix.wav";
    const auto job = engine.startExport(mix.string(), 0.0, 2.0, 32);
    engine.setTrackClips(track, {});  // changes meanwhile are taken...
    engine.removeProcessor(utility);
    engine.removeTrack(track);
    engine.idle();
    CHECK(job->finish() == std::optional<int64_t>(2 * kBeat));  // ...but not heard in it
    sub::Engine reader;
    const auto exported = reader.loadSource(mix.string());
    CHECK_EQ(exported->channels(), 2u);
    CHECK_EQ(exported->frames(), 2 * kBeat);
    for (uint32_t c = 0; c < exported->channels(); ++c) {
        INFO("channel " + std::to_string(c));
        const Samples samples(exported->channelData(c), exported->channelData(c) + exported->frames());
        CHECK_ALLCLOSE(samples, 0.5, 1e-6, 1e-12);  // (pytest.approx)
    }
    CHECK_EQ(at(engine.renderOffline(0.0, 100), 50, 0), 0.f);
}

TEST_CASE("an export as MP3: the mix, encoded at the bitrate asked for") {
    JobEngine e;
    auto& engine = e.engine;
    clipTrack(engine, dcWav());
    const auto mp3 = tempDir() / "mix.mp3";
    const sub::ExportFormat format{sub::ExportFormat::Kind::Mp3, 24, 256};
    const auto job = engine.startExport(mp3.string(), 0.0, 2.0, format);
    CHECK(fileExists(mp3));  // created at once
    CHECK(job->finish() == std::optional<int64_t>(2 * kBeat));  // (the frames rendered)
    const std::vector<uint8_t> bytes = readBytes(mp3);
    REQUIRE(bytes.size() > 4);
    CHECK_EQ(bytes[0], 0xFF);  // MPEG-1 Layer III,
    CHECK_EQ(bytes[1] & 0xFE, 0xFA);
    CHECK_EQ((bytes[2] >> 4) & 0xF, 0xD);  // 256 kbps
    sub::Engine reader;
    const auto decoded = reader.loadSource(mp3.string());
    CHECK_EQ(decoded->channels(), 2u);
    // The second, and at most the encoder's delay and padding more (a frame or two).
    CHECK(decoded->frames() >= 2 * kBeat);
    CHECK(decoded->frames() <= 2 * kBeat + 3 * 1152);
    CHECK_APPROX(at(engine.renderOffline(0.0, 100), 50, 0), 0.5);  // (live again)
    double sum = 0.0;
    for (int64_t i = kBeat - 500; i < kBeat + 500; ++i) sum += decoded->channelData(1)[i];
    CHECK(std::abs(sum / 1000.0 - 0.5) < 0.05);

    // Cancelled, it leaves nothing; a bitrate MP3 hasn't is refused before a file is made.
    auto cancelled = engine.startExport((tempDir() / "long.mp3").string(), 0.0, kTwoHours, format);
    cancelled->cancel();
    CHECK(!cancelled->finish().has_value());
    CHECK(!fileExists(tempDir() / "long.mp3"));
    CHECK_THROWS_AS(engine.startExport((tempDir() / "bad.mp3").string(), 0.0, 2.0,
                                       sub::ExportFormat{sub::ExportFormat::Kind::Mp3, 24, 500}),
                    std::invalid_argument);
    CHECK(!fileExists(tempDir() / "bad.mp3"));
}

TEST_CASE("an MP3 export encoded on a thread of its own is the encoder's file") {
    JobEngine e;
    auto& engine = e.engine;
    clipTrack(engine, rampWav());
    const auto exported = tempDir() / "threaded.mp3", direct = tempDir() / "direct.mp3";
    engine.exportFile(exported.string(), 0.0, 4.0, sub::ExportFormat{sub::ExportFormat::Kind::Mp3, 24, 192});
    // The same render handed to LAME's writer on this thread, in the export's pieces.
    const Samples mix = engine.renderOffline(0.0, 4 * kBeat);
    {
        const auto writer = sub::makeMp3Writer(direct.string(), kSampleRate, 192);
        for (int64_t done = 0; done < 4 * kBeat; done += 4096) {
            writer->write(mix.data() + done * 2, std::min<int64_t>(4096, 4 * kBeat - done));
        }
        writer->keep();
    }
    CHECK(readBytes(exported) == readBytes(direct));
}

TEST_CASE("a render that can't start says why") {
    JobEngine e;
    auto& engine = e.engine;
    CHECK_THROWS_MATCHING(engine.startExport((tempDir() / "missing folder" / "mix.wav").string(), 0.0, 1.0, 24),
                          std::runtime_error, "Could not create");
    CHECK_THROWS_AS(engine.startExport((tempDir() / "mix.wav").string(), 1.0, 1.0, 24), std::invalid_argument);
    CHECK_THROWS_AS(engine.startTrackRender(12345, (tempDir() / "track.wav").string(), 0.0, 1.0, 0.0),
                    std::invalid_argument);
    CHECK(!engine.isRendering());
    CHECK(std::filesystem::is_empty(tempDir()));
}

TEST_CASE("a render let go of unfinished gives the engine back") {
    JobEngine e;
    auto& engine = e.engine;
    auto job = engine.startExport((tempDir() / "mix.wav").string(), 0.0, kTwoHours, 24);
    job.reset();
    CHECK(!engine.isRendering());
    CHECK(!fileExists(tempDir() / "mix.wav"));
    // And an engine going while its render runs stops it first.
    auto other = std::make_unique<sub::Engine>();
    job = other->startExport((tempDir() / "other.wav").string(), 0.0, kTwoHours, 24);
    other.reset();
    CHECK(!job->finish().has_value());
    CHECK(!fileExists(tempDir() / "other.wav"));
}
