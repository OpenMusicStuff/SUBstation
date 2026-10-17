#include "platform/Cpu.h"

#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace sub::platform {

namespace {

constexpr const char* kUnsupported =
    "SUBstation needs a processor with AVX2 (Intel Haswell from 2013, AMD Zen from 2017, or later).\n"
    "This computer's processor doesn't have it, so SUBstation can't run on it.";

#if defined(__x86_64__) || defined(_M_X64)

struct Registers {
    unsigned a = 0, b = 0, c = 0, d = 0;
};

Registers cpuid(unsigned leaf, unsigned subleaf) {
    Registers r;
#if defined(_MSC_VER)
    int out[4] = {};
    __cpuidex(out, static_cast<int>(leaf), static_cast<int>(subleaf));
    r = {static_cast<unsigned>(out[0]), static_cast<unsigned>(out[1]), static_cast<unsigned>(out[2]),
         static_cast<unsigned>(out[3])};
#else
    __cpuid_count(leaf, subleaf, r.a, r.b, r.c, r.d);
#endif
    return r;
}

// The register state the system saves (XCR0).
unsigned long long xgetbv0() {
#if defined(_MSC_VER)
    return _xgetbv(0);
#else
    unsigned low = 0, high = 0;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
    return (static_cast<unsigned long long>(high) << 32) | low;
#endif
}

#endif

}  // namespace

bool cpuRunsEngine() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    if (cpuid(0, 0).a < 7) return false;
    const Registers one = cpuid(1, 0);
    const bool fma = (one.c >> 12) & 1u, osxsave = (one.c >> 27) & 1u, avx = (one.c >> 28) & 1u;
    if (!fma || !osxsave || !avx) return false;
    if ((xgetbv0() & 0x6u) != 0x6u) return false;  // the system saves the XMM and YMM registers
    return (cpuid(7, 0).b >> 5) & 1u;               // AVX2
#else
    return true;
#endif
}

void reportUnsupportedCpu() noexcept {
    std::fprintf(stderr, "%s\n", kUnsupported);
#if defined(_WIN32)
    MessageBoxA(nullptr, kUnsupported, "SUBstation", MB_OK | MB_ICONERROR);
#endif
}

}  // namespace sub::platform
