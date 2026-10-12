// substation-plugin-render: runs audio through a VST3 effect offline, with the
// engine's own VST3 host, for studying how a plug-in sounds (its impulse
// response, its decay, its modulation) when building one of our own like it.
//
//   substation-plugin-render <bundle.vst3 | builtin:id> --list
//       Prints the plug-in's parameters: index, name, range, default, choices,
//       and for continuous ones the plug-in's own text across the range.
//   substation-plugin-render <bundle.vst3> --jobs <file> [--rate 48000] [--warmup 2]
//       Renders each job in the file, one per line (blank lines and lines
//       starting with # are skipped):
//           <output.f32>|<input.f32>|<Param>=<value>;<Param>=<value>...
//       Audio is raw 32-bit float, stereo interleaved, at --rate. The output is
//       as long as the input (pad the input with silence for the tail). Values
//       are as the engine presents them: 0..1 for continuous parameters, the
//       step index or the choice's name for stepped ones. Each job starts from
//       an emptied plug-in (deactivated and activated again) at its defaults,
//       then sets its parameters and runs --warmup seconds of silence (so
//       parameter smoothing settles).
//
// A built-in device (builtin:vintage_hall) renders the same way, to compare with.
//
// Links the engine (sub_engine) and nothing of Qt.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Processor.h"
#include "builtin/BuiltinRegistry.h"
#include "plugins/Vst3Format.h"
#include "rt/RtUtils.h"

namespace {

constexpr int kBlock = 512;

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, separator)) parts.push_back(part);
    return parts;
}

std::shared_ptr<sub::Processor> load(const std::string& bundle, double rate) {
    if (bundle.rfind("builtin:", 0) == 0) {  // a built-in device, by its id
        auto device = sub::BuiltinRegistry::instance().create(bundle.substr(8));
        device->prepare(rate, kBlock);
        return device;
    }
    auto& format = sub::vst3::Vst3Format::instance();
    for (const auto& description : format.scanFile(bundle)) {
        if (!description.isInstrument) return format.instantiate(bundle, description.uid, rate, kBlock);
    }
    throw std::runtime_error("no effect in " + bundle);
}

int findParam(const sub::Processor& processor, const std::string& name) {
    const auto& params = processor.params();
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].name == name || params[i].id == name) return static_cast<int>(i);
    }
    throw std::runtime_error("no parameter \"" + name + "\"");
}

float parseValue(const sub::ParamInfo& info, const std::string& text) {
    for (size_t i = 0; i < info.valueLabels.size(); ++i) {
        if (info.valueLabels[i] == text) return static_cast<float>(i);
    }
    return std::stof(text);
}

void list(sub::Processor& processor) {
    const auto& params = processor.params();
    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        const int index = static_cast<int>(i);
        std::cout << index << "\t" << p.name << "\tid=" << p.id << "\tunit=" << p.unit << "\trange=" << p.minValue
                  << ".." << p.maxValue << "\tdefault=" << p.defaultValue << " ("
                  << processor.paramText(index, p.defaultValue) << ")";
        if (p.hidden) std::cout << "\thidden";
        if (p.readOnly) std::cout << "\treadonly";
        std::cout << "\n";
        if (!p.valueLabels.empty()) {
            for (size_t l = 0; l < p.valueLabels.size(); ++l) std::cout << "\t\t" << l << " = " << p.valueLabels[l] << "\n";
        } else if (p.stepCount() > 0) {
            for (int s = 0; s <= p.stepCount(); ++s) {
                const float value = p.minValue + static_cast<float>(s);
                std::cout << "\t\t" << value << " = " << processor.paramText(index, value) << "\n";
            }
        } else {
            // Some plug-ins name only their current value, whatever value is asked
            // about: set each one first. Runs of one text print once (choices
            // that present themselves as continuous show where each starts).
            std::string previous;
            for (int s = 0; s <= 200; ++s) {
                const float value = p.minValue + (p.maxValue - p.minValue) * static_cast<float>(s) / 200.f;
                processor.setParam(index, value);
                const std::string text = processor.paramText(index, value);
                if (text != previous || s % 10 == 0) std::cout << "\t\t" << value << " = " << text << "\n";
                previous = text;
            }
            processor.setParam(index, p.defaultValue);
        }
    }
}

std::vector<float> readRaw(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot read " + path);
    const auto bytes = static_cast<size_t>(file.tellg());
    std::vector<float> samples(bytes / sizeof(float));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(samples.size() * sizeof(float)));
    return samples;
}

void writeRaw(const std::string& path, const std::vector<float>& samples) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write " + path);
    file.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(samples.size() * sizeof(float)));
}

// Runs `frames` frames of interleaved stereo through the processor (`in` null: silence), into `out` if given.
void run(sub::Processor& processor, double rate, int64_t& position, const float* in, float* out, int64_t frames) {
    sub::ScopedNoDenormals noDenormals;  // as the engine renders
    std::vector<float> left(kBlock), right(kBlock);
    float* channels[2] = {left.data(), right.data()};
    for (int64_t done = 0; done < frames; done += kBlock) {
        const int n = static_cast<int>(std::min<int64_t>(kBlock, frames - done));
        for (int f = 0; f < n; ++f) {
            left[f] = in ? in[(done + f) * 2] : 0.f;
            right[f] = in ? in[(done + f) * 2 + 1] : 0.f;
        }
        sub::ProcessContext ctx;
        ctx.sampleRate = rate;
        ctx.samplePos = position;
        ctx.beatPos = static_cast<double>(position) / ctx.samplesPerBeat();
        ctx.playing = true;
        ctx.offline = true;
        processor.process(ctx, channels, 2, n);
        position += n;
        if (out) {
            for (int f = 0; f < n; ++f) {
                out[(done + f) * 2] = left[f];
                out[(done + f) * 2 + 1] = right[f];
            }
        }
    }
}

void runJobs(sub::Processor& processor, const std::string& jobsPath, double rate, double warmup) {
    std::ifstream jobs(jobsPath);
    if (!jobs) throw std::runtime_error("cannot read " + jobsPath);
    const auto& params = processor.params();
    std::string line;
    int64_t position = 0;
    while (std::getline(jobs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split(line, '|');
        if (fields.size() < 2) throw std::runtime_error("bad job: " + line);
        processor.resetOffline();  // deactivate and activate: empties its buffers (a plug-in's)
        processor.reset();         // (a built-in device's)
        for (size_t i = 0; i < params.size(); ++i) {
            if (!params[i].readOnly) processor.setParam(static_cast<int>(i), params[i].defaultValue);
        }
        if (fields.size() > 2) {
            for (const auto& assignment : split(fields[2], ';')) {
                if (trim(assignment).empty()) continue;
                const auto equals = assignment.find('=');
                if (equals == std::string::npos) throw std::runtime_error("bad parameter: " + assignment);
                const int index = findParam(processor, trim(assignment.substr(0, equals)));
                processor.setParam(index, parseValue(params[index], trim(assignment.substr(equals + 1))));
            }
        }
        run(processor, rate, position, nullptr, nullptr, static_cast<int64_t>(warmup * rate));
        processor.idle();
        const auto input = readRaw(trim(fields[1]));
        std::vector<float> output(input.size());
        run(processor, rate, position, input.data(), output.data(), static_cast<int64_t>(input.size() / 2));
        writeRaw(trim(fields[0]), output);
        std::cout << "rendered " << trim(fields[0]) << std::endl;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: substation-plugin-render <bundle.vst3> --list | --jobs <file> [--rate 48000] [--warmup 2]\n";
        return 2;
    }
    try {
        const std::string bundle = argv[1];
        std::string jobs;
        bool listing = false;
        double rate = 48000.0, warmup = 2.0;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--list") listing = true;
            else if (arg == "--jobs" && i + 1 < argc) jobs = argv[++i];
            else if (arg == "--rate" && i + 1 < argc) rate = std::stod(argv[++i]);
            else if (arg == "--warmup" && i + 1 < argc) warmup = std::stod(argv[++i]);
            else throw std::runtime_error("unknown argument " + arg);
        }
        auto processor = load(bundle, rate);
        if (listing) list(*processor);
        if (!jobs.empty()) runJobs(*processor, jobs, rate, warmup);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
