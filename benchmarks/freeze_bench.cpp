// Freezing: tracks rendered before their faders into WAV files, as freezing
// them does (Engine::renderTrackToWav, with the application's 10 s tail).
//
//     freeze_bench [--tracks 16] [--seconds 30] [--freeze 4] [--threads N] [--json out.json]
//
// The project: --tracks tracks of dense chords on the built-in Synth, each
// through an EQ and a Saturator, every fourth through a Compressor too (the
// second track's keyed by the first: a sidechain). The first four go into a
// group with a Reverb; every track sends into a return with a Delay and a
// Reverb; the master has a Limiter. Measured, best of three, each from the
// start of the arrangement (--seconds long) with the tail:
//
// - one track: a track of its own (not in the group), out of all of them;
// - --freeze tracks, as freezing that many at once does (one render), and one
//   by one (a render each, as it was done before);
// - the group (its four tracks feeding it); the group, its tracks and the
//   return at once (what feeds them shared), and one by one;
// - a short arrangement: one track of a 4-second song (chords end at 3 s), the
//   tail being most of the render;
// - decoding the --freeze tracks' files, which the window did after freezing
//   them before their renders handed their samples over;
// - the whole mix exported (16-bit WAV), for comparison: the code it shares.
//
// Qt-free: it links the engine alone.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "AudioSource.h"
#include "Engine.h"
#include "Json.h"

namespace {

using sub::bench::format;
using sub::bench::Json;
using Clock = std::chrono::steady_clock;

constexpr int kRate = 48000;
constexpr double kTail = 10.0;  // seconds: the application's (kFreezeTailSeconds)

struct Stop : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Options {
    int tracks = 16;
    double seconds = 30.0;
    int freeze = 4;
    int threads = 0;  // 0: the engine's default
    std::string json;
};

// A folder of its own in the temporary folder, gone with it.
class TempFolder {
public:
    TempFolder() {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() / ("sub-freeze-bench-" + std::to_string(random()));
        std::filesystem::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void setParam(sub::Engine& engine, uint32_t processor, const std::string& id, float value) {
    const int index = engine.processorParamIndex(processor, id);
    if (index < 0) throw Stop("no parameter " + id);
    engine.setProcessorParam(processor, index, value);
}

struct Project {
    std::vector<uint32_t> tracks;  // in order: the group's four first
    uint32_t group = 0;
    uint32_t ret = 0;
};

// Chords of quarter notes for `beats` (at 120 BPM), from a seeded root.
std::vector<sub::NoteDesc> chords(std::mt19937& random, double beats) {
    std::vector<sub::NoteDesc> notes;
    std::uniform_int_distribution<int> roots(36, 60);
    for (double beat = 0.0; beat + 1.0 <= beats; beat += 1.0) {
        const int root = roots(random);
        for (const int interval : {0, 4, 7, 11}) notes.push_back({beat, 0.75, root + interval, 100});
    }
    return notes;
}

Project build(sub::Engine& engine, int count, double beats) {
    std::mt19937 random(1);
    Project project;
    project.group = engine.addTrack();
    engine.addBuiltinProcessor(engine.trackChain(project.group), "reverb", -1);
    project.ret = engine.addTrack();
    const uint32_t delay = engine.addBuiltinProcessor(engine.trackChain(project.ret), "delay", -1);
    setParam(engine, delay, "feedback", 30.f);
    setParam(engine, delay, "mix", 100.f);
    const uint32_t reverb = engine.addBuiltinProcessor(engine.trackChain(project.ret), "reverb", -1);
    setParam(engine, reverb, "mix", 100.f);
    engine.addBuiltinProcessor(engine.trackChain(sub::Engine::kMaster), "limiter", -1);
    for (int t = 0; t < count; ++t) {
        const uint32_t track = engine.addTrack();
        const uint32_t chain = engine.trackChain(track);
        const uint32_t synth = engine.addBuiltinProcessor(chain, "synth", -1);
        engine.setProcessorParam(synth, 0, static_cast<float>(t % 4));  // the waveforms in turn
        engine.addBuiltinProcessor(chain, "eq", -1);
        engine.addBuiltinProcessor(chain, "saturator", -1);
        if (t % 4 == 1) {
            const uint32_t compressor = engine.addBuiltinProcessor(chain, "compressor", -1);
            if (t == 1) engine.setProcessorSidechain(compressor, project.tracks.front());
        }
        engine.setTrackNotes(track, chords(random, beats));
        engine.setTrackGain(track, 1.f / static_cast<float>(count));
        if (t < 4) engine.setTrackOutput(track, project.group);
        engine.setTrackSend(track, project.ret, 0.3f, false);
        project.tracks.push_back(track);
    }
    engine.idle();
    return project;
}

double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

// Freezes these tracks as the application does: in one render, each into a WAV
// file of its own, from the start to `beats`, with the tail (`oneByOne`: a
// render for each, as the application did before). The frames written, in all.
int64_t freeze(sub::Engine& engine, const std::vector<uint32_t>& tracks, double beats,
               const std::filesystem::path& folder, bool oneByOne = false) {
    std::vector<sub::TrackFile> files;
    for (const uint32_t track : tracks) files.push_back({track, (folder / ("t" + std::to_string(track) + ".wav")).string()});
    int64_t frames = 0;
    if (oneByOne) {
        for (const sub::TrackFile& file : files) frames += engine.renderTrackToWav(file.trackId, file.path, 0.0, beats, kTail);
    } else {
        for (const int64_t written : engine.renderTracksToWav(files, 0.0, beats, kTail)) frames += written;
    }
    return frames;
}

struct Result {
    std::string name;
    double seconds;   // best of three
    int64_t frames;   // written
    double audio;     // seconds of audio rendered (the arrangement's)
};

Result measure(const std::string& name, double audioSeconds, const std::function<int64_t()>& run) {
    std::vector<double> times;
    int64_t frames = 0;
    for (int i = 0; i < 3; ++i) {
        const auto start = Clock::now();
        frames = run();
        times.push_back(secondsSince(start));
    }
    const double best = *std::min_element(times.begin(), times.end());
    std::printf("  %-34s %8.3f s  (%5.1fx real time; %lld frames written)\n", name.c_str(), best, audioSeconds / best,
                static_cast<long long>(frames));
    std::fflush(stdout);
    return {name, best, frames, audioSeconds};
}

const char* const kUsage =
    "usage: freeze_bench [--tracks 16] [--seconds 30] [--freeze 4] [--threads N] [--json out.json]\n"
    "\n"
    "Tracks rendered before their faders into WAV files, as freezing them does (a 10 s tail):\n"
    "one track, --freeze tracks, a group, one track of a 4-second song; and the whole mix exported.\n"
    "Best of three. --threads: the engine's audio threads (default: one per core but one).\n";

Options parseArguments(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw Stop("argument " + name + ": expected one argument");
            return argv[++i];
        };
        if (name == "--help" || name == "-h") {
            std::printf("%s", kUsage);
            std::exit(0);
        } else if (name == "--tracks") {
            options.tracks = std::stoi(value());
        } else if (name == "--seconds") {
            options.seconds = std::stod(value());
        } else if (name == "--freeze") {
            options.freeze = std::stoi(value());
        } else if (name == "--threads") {
            options.threads = std::stoi(value());
        } else if (name == "--json") {
            options.json = value();
        } else {
            throw Stop("unrecognized arguments: " + name);
        }
    }
    if (options.tracks < 8) throw Stop("--tracks: at least 8");
    if (options.freeze < 1 || options.freeze > options.tracks - 4) throw Stop("--freeze: 1 to --tracks - 4");
    return options;
}

int run(const Options& options) {
    TempFolder folder;
    const double beats = options.seconds * 2.0;  // 120 BPM
    std::vector<Result> results;
    {
        sub::Engine engine;
        if (options.threads > 0) engine.setAudioThreads(options.threads);
        std::printf("freeze_bench: %d tracks, %.0f s, %d audio threads\n", options.tracks, options.seconds,
                    engine.audioThreads());
        const Project project = build(engine, options.tracks, beats);
        const std::vector<uint32_t> own(project.tracks.begin() + 4, project.tracks.end());
        results.push_back(measure("one track", options.seconds,
                                  [&] { return freeze(engine, {own.front()}, beats, folder.path()); }));
        const std::vector<uint32_t> several(own.begin(), own.begin() + options.freeze);
        results.push_back(measure(format("%d tracks", options.freeze), options.seconds * options.freeze,
                                  [&] { return freeze(engine, several, beats, folder.path()); }));
        // What the window waited for after such a freeze before the render handed its samples over
        // (RenderJob::sources()): the files decoded.
        results.push_back(measure(format("decoding the %d files", options.freeze), options.seconds * options.freeze, [&] {
            int64_t frames = 0;
            for (const uint32_t track : several) {
                const auto path = (folder.path() / ("t" + std::to_string(track) + ".wav")).string();
                frames += sub::AudioSource::load(path, kRate)->frames();
            }
            return frames;
        }));
        results.push_back(measure(format("%d tracks, a render each", options.freeze), options.seconds * options.freeze,
                                  [&] { return freeze(engine, several, beats, folder.path(), true); }));
        std::vector<uint32_t> withGroup{project.group, project.ret};
        withGroup.insert(withGroup.end(), project.tracks.begin(), project.tracks.begin() + 4);
        results.push_back(measure("the group, its tracks and the return", options.seconds * 6,
                                  [&] { return freeze(engine, withGroup, beats, folder.path()); }));
        results.push_back(measure("the same, a render each", options.seconds * 6,
                                  [&] { return freeze(engine, withGroup, beats, folder.path(), true); }));
        results.push_back(measure("the group", options.seconds,
                                  [&] { return freeze(engine, {project.group}, beats, folder.path()); }));
        results.push_back(measure("export (the whole mix)", options.seconds, [&] {
            engine.exportWav((folder.path() / "mix.wav").string(), 0.0, beats, 16);
            return std::llround(beats * kRate / 2.0);
        }));
    }
    {
        sub::Engine engine;
        if (options.threads > 0) engine.setAudioThreads(options.threads);
        const Project project = build(engine, options.tracks, 6.0);  // chords to 3 s
        results.push_back(measure("one track of a 4 s song", 4.0, [&] {
            return freeze(engine, {project.tracks[4]}, 8.0, folder.path());
        }));
    }
    if (!options.json.empty()) {
        Json report = Json::object();
        report.set("tracks", options.tracks).set("seconds", options.seconds).set("threads", options.threads);
        Json list = Json::array();
        for (const Result& result : results) {
            list.push(Json::object()
                          .set("name", result.name)
                          .set("seconds", sub::bench::rounded(result.seconds, 4))
                          .set("frames", result.frames)
                          .set("realtime", sub::bench::rounded(result.audio / result.seconds, 2)));
        }
        report.set("results", list);
        if (!report.save(options.json, 2)) throw Stop("can't write " + options.json);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parseArguments(argc, argv));
    } catch (const Stop& stop) {
        std::fprintf(stderr, "freeze_bench: %s\n", stop.what());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "freeze_bench: %s\n", error.what());
        return 1;
    }
}
