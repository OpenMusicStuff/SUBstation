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

#include "AudioSource.h"
#include "Warp.h"

namespace sub {

class StreamCache;
struct ClipRender;

// A stretched clip's output over its whole length: frame n is what a voice
// starting at the clip's first frame plays n frames later, source frame
// sourceOffset() + n * rate() (or, under a tempo that changes along the clip,
// where its curve has it: frameOf() is the inverse). Written by the cache's thread; read-only, from
// any thread, once ready() (but for the live renderer's note of where it
// played it last). Its audio is stereo, in memory; or, for a clip of a streamed
// source, in a file of the stream cache's folder, streamed like its source
// (StreamCache.h): left() and right() are then null.
class WarpRender {
public:
    bool ready() const noexcept { return ready_.load(std::memory_order_acquire); }
    const AudioSource* audio() const noexcept { return audio_.get(); }
    const float* left() const noexcept { return audio_ ? audio_->channelData(0) : nullptr; }
    const float* right() const noexcept { return audio_ ? audio_->channelData(1) : nullptr; }
    int64_t length() const noexcept { return length_; }
    int64_t sourceOffset() const noexcept { return sourceOffset_; }
    double rate() const noexcept { return rate_; }
    // The frame (fractional) that played source frame `source`.
    double frameOf(double source) const noexcept {
        const double from = source - static_cast<double>(sourceOffset_);
        return curve_ ? warpcurve::timeOf(*curve_, from) : from / rate_;
    }

    // The live renderer's note (its thread alone): the block it last played
    // this render in, and the source position it got to. When a clip goes
    // from its render back to a voice, this says whether the render was
    // sounding just before, there (WarpVoice::fadeFrom()).
    void notePlayed(uint64_t block, double sourcePosition) const noexcept {
        playedBlock_.store(block, std::memory_order_relaxed);
        playedTo_.store(sourcePosition, std::memory_order_relaxed);
    }
    uint64_t playedBlock() const noexcept { return playedBlock_.load(std::memory_order_relaxed); }
    double playedTo() const noexcept { return playedTo_.load(std::memory_order_relaxed); }

private:
    friend class WarpCache;
    std::shared_ptr<const AudioSource> audio_;
    int64_t length_ = 0;
    int64_t sourceOffset_ = 0;
    double rate_ = 1.0;
    std::shared_ptr<const WarpCurve> curve_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> wanted_{true};  // false once forgotten: an unfinished render stops
    mutable std::atomic<uint64_t> playedBlock_{0};  // 0: never played
    mutable std::atomic<double> playedTo_{0.0};
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

    // Where the renders of clips of streamed sources go (files in its folder, streamed back); without
    // one, into memory like the others. Any thread.
    void setStreamCache(std::shared_ptr<StreamCache> cache);

private:
    struct Entry {
        // What the render depends on (the source held, so its address isn't reused).
        std::shared_ptr<const AudioSource> source;
        int64_t sourceOffset = 0;
        int64_t length = 0;
        double rate = 1.0;
        std::shared_ptr<const WarpCurve> curve;  // (compared by what it holds: the curve relative to the clip)
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
    bool renderEntry(const Entry& entry);  // false: out of memory (or disk)

    const size_t budget_;
    const std::chrono::milliseconds settle_;
    std::mutex mutex_;
    std::condition_variable wake_;  // something to render, or stop
    std::condition_variable idle_;  // a render finished
    std::vector<Entry> entries_;
    std::shared_ptr<StreamCache> streamCache_;
    size_t buildBytes_ = 0;  // of the entries asked for in the build under way
    bool stop_ = false;
    std::thread thread_;  // last: started once the rest stands
};

}  // namespace sub
