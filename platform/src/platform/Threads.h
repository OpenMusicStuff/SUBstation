// Threads' priorities: the background ones yield to everything, the audio
// ones go before everything.

#pragma once

namespace sub::platform {

// The calling thread's CPU, I/O and memory priority to background, for good: it
// yields to everything else, the audio thread and disk reads for playback
// first. (Windows: THREAD_MODE_BACKGROUND_BEGIN; Linux: the lowest nice value
// and the idle I/O class, for this thread only; elsewhere nothing yet.)
void enterBackgroundMode();

// The calling thread at real-time priority while the object lives, for threads
// that render audio beside the driver's own (the render workers). Windows:
// MMCSS's "Pro Audio" task. Linux: SCHED_FIFO, set directly where the user may
// (rtprio in limits.conf, or CAP_SYS_NICE), else asked of rtkit, which sets
// SCHED_RR (over D-Bus, libdbus loaded when it is first needed); below the
// audio servers' own threads. macOS: the time-constraint policy. Where the system refuses, the
// thread stays as it was and active() is false.
class ScopedRealtimePriority {
public:
    ScopedRealtimePriority() noexcept;
    ~ScopedRealtimePriority();
    ScopedRealtimePriority(const ScopedRealtimePriority&) = delete;
    ScopedRealtimePriority& operator=(const ScopedRealtimePriority&) = delete;

    // Whether the system raised it.
    bool active() const noexcept { return active_; }

private:
    bool active_ = false;
    void* handle_ = nullptr;  // Windows: MMCSS's task handle
    int policy_ = 0;          // POSIX: the thread's scheduling before (to go back to)
    int priority_ = 0;
};

// An audio device's workgroup: on macOS the os_workgroup_t of the device's I/O
// thread (Core Audio's kAudioDevicePropertyIOThreadOSWorkgroup). Threads that
// render for the device join it, so that the system schedules them together
// with the device's thread, to its deadline (on Apple Silicon: on performance
// cores). Other systems have none: always empty. Copies share the workgroup.
class AudioWorkgroup {
public:
    AudioWorkgroup() noexcept = default;  // none
    // Holds a reference of its own to `native` (an os_workgroup_t; null: none).
    explicit AudioWorkgroup(void* native) noexcept;
    AudioWorkgroup(const AudioWorkgroup& other) noexcept;
    AudioWorkgroup& operator=(const AudioWorkgroup& other) noexcept;
    ~AudioWorkgroup();

    bool empty() const noexcept { return native_ == nullptr; }
    void* native() const noexcept { return native_; }
    friend bool operator==(const AudioWorkgroup& a, const AudioWorkgroup& b) noexcept { return a.native_ == b.native_; }

private:
    void* native_ = nullptr;
};

// The calling thread a member of the workgroup while the object lives (and the
// workgroup alive: it holds a copy). Nothing if the workgroup is empty, the
// system has none, or it refuses (a workgroup that was cancelled). Real-time
// safe: neither joining nor leaving allocates or waits.
class ScopedWorkgroupMembership {
public:
    ScopedWorkgroupMembership() noexcept = default;  // a member of none
    explicit ScopedWorkgroupMembership(const AudioWorkgroup& workgroup) noexcept;
    ~ScopedWorkgroupMembership();
    ScopedWorkgroupMembership(const ScopedWorkgroupMembership&) = delete;
    ScopedWorkgroupMembership& operator=(const ScopedWorkgroupMembership&) = delete;

    bool joined() const noexcept { return joined_; }

private:
    alignas(8) unsigned char token_[64] = {};  // macOS: os_workgroup_join_token_s (first: no padding before it)
    AudioWorkgroup workgroup_;
    bool joined_ = false;
};

}  // namespace sub::platform
