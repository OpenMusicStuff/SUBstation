// What the built-in Vital costs against Vital's own VST3 plug-in: the same
// notes held through each, alone (no engine, no graph), in blocks as the audio
// thread gives them, its time against how long the audio plays.
//
//     vital_bench [--vst3 PATH] [--seconds 10] [--block 256] [--rate 48000]
//         [--notes N] [--set PARAM=VALUE ...] [--json out.json] [--profile out.txt]
//
// Without --notes or --set, a set of scenarios at Vital's init patch: silence,
// one note, chords, unison, oversampling. --set changes a parameter by the
// built-in's id and plain value (as Vital stores it); the plug-in gets the same
// value through its parameter of the same name. The load is the best of three
// runs, each on a new instance; 100% would be a whole core.
//
// --profile (Windows): instead, plays the scenario (8 notes without --notes or
// --set) through each once while sampling the thread's instruction pointer about
// every 50 us, and writes the samples, a line each: "builtin|vst3 module offset"
// (offsets from the module's base). vital_profile_report.py makes them a report
// (for the built-in, with addr2line on a build with -g).
//
// Qt-free: it links the engine alone.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Json.h"
#include "Processor.h"
#include "builtin/BuiltinRegistry.h"
#include "plugins/Vst3Format.h"
#include "rt/RtUtils.h"

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <fstream>
#include <thread>
#endif

namespace {

using sub::bench::Json;
using Clock = std::chrono::steady_clock;
using Settings = std::vector<std::pair<std::string, float>>;

struct Scenario {
    std::string name;
    int notes = 1;
    Settings settings;
};

struct Options {
    std::string vst3 = "C:/Program Files/Common Files/VST3/Vital.vst3";
    double seconds = 10.0;
    int block = 256;
    double rate = 48000.0;
    int notes = -1;
    Settings settings;
    std::string json;
    std::string profile;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--vst3") {
            o.vst3 = next();
        } else if (arg == "--seconds") {
            o.seconds = std::stod(next());
        } else if (arg == "--block") {
            o.block = std::clamp(std::stoi(next()), 1, 4096);
        } else if (arg == "--rate") {
            o.rate = std::stod(next());
        } else if (arg == "--notes") {
            o.notes = std::clamp(std::stoi(next()), 0, 64);
        } else if (arg == "--set") {
            const std::string setting = next();
            const auto at = setting.find('=');
            if (at == std::string::npos) throw std::runtime_error("--set takes PARAM=VALUE");
            o.settings.emplace_back(setting.substr(0, at), std::stof(setting.substr(at + 1)));
        } else if (arg == "--json") {
            o.json = next();
        } else if (arg == "--profile") {
            o.profile = next();
        } else {
            throw std::runtime_error("unknown argument " + arg);
        }
    }
    return o;
}

std::vector<Scenario> scenarios(const Options& o) {
    if (o.notes >= 0 || !o.settings.empty()) return {{"custom", std::max(0, o.notes < 0 ? 1 : o.notes), o.settings}};
    return {
        {"silence (no notes)", 0, {}},
        {"1 note", 1, {}},
        {"4-note chord", 4, {}},
        {"8-note chord", 8, {}},
        {"8 notes, unison 8 (osc 1)", 8, {{"osc_1_unison_voices", 8.f}}},
        {"8 notes, unison 16 (osc 1)", 8, {{"osc_1_unison_voices", 16.f}}},
        {"8 notes, 3 oscs unison 8", 8,
         {{"osc_2_on", 1.f}, {"osc_3_on", 1.f}, {"osc_1_unison_voices", 8.f}, {"osc_2_unison_voices", 8.f},
          {"osc_3_unison_voices", 8.f}}},
        {"8 notes, oversampling 4x", 8, {{"oversampling", 2.f}}},
        {"16 notes, poly 16", 16, {{"polyphony", 16.f}}},
    };
}

int findParam(const sub::Processor& p, const std::string& by, bool byName) {
    const auto& params = p.params();
    for (size_t i = 0; i < params.size(); ++i)
        if ((byName ? params[i].name : params[i].id) == by) return static_cast<int>(i);
    return -1;
}

// Sets `settings` (built-in ids, plain values) on `device`: by id on the built-in,
// by the built-in's name for that id on the plug-in, as the same fraction of its range.
void apply(sub::Processor& device, const sub::Processor& builtin, const Settings& settings, bool isPlugin,
           bool report) {
    for (const auto& [id, value] : settings) {
        const int b = findParam(builtin, id, false);
        if (b < 0) throw std::runtime_error("the built-in has no parameter " + id);
        if (!isPlugin) {
            device.setParam(b, value);
            continue;
        }
        const sub::ParamInfo& info = builtin.params()[static_cast<size_t>(b)];
        const int v = findParam(device, info.name, true);
        if (v < 0) throw std::runtime_error("the plug-in has no parameter named \"" + info.name + "\"");
        const sub::ParamInfo& theirs = device.params()[static_cast<size_t>(v)];
        const float fraction = (value - info.minValue) / (info.maxValue - info.minValue);
        const float plain = theirs.minValue + fraction * (theirs.maxValue - theirs.minValue);
        device.setParam(v, plain);
        if (report) {
            std::printf("    %s = %s (built-in: %s)\n", info.name.c_str(),
                        device.paramText(v, device.getParam(v)).c_str(), builtin.paramText(b, value).c_str());
        }
    }
}

// Seconds of processing for `frames` frames, `notes` held from the start, in blocks; `rmsDb`: the
// level of what it played (left), to see that both play alike.
double run(sub::Processor& device, int notes, size_t frames, int block, double rate, double* rmsDb = nullptr) {
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    float* channels[2] = {l.data(), r.data()};
    std::vector<sub::ProcessEvent> ons;
    static const uint8_t kKeys[] = {48, 52, 55, 59, 60, 64, 67, 71, 72, 76, 79, 83, 84, 88, 91, 95,
                                    36, 40, 43, 47, 62, 65, 69, 74, 77, 81, 86, 89, 93, 96, 98, 100};
    for (int n = 0; n < notes; ++n) {
        sub::ProcessEvent on = sub::ProcessEvent::noteOn(0, kKeys[n % 32], 100);
        on.noteId = n + 1;
        ons.push_back(on);
    }
    sub::ProcessContext ctx;
    ctx.sampleRate = rate;
    ctx.playing = true;
    sub::ScopedNoDenormals noDenormals;
    double busy = 0.0, energy = 0.0;
    for (size_t at = 0; at + static_cast<size_t>(block) <= frames; at += static_cast<size_t>(block)) {
        std::fill(l.begin(), l.end(), 0.f);
        std::fill(r.begin(), r.end(), 0.f);
        ctx.samplePos = static_cast<int64_t>(at);
        ctx.beatPos = static_cast<double>(at) / ctx.samplesPerBeat();
        ctx.inEvents = at == 0 ? sub::EventList{ons.data(), ons.size()} : sub::EventList{};
        const auto start = Clock::now();
        device.process(ctx, channels, 2, block);
        busy += std::chrono::duration<double>(Clock::now() - start).count();
        for (float x : l) energy += static_cast<double>(x) * x;
    }
    if (rmsDb) *rmsDb = 10.0 * std::log10(std::max(energy / static_cast<double>(frames), 1e-20));
    return busy;
}

#ifdef _WIN32
// Samples a thread's instruction pointer from a thread of its own until stopped.
class Sampler {
public:
    explicit Sampler(HANDLE target) : target_(target), thread_([this] { loop(); }) {}
    ~Sampler() { stop(); }
    std::vector<DWORD64> stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        return std::move(samples_);
    }

private:
    void loop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        LARGE_INTEGER frequency, now, next;
        QueryPerformanceFrequency(&frequency);
        const LONGLONG interval = frequency.QuadPart / 20000;  // 50 us
        QueryPerformanceCounter(&next);
        while (running_) {
            if (SuspendThread(target_) != static_cast<DWORD>(-1)) {
                CONTEXT context = {};
                context.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(target_, &context)) samples_.push_back(context.Rip);
                ResumeThread(target_);
            }
            next.QuadPart += interval;
            do QueryPerformanceCounter(&now);
            while (now.QuadPart < next.QuadPart && running_);
        }
    }
    HANDLE target_;
    std::atomic<bool> running_{true};
    std::vector<DWORD64> samples_;
    std::thread thread_;
};

// Each sample as "label module offset".
void writeSamples(std::ofstream& out, const char* label, const std::vector<DWORD64>& samples) {
    for (DWORD64 rip : samples) {
        HMODULE module = nullptr;
        char path[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(rip), &module)) {
            GetModuleFileNameA(module, path, MAX_PATH);
        }
        const auto base = reinterpret_cast<DWORD64>(module);
        out << label << ' ' << std::filesystem::path(path).filename().string() << " 0x" << std::hex << (rip - base)
            << std::dec << '\n';
    }
}
#endif

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        if (!std::filesystem::exists(o.vst3)) throw std::runtime_error("no plug-in at " + o.vst3);
        auto& format = sub::vst3::Vst3Format::instance();
        const auto found = format.scanFile(o.vst3);
        const auto it = std::find_if(found.begin(), found.end(), [](const auto& d) { return d.isInstrument; });
        if (it == found.end()) throw std::runtime_error("no instrument in " + o.vst3);
        const sub::PluginDescription plugin = *it;
        std::printf("plug-in: %s %s (%s), block %d, %.0f Hz, %.0f s per run, best of 3\n\n", plugin.name.c_str(),
                    plugin.version.c_str(), plugin.vendor.c_str(), o.block, o.rate, o.seconds);

        auto& registry = sub::BuiltinRegistry::instance();
        const std::shared_ptr<sub::Processor> reference = registry.create("vital");
        const auto frames = static_cast<size_t>(o.seconds * o.rate);

#ifdef _WIN32
        if (!o.profile.empty()) {
            Scenario s = scenarios(o).size() == 1 ? scenarios(o).front() : Scenario{"8-note chord", 8, {}};
            std::ofstream out(o.profile);
            if (!out) throw std::runtime_error("couldn't write " + o.profile);
            HANDLE self = nullptr;
            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0);
            for (int which = 0; which < 2; ++which) {
                std::shared_ptr<sub::Processor> device;
                if (which == 0) {
                    device = registry.create("vital");
                    apply(*device, *reference, s.settings, false, false);
                    device->prepare(o.rate, o.block);
                    device->idle();
                } else {
                    device = format.instantiate(plugin.path, plugin.uid, o.rate, o.block);
                    apply(*device, *reference, s.settings, true, false);
                    device->idle();
                    run(*device, 0, static_cast<size_t>(o.block), o.block, o.rate);
                }
                Sampler sampler(self);
                const double busy = run(*device, s.notes, frames, o.block, o.rate);
                const auto samples = sampler.stop();
                writeSamples(out, which == 0 ? "builtin" : "vst3", samples);
                std::printf("%s: %s, %.2f%% load, %zu samples\n", which == 0 ? "built-in" : "VST3", s.name.c_str(),
                            100.0 * busy / o.seconds, samples.size());
            }
            CloseHandle(self);
            return 0;
        }
#endif
        std::printf("%-30s %10s %10s %8s   %s\n", "scenario", "built-in", "VST3", "ratio", "level (built-in / VST3)");
        Json results = Json::array();
        for (const Scenario& s : scenarios(o)) {
            if (!s.settings.empty()) std::printf("  %s:\n", s.name.c_str());
            double best[2] = {1e300, 1e300}, level[2] = {0.0, 0.0};
            for (int attempt = 0; attempt < 3; ++attempt) {
                for (int which = 0; which < 2; ++which) {
                    std::shared_ptr<sub::Processor> device;
                    if (which == 0) {
                        device = registry.create("vital");
                        apply(*device, *reference, s.settings, false, false);
                        device->prepare(o.rate, o.block);
                    } else {
                        device = format.instantiate(plugin.path, plugin.uid, o.rate, o.block);
                        apply(*device, *reference, s.settings, true, attempt == 0);
                        device->idle();
                        // (A plug-in takes its parameters in its next process call: a block of silence first.)
                        run(*device, 0, static_cast<size_t>(o.block), o.block, o.rate);
                    }
                    if (which == 0) device->idle();  // (an oversampling change: the engine rebuilt now)
                    best[which] = std::min(best[which], run(*device, s.notes, frames, o.block, o.rate, &level[which]));
                }
            }
            const double ours = 100.0 * best[0] / o.seconds, theirs = 100.0 * best[1] / o.seconds;
            std::printf("%-30s %9.2f%% %9.2f%% %7.2fx   %6.1f / %6.1f dB\n", s.name.c_str(), ours, theirs,
                        theirs / std::max(ours, 1e-9), level[0], level[1]);
            results.push(Json::object()
                             .set("scenario", s.name)
                             .set("notes", s.notes)
                             .set("builtin_load_percent", ours)
                             .set("vst3_load_percent", theirs));
        }
        std::printf("\nratio: the plug-in's load over the built-in's (above 1: the built-in is cheaper)\n");
        if (!o.json.empty()) {
            const Json report = Json::object()
                                    .set("plugin", plugin.name + " " + plugin.version)
                                    .set("seconds", o.seconds)
                                    .set("block", o.block)
                                    .set("rate", o.rate)
                                    .set("results", results);
            if (!report.save(o.json, 2)) throw std::runtime_error("couldn't write " + o.json);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "vital_bench: %s\n", e.what());
        return 1;
    }
}
