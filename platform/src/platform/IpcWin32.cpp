// Ipc.h on Windows: two anonymous pipes for the channel (the child inherits its
// ends, and only those), the child in a job that kills it when we go, named
// file mappings for shared memory, and a named auto-reset event beside each
// shared counter.

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "platform/Ipc.h"
#include "platform/Unicode.h"

namespace sub::platform {

namespace {

constexpr const char* kChannelPrefix = "--channel=";

std::runtime_error windowsError(const std::string& what) {
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string message = what + ": " + (text ? fromWide(text) : "error " + std::to_string(code));
    LocalFree(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) message.pop_back();
    return std::runtime_error(message);
}

// Session-local names ("Local\..."), as every process of the user's session sees them.
std::wstring objectName(const std::string& name) { return L"Local\\" + toWide(name); }

// One argument of a command line, quoted as CommandLineToArgvW and the C runtime read it.
std::wstring quoted(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
        else out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

class PipeChannel final : public Channel {
public:
    PipeChannel(HANDLE read, HANDLE write) : read_(read), write_(write) {}
    ~PipeChannel() override {
        close();
        CloseHandle(read_);
    }

    bool write(const void* data, size_t size) override {
        const auto* bytes = static_cast<const char*>(data);
        while (size > 0) {
            if (!write_) return false;
            DWORD written = 0;
            const auto chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
            if (!WriteFile(write_, bytes, chunk, &written, nullptr) || written == 0) return false;
            bytes += written;
            size -= written;
        }
        return true;
    }

    size_t read(void* data, size_t size) override {
        DWORD got = 0;
        const auto chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
        if (!ReadFile(read_, data, chunk, &got, nullptr)) return 0;
        return got;
    }

    void close() override {
        if (write_) {
            CloseHandle(write_);
            write_ = nullptr;
        }
        CancelIoEx(read_, nullptr);  // (a read blocked on another thread, where the system lets it go)
    }

private:
    HANDLE read_;
    HANDLE write_;
};

class WinChild final : public ChildProcess {
public:
    WinChild(HANDLE process, DWORD pid, HANDLE job, HANDLE read, HANDLE write)
        : process_(process), pid_(pid), job_(job), channel_(read, write) {}
    ~WinChild() override {
        channel_.close();
        if (!wait(0)) {
            kill();
            wait(-1);
        }
        CloseHandle(process_);
        if (job_) CloseHandle(job_);
    }

    Channel& channel() override { return channel_; }
    int pid() const override { return static_cast<int>(pid_); }
    bool running() override { return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT; }
    void kill() override { TerminateProcess(process_, 1); }
    bool wait(int milliseconds) override {
        return WaitForSingleObject(process_, milliseconds < 0 ? INFINITE : static_cast<DWORD>(milliseconds)) ==
               WAIT_OBJECT_0;
    }
    void allowForeground() override { AllowSetForegroundWindow(pid_); }

private:
    HANDLE process_;
    DWORD pid_;
    HANDLE job_;
    PipeChannel channel_;
};

class WinSharedMemory final : public SharedMemory {
public:
    WinSharedMemory(HANDLE mapping, void* data, size_t size) : mapping_(mapping), data_(data), size_(size) {}
    ~WinSharedMemory() override {
        UnmapViewOfFile(data_);
        CloseHandle(mapping_);
    }
    void* data() const override { return data_; }
    size_t size() const override { return size_; }
    void forgetName() override {}  // (a mapping's name goes with its last handle)

private:
    HANDLE mapping_;
    void* data_;
    size_t size_;
};

class WinSignal final : public IpcSignal {
public:
    WinSignal(HANDLE event, std::atomic<uint32_t>& word) : event_(event), word_(word) {}
    ~WinSignal() override { CloseHandle(event_); }

    void notify() noexcept override { SetEvent(event_); }

    bool wait(uint32_t seen, int64_t timeoutMicroseconds) noexcept override {
        using Clock = std::chrono::steady_clock;
        const auto until = Clock::now() + std::chrono::microseconds(std::max<int64_t>(0, timeoutMicroseconds));
        // (An auto-reset event may hold a wake-up meant for an earlier change: the counter decides.)
        while (word_.load(std::memory_order_acquire) == seen) {
            DWORD ms = INFINITE;
            if (timeoutMicroseconds >= 0) {
                const auto left = std::chrono::duration_cast<std::chrono::microseconds>(until - Clock::now()).count();
                if (left <= 0) break;
                ms = static_cast<DWORD>((left + 999) / 1000);
            }
            if (WaitForSingleObject(event_, ms) != WAIT_OBJECT_0 && timeoutMicroseconds >= 0 && Clock::now() >= until) {
                break;
            }
        }
        return word_.load(std::memory_order_acquire) != seen;
    }

private:
    HANDLE event_;
    std::atomic<uint32_t>& word_;
};

void* mapView(HANDLE mapping, size_t size) { return MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size); }

}  // namespace

int currentProcessId() { return static_cast<int>(GetCurrentProcessId()); }

std::unique_ptr<ChildProcess> ChildProcess::start(const std::string& program, const std::vector<std::string>& arguments) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE toChildRead = nullptr, toChildWrite = nullptr, fromChildRead = nullptr, fromChildWrite = nullptr;
    if (!CreatePipe(&toChildRead, &toChildWrite, &inherit, 0)) throw windowsError("CreatePipe");
    if (!CreatePipe(&fromChildRead, &fromChildWrite, &inherit, 0)) {
        const auto error = windowsError("CreatePipe");
        CloseHandle(toChildRead);
        CloseHandle(toChildWrite);
        throw error;
    }
    // Our ends stay ours.
    SetHandleInformation(toChildWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(fromChildRead, HANDLE_FLAG_INHERIT, 0);
    const auto closeAll = [&] {
        for (HANDLE h : {toChildRead, toChildWrite, fromChildRead, fromChildWrite}) CloseHandle(h);
    };

    std::wstring commandLine = quoted(toWide(program));
    for (const std::string& arg : arguments) commandLine += L" " + quoted(toWide(arg));
    commandLine += L" " + toWide(kChannelPrefix) + std::to_wstring(reinterpret_cast<uintptr_t>(toChildRead)) + L":" +
                   std::to_wstring(reinterpret_cast<uintptr_t>(fromChildWrite));

    // The child inherits its two ends and nothing else of ours.
    HANDLE inherited[2] = {toChildRead, fromChildWrite};
    SIZE_T attributesSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
    std::vector<char> attributesData(attributesSize);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributesData.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributesSize) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited,
                                   nullptr, nullptr)) {
        const auto error = windowsError("Could not start " + program);
        closeAll();
        throw error;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    const std::wstring path = toWide(program);
    const BOOL started =
        CreateProcessW(path.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                       nullptr, nullptr, &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(attributes);
    if (!started) {
        const auto error = windowsError("Could not start " + program);
        closeAll();
        throw error;
    }
    // Killed when the job's last handle closes: ours, when we go (however we go).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits) ||
            !AssignProcessToJobObject(job, info.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    CloseHandle(toChildRead);
    CloseHandle(fromChildWrite);
    return std::make_unique<WinChild>(info.hProcess, info.dwProcessId, job, fromChildRead, toChildWrite);
}

bool isChannelArgument(const std::string& argument) { return argument.rfind(kChannelPrefix, 0) == 0; }

std::unique_ptr<Channel> parentChannel(const std::string& argument) {
    if (!isChannelArgument(argument)) return nullptr;
    const std::string value = argument.substr(std::strlen(kChannelPrefix));
    const size_t colon = value.find(':');
    if (colon == std::string::npos) return nullptr;
    char* end = nullptr;
    const unsigned long long read = std::strtoull(value.c_str(), &end, 10);
    if (end != value.c_str() + colon) return nullptr;
    const unsigned long long write = std::strtoull(value.c_str() + colon + 1, &end, 10);
    if (*end != '\0') return nullptr;
    const auto readHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(read));
    const auto writeHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(write));
    // (Nothing the child starts in turn gets them.)
    SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(writeHandle, HANDLE_FLAG_INHERIT, 0);
    return std::make_unique<PipeChannel>(readHandle, writeHandle);
}

void childProcessStarted(int /*parentPid*/) {
    // No "program stopped working" or "insert a disk" dialogs if a plug-in misbehaves: the
    // process just ends, and the parent hears of it at once. (The parent's job ends it with the parent.)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}

std::unique_ptr<SharedMemory> SharedMemory::create(const std::string& name, size_t size) {
    const auto big = static_cast<unsigned long long>(size);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(big >> 32),
                                        static_cast<DWORD>(big & 0xFFFFFFFFu), objectName(name).c_str());
    if (!mapping) throw windowsError("CreateFileMapping " + name);
    void* data = mapView(mapping, size);
    if (!data) {
        const auto error = windowsError("MapViewOfFile " + name);
        CloseHandle(mapping);
        throw error;
    }
    std::memset(data, 0, size);  // (a new mapping is zeroed already; one that existed may not be)
    return std::make_unique<WinSharedMemory>(mapping, data, size);
}

std::unique_ptr<SharedMemory> SharedMemory::open(const std::string& name, size_t size) {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, objectName(name).c_str());
    if (!mapping) throw windowsError("OpenFileMapping " + name);
    void* data = mapView(mapping, size);
    if (!data) {
        const auto error = windowsError("MapViewOfFile " + name);
        CloseHandle(mapping);
        throw error;
    }
    return std::make_unique<WinSharedMemory>(mapping, data, size);
}

std::string uniqueIpcName(const std::string& what) {
    static std::atomic<uint32_t> counter{0};
    return "sub" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++counter) + what;
}

std::unique_ptr<IpcSignal> IpcSignal::create(const std::string& name, std::atomic<uint32_t>& word) {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, objectName(name).c_str());
    if (!event) throw windowsError("CreateEvent " + name);
    return std::make_unique<WinSignal>(event, word);
}

std::unique_ptr<IpcSignal> IpcSignal::open(const std::string& name, std::atomic<uint32_t>& word) {
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, objectName(name).c_str());
    if (!event) throw windowsError("OpenEvent " + name);
    return std::make_unique<WinSignal>(event, word);
}

}  // namespace sub::platform
