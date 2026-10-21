// What loading an audio file into memory costs (AudioSource::load(), on the
// engine bridge's decoding threads): a stereo WAV decoded at its own rate, and
// at another through the resampler (Resampler.h), against miniaudio's linear
// resampler it replaced (its highest filter order, as decoding had it); and its
// peaks made from its frames against read from its peak file
// (StreamCache::loadResident(), a long file's).
//
//     decode_bench [--seconds 180] [--repeats 3] [--json out.json]
//
// For each, the best of `--repeats` runs: milliseconds, and how many times
// faster than the file plays. The files are written to a temporary folder
// first (and read from the system's file cache: the disk isn't measured).
// The engine alone, no Qt.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "AudioSource.h"
#include "Json.h"
#include "StreamCache.h"
#include "miniaudio.h"

namespace {

using sub::bench::Json;

struct Options {
    double seconds = 180.0;
    int repeats = 3;
    std::string json;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--seconds") {
            o.seconds = std::max(1.0, std::stod(value()));
        } else if (arg == "--repeats") {
            o.repeats = std::max(1, std::stoi(value()));
        } else if (arg == "--json") {
            o.json = value();
        } else if (arg == "--help" || arg == "-h") {
            std::printf("decode_bench [--seconds 180] [--repeats 3] [--json out.json]\n");
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option " + arg);
        }
    }
    return o;
}

// A 16-bit stereo WAV of noise and tones, `seconds` long at `rate`.
std::string writeWav(const std::filesystem::path& path, double seconds, uint32_t rate) {
    const auto frames = static_cast<uint32_t>(seconds * rate);
    std::ofstream out(path, std::ios::binary);
    const auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    u32(36 + frames * 4);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(16);
    out.write("data", 4);
    u32(frames * 4);
    std::mt19937 random(7);
    std::uniform_real_distribution<double> noise(-0.2, 0.2);
    std::vector<int16_t> block;
    block.reserve(2 * 65536);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double left = 0.4 * std::sin(2.0 * 3.141592653589793 * 440.0 * t) + noise(random);
        const double right = 0.4 * std::sin(2.0 * 3.141592653589793 * 9000.0 * t) + noise(random);
        block.push_back(static_cast<int16_t>(std::lround(left * 32767.0)));
        block.push_back(static_cast<int16_t>(std::lround(right * 32767.0)));
        if (block.size() == block.capacity()) {
            out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size() * 2));
            block.clear();
        }
    }
    out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size() * 2));
    return path.string();
}

// How decoding read a file at another rate before Resampler.h: miniaudio's own
// linear resampler, at its highest filter order, into planar memory.
int64_t decodeLinear(const std::string& path, uint32_t rate) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, rate);
    config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) throw std::runtime_error("can't open " + path);
    std::vector<float> left, right, chunk(16384 * 2);
    for (;;) {
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&decoder, chunk.data(), 16384, &read);
        for (ma_uint64 i = 0; i < read; ++i) {
            left.push_back(chunk[2 * i]);
            right.push_back(chunk[2 * i + 1]);
        }
        if (read < 16384) break;
    }
    ma_decoder_uninit(&decoder);
    return static_cast<int64_t>(left.size());
}

double bestMs(int repeats, const std::function<void()>& run) {
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < repeats; ++i) {
        const auto start = std::chrono::steady_clock::now();
        run();
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    return best;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "substation-decode-bench";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        struct Case {
            const char* name;
            uint32_t fileRate;
            uint32_t rate;
            bool linear;      // miniaudio's linear resampler, as before
            bool peakFile;    // through the stream cache: peaks from the peak file (made by a first load)
        };
        const std::vector<Case> cases{
            {"48 kHz as it is", 48000, 48000, false, false},
            {"48 kHz, peaks from their file", 48000, 48000, false, true},
            {"44.1 -> 48 kHz, linear (before)", 44100, 48000, true, false},
            {"44.1 -> 48 kHz", 44100, 48000, false, false},
            {"48 -> 44.1 kHz, linear (before)", 48000, 44100, true, false},
            {"48 -> 44.1 kHz", 48000, 44100, false, false},
            {"96 -> 48 kHz, linear (before)", 96000, 48000, true, false},
            {"96 -> 48 kHz", 96000, 48000, false, false},
            {"44.1 -> 48 kHz, peaks from their file", 44100, 48000, false, true},
        };
        auto cache = sub::StreamCache::create();
        cache->setFolder((dir / "cache").string());
        std::printf("%d s of 16-bit stereo, best of %d\n", static_cast<int>(o.seconds), o.repeats);
        std::printf("%-40s %10s %10s\n", "", "ms", "x real time");
        Json results = Json::array();
        for (const Case& c : cases) {
            const std::string path =
                (dir / ("at" + std::to_string(c.fileRate) + ".wav")).string();
            if (!std::filesystem::exists(path)) writeWav(path, o.seconds, c.fileRate);
            if (c.peakFile) cache->loadResident(path, c.rate);  // (its peak file made)
            const double ms = bestMs(o.repeats, [&] {
                if (c.linear) {
                    decodeLinear(path, c.rate);
                } else if (c.peakFile) {
                    cache->loadResident(path, c.rate);
                } else {
                    sub::AudioSource::load(path, c.rate);
                }
            });
            const double speed = o.seconds * 1000.0 / ms;
            std::printf("%-40s %10.1f %10.0f\n", c.name, ms, speed);
            std::fflush(stdout);
            Json row = Json::object();
            row.set("case", std::string(c.name));
            row.set("ms", sub::bench::rounded(ms, 2));
            row.set("realtime", sub::bench::rounded(speed, 1));
            results.push(std::move(row));
        }
        std::filesystem::remove_all(dir);
        if (!o.json.empty()) {
            Json report = Json::object();
            report.set("seconds", o.seconds);
            report.set("results", std::move(results));
            if (!report.save(o.json, 2)) throw std::runtime_error("could not write " + o.json);
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "decode_bench: %s\n", error.what());
        return 1;
    }
}
