// The platform layer (platform/src/platform): what every layer needs from the
// operating system, tested on whichever this is.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sched.h>
#endif

#include "harness/Test.h"
#include "platform/Bytes.h"
#include "platform/Cpu.h"
#include "platform/Files.h"
#include "platform/Paths.h"
#include "platform/Threads.h"
#include "platform/Unicode.h"

using namespace sub::platform;

namespace {

std::string utf8(uint32_t c) {
    std::string out;
    appendUtf8(out, c);
    return out;
}

}  // namespace

TEST_CASE("the processor check agrees with the compiler's") {
    // (The engine and these tests are built for AVX2: on a machine that runs them, it's there.)
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    CHECK_EQ(sub::platform::cpuRunsEngine(), __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"));
#endif
    CHECK(sub::platform::cpuRunsEngine());
}

TEST_CASE("WTF-8 and UTF-16 both ways, unpaired surrogates too") {
    const std::string text = "Kick " + utf8(0xE9) + utf8(0x3A3) + utf8(0x1F941);  // é Σ 🥁
    const std::wstring wide = toWide(text);
    CHECK_EQ(wide.size(), size_t(9));  // the drum as a surrogate pair
    CHECK_EQ(fromWide(wide), text);
    // A lone surrogate (Windows file names may hold one) survives the trip as three bytes.
    const std::string lone = "a" + utf8(0xD800) + "b";
    CHECK_EQ(lone.size(), size_t(5));
    CHECK_EQ(toWide(lone), std::wstring({L'a', static_cast<wchar_t>(0xD800), L'b'}));
    CHECK_EQ(fromWide(toWide(lone)), lone);
    // Bytes that make no code point read as U+FFFD, one at a time.
    CHECK_EQ(toWide("\xff" "a"), std::wstring({static_cast<wchar_t>(0xFFFD), L'a'}));
    CHECK(isAscii("Kick.wav"));
    CHECK(!isAscii(text));
}

TEST_CASE("a name is appended with one separator, none after a drive alone on Windows") {
    std::string path = "folder";
    appendName(path, std::string_view("a.wav"));
    CHECK_EQ(path, std::string("folder") + kSeparator + "a.wav");
    std::string separated = "folder/";
    appendName(separated, std::string_view("a.wav"));
    CHECK_EQ(separated, std::string("folder/a.wav"));
    std::string drive = "C:";
    appendName(drive, std::string_view("a.wav"));
    CHECK_EQ(drive, kSeparator == '\\' ? std::string("C:a.wav") : std::string("C:/a.wav"));
    static_assert(isSeparator('/'));
    static_assert(isSeparator('\\') == (kSeparator == '\\'));
}

TEST_CASE("paths compare as the file system does") {
    const std::string accented = "a/" + utf8(0xE9) + ".wav";
    CHECK_EQ(fromPath(toPath(accented)), accented);
    CHECK_EQ(fileKey("a/./b/../c.wav"), fileKey("a/c.wav"));
    if (kCaseSensitivePaths) {
        CHECK_EQ(pathKey("/Samples/Kick.WAV"), std::string("/Samples/Kick.WAV"));
        CHECK_EQ(nameKey("Kick.WAV"), std::string("Kick.WAV"));
        CHECK(fileKey("a/Kick.wav") != fileKey("a/kick.wav"));
    } else {
        CHECK_EQ(pathKey("C:/Samples/Kick.WAV"), std::string("c:\\samples\\kick.wav"));
        CHECK_EQ(nameKey("K/I.WAV"), std::string("k/i.wav"));  // (a name keeps its slashes)
        CHECK_EQ(pathKey("D:/" + utf8(0xC0) + utf8(0x3A3)), "d:\\" + utf8(0xE0) + utf8(0x3C3));
        CHECK_EQ(fileKey("a/Kick.wav"), fileKey("A\\KICK.WAV"));
    }
}

TEST_CASE("binary fields are little-endian and read back; a short read sticks") {
    ByteWriter w;
    w.raw("AB", 2);
    w.u8(7);
    w.u32(0x01020304u);
    w.u64(0x1122334455667788ull);
    w.f32(1.5f);
    w.str("name");
    CHECK_EQ(w.bytes.substr(3, 4), std::string("\x04\x03\x02\x01", 4));
    ByteReader r(w.bytes);
    char magic[2];
    CHECK(r.raw(magic, 2));
    CHECK_EQ(r.u8(), uint8_t(7));
    CHECK_EQ(r.u32(), 0x01020304u);
    CHECK_EQ(r.u64(), 0x1122334455667788ull);
    CHECK_EQ(r.f32(), 1.5f);
    CHECK_EQ(r.str(), std::string("name"));
    CHECK(r.ok());
    CHECK(r.atEnd());
    CHECK_EQ(r.u32(), 0u);  // past the end: zero, and not ok from now on
    CHECK(!r.ok());
    CHECK_EQ(r.u8(), uint8_t(0));
    CHECK(!r.ok());
    // FNV-1a's published value for "a".
    CHECK_EQ(fnv1a("a"), 0xaf63dc4c8601ec8cull);
    CHECK_EQ(fnv1a("b", fnv1a("a")), fnv1a("ab"));
}

TEST_CASE("a file is written whole, its folder made, and read back") {
    std::string folder = fromPath(subtest::tempDir());
    appendName(folder, std::string_view("f" + utf8(0xE9)));
    std::string file = folder;
    appendName(file, std::string_view("store.bin"));
    CHECK(!readFile(file));
    CHECK(!stamp(file));
    const std::string bytes("one\0two", 7);
    REQUIRE(writeFileAtomically(file, bytes));
    CHECK_EQ(readFile(file).value_or(""), bytes);
    const auto asVector = readFile<std::vector<char>>(file);
    REQUIRE(asVector);
    CHECK_EQ(std::string(asVector->begin(), asVector->end()), bytes);
    REQUIRE(stamp(file));
    CHECK_EQ(stamp(file)->size, uint64_t(7));
    CHECK(!stamp(folder));  // a folder has no stamp
    REQUIRE(writeFileAtomically(file, "again"));
    CHECK_EQ(readFile(file).value_or(""), std::string("again"));
    CHECK(!readFile(file + ".tmp"));  // moved over the file, not left beside it
}

TEST_CASE("a file's or folder's last write in nanoseconds since 1970, the fraction of a second kept") {
    std::string folder = fromPath(subtest::tempDir());
    appendName(folder, std::string_view("m" + utf8(0xE9)));
    std::string file = folder;
    appendName(file, std::string_view("plugin.dll"));
    CHECK(!modifiedTime(file));
    REQUIRE(writeFileAtomically(file, "code"));
    CHECK(modifiedTime(folder));  // a folder has one too (a plug-in bundle without its binary)
    // 2026-10-16 12:34:56.7891234 UTC (a whole number of Windows' 100 ns).
    const int64_t when = 1'792'154'096'789'123'400;
    const std::chrono::sys_time<std::chrono::nanoseconds> at{std::chrono::nanoseconds(when)};
#ifdef _MSC_VER
    const auto written = std::chrono::clock_cast<std::chrono::file_clock>(at);
#else
    const auto written = std::chrono::file_clock::from_sys(at);
#endif
    std::filesystem::last_write_time(toPath(file), written);
#ifdef __MINGW32__
    // (MinGW's std::filesystem sets whole seconds, as it reads them: what modifiedTime() is for.)
    CHECK_EQ(modifiedTime(file).value_or(0), when / 1'000'000'000 * 1'000'000'000);
#else
    CHECK_EQ(modifiedTime(file).value_or(0), when);
#endif
}

TEST_CASE("real-time priority while the object lives, where the system allows, and as before after") {
    // On a thread of its own: the test's thread keeps its priority whatever happens.
    // (CHECKs only in it: a REQUIRE's exception would end the program there.)
    std::thread([] {
#ifdef __linux__
        // (The kernel's: glibc's pthread_getschedparam() misses what rtkit sets.)
        const auto policyNow = [] { return sched_getscheduler(0) & ~SCHED_RESET_ON_FORK; };
        const auto priorityNow = [] {
            sched_param param{};
            sched_getparam(0, &param);
            return param.sched_priority;
        };
        const int policyBefore = policyNow();
        const int priorityBefore = priorityNow();
#endif
        {
            const ScopedRealtimePriority realtime;
#ifdef __linux__
            // Raised means SCHED_FIFO (set directly) or SCHED_RR (rtkit's); refused, as it was.
            const int policy = policyNow();
            CHECK_EQ(policy == SCHED_FIFO || policy == SCHED_RR, realtime.active());
            if (realtime.active()) CHECK(priorityNow() > 0 && priorityNow() <= 70);
#endif
            std::printf("  real-time priority: %s\n", realtime.active() ? "granted" : "refused");
        }
#ifdef __linux__
        CHECK_EQ(policyNow(), policyBefore);
        CHECK_EQ(priorityNow(), priorityBefore);
#endif
    }).join();
}

TEST_CASE("an empty workgroup is joined by nobody; elsewhere than macOS every one is empty") {
    const AudioWorkgroup none;
    CHECK(none.empty());
    CHECK(AudioWorkgroup(nullptr).empty());
    const ScopedWorkgroupMembership member(none);
    CHECK(!member.joined());
    CHECK(!ScopedWorkgroupMembership().joined());
#ifndef __APPLE__
    int notAWorkgroup = 0;  // never dereferenced: there are none to hold
    const AudioWorkgroup other(&notAWorkgroup);
    CHECK(other.empty());
    CHECK(other == none);
    const ScopedWorkgroupMembership otherMember(other);
    CHECK(!otherMember.joined());
#endif
}
