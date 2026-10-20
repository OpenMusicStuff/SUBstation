#pragma once
// The disk threads (docs/engine/streaming.md): they read the pages streamed
// sources will play soon, before the audio thread gets there. The audio thread
// doesn't say what it will play: the snapshot does. Every kPassInterval (or when
// woken: a locate, a new snapshot, new pins, a page the audio thread missed) the
// planner reads the published snapshot, the playhead and the loop; for each
// track it walks the clips within the horizon ahead of the playhead, turns
// their timeline samples into source frames as the renderer does (clipSpans()),
// and wants every page they fall in, with a deadline: the time until the
// playhead gets there. The readers read them earliest deadline first: pages the
// audio thread missed, then those due, then pinned ones (clip starts) not in
// memory yet. Up to kMaxPagesARead neighbouring pages of a file are one read.
//
// It never takes the engine's mutex: what it needs is in the snapshot it holds
// (the engine hands it each one it builds) and the transport's atomics.

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Snapshot.h"
#include "StreamCache.h"
#include "Transport.h"
#include "WarpCache.h"

namespace sub {

// The spans of source frames a clip plays from over timeline samples [from, to):
// `use(source, first, end)` for each (its source; and its render's, once the
// render is ready, for a stretched clip). The kernel's reach for Re-Pitch, and
// the stretcher's look-ahead for a stretched clip. `starting`: playback starts
// at `from` (a stretched clip with a render ready plays from it alone then).
template <typename F>
void clipSpans(const ClipRender& clip, int64_t from, int64_t to, double sampleRate, bool starting, F&& use) {
    switch (clip.playback) {
        case ClipRender::Playback::Direct: {
            const int64_t first = clip.sourceOffset + (from - clip.start);
            use(*clip.source, first, first + (to - from));
            return;
        }
        case ClipRender::Playback::Resample: {
            constexpr int64_t kReach = 33;  // the kernel's half width at its widest (8 zero crossings, rate 4), and a frame
            const double a = clip.sourceAt(from), b = clip.sourceAt(to);
            use(*clip.source, static_cast<int64_t>(std::floor(std::min(a, b))) - kReach,
                static_cast<int64_t>(std::ceil(std::max(a, b))) + kReach + 1);
            return;
        }
        case ClipRender::Playback::Stretch: {
            const WarpRender* render = clip.rendered.get();
            const bool fromRender = render && render->ready() && render->audio();
            if (fromRender) use(*render->audio(), from - clip.start, to - clip.start);
            if (fromRender && starting) return;
            // A voice: from where it seeks, on to what it feeds ahead of its output.
            const StretchTiming timing = stretchTiming(clip.stretchConfig);
            const double rate = std::max(clip.rateAt(from), clip.rateAt(to));
            const double lead = (timing.blockSeconds + timing.intervalSeconds) * sampleRate * (1.0 + rate);
            use(*clip.source, static_cast<int64_t>(std::floor(clip.sourceAt(from))) - 2,
                static_cast<int64_t>(std::ceil(clip.sourceAt(to) + lead)) + 2);
            return;
        }
    }
}

// Whether every page that the snapshot's clips play from over [position,
// position + frames) is in memory (resident sources always are). Real-time:
// the renderer asks it as playback starts (Renderer's pre-roll).
bool streamsReady(const RenderSnapshot& snap, int64_t position, int64_t frames) noexcept;

class StreamScheduler {
public:
    static constexpr std::chrono::milliseconds kPassInterval{20};
    static constexpr double kDefaultHorizon = 4.0;  // seconds ahead of the playhead
    static constexpr int kDefaultThreads = 2;
    static constexpr int kMaxThreads = 8;
    // A locate's place is read first for this long, until the audio thread has taken it.
    static constexpr std::chrono::milliseconds kLocateHint{1000};

    // `shared`: the transport the audio thread publishes (playhead, playing, the preview's position).
    StreamScheduler(std::shared_ptr<StreamCache> cache, const SharedState& shared, int threads = kDefaultThreads);
    ~StreamScheduler();
    StreamScheduler(const StreamScheduler&) = delete;
    StreamScheduler& operator=(const StreamScheduler&) = delete;

    // The snapshot the audio thread plays (each one the engine publishes).
    void setSnapshot(std::shared_ptr<const RenderSnapshot> snapshot);
    // The browser's preview, if it streams (null: none). Its position is SharedState::previewPosition.
    void setPreview(std::shared_ptr<const AudioSource> source);
    // The playhead goes to `beat` (Engine::setPositionBeats()): its pages first.
    void locate(double beat);
    void setHorizon(double seconds);
    double horizon() const;
    // How many threads read (1 to kMaxThreads): one for a spinning disk, several for an SSD.
    void setThreads(int threads);
    int threads() const;
    void wake();
    // Waits until a pass begun after the call finds nothing to read and nothing is being read: the
    // pages the horizon and the pins want are in memory (or their files fail). Tests and benchmarks.
    void waitIdle();

private:
    struct Read {
        std::shared_ptr<StreamedAudio> stream;
        int64_t first = 0;
        int64_t count = 1;
        int64_t deadline = 0;
    };

    void planner();
    std::vector<StreamCache::Want> wanted();
    void startReadersLocked();

    std::shared_ptr<StreamCache> cache_;
    const SharedState& shared_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;   // the planner: something changed
    std::condition_variable work_;   // the readers: reads queued
    std::condition_variable idle_;   // waitIdle()
    std::shared_ptr<const RenderSnapshot> snapshot_;
    std::shared_ptr<const AudioSource> preview_;
    double locateBeat_ = 0.0;
    std::chrono::steady_clock::time_point locateUntil_{};
    double horizon_ = kDefaultHorizon;
    int threads_ = 0;
    std::vector<Read> queue_;  // the last pass's reads, earliest first
    int inFlight_ = 0;
    bool woken_ = false;
    bool stop_ = false;
    uint64_t passes_ = 0;      // passes begun
    uint64_t idlePass_ = 0;    // the last one that found nothing to read with nothing in flight
    std::vector<std::thread> readers_;
    std::vector<std::shared_ptr<std::atomic<bool>>> readerStop_;  // (one each: fewer threads end some)
    std::thread planner_;  // last: started once the rest stands
};

}  // namespace sub
