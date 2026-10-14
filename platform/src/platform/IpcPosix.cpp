// Ipc.h elsewhere than Windows: a Unix socket pair for the channel (the child
// gets its end as a file descriptor), posix_spawn, shm_open; on Linux futexes
// on the shared counters and the parent-death signal, elsewhere a polled wait.

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

#ifdef __linux__
#include <linux/futex.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

#include "platform/Ipc.h"

extern char** environ;

namespace sub::platform {

namespace {

constexpr const char* kChannelPrefix = "--channel=";

std::runtime_error systemError(const std::string& what) {
    return std::runtime_error(what + ": " + std::strerror(errno));
}

void closeOnExec(int fd) { ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC); }

// One end of a socket pair. A peer that went never raises SIGPIPE here: the
// write just fails.
class SocketChannel final : public Channel {
public:
    explicit SocketChannel(int fd) : fd_(fd) {
#ifdef SO_NOSIGPIPE
        const int on = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    }
    ~SocketChannel() override { ::close(fd_); }

    bool write(const void* data, size_t size) override {
        const auto* bytes = static_cast<const char*>(data);
        while (size > 0) {
#ifdef MSG_NOSIGNAL
            const ssize_t n = ::send(fd_, bytes, size, MSG_NOSIGNAL);
#else
            const ssize_t n = ::send(fd_, bytes, size, 0);
#endif
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            bytes += n;
            size -= static_cast<size_t>(n);
        }
        return true;
    }

    size_t read(void* data, size_t size) override {
        for (;;) {
            const ssize_t n = ::recv(fd_, data, size, 0);
            if (n < 0 && errno == EINTR) continue;
            return n > 0 ? static_cast<size_t>(n) : 0;
        }
    }

    // (Both ways: a read blocked on another thread returns 0 at once.)
    void close() override { ::shutdown(fd_, SHUT_RDWR); }

private:
    int fd_;
};

class PosixChild final : public ChildProcess {
public:
    PosixChild(pid_t pid, int fd) : pid_(pid), channel_(fd) {}
    ~PosixChild() override {
        channel_.close();
        if (!wait(0)) {
            kill();
            wait(-1);
        }
    }

    Channel& channel() override { return channel_; }
    int pid() const override { return static_cast<int>(pid_); }
    bool running() override { return !reaped(false); }
    void kill() override {
        if (!ended_) ::kill(pid_, SIGKILL);
    }
    bool wait(int milliseconds) override {
        if (milliseconds < 0) return reaped(true);
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        for (;;) {
            if (reaped(false)) return true;
            if (std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    void allowForeground() override {}

private:
    // Whether it has ended (and was reaped), waiting for it if `block`.
    bool reaped(bool block) {
        if (ended_) return true;
        int status = 0;
        pid_t r;
        do {
            r = ::waitpid(pid_, &status, block ? 0 : WNOHANG);
        } while (r < 0 && errno == EINTR);
        if (r == pid_ || (r < 0 && errno == ECHILD)) ended_ = true;
        return ended_;
    }

    pid_t pid_;
    SocketChannel channel_;
    bool ended_ = false;
};

class PosixSharedMemory final : public SharedMemory {
public:
    PosixSharedMemory(std::string name, void* data, size_t size, bool owner)
        : name_(std::move(name)), data_(data), size_(size), owner_(owner) {}
    ~PosixSharedMemory() override {
        ::munmap(data_, size_);
        forgetName();
    }
    void* data() const override { return data_; }
    size_t size() const override { return size_; }
    void forgetName() override {
        if (owner_) ::shm_unlink(name_.c_str());
        owner_ = false;
    }

private:
    std::string name_;
    void* data_;
    size_t size_;
    bool owner_;
};

std::string shmName(const std::string& name) { return "/" + name; }

void* mapShared(int fd, size_t size) {
    void* data = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return data == MAP_FAILED ? nullptr : data;
}

#ifdef __linux__
long futex(std::atomic<uint32_t>& word, int op, uint32_t value, const timespec* timeout) {
    // (Not FUTEX_PRIVATE_FLAG: the word is shared with another process.)
    return ::syscall(SYS_futex, reinterpret_cast<uint32_t*>(&word), op, value, timeout, nullptr, 0);
}
#endif

class PosixSignal final : public IpcSignal {
public:
    explicit PosixSignal(std::atomic<uint32_t>& word) : word_(word) {}

    void notify() noexcept override {
#ifdef __linux__
        futex(word_, FUTEX_WAKE, INT_MAX, nullptr);
#endif
    }

    bool wait(uint32_t seen, int64_t timeoutMicroseconds) noexcept override {
        if (word_.load(std::memory_order_acquire) != seen) return true;
#ifdef __linux__
        timespec timeout{};
        if (timeoutMicroseconds >= 0) {
            timeout.tv_sec = static_cast<time_t>(timeoutMicroseconds / 1000000);
            timeout.tv_nsec = static_cast<long>((timeoutMicroseconds % 1000000) * 1000);
        }
        futex(word_, FUTEX_WAIT, seen, timeoutMicroseconds >= 0 ? &timeout : nullptr);
#else
        // No futex here: a short sleep at a time. (A wake-up comes up to 50 µs late.)
        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(timeoutMicroseconds);
        while (word_.load(std::memory_order_acquire) == seen) {
            if (timeoutMicroseconds >= 0 && std::chrono::steady_clock::now() >= until) break;
            timespec nap{0, 50000};
            ::nanosleep(&nap, nullptr);
        }
#endif
        return word_.load(std::memory_order_acquire) != seen;
    }

private:
    std::atomic<uint32_t>& word_;
};

}  // namespace

int currentProcessId() { return static_cast<int>(::getpid()); }

std::unique_ptr<ChildProcess> ChildProcess::start(const std::string& program, const std::vector<std::string>& arguments) {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) throw systemError("socketpair");
    closeOnExec(fds[0]);
    closeOnExec(fds[1]);
    // The child's end is its descriptor 3 (dup2 clears close-on-exec there); stdin is /dev/null.
    constexpr int kChildFd = 3;
    std::vector<std::string> args{program};
    args.insert(args.end(), arguments.begin(), arguments.end());
    args.push_back(kChannelPrefix + std::to_string(kChildFd));
    std::vector<char*> argv;
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], kChildFd);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    pid_t pid = 0;
    const int error = ::posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);
    if (error != 0) {
        ::close(fds[0]);
        errno = error;
        throw systemError("Could not start " + program);
    }
    return std::make_unique<PosixChild>(pid, fds[0]);
}

bool isChannelArgument(const std::string& argument) { return argument.rfind(kChannelPrefix, 0) == 0; }

std::unique_ptr<Channel> parentChannel(const std::string& argument) {
    if (!isChannelArgument(argument)) return nullptr;
    char* end = nullptr;
    const long fd = std::strtol(argument.c_str() + std::strlen(kChannelPrefix), &end, 10);
    if (end == nullptr || *end != '\0' || fd < 0) return nullptr;
    closeOnExec(static_cast<int>(fd));  // (nothing the child starts in turn gets it)
    return std::make_unique<SocketChannel>(static_cast<int>(fd));
}

void childProcessStarted(int parentPid) {
#ifdef __linux__
    ::prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
    // The parent may have gone before that was set (we're now someone else's child).
    if (parentPid > 0 && ::getppid() != parentPid) std::_Exit(0);
}

std::unique_ptr<SharedMemory> SharedMemory::create(const std::string& name, size_t size) {
    const std::string path = shmName(name);
    int fd = ::shm_open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0 && errno == EEXIST) {  // a process of ours that died left it: ours now
        ::shm_unlink(path.c_str());
        fd = ::shm_open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    }
    if (fd < 0) throw systemError("shm_open " + name);
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const int error = errno;
        ::close(fd);
        ::shm_unlink(path.c_str());
        errno = error;
        throw systemError("ftruncate " + name);
    }
    void* data = mapShared(fd, size);
    ::close(fd);
    if (data == nullptr) {
        ::shm_unlink(path.c_str());
        throw systemError("mmap " + name);
    }
    return std::make_unique<PosixSharedMemory>(path, data, size, true);  // (ftruncate zeroed it)
}

std::unique_ptr<SharedMemory> SharedMemory::open(const std::string& name, size_t size) {
    const std::string path = shmName(name);
    const int fd = ::shm_open(path.c_str(), O_RDWR, 0600);
    if (fd < 0) throw systemError("shm_open " + name);
    struct stat st {};
    if (::fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < size) {
        ::close(fd);
        throw std::runtime_error("Shared memory " + name + " is smaller than expected");
    }
    void* data = mapShared(fd, size);
    ::close(fd);
    if (data == nullptr) throw systemError("mmap " + name);
    return std::make_unique<PosixSharedMemory>(path, data, size, false);
}

std::string uniqueIpcName(const std::string& what) {
    static std::atomic<uint32_t> counter{0};
    // (Short: macOS takes 31 characters at most.)
    return "sub" + std::to_string(::getpid()) + "-" + std::to_string(++counter) + what;
}

std::unique_ptr<IpcSignal> IpcSignal::create(const std::string& /*name*/, std::atomic<uint32_t>& word) {
    return std::make_unique<PosixSignal>(word);
}

std::unique_ptr<IpcSignal> IpcSignal::open(const std::string& /*name*/, std::atomic<uint32_t>& word) {
    return std::make_unique<PosixSignal>(word);
}

}  // namespace sub::platform
