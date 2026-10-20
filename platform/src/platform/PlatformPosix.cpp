// The platform layer elsewhere than Windows: POSIX calls; on Linux per-thread
// priorities and rtkit, on macOS Mach's thread policies and audio workgroups.

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#ifdef __linux__
#include <dlfcn.h>
#include <sys/syscall.h>

#include <mutex>
#include <type_traits>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <os/workgroup.h>
#endif

#include "platform/Files.h"
#include "platform/Paths.h"
#include "platform/StreamFile.h"
#include "platform/Threads.h"

namespace sub::platform {

// --- Paths.h ---------------------------------------------------------------------------

std::string nameKey(std::string_view name) { return std::string(name); }

std::string pathKey(std::string_view path) { return std::string(path); }

// --- Files.h ---------------------------------------------------------------------------

std::optional<FileStamp> stamp(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return std::nullopt;
#ifdef __APPLE__
    const timespec& time = st.st_mtimespec;
#else
    const timespec& time = st.st_mtim;
#endif
    FileStamp s;
    s.size = static_cast<uint64_t>(st.st_size);
    s.modified = static_cast<uint64_t>(time.tv_sec) * 1'000'000'000u + static_cast<uint64_t>(time.tv_nsec);
    return s;
}

std::optional<int64_t> modifiedTime(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return std::nullopt;
#ifdef __APPLE__
    const timespec& time = st.st_mtimespec;
#else
    const timespec& time = st.st_mtim;
#endif
    return static_cast<int64_t>(time.tv_sec) * 1'000'000'000 + static_cast<int64_t>(time.tv_nsec);
}

bool replaceFile(const std::string& from, const std::string& to) { return std::rename(from.c_str(), to.c_str()) == 0; }

std::FILE* openFile(const std::string& path, bool write) { return std::fopen(path.c_str(), write ? "wb" : "rb"); }

std::string userCacheFolder() {
    const char* home = std::getenv("HOME");
#ifdef __APPLE__
    if (home && *home) return std::string(home) + "/Library/Caches";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg == '/') return xdg;  // (relative: ignored, as the spec says)
    if (home && *home) return std::string(home) + "/.cache";
#endif
    std::error_code ignored;
    return fromPath(std::filesystem::temp_directory_path(ignored));
}

// --- StreamFile.h ----------------------------------------------------------------------

StreamFile::~StreamFile() { close(); }

bool StreamFile::open(const std::string& path) {
    close();
    // Unbuffered first; a file system that refuses it (O_DIRECT on tmpfs), or refuses
    // our alignment (a disk with bigger sectors), is read through the cache instead.
    return openAs(path, true) || openAs(path, false);
}

bool StreamFile::openAs(const std::string& path, bool unbuffered) {
    int flags = O_RDONLY | O_CLOEXEC;
#ifdef __linux__
    if (unbuffered) flags |= O_DIRECT;
#endif
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd, &st) != 0 || S_ISDIR(st.st_mode)) {
        ::close(fd);
        return false;
    }
#ifdef __APPLE__
    if (unbuffered && ::fcntl(fd, F_NOCACHE, 1) != 0) {
        ::close(fd);
        return false;
    }
#endif
    fd_ = fd;
    size_ = static_cast<uint64_t>(st.st_size);
    unbuffered_ = unbuffered;
    if (unbuffered && size_ > 0) {  // a first read says whether it is really taken
        AlignedBuffer probe(kAlignment);
        if (read(0, probe.data(), kAlignment) < 0) {
            close();
            return false;
        }
    }
#ifdef __linux__
    if (!unbuffered) ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    return true;
}

void StreamFile::close() noexcept {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    size_ = 0;
    unbuffered_ = false;
}

bool StreamFile::isOpen() const noexcept { return fd_ >= 0; }

int64_t StreamFile::read(uint64_t offset, void* buffer, size_t bytes) noexcept {
    if (fd_ < 0) return -1;
    size_t done = 0;
    while (done < bytes) {
        const ssize_t n = ::pread(fd_, static_cast<char*>(buffer) + done, bytes - done, static_cast<off_t>(offset + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;  // the end of the file
        done += static_cast<size_t>(n);
        if (unbuffered_ && done % kAlignment != 0) break;  // (a direct read short of a block: the end)
    }
    return static_cast<int64_t>(done);
}

void StreamFile::willNeed(uint64_t offset, uint64_t bytes) noexcept {
    if (fd_ < 0 || bytes == 0) return;
#ifdef __linux__
    if (!unbuffered_) ::posix_fadvise(fd_, static_cast<off_t>(offset), static_cast<off_t>(bytes), POSIX_FADV_WILLNEED);
#elif defined(__APPLE__)  // (F_NOCACHE keeps nothing it reads; what is read ahead is still found)
    radvisory advice{};
    advice.ra_offset = static_cast<off_t>(offset);
    advice.ra_count = static_cast<int>(std::min<uint64_t>(bytes, INT32_MAX));
    ::fcntl(fd_, F_RDADVISE, &advice);
#else
    (void)offset;
#endif
}

// --- Threads.h -------------------------------------------------------------------------

void enterBackgroundMode() {
#ifdef __linux__
    // On Linux the nice value and the I/O priority are per thread.
    const auto thread = static_cast<int>(syscall(SYS_gettid));
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(thread), 19) != 0) {
        // Not allowed: it stays at its priority.
    }
    // The idle I/O class: the disk is read for it only when nothing else wants it.
    constexpr int kWhoProcess = 1, kClassIdle = 3, kClassShift = 13;
    if (syscall(SYS_ioprio_set, kWhoProcess, thread, kClassIdle << kClassShift) != 0) {
        // Not supported: best effort as before.
    }
#endif
}

void raiseNormalPriority() {
#ifdef __linux__
    const auto thread = static_cast<int>(syscall(SYS_gettid));
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(thread), -5) != 0) {
        // Not allowed (it takes CAP_SYS_NICE, or RLIMIT_NICE): it stays at its priority.
    }
    // The best-effort I/O class at its highest level: its reads go before other programs' of the same class.
    constexpr int kWhoProcess = 1, kClassBestEffort = 2, kClassShift = 13;
    if (syscall(SYS_ioprio_set, kWhoProcess, thread, kClassBestEffort << kClassShift) != 0) {
        // Not supported: as before.
    }
#endif
}

#ifdef __linux__
namespace {

// The SCHED_FIFO priority asked for: under the audio servers' own threads
// (PipeWire's data thread asks for 88), which the workers wait on and must not
// preempt; over everything that isn't audio. Lowered to what the user may have.
constexpr int kRealtimePriority = 70;

// rtkit (org.freedesktop.RealtimeKit1) over the system bus, through libdbus,
// loaded when first needed: a build needs no D-Bus headers, and a system without
// libdbus or rtkit only goes without. Its few types as the library lays them out.
class RtKit {
public:
    static RtKit& instance() {
        static RtKit rtkit;
        return rtkit;
    }

    // Asks rtkit for real-time priority (it sets SCHED_RR) at `priority` (lowered to its maximum) for this
    // thread; the priority given, or 0 if refused.
    int makeRealtime(int priority) {
        const std::lock_guard lock(mutex_);  // workers start together; one request at a time
        if (!loaded_ || unresponsive_) return 0;
        Connection bus(*this);
        if (!bus) return 0;
        if (!limitsRead_) {
            maxPriority_ = static_cast<int>(property(bus, "MaxRealtimePriority", kInt32));
            if (!unresponsive_) rtTimeMax_ = property(bus, "RTTimeUSecMax", kInt64);
            limitsRead_ = true;
        }
        priority = std::min(priority, maxPriority_);
        if (priority <= 0 || rtTimeMax_ <= 0) return 0;
        // rtkit only raises a process whose real-time threads must block now and
        // then: RLIMIT_RTTIME, at most its maximum (200 ms by default; a thread
        // that runs longer without blocking ends the process). The workers sleep
        // between runs, and at least every few milliseconds (Scheduler.cpp).
        rlimit limit{};
        if (getrlimit(RLIMIT_RTTIME, &limit) != 0) return 0;
        const auto most = static_cast<rlim_t>(rtTimeMax_);
        if (limit.rlim_max == RLIM_INFINITY || limit.rlim_max > most) {
            limit.rlim_cur = limit.rlim_max = most;
            if (setrlimit(RLIMIT_RTTIME, &limit) != 0) return 0;
        }
        const auto thread = static_cast<uint64_t>(syscall(SYS_gettid));
        const auto value = static_cast<uint32_t>(priority);
        Message call(*this, newMethodCall_(kService, kPath, kInterface, "MakeThreadRealtime"));
        if (!call || !appendArgs_(call.get(), kUint64, &thread, kUint32, &value, kInvalid)) return 0;
        Message reply(*this, send(bus, call));
        return reply ? priority : 0;
    }

private:
    // libdbus's types and constants (dbus/dbus-protocol.h, dbus/dbus-errors.h).
    struct Error {
        const char* name;
        const char* message;
        unsigned int flags;
        void* padding;
    };
    struct alignas(void*) Iter {
        unsigned char opaque[128];  // DBusMessageIter is 72 bytes on 64-bit systems
    };
    static constexpr int kInvalid = 0, kInt32 = 'i', kInt64 = 'x', kUint32 = 'u', kUint64 = 't', kString = 's',
                         kVariant = 'v';
    static constexpr int kSystemBus = 1;
    // rtkit answers within milliseconds (polkit's check included). A scheduler
    // waits for its workers' requests, one at a time, while live audio is held:
    // after one goes unanswered rtkit isn't asked again (`unresponsive_`).
    static constexpr int kTimeoutMs = 500;
    static constexpr const char* kService = "org.freedesktop.RealtimeKit1";
    static constexpr const char* kPath = "/org/freedesktop/RealtimeKit1";
    static constexpr const char* kInterface = "org.freedesktop.RealtimeKit1";

    // A connection of its own (never libdbus's shared one, which Qt may use too).
    class Connection {
    public:
        explicit Connection(RtKit& kit) : kit_(kit) {
            Error error{};
            kit_.errorInit_(&error);
            connection_ = kit_.busGetPrivate_(kSystemBus, &error);
            kit_.errorFree_(&error);
            if (connection_) kit_.setExitOnDisconnect_(connection_, 0);  // (libdbus would end the process)
        }
        ~Connection() {
            if (!connection_) return;
            kit_.connectionClose_(connection_);
            kit_.connectionUnref_(connection_);
        }
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        explicit operator bool() const { return connection_ != nullptr; }
        void* get() const { return connection_; }

    private:
        RtKit& kit_;
        void* connection_ = nullptr;
    };

    class Message {
    public:
        Message(RtKit& kit, void* message) : kit_(kit), message_(message) {}
        ~Message() {
            if (message_) kit_.messageUnref_(message_);
        }
        Message(const Message&) = delete;
        Message& operator=(const Message&) = delete;
        explicit operator bool() const { return message_ != nullptr; }
        void* get() const { return message_; }

    private:
        RtKit& kit_;
        void* message_ = nullptr;
    };

    RtKit() {
        library_ = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!library_) return;
        const auto load = [this](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library_, name));
            return function != nullptr;
        };
        loaded_ = load(threadsInitDefault_, "dbus_threads_init_default") && load(errorInit_, "dbus_error_init") &&
                  load(errorFree_, "dbus_error_free") && load(busGetPrivate_, "dbus_bus_get_private") &&
                  load(setExitOnDisconnect_, "dbus_connection_set_exit_on_disconnect") &&
                  load(connectionClose_, "dbus_connection_close") &&
                  load(connectionUnref_, "dbus_connection_unref") &&
                  load(newMethodCall_, "dbus_message_new_method_call") &&
                  load(appendArgs_, "dbus_message_append_args") && load(messageUnref_, "dbus_message_unref") &&
                  load(sendWithReply_, "dbus_connection_send_with_reply_and_block") &&
                  load(iterInit_, "dbus_message_iter_init") && load(iterArgType_, "dbus_message_iter_get_arg_type") &&
                  load(iterRecurse_, "dbus_message_iter_recurse") && load(iterBasic_, "dbus_message_iter_get_basic");
        if (loaded_) threadsInitDefault_();
    }
    // (Never unloaded: libdbus keeps state for the process's life.)

    // The reply to `call`, or null (an error, or no rtkit on the bus).
    void* send(const Connection& bus, const Message& call) {
        Error error{};
        errorInit_(&error);
        void* reply = sendWithReply_(bus.get(), call.get(), kTimeoutMs, &error);
        if (!reply && error.name &&
            (std::strcmp(error.name, "org.freedesktop.DBus.Error.NoReply") == 0 ||
             std::strcmp(error.name, "org.freedesktop.DBus.Error.Timeout") == 0)) {
            unresponsive_ = true;
        }
        errorFree_(&error);
        return reply;
    }

    // One of rtkit's integer properties (`type`: 'i' or 'x'); 0 if it can't be read.
    int64_t property(const Connection& bus, const char* name, int type) {
        Message call(*this, newMethodCall_(kService, kPath, "org.freedesktop.DBus.Properties", "Get"));
        const char* interface = kInterface;
        if (!call || !appendArgs_(call.get(), kString, &interface, kString, &name, kInvalid)) return 0;
        Message reply(*this, send(bus, call));
        if (!reply) return 0;
        Iter iter{}, variant{};
        if (!iterInit_(reply.get(), &iter) || iterArgType_(&iter) != kVariant) return 0;
        iterRecurse_(&iter, &variant);
        if (iterArgType_(&variant) != type) return 0;
        if (type == kInt32) {
            int32_t value = 0;
            iterBasic_(&variant, &value);
            return value;
        }
        int64_t value = 0;
        iterBasic_(&variant, &value);
        return value;
    }

    std::mutex mutex_;
    void* library_ = nullptr;
    bool loaded_ = false;
    bool limitsRead_ = false;
    bool unresponsive_ = false;
    int maxPriority_ = 0;
    int64_t rtTimeMax_ = 0;

    unsigned (*threadsInitDefault_)() = nullptr;
    void (*errorInit_)(Error*) = nullptr;
    void (*errorFree_)(Error*) = nullptr;
    void* (*busGetPrivate_)(int, Error*) = nullptr;
    void (*setExitOnDisconnect_)(void*, unsigned) = nullptr;
    void (*connectionClose_)(void*) = nullptr;
    void (*connectionUnref_)(void*) = nullptr;
    void* (*newMethodCall_)(const char*, const char*, const char*, const char*) = nullptr;
    unsigned (*appendArgs_)(void*, int, ...) = nullptr;
    void (*messageUnref_)(void*) = nullptr;
    void* (*sendWithReply_)(void*, void*, int, Error*) = nullptr;
    unsigned (*iterInit_)(void*, Iter*) = nullptr;
    int (*iterArgType_)(Iter*) = nullptr;
    void (*iterRecurse_)(Iter*, Iter*) = nullptr;
    void (*iterBasic_)(Iter*, void*) = nullptr;
};

// The calling thread's scheduling, as the kernel has it. (On Linux the sched_
// calls are the calling thread's, not the process's; glibc's pthread_ ones keep
// a copy of their own, which misses what rtkit sets from outside.)
int policyNow(int& priority) {
    sched_param param{};
    sched_getparam(0, &param);
    priority = param.sched_priority;
    return sched_getscheduler(0) & ~SCHED_RESET_ON_FORK;
}

bool setPolicy(int policy, int priority) {
    sched_param param{};
    param.sched_priority = priority;
    return sched_setscheduler(0, policy, &param) == 0;
}

// SCHED_FIFO set directly: allowed with CAP_SYS_NICE, or up to RLIMIT_RTPRIO
// (rtprio in /etc/security/limits.conf, often the "audio" group's).
bool setFifo(int priority) {
    if (setPolicy(SCHED_FIFO, priority)) return true;
    rlimit limit{};
    if (getrlimit(RLIMIT_RTPRIO, &limit) != 0 || limit.rlim_cur == 0 || limit.rlim_cur >= static_cast<rlim_t>(priority))
        return false;
    return setPolicy(SCHED_FIFO, static_cast<int>(limit.rlim_cur));
}

}  // namespace
#endif

#ifdef __APPLE__
namespace {

// The time-constraint policy's numbers, as other audio software sets them for
// its audio threads: a 2.9 ms period (128 frames at 44.1 kHz), the thread
// computing for up to 75% of it, done within 85%. Joined to a device's workgroup
// (AudioWorkgroup), the device's deadline schedules the thread as well.
constexpr double kPeriodMs = 2.9;
constexpr double kComputation = 0.75;
constexpr double kConstraint = 0.85;

uint32_t machTicks(double ms) {
    mach_timebase_info_data_t timebase{};
    mach_timebase_info(&timebase);
    return static_cast<uint32_t>(ms * 1'000'000.0 * timebase.denom / timebase.numer);
}

}  // namespace
#endif

ScopedRealtimePriority::ScopedRealtimePriority() noexcept {
#if defined(__linux__)
    policy_ = policyNow(priority_);
    if (policy_ < 0) return;
    if (policy_ == SCHED_FIFO || policy_ == SCHED_RR) {  // real-time already: left as it is
        active_ = true;
        policy_ = -1;  // (nothing to go back to)
        return;
    }
    const int priority = std::min(kRealtimePriority, sched_get_priority_max(SCHED_FIFO));
    active_ = setFifo(priority) || RtKit::instance().makeRealtime(priority) > 0;
#elif defined(__APPLE__)
    thread_time_constraint_policy_data_t policy{};
    policy.period = machTicks(kPeriodMs);
    policy.computation = machTicks(kPeriodMs * kComputation);
    policy.constraint = machTicks(kPeriodMs * kConstraint);
    policy.preemptible = 1;
    active_ = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                                reinterpret_cast<thread_policy_t>(&policy),
                                THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
#endif
}

ScopedRealtimePriority::~ScopedRealtimePriority() {
    if (!active_) return;
#if defined(__linux__)
    if (policy_ < 0) return;
    // Lowering is always allowed, but keeping rtkit's SCHED_RESET_ON_FORK: only
    // a privileged thread may clear it (it only means children start normal).
    setPolicy(policy_ | (sched_getscheduler(0) & SCHED_RESET_ON_FORK), priority_);
#elif defined(__APPLE__)
    thread_standard_policy_data_t standard{};
    thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_STANDARD_POLICY,
                      reinterpret_cast<thread_policy_t>(&standard), THREAD_STANDARD_POLICY_COUNT);
#endif
}

#ifdef __APPLE__

AudioWorkgroup::AudioWorkgroup(void* native) noexcept : native_(native) {
    if (native_) os_retain(native_);
}

AudioWorkgroup::AudioWorkgroup(const AudioWorkgroup& other) noexcept : native_(other.native_) {
    if (native_) os_retain(native_);
}

AudioWorkgroup& AudioWorkgroup::operator=(const AudioWorkgroup& other) noexcept {
    if (other.native_) os_retain(other.native_);
    if (native_) os_release(native_);
    native_ = other.native_;
    return *this;
}

AudioWorkgroup::~AudioWorkgroup() {
    if (native_) os_release(native_);
}

static_assert(sizeof(os_workgroup_join_token_s) <= 64);

ScopedWorkgroupMembership::ScopedWorkgroupMembership(const AudioWorkgroup& workgroup) noexcept
    : workgroup_(workgroup) {
    if (workgroup_.empty()) return;
    if (__builtin_available(macOS 11.0, *)) {
        auto* token = reinterpret_cast<os_workgroup_join_token_t>(token_);
        joined_ = os_workgroup_join(static_cast<os_workgroup_t>(workgroup_.native()), token) == 0;
    }
}

ScopedWorkgroupMembership::~ScopedWorkgroupMembership() {
    if (!joined_) return;
    if (__builtin_available(macOS 11.0, *)) {
        os_workgroup_leave(static_cast<os_workgroup_t>(workgroup_.native()),
                           reinterpret_cast<os_workgroup_join_token_t>(token_));
    }
}

#else

// No workgroups here: every one is empty.
AudioWorkgroup::AudioWorkgroup(void*) noexcept {}
AudioWorkgroup::AudioWorkgroup(const AudioWorkgroup&) noexcept {}
AudioWorkgroup& AudioWorkgroup::operator=(const AudioWorkgroup&) noexcept { return *this; }
AudioWorkgroup::~AudioWorkgroup() = default;

ScopedWorkgroupMembership::ScopedWorkgroupMembership(const AudioWorkgroup&) noexcept {}
ScopedWorkgroupMembership::~ScopedWorkgroupMembership() = default;

#endif

}  // namespace sub::platform
