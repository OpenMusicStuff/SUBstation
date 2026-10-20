// What playing an audio clip costs, per playback: Direct (the source as it is),
// Re-Pitch (resampled) at a few rates, and each stretch configuration (time
// stretched, transposed, keeping formants). The clip's own rendering alone, as
// Renderer::renderClips() asks for it (WarpVoice::render(), renderResampled()),
// in blocks as the audio thread gives them; no engine, no graph, no gain or pan.
//
//     warp_bench [--case NAME[,NAME...]] [--seconds 30] [--block 256] [--rate 48000]
//         [--repeat 3] [--input sound|silence] [--json out.json] [--help]
//
// The clip is --seconds of stereo noise and tones at about -12 dBFS (or of
// silence: what a clip costs where its file is quiet), written to a float WAV
// in the temporary folder and decoded as the engine does. Every case
// plays it from the start for as long as it lasts at its rate (and at most
// --seconds of output). The cost is in nanoseconds per output frame and as the
// load on one core at --rate (best of --repeat runs; 100% would be a whole core).
// The "-cached" cases play a stretched clip from its WarpCache render, as live
// playback does once it is ready; the time the render took (on the cache's own
// thread) is reported beside them. The "-streamed" cases play the clip streamed
// from disk (docs/engine/streaming.md), every page of it in memory: what reading
// through the stream cache's pages costs over reading a resident source in place.
//
// Qt-free: it links the engine alone.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "AudioSource.h"
#include "Json.h"
#include "Snapshot.h"
#include "SourceCursor.h"
#include "StreamCache.h"
#include "Warp.h"
#include "WarpCache.h"
#include "rt/RtUtils.h"

namespace {

using sub::bench::Json;
using Clock = std::chrono::steady_clock;
using Playback = sub::ClipRender::Playback;

constexpr const char* kUsage =
    "warp_bench [--case NAME[,NAME...]] [--seconds 30] [--block 256] [--rate 48000]\n"
    "    [--repeat 3] [--input sound|silence] [--json out.json] [--help]\n";

struct Options {
    std::vector<std::string> cases;
    double seconds = 30.0;
    int block = 256;
    double rate = 48000.0;
    int repeat = 3;
    std::string input = "sound";
    std::string json;
};

std::vector<std::string> split(const std::string& text, char by) {
    std::vector<std::string> parts;
    std::stringstream in(text);
    for (std::string part; std::getline(in, part, by);)
        if (!part.empty()) parts.push_back(part);
    return parts;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--case") {
            o.cases = split(next(), ',');
        } else if (arg == "--seconds") {
            o.seconds = std::stod(next());
            if (!(o.seconds >= 1.0 && o.seconds <= 3600.0)) throw std::runtime_error("--seconds takes 1 to 3600");
        } else if (arg == "--block") {
            o.block = std::clamp(std::stoi(next()), 1, 4096);
        } else if (arg == "--rate") {
            o.rate = std::stod(next());
            if (!(o.rate >= 8000.0 && o.rate <= 768000.0)) throw std::runtime_error("--rate takes 8000 to 768000");
        } else if (arg == "--repeat") {
            o.repeat = std::max(1, std::stoi(next()));
        } else if (arg == "--input") {
            o.input = next();
            if (o.input != "sound" && o.input != "silence") throw std::runtime_error("--input takes sound or silence");
        } else if (arg == "--json") {
            o.json = next();
        } else if (arg == "--help") {
            std::printf("%s", kUsage);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument " + arg);
        }
    }
    return o;
}

// One way of playing the clip.
struct Case {
    std::string name;
    Playback playback;
    double rate = 1.0;  // source frames per output frame
    sub::StretchConfig config = sub::StretchConfig::Standard;
    float transpose = 0.f;
    bool formants = false;
    bool cached = false;  // from its WarpCache render
    bool streamed = false;  // the source streamed from disk (all of it in memory)
};

const std::vector<Case>& allCases() {
    using sub::StretchConfig;
    static const std::vector<Case> cases = {
        {"direct", Playback::Direct},
        {"repitch-0.5", Playback::Resample, 0.5},
        {"repitch-0.9", Playback::Resample, 0.9},
        {"repitch-1.05", Playback::Resample, 1.05},
        {"repitch-1.5", Playback::Resample, 1.5},
        {"repitch-2", Playback::Resample, 2.0},
        {"transients-0.9", Playback::Stretch, 0.9, StretchConfig::Transient},
        {"standard-0.9", Playback::Stretch, 0.9, StretchConfig::Standard},
        {"smooth-0.9", Playback::Stretch, 0.9, StretchConfig::Smooth},
        {"standard-1.25", Playback::Stretch, 1.25, StretchConfig::Standard},
        {"transpose+7", Playback::Stretch, 1.0, StretchConfig::Standard, 7.f},
        {"formants+7", Playback::Stretch, 1.0, StretchConfig::Standard, 7.f, true},
        {"standard-0.9-cached", Playback::Stretch, 0.9, StretchConfig::Standard, 0.f, false, true},
        {"formants+7-cached", Playback::Stretch, 1.0, StretchConfig::Standard, 7.f, true, true},
        {"direct-streamed", Playback::Direct, 1.0, StretchConfig::Standard, 0.f, false, false, true},
        {"repitch-0.9-streamed", Playback::Resample, 0.9, StretchConfig::Standard, 0.f, false, false, true},
        {"repitch-1.5-streamed", Playback::Resample, 1.5, StretchConfig::Standard, 0.f, false, false, true},
        {"standard-0.9-streamed", Playback::Stretch, 0.9, StretchConfig::Standard, 0.f, false, false, true},
    };
    return cases;
}

// Stereo test audio: noise and two tones, a different mix per side, about -12 dBFS
// (as builtin_devices_bench plays).
void fill(std::vector<float>& left, std::vector<float>& right, double rate) {
    uint32_t state = 0x12345678u;
    for (size_t i = 0; i < left.size(); ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const float noise = static_cast<float>(state) * (2.f / 4294967296.f) - 1.f;
        const double t = static_cast<double>(i) / rate;
        const auto low = static_cast<float>(std::sin(2 * 3.14159265358979 * 110.0 * t));
        const auto high = static_cast<float>(std::sin(2 * 3.14159265358979 * 1760.0 * t));
        left[i] = 0.12f * noise + 0.15f * low + 0.05f * high;
        right[i] = 0.12f * noise + 0.10f * low + 0.08f * high;
    }
}

// A stereo 32-bit float WAV.
void writeWav(const std::filesystem::path& path, const std::vector<float>& left, const std::vector<float>& right,
              uint32_t rate) {
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<uint32_t>(left.size() * 2 * sizeof(float));
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(3);  // IEEE float
    u16(2);
    u32(rate);
    u32(rate * 2 * static_cast<uint32_t>(sizeof(float)));
    u16(static_cast<uint16_t>(2 * sizeof(float)));
    u16(32);
    out.write("data", 4);
    u32(bytes);
    for (size_t i = 0; i < left.size(); ++i) {
        const float frame[2] = {left[i], right[i]};
        out.write(reinterpret_cast<const char*>(frame), sizeof frame);
    }
    if (!out) throw std::runtime_error("couldn't write " + path.string());
}

// The temporary WAV (and the streamed one's cache folder), removed however the run ends.
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// Seconds spent rendering `frames` output frames of `clip` in blocks; `checksum`
// collects the output so nothing is optimised away.
double run(const Case& c, const sub::ClipRender& clip, int64_t frames, int block, double rate, double& checksum) {
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    std::vector<float> span(2 * sub::kResampleSpan);  // (a worker's scratch: Re-Pitch's span of a streamed source)
    std::unique_ptr<sub::WarpVoice> voice;
    if (c.playback == Playback::Stretch && !c.cached) voice = std::make_unique<sub::WarpVoice>(c.config, rate);
    sub::ScopedNoDenormals noDenormals;
    double busy = 0.0;
    double sum = 0.0;
    for (int64_t at = 0; at + block <= frames; at += block) {
        const auto start = Clock::now();
        switch (c.playback) {
            case Playback::Direct: {
                if (c.streamed) {  // what the renderer fetches through the source's cursor
                    sub::SourceCursor(*clip.source).fetch(clip.sourceOffset + at, block, l.data(), r.data());
                    break;
                }
                // What the renderer reads in place: the source's frames as they are.
                const float* srcL = clip.source->stereoChannel(0);
                const float* srcR = clip.source->stereoChannel(1);
                std::copy_n(srcL + clip.sourceOffset + at, block, l.data());
                std::copy_n(srcR + clip.sourceOffset + at, block, r.data());
                break;
            }
            case Playback::Resample: sub::renderResampled(clip, at, block, l.data(), r.data(), span.data()); break;
            case Playback::Stretch:
                if (c.cached) {  // what the renderer reads in place
                    std::copy_n(clip.rendered->left() + at, block, l.data());
                    std::copy_n(clip.rendered->right() + at, block, r.data());
                } else {
                    voice->render(clip, at, block, at > 0, l.data(), r.data());
                }
                break;
        }
        busy += std::chrono::duration<double>(Clock::now() - start).count();
        sum += static_cast<double>(l[static_cast<size_t>(block) / 2]) + static_cast<double>(r[0]);
    }
    checksum += sum;
    return busy;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        std::vector<Case> cases;
        for (const Case& c : allCases()) {
            if (o.cases.empty() || std::find(o.cases.begin(), o.cases.end(), c.name) != o.cases.end())
                cases.push_back(c);
        }
        for (const std::string& name : o.cases) {
            if (std::none_of(cases.begin(), cases.end(), [&](const Case& c) { return c.name == name; })) {
                std::string names;
                for (const Case& c : allCases()) names += " " + c.name;
                throw std::runtime_error("no case " + name + "; the cases are:" + names);
            }
        }

        const auto sampleRate = static_cast<uint32_t>(o.rate);
        const auto sourceFrames = static_cast<size_t>(o.seconds * o.rate);
        std::vector<float> left(sourceFrames), right(sourceFrames);
        if (o.input == "sound") fill(left, right, o.rate);
        // A name of its own: runs side by side don't share it.
        const auto unique = std::to_string(std::random_device{}()) + "_" +
                            std::to_string(Clock::now().time_since_epoch().count());
        const TemporaryFile wav{std::filesystem::temp_directory_path() / ("warp_bench_" + unique + ".wav")};
        const TemporaryFile folder{std::filesystem::temp_directory_path() / ("warp_bench_" + unique)};
        writeWav(wav.path, left, right, sampleRate);
        const std::shared_ptr<const sub::AudioSource> source = sub::AudioSource::load(wav.path.string(), sampleRate);
        std::shared_ptr<const sub::AudioSource> streamed;
        const auto streams = sub::StreamCache::create();
        if (std::any_of(cases.begin(), cases.end(), [](const Case& c) { return c.streamed; })) {
            // The float WAV streamed as it is, every page of it read into the pool first.
            streams->setFolder(folder.path.string());
            streams->setPoolBytes(sourceFrames * 2 * sizeof(float) * 2 + (size_t{64} << 20));
            streamed = streams->open(wav.path.string(), sampleRate);
            std::vector<float> l(sourceFrames), r(sourceFrames);
            streamed->stream()->fetchBlocking(0, static_cast<int64_t>(sourceFrames), l.data(), r.data());
        }
        sub::WarpCache cache(sub::WarpCache::kDefaultBudget, std::chrono::milliseconds(0));

        std::printf("%-20s %10s %10s %10s\n", "case", "ns/frame", "load", "x realtime");
        Json results = Json::array();
        double checksum = 0.0;
        for (const Case& c : cases) {
            sub::ClipRender clip;
            clip.source = c.streamed ? streamed : source;
            clip.rate = c.rate;
            clip.playback = c.playback;
            clip.stretchConfig = c.config;
            clip.transpose = c.transpose;
            clip.preserveFormants = c.formants;
            clip.key = 1;
            // The whole file at this rate, at most --seconds of output.
            const auto frames = std::min<int64_t>(static_cast<int64_t>(static_cast<double>(sourceFrames) / c.rate),
                                                  static_cast<int64_t>(sourceFrames));
            clip.length = frames;
            if (frames < o.block) throw std::runtime_error(c.name + ": --seconds too short for one --block");
            double renderSeconds = 0.0;
            if (c.cached) {  // rendered whole first, as the cache's thread does
                const auto start = Clock::now();
                cache.beginBuild();
                clip.rendered = cache.request(clip, o.rate);
                cache.endBuild();
                cache.waitIdle();
                renderSeconds = std::chrono::duration<double>(Clock::now() - start).count();
                if (!clip.rendered || !clip.rendered->ready()) throw std::runtime_error(c.name + ": no render");
            }
            double best = 1e300;
            for (int attempt = 0; attempt < o.repeat; ++attempt)
                best = std::min(best, run(c, clip, frames, o.block, o.rate, checksum));
            const double played = static_cast<double>(frames / o.block * o.block);
            const double nsPerFrame = 1e9 * best / played;
            const double load = best / (played / o.rate);
            std::printf("%-20s %10.2f %9.3f%% %10.0f", c.name.c_str(), nsPerFrame, 100.0 * load,
                        1.0 / std::max(load, 1e-12));
            Json result = Json::object()
                              .set("case", c.name)
                              .set("ns_per_frame", sub::bench::rounded(nsPerFrame, 3))
                              .set("load_percent", sub::bench::rounded(100.0 * load, 4));
            if (c.cached) {
                const double renderNs = 1e9 * renderSeconds / static_cast<double>(frames);
                std::printf("   (rendered in %.0f ms: %.1f ns/frame)", 1e3 * renderSeconds, renderNs);
                result.set("render_ns_per_frame", sub::bench::rounded(renderNs, 3));
            }
            std::printf("\n");
            results.push(result);
        }
        if (checksum == 12345.678) std::printf(" ");  // (keeps the output alive)
        if (!o.json.empty()) {
            const Json report = Json::object()
                                    .set("seconds", o.seconds)
                                    .set("block", o.block)
                                    .set("rate", o.rate)
                                    .set("repeat", o.repeat)
                                    .set("input", o.input)
                                    .set("results", results);
            if (!report.save(o.json, 2)) throw std::runtime_error("couldn't write " + o.json);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "warp_bench: %s\n", e.what());
        return 1;
    }
}
