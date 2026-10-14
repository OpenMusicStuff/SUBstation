#pragma once
// Stretched clips rendered whole in the background, so that live playback can
// read them instead of running a stretcher: the cost of a stretched clip falls
// to that of a clip played as it is.
//
// A snapshot build asks the cache for each stretched clip's render (request());
// the clip carries it to the audio thread, which plays from it once ready()
// (Renderer::assignVoices() says when). The cache's own thread renders what was
// asked for, one at a time, at background priority, with a fresh voice and the
// fixed seed: a render is the same every time. What no snapshot asks for any
// more is forgotten: its render stops, and its memory goes with the last
// snapshot holding it (never on the audio thread).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Warp.h"

namespace sub {

class AudioSource;
struct ClipRender;

// A stretched clip's output over its whole length: frame n is what a voice
// starting at the clip's first frame plays n frames later. Written by the
// cache's thread; read-only, from any thread, once ready().
class WarpRender {
public:
    bool ready() const noexcept { return ready_.load(std::memory_order_acquire); }
    const float* left() const noexcept { return left_.data(); }
    const float* right() const noexcept { return right_.data(); }

private:
    friend class WarpCache;
    std::vector<float> left_, right_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> wanted_{true};  // false once forgotten: an unfinished render stops
};

class WarpCache {
public:
    // At most this much audio, in bytes (512 MiB: 23 minutes of stereo at
    // 48 kHz); clips past it stretch live.
    static constexpr size_t kDefaultBudget = size_t{512} << 20;
    // A render starts once its clip has stayed the same this long, so that a
    // tempo dragged through many values isn't rendered at each of them.
    static constexpr std::chrono::milliseconds kDefaultSettle{250};

    explicit WarpCache(size_t budget = kDefaultBudget, std::chrono::milliseconds settle = kDefaultSettle);
    ~WarpCache();
    WarpCache(const WarpCache&) = delete;
    WarpCache& operator=(const WarpCache&) = delete;

    // A snapshot build, on one thread at a time (the engine's, under its mutex):
    // beginBuild(), request() for each stretched clip, endBuild(), which forgets
    // the renders no clip asked for. request() gives the clip's render, ready or
    // still to come, or null where the budget is spent.
    void beginBuild();
    std::shared_ptr<const WarpRender> request(const ClipRender& clip, double sampleRate);
    void endBuild();

    // Waits until every render asked for is ready (tests and benchmarks).
    void waitIdle();

private:
    struct Entry {
        // What the render depends on (the source held, so its address isn't reused).
        std::shared_ptr<const AudioSource> source;
        int64_t sourceOffset = 0;
        int64_t length = 0;
        double rate = 1.0;
        StretchConfig config = StretchConfig::Standard;
        float transpose = 0.f;
        bool formants = false;
        double sampleRate = 0.0;

        std::shared_ptr<WarpRender> render;
        std::chrono::steady_clock::time_point asked;  // first asked for
        bool started = false;                         // the thread has taken it
        bool askedThisBuild = false;

        bool matches(const ClipRender& clip, double rate) const noexcept;
        size_t bytes() const noexcept { return static_cast<size_t>(length) * 2 * sizeof(float); }
    };

    void run();
    static bool renderEntry(const Entry& entry);  // false: out of memory

    const size_t budget_;
    const std::chrono::milliseconds settle_;
    std::mutex mutex_;
    std::condition_variable wake_;  // something to render, or stop
    std::condition_variable idle_;  // a render finished
    std::vector<Entry> entries_;
    size_t buildBytes_ = 0;  // of the entries asked for in the build under way
    bool stop_ = false;
    std::thread thread_;  // last: started once the rest stands
};

}  // namespace sub
