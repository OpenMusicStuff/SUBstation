// substation-fxrender: runs audio files through a VST3 effect offline, for
// studying what a plug-in does to known signals (test tones in, its output out).
//
//   substation-fxrender <bundle.vst3> params
//       Lists the plug-in's parameters as JSON lines: index, id, name, unit,
//       range, default, steps, labels, and its own text for the default.
//   substation-fxrender <bundle.vst3> render
//       Reads jobs from stdin, one per line, and renders each:
//         <in.wav> <out.wav> [tail=<seconds>] [preroll=<seconds>] [block=<frames>] [notes=<key>,<key>...]
//                            [<param>=<value>]... [<param>~<normalized>]...
//       <param> is a parameter's index, id or name (spaces as '_'). Every job
//       gets a new instance of the plug-in (some keep history through a
//       deactivate and activate, so one job would colour the next), set up,
//       then fed `preroll` seconds of silence (default 0.5: parameter smoothing
//       settles) that are left out of the output. notes= holds those MIDI keys
//       (60 = C3) from the pre-roll's start to the end, velocity 100, for
//       plug-ins that take notes. Answers each with {"out": ..., "frames": ...,
//       "latency": ...} or {"error": ...}. Input: WAV, float32 or 16/24-bit PCM, mono or stereo;
//       output: stereo float32 WAV at the input's rate, latency removed.
//
// Links the engine's VST3 host (sub_engine) and nothing of Qt.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "builtin/BuiltinRegistry.h"
#include "plugins/Vst3Format.h"

namespace {

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

struct Audio {
    double sampleRate = 48000.0;
    std::vector<float> left, right;
};

template <typename T>
T readLe(const uint8_t* p) {
    T value;
    std::memcpy(&value, p, sizeof(T));
    return value;
}

Audio readWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("can't open " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0)
        throw std::runtime_error(path + " is not a WAV file");
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* samples = nullptr;
    size_t bytes = 0;
    for (size_t at = 12; at + 8 <= data.size();) {
        const uint32_t size = readLe<uint32_t>(&data[at + 4]);
        const uint8_t* chunk = &data[at + 8];
        const size_t available = std::min<size_t>(size, data.size() - at - 8);
        if (std::memcmp(&data[at], "fmt ", 4) == 0 && available >= 16) {
            format = readLe<uint16_t>(chunk);
            channels = readLe<uint16_t>(chunk + 2);
            rate = readLe<uint32_t>(chunk + 4);
            bits = readLe<uint16_t>(chunk + 14);
            if (format == 0xFFFE && available >= 26) format = readLe<uint16_t>(chunk + 24);
        } else if (std::memcmp(&data[at], "data", 4) == 0) {
            samples = chunk;
            bytes = available;
        }
        at += 8 + size + (size & 1);
    }
    if (!samples || channels < 1 || rate == 0) throw std::runtime_error(path + ": no audio");
    const int width = bits / 8;
    if (!((format == 3 && bits == 32) || (format == 1 && (bits == 16 || bits == 24))))
        throw std::runtime_error(path + ": only float32 and 16/24-bit PCM");
    const size_t frames = bytes / (static_cast<size_t>(width) * channels);
    Audio audio;
    audio.sampleRate = rate;
    audio.left.resize(frames);
    audio.right.resize(frames);
    auto sample = [&](size_t frame, int channel) -> float {
        const uint8_t* p = samples + (frame * channels + channel) * width;
        if (format == 3) return readLe<float>(p);
        if (bits == 16) return readLe<int16_t>(p) / 32768.f;
        const int32_t v = static_cast<int32_t>((p[0] << 8) | (p[1] << 16) | (p[2] << 24)) >> 8;
        return v / 8388608.f;
    };
    for (size_t i = 0; i < frames; ++i) {
        audio.left[i] = sample(i, 0);
        audio.right[i] = sample(i, std::min(1, channels - 1));
    }
    return audio;
}

void writeWav(const std::string& path, const Audio& audio) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("can't write " + path);
    const auto frames = static_cast<uint32_t>(audio.left.size());
    const uint32_t dataBytes = frames * 8;
    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    file.write("RIFF", 4);
    u32(36 + dataBytes);
    file.write("WAVEfmt ", 8);
    u32(16);
    u16(3);
    u16(2);
    u32(static_cast<uint32_t>(audio.sampleRate));
    u32(static_cast<uint32_t>(audio.sampleRate) * 8);
    u16(8);
    u16(32);
    file.write("data", 4);
    u32(dataBytes);
    std::vector<float> interleaved(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        interleaved[2 * i] = audio.left[i];
        interleaved[2 * i + 1] = audio.right[i];
    }
    file.write(reinterpret_cast<const char*>(interleaved.data()), static_cast<std::streamsize>(dataBytes));
    if (!file) throw std::runtime_error("short write: " + path);
}

std::string underscored(std::string text) {
    std::replace(text.begin(), text.end(), ' ', '_');
    return text;
}

int findParam(sub::Processor& processor, const std::string& key) {
    const auto& params = processor.params();
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].id == key || underscored(params[i].name) == key) return static_cast<int>(i);
    }
    if (!key.empty() && std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const int index = std::stoi(key);
        if (index < static_cast<int>(params.size())) return index;
    }
    throw std::runtime_error("no parameter " + key);
}

void listParams(sub::Processor& processor) {
    const auto& params = processor.params();
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        std::ostringstream line;
        line << "{\"index\": " << i << ", \"id\": " << quote(p.id) << ", \"name\": " << quote(p.name)
             << ", \"unit\": " << quote(p.unit) << ", \"min\": " << p.minValue << ", \"max\": " << p.maxValue
             << ", \"default\": " << p.defaultValue << ", \"steps\": " << p.stepCount()
             << ", \"value\": " << processor.getParam(static_cast<int>(i))
             << ", \"text\": " << quote(processor.paramText(static_cast<int>(i), processor.getParam(static_cast<int>(i))))
             << ", \"hidden\": " << (p.hidden ? "true" : "false") << ", \"readOnly\": " << (p.readOnly ? "true" : "false");
        if (!p.valueLabels.empty()) {
            line << ", \"labels\": [";
            for (size_t l = 0; l < p.valueLabels.size(); ++l) line << (l ? ", " : "") << quote(p.valueLabels[l]);
            line << "]";
        }
        // For continuous parameters: the plug-in's text at a few points of its range.
        if (p.stepCount() == 0 || p.stepCount() > 16) {
            line << ", \"texts\": [";
            for (int k = 0; k <= 4; ++k) {
                const float plain = p.fromNormalized(k / 4.f);
                line << (k ? ", " : "") << quote(processor.paramText(static_cast<int>(i), plain));
            }
            line << "]";
        } else {
            line << ", \"texts\": [";
            for (int k = 0; k <= p.stepCount(); ++k) {
                line << (k ? ", " : "") << quote(processor.paramText(static_cast<int>(i), p.minValue + k));
            }
            line << "]";
        }
        std::cout << line.str() << "}\n";
    }
    std::cout << "{\"latency\": " << processor.latencySamples() << ", \"tail\": " << processor.tailSamples()
              << ", \"acceptsMidi\": " << (processor.acceptsMidi() ? "true" : "false")
              << ", \"sidechain\": " << (processor.hasSidechain() ? "true" : "false") << "}\n";
}

constexpr int kMaxBlock = 8192;  // the most a job's block= may ask for

// Renders one job line. Returns the answer.
std::string renderJob(const std::function<std::shared_ptr<sub::Processor>()>& create, double sampleRate,
                      const std::string& line) {
    std::istringstream words(line);
    std::string inPath, outPath, word;
    words >> inPath >> outPath;
    if (outPath.empty()) throw std::runtime_error("a job is <in.wav> <out.wav> [settings]");

    const auto instance = create();
    sub::Processor& processor = *instance;
    processor.idle();
    double tail = 1.0, preroll = 0.5;
    int block = 512;
    std::vector<uint8_t> keys;
    while (words >> word) {
        if (word.rfind("notes=", 0) == 0) {
            std::istringstream list(word.substr(6));
            std::string key;
            while (std::getline(list, key, ',')) {
                if (!key.empty()) keys.push_back(static_cast<uint8_t>(std::clamp(std::stoi(key), 0, 127)));
            }
            continue;
        }
        const auto at = word.find_first_of("=~");
        if (at == std::string::npos) throw std::runtime_error("not a setting: " + word);
        const std::string key = word.substr(0, at);
        const float value = std::stof(word.substr(at + 1));
        if (key == "block") {
            block = std::clamp(static_cast<int>(value), 1, kMaxBlock);
            continue;
        }
        if (key == "tail" || key == "preroll") {
            (key == "tail" ? tail : preroll) = value;
            continue;
        }
        const int index = findParam(processor, key);
        const auto& info = processor.params()[index];
        processor.setParam(index, word[at] == '~' ? info.fromNormalized(value) : value);
    }
    processor.idle();
    // Prepared again: some plug-ins only read settings such as an FFT size there.
    processor.prepare(sampleRate, kMaxBlock);
    processor.resetOffline();
    processor.idle();

    const Audio in = readWav(inPath);
    if (in.sampleRate != sampleRate) throw std::runtime_error(inPath + " is not at the plug-in's sample rate");
    const int latency = processor.latencySamples();
    const auto tailFrames = static_cast<size_t>(tail * in.sampleRate);
    const auto prerollFrames = static_cast<size_t>(preroll * in.sampleRate);
    const size_t total = prerollFrames + in.left.size() + tailFrames + static_cast<size_t>(latency);
    Audio out;
    out.sampleRate = in.sampleRate;
    out.left.assign(total, 0.f);
    out.right.assign(total, 0.f);

    std::vector<float> left(static_cast<size_t>(block)), right(static_cast<size_t>(block));
    float* channels[2] = {left.data(), right.data()};
    sub::ProcessContext ctx;
    ctx.sampleRate = in.sampleRate;
    ctx.playing = true;
    ctx.offline = true;
    std::vector<sub::ProcessEvent> events;
    for (size_t pos = 0; pos < total; pos += static_cast<size_t>(block)) {
        const int frames = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), total - pos));
        events.clear();
        for (const uint8_t key : keys) {
            if (pos == 0) events.push_back(sub::ProcessEvent::noteOn(0, key, 100));
            if (pos + static_cast<size_t>(block) >= total) events.push_back(sub::ProcessEvent::noteOff(frames - 1, key));
        }
        std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            return a.sampleOffset < b.sampleOffset;
        });
        ctx.inEvents = {events.data(), events.size()};
        for (int i = 0; i < frames; ++i) {
            const size_t at = pos + i - std::min(pos + i, prerollFrames);
            const bool playing = pos + i >= prerollFrames && at < in.left.size();
            left[i] = playing ? in.left[at] : 0.f;
            right[i] = playing ? in.right[at] : 0.f;
        }
        ctx.samplePos = static_cast<int64_t>(pos);
        ctx.beatPos = static_cast<double>(pos) / ctx.samplesPerBeat();
        processor.process(ctx, channels, 2, frames);
        std::copy_n(left.data(), frames, out.left.data() + pos);
        std::copy_n(right.data(), frames, out.right.data() + pos);
        if ((pos / static_cast<size_t>(block)) % 64 == 0) processor.idle();
    }
    const auto skip = static_cast<std::ptrdiff_t>(prerollFrames) + latency;
    out.left.erase(out.left.begin(), out.left.begin() + skip);
    out.right.erase(out.right.begin(), out.right.begin() + skip);
    writeWav(outPath, out);
    std::ostringstream answer;
    answer << "{\"out\": " << quote(outPath) << ", \"frames\": " << out.left.size() << ", \"latency\": " << latency
           << "}";
    return answer.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: substation-fxrender <bundle.vst3> params|render [sampleRate]\n";
        return 2;
    }
    const std::string bundle = argv[1];
    const std::string mode = argv[2];
    const double sampleRate = argc > 3 ? std::stod(argv[3]) : 48000.0;
    try {
        // "builtin:<id>": one of the engine's own devices instead of a VST3 bundle.
        std::function<std::shared_ptr<sub::Processor>()> create;
        std::shared_ptr<sub::Processor> processor;
        if (bundle.rfind("builtin:", 0) == 0) {
            const std::string id = bundle.substr(8);
            create = [id, sampleRate] {
                auto p = sub::BuiltinRegistry::instance().create(id);
                p->prepare(sampleRate, kMaxBlock);
                return p;
            };
            processor = create();
            std::cerr << "loaded " << processor->name() << " (built-in)\n";
        } else {
            auto& format = sub::vst3::Vst3Format::instance();
            const auto found = format.scanFile(bundle);
            const auto effect = std::find_if(found.begin(), found.end(), [](const auto& d) { return !d.isInstrument; });
            if (effect == found.end()) throw std::runtime_error("no effect in " + bundle);
            processor = format.instantiate(bundle, effect->uid, sampleRate, kMaxBlock);
            processor->idle();
            std::cerr << "loaded " << effect->name << " " << effect->version << " (" << effect->vendor << ")\n";
            // (The first instance stays: it keeps the module loaded.)
            const std::string uid = effect->uid;
            create = [&format, bundle, uid, sampleRate] { return format.instantiate(bundle, uid, sampleRate, kMaxBlock); };
        }
        if (mode == "params") {
            listParams(*processor);
            return 0;
        }
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            try {
                std::cout << renderJob(create, sampleRate, line) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "{\"error\": " << quote(e.what()) << ", \"job\": " << quote(line) << "}" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
