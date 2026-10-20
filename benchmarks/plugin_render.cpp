// plugin_render PLUGIN --list
// plugin_render PLUGIN IN.wav OUT.wav [--set NAME=VALUE]... [--rate 48000] [--block 512] [--tail SECONDS]
//               [--note START:KEY:LENGTH]... [--preroll SECONDS]
//
// A WAV file played through one effect offline, the way an export plays it: a VST3 or VST2 plug-in (its file),
// or a built-in device ("builtin:gigatune"). For comparing a built-in with the plug-ins it stands beside.
//
// --list prints the parameters: index, name, range, default and the device's own text of its value.
// --set NAME=VALUE sets a parameter by its name (case ignored), to a plain value, a normalized one ("n0.25"),
// or a choice's label ("=Robot"). --note plays a MIDI note (seconds, key, seconds) into the device. The output
// is as long as the input plus --tail (the latency is reported, and left in: the output is not shifted), 32-bit
// float, two channels; the input may be one or two channels, 16/24-bit PCM or 32-bit float. --displays FILE.csv
// writes a built-in device's displays (a column each, from the silence before the input on).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Processor.h"
#include "builtin/BuiltinRegistry.h"
#include "plugins/PluginFormat.h"
#include "rt/RtUtils.h"

namespace {

struct Audio {
    double rate = 48000.0;
    std::vector<std::vector<float>> channels;
    size_t frames() const { return channels.empty() ? 0 : channels[0].size(); }
};

uint32_t u32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint16_t u16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

Audio readWav(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("can't read " + path);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) || std::memcmp(data.data() + 8, "WAVE", 4))
        throw std::runtime_error(path + " is not a WAV file");
    int format = 0, numChannels = 0, bits = 0;
    Audio audio;
    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const uint32_t size = u32(&data[pos + 4]);
        const unsigned char* body = &data[pos + 8];
        if (!std::memcmp(&data[pos], "fmt ", 4)) {
            format = u16(body);
            numChannels = u16(body + 2);
            audio.rate = u32(body + 4);
            bits = u16(body + 14);
            if (format == 0xfffe) format = u16(body + 24);  // WAVE_FORMAT_EXTENSIBLE: the sub-format
        } else if (!std::memcmp(&data[pos], "data", 4)) {
            if (numChannels <= 0) throw std::runtime_error(path + ": data before fmt");
            const int bytes = bits / 8;
            const size_t frames = std::min<size_t>(size, data.size() - pos - 8) / (bytes * numChannels);
            audio.channels.assign(numChannels, std::vector<float>(frames));
            for (size_t f = 0; f < frames; ++f) {
                for (int c = 0; c < numChannels; ++c) {
                    const unsigned char* s = body + (f * numChannels + c) * bytes;
                    float v = 0.f;
                    if (format == 3 && bits == 32) std::memcpy(&v, s, 4);
                    else if (format == 1 && bits == 16) v = static_cast<int16_t>(u16(s)) / 32768.f;
                    else if (format == 1 && bits == 24)
                        v = static_cast<float>(static_cast<int32_t>(u32(s - 1) & 0xffffff00u) / 2147483648.0);
                    else throw std::runtime_error(path + ": unsupported sample format");
                    audio.channels[c][f] = v;
                }
            }
            return audio;
        }
        pos += 8 + size + (size & 1);
    }
    throw std::runtime_error(path + ": no data");
}

void writeWav(const std::string& path, const Audio& audio) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("can't write " + path);
    const auto numChannels = static_cast<uint32_t>(audio.channels.size());
    const auto frames = static_cast<uint32_t>(audio.frames());
    const uint32_t dataSize = frames * numChannels * 4;
    auto put32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto put16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    put32(36 + dataSize);
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(3);
    put16(static_cast<uint16_t>(numChannels));
    put32(static_cast<uint32_t>(audio.rate));
    put32(static_cast<uint32_t>(audio.rate) * numChannels * 4);
    put16(static_cast<uint16_t>(numChannels * 4));
    put16(32);
    out.write("data", 4);
    put32(dataSize);
    for (uint32_t f = 0; f < frames; ++f)
        for (uint32_t c = 0; c < numChannels; ++c) out.write(reinterpret_cast<const char*>(&audio.channels[c][f]), 4);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::shared_ptr<sub::Processor> load(const std::string& what, double rate, int block) {
    if (what.rfind("builtin:", 0) == 0) {
        auto device = sub::BuiltinRegistry::instance().create(what.substr(8));
        if (!device) throw std::runtime_error("no built-in " + what);
        device->prepare(rate, block);
        return device;
    }
    sub::PluginFormat* format = sub::pluginFormatOfFile(what);
    if (!format) throw std::runtime_error("not a plug-in file: " + what);
    const auto found = format->scanFile(what);
    if (found.empty()) throw std::runtime_error("no plug-in in " + what);
    const auto it = std::find_if(found.begin(), found.end(), [](const auto& d) { return !d.isInstrument; });
    const sub::PluginDescription& plugin = it != found.end() ? *it : found.front();
    std::fprintf(stderr, "%s %s (%s, %s)\n", plugin.name.c_str(), plugin.version.c_str(), plugin.vendor.c_str(),
                 plugin.format.c_str());
    return format->instantiate(plugin.path, plugin.uid, rate, block);
}

void list(sub::Processor& device) {
    const auto& params = device.params();
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        const float value = device.getParam(static_cast<int>(i));
        std::string labels;
        for (const auto& label : p.valueLabels) labels += (labels.empty() ? " [" : "|") + label;
        if (!labels.empty()) labels += "]";
        std::printf("%3zu %-28s %10g .. %-10g default %-8g now %-8g (%s) %s%s%s\n", i, p.name.c_str(), p.minValue,
                    p.maxValue, p.defaultValue, value, device.paramText(static_cast<int>(i), value).c_str(),
                    p.unit.c_str(), labels.c_str(), p.hidden ? " hidden" : "");
    }
}

void set(sub::Processor& device, const std::string& assignment) {
    const auto eq = assignment.find('=');
    if (eq == std::string::npos) throw std::runtime_error("--set NAME=VALUE: " + assignment);
    const std::string name = lower(assignment.substr(0, eq)), value = assignment.substr(eq + 1);
    const auto& params = device.params();
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        if (lower(p.name) != name && lower(p.id) != name) continue;
        float plain = 0.f;
        const auto label = std::find_if(p.valueLabels.begin(), p.valueLabels.end(),
                                        [&](const std::string& l) { return lower(l) == lower(value); });
        if (label != p.valueLabels.end()) plain = static_cast<float>(label - p.valueLabels.begin()) + p.minValue;
        else if (!value.empty() && value[0] == 'n') plain = p.fromNormalized(std::stof(value.substr(1)));
        else plain = std::stof(value);
        device.setParam(static_cast<int>(i), plain);
        std::fprintf(stderr, "set %s = %g (%s)\n", p.name.c_str(), device.getParam(static_cast<int>(i)),
                     device.paramText(static_cast<int>(i), device.getParam(static_cast<int>(i))).c_str());
        return;
    }
    throw std::runtime_error("no parameter " + assignment.substr(0, eq));
}

struct Note {
    double start = 0.0, length = 0.0;
    int key = 60;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::fprintf(stderr, "plugin_render PLUGIN --list | IN.wav OUT.wav [--set NAME=VALUE]...\n");
            return 2;
        }
        const std::string what = argv[1];
        double rate = 48000.0, tail = 1.0, preroll = 0.25;
        int block = 512;
        std::vector<std::string> sets;
        std::vector<Note> notes;
        std::vector<std::string> files;
        std::string displaysPath;
        bool listOnly = false;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
                return argv[++i];
            };
            if (arg == "--list") listOnly = true;
            else if (arg == "--set") sets.push_back(next());
            else if (arg == "--rate") rate = std::stod(next());
            else if (arg == "--block") block = std::stoi(next());
            else if (arg == "--tail") tail = std::stod(next());
            else if (arg == "--preroll") preroll = std::stod(next());
            else if (arg == "--displays") displaysPath = next();
            else if (arg == "--note") {
                Note note;
                const std::string spec = next();
                if (std::sscanf(spec.c_str(), "%lf:%d:%lf", &note.start, &note.key, &note.length) != 3)
                    throw std::runtime_error("--note START:KEY:LENGTH: " + spec);
                notes.push_back(note);
            } else files.push_back(arg);
        }
        Audio input;
        if (!listOnly) {
            if (files.size() != 2) throw std::runtime_error("IN.wav and OUT.wav");
            input = readWav(files[0]);
            rate = input.rate;
        }
        auto device = load(what, rate, block);
        for (const auto& s : sets) set(*device, s);
        device->idle();
        if (listOnly) {
            list(*device);
            std::printf("latency %d samples\n", device->latencySamples());
            return 0;
        }

        sub::ScopedNoDenormals noDenormals;
        device->resetOffline(true);
        std::vector<std::vector<float>> buffers(2, std::vector<float>(block));
        float* channels[2] = {buffers[0].data(), buffers[1].data()};
        sub::ProcessContext ctx;
        ctx.sampleRate = rate;
        ctx.offline = true;
        ctx.playing = true;
        // Silence first: plug-ins take their parameters in a process call.
        const auto prerollFrames = static_cast<int64_t>(preroll * rate);
        const auto inFrames = static_cast<int64_t>(input.frames());
        const auto total = inFrames + static_cast<int64_t>(tail * rate);
        Audio output;
        output.rate = rate;
        output.channels.assign(2, std::vector<float>(static_cast<size_t>(total)));
        std::vector<sub::ProcessEvent> events;
        for (int64_t pos = -prerollFrames; pos < total;) {
            const int n = static_cast<int>(std::min<int64_t>(block, total - pos));
            events.clear();
            for (const Note& note : notes) {
                const auto on = static_cast<int64_t>(note.start * rate), off = static_cast<int64_t>((note.start + note.length) * rate);
                if (on >= pos && on < pos + n)
                    events.push_back(sub::ProcessEvent::noteOn(static_cast<int32_t>(on - pos), static_cast<uint8_t>(note.key), 100));
                if (off >= pos && off < pos + n)
                    events.push_back(sub::ProcessEvent::noteOff(static_cast<int32_t>(off - pos), static_cast<uint8_t>(note.key)));
            }
            std::stable_sort(events.begin(), events.end(),
                             [](const auto& a, const auto& b) { return a.sampleOffset < b.sampleOffset; });
            ctx.inEvents = {events.data(), events.size()};
            for (int c = 0; c < 2; ++c) {
                const auto& source = input.channels[std::min<size_t>(c, input.channels.size() - 1)];
                for (int i = 0; i < n; ++i) {
                    const int64_t at = pos + i;
                    buffers[c][i] = at >= 0 && at < inFrames ? source[static_cast<size_t>(at)] : 0.f;
                }
            }
            ctx.samplePos = std::max<int64_t>(0, pos + prerollFrames);
            device->process(ctx, channels, 2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    if (pos + i >= 0) output.channels[c][static_cast<size_t>(pos + i)] = buffers[c][i];
            pos += n;
            if (pos <= 0 || ((pos / n) % 64) == 0) device->idle();
        }
        device->resetOffline(false);
        writeWav(files[1], output);
        if (!displaysPath.empty()) {
            // every display's values (from the silence before the input on), a column each
            std::ofstream out(displaysPath);
            const auto infos = device->displays();
            std::vector<std::vector<float>> columns(infos.size());
            size_t rows = 0;
            for (size_t d = 0; d < infos.size(); ++d) {
                device->readDisplay(static_cast<int>(d), 0, columns[d]);
                rows = std::max(rows, columns[d].size());
                out << (d ? "," : "") << infos[d].id << "/" << infos[d].samplesPerValue;
            }
            out << "\n";
            for (size_t r = 0; r < rows; ++r) {
                for (size_t d = 0; d < columns.size(); ++d) out << (d ? "," : "") << (r < columns[d].size() ? columns[d][r] : 0.f);
                out << "\n";
            }
        }
        std::printf("latency %d samples\n", device->latencySamples());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "plugin_render: %s\n", e.what());
        return 1;
    }
}
