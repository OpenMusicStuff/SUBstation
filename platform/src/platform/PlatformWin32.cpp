// The platform layer on Windows: the wide (W) calls, with paths in UTF-16.

#include <windows.h>
#include <avrt.h>

#include <algorithm>
#include <string_view>

#include "platform/Files.h"
#include "platform/Paths.h"
#include "platform/StreamFile.h"
#include "platform/Threads.h"

namespace sub::platform {

namespace {

// Windows' own lower case (LCMapStringEx, invariant locale): what
// os.path.normcase() uses, so keys come out as Python made them.
std::wstring lowerCase(std::wstring_view s) {
    if (s.empty()) return {};
    std::wstring lowered(s.size(), L'\0');
    const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(), static_cast<int>(s.size()),
                                lowered.data(), static_cast<int>(lowered.size()), nullptr, nullptr, 0);
    if (n <= 0) return std::wstring(s);
    lowered.resize(static_cast<size_t>(n));
    return lowered;
}

// A key of a name or path: Windows' lower case, and with `separators` '/' as '\'.
std::string key(std::string_view text, bool separators) {
    if (isAscii(text)) {  // (Windows' lower case of ASCII is ASCII's)
        std::string key(text);
        for (char& c : key) {
            if (separators && c == '/') c = '\\';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        }
        return key;
    }
    std::wstring wide = toWide(text);
    if (separators)
        for (wchar_t& c : wide)
            if (c == L'/') c = L'\\';
    return fromWide(lowerCase(wide));
}

}  // namespace

// --- Paths.h ---------------------------------------------------------------------------

std::string nameKey(std::string_view name) { return key(name, false); }

std::string pathKey(std::string_view path) { return key(path, true); }

// --- Files.h ---------------------------------------------------------------------------

std::optional<FileStamp> stamp(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(toWide(path).c_str(), GetFileExInfoStandard, &data)) return std::nullopt;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return std::nullopt;
    FileStamp s;
    s.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    s.modified = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    return s;
}

std::optional<int64_t> modifiedTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(toWide(path).c_str(), GetFileExInfoStandard, &data)) return std::nullopt;
    // 100 ns ticks since 1601.
    const int64_t ticks = static_cast<int64_t>((static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                                               data.ftLastWriteTime.dwLowDateTime);
    constexpr int64_t kTicksTo1970 = 116'444'736'000'000'000;
    return (ticks - kTicksTo1970) * 100;
}

bool replaceFile(const std::string& from, const std::string& to) {
    return MoveFileExW(toWide(from).c_str(), toWide(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::FILE* openFile(const std::string& path, bool write) {
    return _wfopen(toWide(path).c_str(), write ? L"wb" : L"rb");
}

std::string userCacheFolder() {
    wchar_t buffer[MAX_PATH + 1];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH + 1);
    if (n > 0 && n <= MAX_PATH) return fromWide(std::wstring_view(buffer, n));
    const DWORD t = GetTempPathW(MAX_PATH + 1, buffer);
    if (t > 0 && t <= MAX_PATH) return fromWide(std::wstring_view(buffer, t));
    return ".";
}

// --- StreamFile.h ----------------------------------------------------------------------

StreamFile::~StreamFile() { close(); }

bool StreamFile::open(const std::string& path) {
    close();
    // Unbuffered first; a volume that refuses our alignment (sectors bigger than
    // kAlignment) is read through the system's cache instead.
    return openAs(path, true) || openAs(path, false);
}

bool StreamFile::openAs(const std::string& path, bool unbuffered) {
    const DWORD flags = unbuffered ? FILE_FLAG_NO_BUFFERING : FILE_FLAG_SEQUENTIAL_SCAN;
    // Others may go on writing, renaming or deleting it (a cache file made again): our reads fail then.
    const HANDLE file = CreateFileW(toWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, flags, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return false;
    }
    handle_ = file;
    size_ = static_cast<uint64_t>(size.QuadPart);
    unbuffered_ = unbuffered;
    if (unbuffered && size_ > 0) {  // a first read says whether the volume takes our alignment
        AlignedBuffer probe(kAlignment);
        if (read(0, probe.data(), kAlignment) < 0) {
            close();
            return false;
        }
    }
    return true;
}

void StreamFile::close() noexcept {
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
    size_ = 0;
    unbuffered_ = false;
}

bool StreamFile::isOpen() const noexcept { return handle_ != nullptr; }

int64_t StreamFile::read(uint64_t offset, void* buffer, size_t bytes) noexcept {
    if (!handle_) return -1;
    size_t done = 0;
    while (done < bytes) {
        // A synchronous read at an offset of its own: threads reading at once don't move each other.
        OVERLAPPED at{};
        const uint64_t from = offset + done;
        at.Offset = static_cast<DWORD>(from & 0xFFFFFFFFu);
        at.OffsetHigh = static_cast<DWORD>(from >> 32);
        const auto want = static_cast<DWORD>(std::min<size_t>(bytes - done, size_t{1} << 30));
        DWORD n = 0;
        if (!ReadFile(static_cast<HANDLE>(handle_), static_cast<char*>(buffer) + done, want, &n, &at)) {
            if (GetLastError() == ERROR_HANDLE_EOF) break;
            return -1;
        }
        if (n == 0) break;  // the end of the file
        done += n;
        if (n < want) break;
    }
    return static_cast<int64_t>(done);
}

// Windows reads ahead by itself for a buffered file opened for sequential reading; an unbuffered one is ours.
void StreamFile::willNeed(uint64_t, uint64_t) noexcept {}

// --- Threads.h -------------------------------------------------------------------------

void enterBackgroundMode() { SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN); }

void raiseNormalPriority() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); }

ScopedRealtimePriority::ScopedRealtimePriority() noexcept {
    DWORD task = 0;
    handle_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    active_ = handle_ != nullptr;
}

ScopedRealtimePriority::~ScopedRealtimePriority() {
    if (handle_) AvRevertMmThreadCharacteristics(handle_);
}

// No workgroups on Windows (MMCSS's task is the nearest): every one is empty.
AudioWorkgroup::AudioWorkgroup(void*) noexcept {}
AudioWorkgroup::AudioWorkgroup(const AudioWorkgroup&) noexcept {}
AudioWorkgroup& AudioWorkgroup::operator=(const AudioWorkgroup&) noexcept { return *this; }
AudioWorkgroup::~AudioWorkgroup() = default;

ScopedWorkgroupMembership::ScopedWorkgroupMembership(const AudioWorkgroup&) noexcept {}
ScopedWorkgroupMembership::~ScopedWorkgroupMembership() = default;

}  // namespace sub::platform
