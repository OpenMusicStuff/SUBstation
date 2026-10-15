#pragma once
// A PipeWire daemon of the test's own (Linux, engines built with PipeWire): the
// `pipewire` program run with a configuration of its own, in a folder of its
// own, which the engine's PipeWire backend connects to (PIPEWIRE_RUNTIME_DIR)
// instead of the user's. No session manager: the backend links its ports
// itself. Its devices, each a null node (what is played is dropped) that
// drives the graph:
//
//   "Test Duplex"  test-duplex  Audio/Duplex, FL FR: what is played to it comes
//                               back on its capture ports (a loopback); the default
//   "Test Sink"    test-sink    Audio/Sink, FL FR FC LFE
//
// at 48000 Hz (44100 allowed), a quantum of 256 (32 to 2048). Where the
// program isn't installed it doesn't run (running(), and why not: tests skip).
// pw-cli and pw-metadata (pipewire-bin) change it while it runs. Needs nothing
// of a test harness: the engine's tests and the application's use it.

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace subtest {

class PipeWireDaemon {
public:
    PipeWireDaemon() {
        dir_ = std::filesystem::temp_directory_path() /
               ("substation-pipewire-" + std::to_string(::getpid()) + "-" + std::to_string(++count()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        const std::filesystem::path config = dir_ / "pipewire.conf";
        std::ofstream(config) << kConfig;
        ::setenv("PIPEWIRE_RUNTIME_DIR", dir_.c_str(), 1);
        ::setenv("PIPEWIRE_REMOTE", "pipewire-0", 1);
        start();
    }

    // Whether it runs; if not, why.
    bool running() const { return pid_ != 0; }
    const std::string& whyNot() const { return whyNot_; }

    ~PipeWireDaemon() {
        stop();
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
    }

    PipeWireDaemon(const PipeWireDaemon&) = delete;
    PipeWireDaemon& operator=(const PipeWireDaemon&) = delete;

    // Ends the daemon (as if it crashed or the user's session ended).
    void stop() {
        if (pid_ == 0) return;
        ::kill(pid_, SIGTERM);
        int status = 0;
        ::waitpid(pid_, &status, 0);
        pid_ = 0;
    }

    // Freezes the daemon for a while (its devices' clocks run on).
    void freeze(std::chrono::milliseconds duration) {
        ::kill(pid_, SIGSTOP);
        std::this_thread::sleep_for(duration);
        ::kill(pid_, SIGCONT);
    }

    // Runs a PipeWire tool (pw-cli, pw-metadata) against this daemon: whether it succeeded.
    static bool run(const std::string& command) {
        return std::system(("timeout 5 " + command + " > /dev/null 2>&1").c_str()) == 0;
    }

    // Makes `node` (its node.name) the default output, as a session manager would.
    static bool setDefaultSink(const std::string& node) {
        return run("pw-metadata -n default 0 default.audio.sink '{\"name\":\"" + node + "\"}' Spa:String:JSON");
    }

    // Takes a device away, as if it were unplugged.
    static bool destroyNode(const std::string& node) { return run("pw-cli destroy " + node); }

private:
    void start() {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        const std::string config = (dir_ / "pipewire.conf").string();
        std::string program = "pipewire", flag = "-c", path = config;
        char* argv[] = {program.data(), flag.data(), path.data(), nullptr};
        const int error = posix_spawnp(&pid_, "pipewire", &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        if (error != 0) {
            pid_ = 0;
            whyNot_ = "the pipewire program isn't installed";
            return;
        }
        // Its socket listening (it shows before then: bind() makes the file, listen() comes after, and
        // until then a client is refused: "PipeWire isn't running").
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!accepts((dir_ / "pipewire-0").string())) {
            int status = 0;
            if (::waitpid(pid_, &status, WNOHANG) == pid_ || std::chrono::steady_clock::now() > deadline) {
                stop();
                whyNot_ = "pipewire didn't start (are its SPA plug-ins installed?)";
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Whether a client connecting to the socket at `path` is let in.
    static bool accepts(const std::string& path) {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (path.size() >= sizeof address.sun_path) return false;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;
        const bool connected = ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0;
        ::close(fd);
        return connected;
    }

    static int& count() {
        static int n = 0;
        return n;
    }

    static constexpr const char* kConfig = R"(
context.properties = {
    core.daemon = true
    core.name = pipewire-0
    support.dbus = false
    module.x11.bell = false
    default.clock.rate = 48000
    default.clock.allowed-rates = [ 44100 48000 ]
    default.clock.quantum = 256
    default.clock.min-quantum = 32
    default.clock.max-quantum = 2048
}
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.*       = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-access }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-spa-node-factory }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-link-factory }
]
context.objects = [
    { factory = adapter
        args = { factory.name = support.null-audio-sink node.name = test-duplex node.description = "Test Duplex"
                 media.class = Audio/Duplex audio.position = "FL,FR"
                 adapter.auto-port-config = { mode = dsp monitor = true position = preserve } } }
    { factory = adapter
        args = { factory.name = support.null-audio-sink node.name = test-sink node.description = "Test Sink"
                 media.class = Audio/Sink audio.position = "FL,FR,FC,LFE"
                 adapter.auto-port-config = { mode = dsp monitor = true position = preserve } } }
    { factory = metadata
        args = { metadata.name = default
                 metadata.values = [ { key = default.audio.sink value = { name = test-duplex } } ] } }
]
)";

    std::filesystem::path dir_;
    pid_t pid_ = 0;
    std::string whyNot_;
};

}  // namespace subtest
