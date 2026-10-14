// project_bench PROJECT.gilproj [--modes within,together] [--buffer 512] [--rate 48000]
//               [--seconds 20] [--start BEAT] [--threads N] [--json out.json]
//
// A real project played live, as the application plays it, in each plug-in
// hosting mode (docs/engine/sandboxes.md): the project opened through the
// engine bridge (no UI), its plug-ins loaded where the mode says, then played
// through the fake ASIO driver (tests/asio_driver) at the pace a real device
// asks for buffers, on a thread of its own at real-time priority, while the
// main thread runs the bridge's timers. Reported per mode: the callback's time
// (what the application's CPU meter shows: time / buffer length), and the CPU
// time used meanwhile by this process and by the sandboxes' (in % of one core).
// Windows only (the fake ASIO driver).

#include "BridgeTestSupport.h"
#include "harness/AsioDriver.h"
#include "io/Serialization.h"
#include "Json.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <array>
#include <map>


#include <windows.h>
#include <timeapi.h>
#include <tlhelp32.h>
#include <intrin.h>

namespace subtest {
void fail(const char* file, int line, const std::string& message) {
    std::fprintf(stderr, "%s:%d: %s\n", std::filesystem::path(file).filename().string().c_str(), line, message.c_str());
}
}  // namespace subtest

namespace {

using sub::app::PluginHostingMode;
using sub::bench::Json;
using Clock = std::chrono::steady_clock;

struct Options {
    std::string project;
    std::vector<std::string> modes{"within", "together"};
    int buffer = 512;
    int rate = 48000;
    double seconds = 20.0;
    double start = 0.0;
    int threads = 0;
    std::string json;
};

PluginHostingMode modeNamed(const std::string& name) {
    if (name == "within") return PluginHostingMode::Within;
    if (name == "together") return PluginHostingMode::Together;
    if (name == "bymanufacturer") return PluginHostingMode::ByManufacturer;
    if (name == "byplugin") return PluginHostingMode::ByPlugin;
    if (name == "individually") return PluginHostingMode::Individually;
    throw std::runtime_error("no hosting mode " + name);
}

double fileTimeSeconds(const FILETIME& t) {
    return static_cast<double>((static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-7;
}

// The time-stamp counter's rate (the cycle times below count it), measured.
double tscHz() {
    static const double hz = [] {
        const auto t0 = Clock::now();
        const uint64_t c0 = __rdtsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const uint64_t c1 = __rdtsc();
        return static_cast<double>(c1 - c0) / std::chrono::duration<double>(Clock::now() - t0).count();
    }();
    return hz;
}

// The CPU time a process has used, in seconds (-1: gone), from its cycles: GetProcessTimes'
// times are charged a clock tick at a time, and miss most of a thread that runs in short bursts.
double processCpu(HANDLE process) {
    ULONG64 cycles = 0;
    if (!QueryProcessCycleTime(process, &cycles)) return -1.0;
    return static_cast<double>(cycles) / tscHz();
}

void pump(int ms) {
    const auto until = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// The plug-ins the project uses, as the scanner would list them (the application's
// index knows them; here the project says where they are).
void collectPlugins(const QJsonValue& value, std::vector<sub::app::PluginInfo>& out) {
    if (value.isArray()) {
        for (const QJsonValue& v : value.toArray()) collectPlugins(v, out);
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key() == QStringLiteral("plugin") && it->isObject()) {
                const QJsonObject p = it->toObject();
                sub::app::PluginInfo info;
                info.name = p.value(QStringLiteral("name")).toString();
                info.format = p.value(QStringLiteral("format")).toString();
                info.path = p.value(QStringLiteral("path")).toString();
                info.uid = p.value(QStringLiteral("uid")).toString();
                info.vendor = p.value(QStringLiteral("vendor")).toString();
                info.instrument = p.value(QStringLiteral("instrument")).toBool();
                if (!info.uid.isEmpty()) out.push_back(info);
            } else {
                collectPlugins(*it, out);
            }
        }
    }
}

// How many of the project's plug-ins run, and why the others don't (the first few).
void countPlugins(sub::app::EngineBridge& bridge, const QString& trackId, const std::vector<sub::app::Device>& devices,
                  int& running, int& other, std::vector<QString>& problems) {
    for (const sub::app::Device& device : devices) {
        if (device.isPlugin()) {
            if (bridge.pluginRunning(trackId, device.id)) {
                ++running;
            } else {
                ++other;
                if (problems.size() < 8) problems.push_back(device.plugin->name + QStringLiteral(": ") + bridge.pluginError(device.id));
            }
        }
        for (const auto& chain : device.chains) countPlugins(bridge, trackId, chain.devices, running, other, problems);
    }
}

// Each thread of process `pid`: its CPU time (s), and its name if it has one.
std::map<DWORD, std::pair<double, std::string>> threadTimes(DWORD pid) {
    std::map<DWORD, std::pair<double, std::string>> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof entry;
    for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry)) {
        if (entry.th32OwnerProcessID != pid) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        ULONG64 cycles = 0;
        if (QueryThreadCycleTime(thread, &cycles)) out[entry.th32ThreadID] = {static_cast<double>(cycles) / tscHz(), std::string()};
        CloseHandle(thread);
    }
    CloseHandle(snap);
    return out;
}

// The threads that used the most CPU between two snapshots, in % of one core.
void printTopThreads(const char* what, const std::map<DWORD, std::pair<double, std::string>>& before,
                     const std::map<DWORD, std::pair<double, std::string>>& after, double wall, int top) {
    std::vector<std::pair<double, std::string>> used;
    double busy = 0.0;
    int count = 0;
    for (const auto& [tid, entry] : after) {
        const auto it = before.find(tid);
        const double delta = entry.first - (it != before.end() ? it->second.first : 0.0);
        if (delta <= 0.0) continue;
        used.push_back({delta / wall * 100.0, std::to_string(tid) + (entry.second.empty() ? "" : " " + entry.second)});
        busy += delta / wall * 100.0;
        ++count;
    }
    std::sort(used.rbegin(), used.rend());
    std::printf("    %s: %d threads used CPU, %.1f%% in all; the most:", what, count, busy);
    for (int i = 0; i < top && i < static_cast<int>(used.size()); ++i) std::printf(" %.1f%%", used[static_cast<size_t>(i)].first);
    std::printf("\n");
}

struct Result {
    std::string mode;
    int sandboxes = 0;
    double loadSeconds = 0.0;
    double meanUs = 0.0, p50Us = 0.0, p99Us = 0.0, maxUs = 0.0;
    double meter = 0.0;  // mean callback time / buffer length
    uint64_t dropouts = 0;
    double selfCpu = 0.0, sandboxCpu = 0.0;  // % of one core
    double audioThreadCpu = 0.0;             // the driver thread's own (% of one core)
};

Result runMode(const Options& options, const std::string& modeName) {
    Result result;
    result.mode = modeName;
    sub::app::test::Studio studio;
    sub::Engine& engine = studio.engine;
    sub::app::EngineBridge& bridge = *studio.bridge;
    bridge.setPluginHostProgram(QStringLiteral(SUBSTATION_PLUGIN_HOST));
    bridge.setPluginHosting(modeNamed(modeName));
    if (options.threads > 0) engine.setAudioThreads(options.threads);

    subtest::AsioDriver driver;
    driver.setManual(true);
    subtest::openAsio(engine, static_cast<uint32_t>(options.rate), static_cast<uint32_t>(options.buffer));

    {
        QFile file(QString::fromStdString(options.project));
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("can't read " + options.project);
        std::vector<sub::app::PluginInfo> plugins;
        collectPlugins(QJsonDocument::fromJson(file.readAll()).object(), plugins);
        bridge.setKnownPlugins(plugins);
        std::printf("  %s: %zu plug-in devices\n", modeName.c_str(), plugins.size());
    }
    const auto loadStart = Clock::now();
    sub::app::loadProject(studio.project, QString::fromStdString(options.project));
    std::set<QString> sources;
    for (const auto* list : {&studio.project.tracks(), &studio.project.returns()}) {
        for (const auto& track : *list) {
            for (const auto& clip : track.clips) {
                if (clip.isAudio() && !clip.path.isEmpty()) sources.insert(clip.path);
            }
        }
    }
    for (;;) {
        pump(50);
        const bool decoding = std::any_of(sources.begin(), sources.end(), [&](const QString& p) { return bridge.isLoading(p); });
        if (bridge.pluginsPending() == 0 && bridge.devicesReady() && !decoding) break;
        if (Clock::now() - loadStart > std::chrono::minutes(5)) throw std::runtime_error("the project didn't load in 5 minutes");
    }
    result.loadSeconds = std::chrono::duration<double>(Clock::now() - loadStart).count();
    {
        int running = 0, other = 0;
        std::vector<QString> problems;
        countPlugins(bridge, sub::app::kMaster, studio.project.master().devices, running, other, problems);
        for (const auto* list : {&studio.project.tracks(), &studio.project.returns()}) {
            for (const auto& track : *list) countPlugins(bridge, track.id, track.devices, running, other, problems);
        }
        std::printf("  %s: %d plug-ins running, %d not\n", modeName.c_str(), running, other);
        for (const QString& p : problems) std::printf("    %s\n", p.toLocal8Bit().constData());
    }
    const QList<int> pids = bridge.sandboxProcessIds();
    result.sandboxes = static_cast<int>(pids.size());
    std::printf("  %s: loaded in %.1f s, %d sandbox(es)\n", modeName.c_str(), result.loadSeconds, result.sandboxes);
    std::fflush(stdout);
    pump(2000);  // (settled: plug-ins' first idles, editors' threads)

    std::vector<HANDLE> hosts;
    for (const int pid : pids) {
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid))) hosts.push_back(h);
    }
    const auto sandboxCpu = [&] {
        double sum = 0.0;
        for (HANDLE h : hosts) sum += std::max(0.0, processCpu(h));
        return sum;
    };

    engine.setPositionBeats(options.start);
    engine.play();
    const double period = static_cast<double>(options.buffer) / options.rate;
    const int warmup = static_cast<int>(2.0 / period);
    const int count = std::max(1, static_cast<int>(options.seconds / period));
    std::vector<double> times;
    times.reserve(static_cast<size_t>(count));
    std::atomic<bool> done{false};
    double selfStart = 0, selfEnd = 0, hostStart = 0, hostEnd = 0, threadCpu = 0;
    Clock::time_point measureStart, measureEnd;
    uint64_t dropoutsStart = 0;
    double meterSum = 0.0;
    std::map<DWORD, std::pair<double, std::string>> selfThreadsStart, selfThreadsEnd;
    std::vector<std::map<DWORD, std::pair<double, std::string>>> hostThreadsStart, hostThreadsEnd;

    std::thread audio([&] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        auto next = Clock::now();
        const auto step = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(period));
        ULONG64 cycles0 = 0, cycles1 = 0;
        for (int i = 0; i < warmup + count; ++i) {
            if (i == warmup) {
                selfThreadsStart = threadTimes(GetCurrentProcessId());
                for (const int pid : pids) hostThreadsStart.push_back(threadTimes(static_cast<DWORD>(pid)));
                measureStart = Clock::now();
                selfStart = processCpu(GetCurrentProcess());
                hostStart = sandboxCpu();
                dropoutsStart = engine.deviceStatus().dropouts;
                QueryThreadCycleTime(GetCurrentThread(), &cycles0);
            }
            const auto started = Clock::now();
            driver.process(1);
            if (i >= warmup) {
                times.push_back(std::chrono::duration<double>(Clock::now() - started).count());
                meterSum += engine.cpuLoad();
            }
            if (i % 256 == 0) driver.clearOutput();
            next += step;
            const auto left = next - Clock::now();
            if (left > Clock::duration::zero()) {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(std::chrono::duration_cast<std::chrono::nanoseconds>(left).count() / 100);
                SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject(timer, INFINITE);
            } else {
                next = Clock::now();  // (late: the next one is due at once, as a device would have it)
            }
        }
        measureEnd = Clock::now();
        selfEnd = processCpu(GetCurrentProcess());
        hostEnd = sandboxCpu();
        selfThreadsEnd = threadTimes(GetCurrentProcessId());
        for (const int pid : pids) hostThreadsEnd.push_back(threadTimes(static_cast<DWORD>(pid)));
        QueryThreadCycleTime(GetCurrentThread(), &cycles1);
        threadCpu = static_cast<double>(cycles1 - cycles0) / tscHz();
        CloseHandle(timer);
        done = true;
    });
    while (!done) pump(20);
    audio.join();
    result.dropouts = engine.deviceStatus().dropouts - dropoutsStart;
    engine.stop();
    driver.process(2);
    for (HANDLE h : hosts) CloseHandle(h);

    const double wall = std::chrono::duration<double>(measureEnd - measureStart).count();
    printTopThreads("engine process", selfThreadsStart, selfThreadsEnd, wall, 12);
    for (size_t i = 0; i < hostThreadsStart.size() && i < hostThreadsEnd.size() && i < 4; ++i) {
        printTopThreads("a sandbox", hostThreadsStart[i], hostThreadsEnd[i], wall, 8);
    }
    std::vector<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    const auto at = [&](double q) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(q * sorted.size()))] * 1e6; };
    result.meanUs = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size() * 1e6;
    result.p50Us = at(0.5);
    result.p99Us = at(0.99);
    result.maxUs = sorted.back() * 1e6;
    result.meter = meterSum / static_cast<double>(times.size());
    result.selfCpu = (selfEnd - selfStart) / wall * 100.0;
    result.sandboxCpu = (hostEnd - hostStart) / wall * 100.0;
    result.audioThreadCpu = threadCpu / wall * 100.0;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("SUBstation Bench"));
    QCoreApplication::setApplicationName(QStringLiteral("SUBstation Bench"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Options options;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
                return argv[++i];
            };
            if (arg == "--modes") {
                options.modes.clear();
                std::string list = value();
                for (size_t from = 0; from <= list.size();) {
                    const size_t comma = std::min(list.find(',', from), list.size());
                    options.modes.push_back(list.substr(from, comma - from));
                    from = comma + 1;
                }
            } else if (arg == "--buffer") options.buffer = std::stoi(value());
            else if (arg == "--rate") options.rate = std::stoi(value());
            else if (arg == "--seconds") options.seconds = std::stod(value());
            else if (arg == "--start") options.start = std::stod(value());
            else if (arg == "--threads") options.threads = std::stoi(value());
            else if (arg == "--json") options.json = value();
            else if (options.project.empty() && arg[0] != '-') options.project = arg;
            else throw std::runtime_error("not understood: " + arg);
        }
        if (options.project.empty()) throw std::runtime_error("usage: project_bench PROJECT.gilproj [--modes within,together] ...");
        qputenv("SUBSTATION_PLUGIN_HOSTING", "");
        timeBeginPeriod(1);
        std::printf("%s at %d Hz, %d frames, %g s per mode\n", options.project.c_str(), options.rate, options.buffer, options.seconds);
        std::vector<Result> results;
        for (const std::string& mode : options.modes) results.push_back(runMode(options, mode));
        const double budgetUs = 1e6 * options.buffer / options.rate;
        std::printf("\n%-15s %9s %9s %9s %9s %7s %8s %9s %9s %9s %7s\n", "mode", "mean", "p50", "p99", "max", "meter",
                    "dropouts", "audio thr", "engine", "sandboxes", "total");
        Json rows = Json::array();
        for (const Result& r : results) {
            std::printf("%-15s %7.0fus %7.0fus %7.0fus %7.0fus %6.1f%% %8llu %8.1f%% %8.1f%% %8.1f%% %6.1f%%\n", r.mode.c_str(),
                        r.meanUs, r.p50Us, r.p99Us, r.maxUs, r.meanUs / budgetUs * 100.0,
                        static_cast<unsigned long long>(r.dropouts), r.audioThreadCpu, r.selfCpu, r.sandboxCpu,
                        r.selfCpu + r.sandboxCpu);
            rows.push(Json::object()
                          .set("mode", r.mode)
                          .set("sandboxes", r.sandboxes)
                          .set("mean_us", r.meanUs)
                          .set("p99_us", r.p99Us)
                          .set("max_us", r.maxUs)
                          .set("engine_meter", r.meter)
                          .set("engine_cpu_pct", r.selfCpu)
                          .set("sandbox_cpu_pct", r.sandboxCpu));
        }
        if (!options.json.empty()) {
            Json report = Json::object();
            report.set("results", rows);
            report.save(options.json, 2);
        }
        std::fflush(stdout);
        std::_Exit(0);  // (plug-ins' own threads needn't be waited for)
    } catch (const std::exception& error) {
        std::fprintf(stderr, "project_bench: %s\n", error.what());
        return 1;
    }
}
