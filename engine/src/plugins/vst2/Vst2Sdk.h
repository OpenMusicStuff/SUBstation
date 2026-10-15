#pragma once
// The VST 2.x interface, from Xaymar's clean-room, header-only vst2sdk
// (third_party/vst2sdk, BSD-3-Clause), and what a host needs that it leaves out.
// Include this, never vst.h itself:
//
//  * vst.h defines two tables of feature names (vst_host_supports,
//    vst_effect_supports) as variables in the header, which two files including
//    it would both define: here they are made static to each file.
//  * The VST 3 SDK defines VST_VERSION, which vst.h names an enum: kept aside.
//  * `__cdecl`, its calling convention, is no keyword on GCC or Clang outside
//    Windows (where 64-bit code has one convention anyway).
//  * Its vst_events_t holds a pointer to the events, where plug-ins read the
//    pointers themselves, after the count and a pointer-sized reserved field
//    (`EventList`).
//  * It has no time info (what the host answers `kGetTime` with): `TimeInfo`.
//  * Host opcodes it names only by number, and the feature names a host is
//    asked about.

#include <cstddef>
#include <cstdint>

#if !defined(_MSC_VER) && !defined(__MINGW32__) && !defined(__cdecl)
#define __cdecl
#endif

#pragma push_macro("VST_VERSION")  // (the VST 3 SDK's version macro: vst.h names an enum so)
#undef VST_VERSION
#define vst_host_supports static vst_host_supports [[maybe_unused]]
#define vst_effect_supports static vst_effect_supports [[maybe_unused]]
#include "vst.h"
#undef vst_host_supports
#undef vst_effect_supports
#pragma pop_macro("VST_VERSION")

namespace sub::vst2 {

// The events of one process call, as plug-ins read them (VST_EFFECT_OPCODE_EVENT):
// the count, a reserved field, then the pointers.
template <size_t Capacity>
struct EventList {
    int32_t count = 0;
    intptr_t reserved = 0;
    vst_event_t* events[Capacity] = {};
};
static_assert(offsetof(EventList<2>, events) == 2 * sizeof(intptr_t), "plug-ins read the pointers here");

// Where the block is, as the host answers kGetTime (a pointer to one of these,
// valid until the next call).
struct TimeInfo {
    double samplePos = 0.0;      // in samples, of the block's first frame
    double sampleRate = 0.0;
    double nanoSeconds = 0.0;    // the system's time
    double ppqPos = 0.0;         // in quarter notes
    double tempo = 0.0;          // beats per minute
    double barStartPos = 0.0;    // the last bar's start, in quarter notes
    double cycleStartPos = 0.0;  // the loop's, in quarter notes
    double cycleEndPos = 0.0;
    int32_t timeSigNumerator = 4;
    int32_t timeSigDenominator = 4;
    int32_t smpteOffset = 0;
    int32_t smpteFrameRate = 0;
    int32_t samplesToNextClock = 0;
    int32_t flags = 0;  // TimeInfoFlags
};
static_assert(sizeof(TimeInfo) == 8 * sizeof(double) + 6 * sizeof(int32_t));

enum TimeInfoFlags : int32_t {
    kTransportChanged = 1 << 0,
    kTransportPlaying = 1 << 1,
    kTransportCycleActive = 1 << 2,
    kTransportRecording = 1 << 3,
    kNanosValid = 1 << 8,
    kPpqPosValid = 1 << 9,
    kTempoValid = 1 << 10,
    kBarsValid = 1 << 11,
    kCyclePosValid = 1 << 12,
    kTimeSigValid = 1 << 13,
    kSmpteValid = 1 << 14,
    kClockValid = 1 << 15,
};

// Host opcodes vst.h leaves unnamed (VST_HOST_OPCODE_xx).
enum HostOpcode : int32_t {
    kWantMidi = 0x06,              // (VST 2.0's) the plug-in wants MIDI
    kGetTime = 0x07,               // p_int2: the TimeInfoFlags it needs; returns a TimeInfo*
    kProcessLevel = 0x17,          // VST_HOST_OPCODE_GET_ACTIVE_THREAD
    kAutomationState = 0x18,       // whether automation reads or writes: 1, off
    kBeginEdit = VST_HOST_OPCODE_PARAM_START_EDIT,
    kEndEdit = VST_HOST_OPCODE_PARAM_STOP_EDIT,
};

// The plug-in's category (VST_EFFECT_OPCODE_CATEGORY).
inline constexpr int32_t kCategoryShell = VST_EFFECT_CATEGORY_CONTAINER;

}  // namespace sub::vst2
