#include "StreamScheduler.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "platform/Threads.h"

namespace sub {
namespace {

constexpr int64_t kMaxPagesARead = 8;  // as StreamCache reads them

}  // namespace

bool streamsReady(const RenderSnapshot& snap, int64_t position, int64_t frames) noexcept {
    bool ready = true;
    for (const TrackRender& track : snap.tracks) {
        forEachClip(track, position, position + frames, [&](const ClipRender& clip, int64_t from, int64_t to) {
            if (!ready) return;
            clipSpans(clip, from, to, snap.sampleRate, true, [&](const AudioSource& source, int64_t first, int64_t end) {
                const StreamedAudio* stream = source.stream();
                if (!stream) return;
                first = std::max<int64_t>(first, 0);
                end = std::min(end, stream->frames());
                for (int64_t k = first >> kStreamPageShift; ready && first < end && k <= (end - 1) >> kStreamPageShift; ++k)
                    ready = stream->loaded(k);
            });
        });
        if (!ready) return false;
    }
    return true;
}

StreamScheduler::StreamScheduler(std::shared_ptr<StreamCache> cache, const SharedState& shared, int threads)
    : cache_(std::move(cache)), shared_(shared) {
    {
        std::lock_guard lock(mutex_);
        threads_ = std::clamp(threads, 1, kMaxThreads);
        startReadersLocked();
    }
    cache_->setWakeHook([this] { wake(); });
    planner_ = std::thread([this] { planner(); });
}

StreamScheduler::~StreamScheduler() {
    cache_->setWakeHook(nullptr);
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    work_.notify_all();
    idle_.notify_all();
    planner_.join();
    for (std::thread& reader : readers_) reader.join();
}

void StreamScheduler::startReadersLocked() {
    while (static_cast<int>(readers_.size()) < threads_) {
        auto stop = std::make_shared<std::atomic<bool>>(false);
        readerStop_.push_back(stop);
        readers_.emplace_back([this, stop] {
            platform::raiseNormalPriority();  // (never background: that lowers its reads' priority too)
            platform::AlignedBuffer buffer(static_cast<size_t>(kMaxPagesARead) * StreamCache::kPageBytes +
                                           2 * platform::StreamFile::kAlignment);
            std::unique_lock lock(mutex_);
            while (!stop_ && !stop->load()) {
                if (queue_.empty()) {
                    work_.wait(lock);
                    continue;
                }
                Read read = std::move(queue_.front());
                queue_.erase(queue_.begin());
                ++inFlight_;
                lock.unlock();
                cache_->load(*read.stream, read.first, read.count, false, buffer);
                read.stream.reset();  // (not under the lock: it may be the last holder)
                lock.lock();
                --inFlight_;
            }
        });
    }
}

void StreamScheduler::setSnapshot(std::shared_ptr<const RenderSnapshot> snapshot) {
    std::shared_ptr<const RenderSnapshot> old;
    {
        std::lock_guard lock(mutex_);
        old = std::exchange(snapshot_, std::move(snapshot));
        woken_ = true;
    }
    wake_.notify_one();
}

void StreamScheduler::setPreview(std::shared_ptr<const AudioSource> source) {
    std::shared_ptr<const AudioSource> old;
    {
        std::lock_guard lock(mutex_);
        old = std::exchange(preview_, std::move(source));
        woken_ = true;
    }
    wake_.notify_one();
}

void StreamScheduler::locate(double beat) {
    {
        std::lock_guard lock(mutex_);
        locateBeat_ = beat;
        locateUntil_ = std::chrono::steady_clock::now() + kLocateHint;
        woken_ = true;
    }
    wake_.notify_one();
}

void StreamScheduler::setHorizon(double seconds) {
    std::lock_guard lock(mutex_);
    horizon_ = std::clamp(seconds, 0.5, 60.0);
}

double StreamScheduler::horizon() const {
    std::lock_guard lock(mutex_);
    return horizon_;
}

void StreamScheduler::setThreads(int threads) {
    threads = std::clamp(threads, 1, kMaxThreads);
    std::vector<std::thread> ending;
    {
        std::lock_guard lock(mutex_);
        threads_ = threads;
        while (static_cast<int>(readers_.size()) > threads_) {
            readerStop_.back()->store(true);
            readerStop_.pop_back();
            ending.push_back(std::move(readers_.back()));
            readers_.pop_back();
        }
        startReadersLocked();
    }
    work_.notify_all();
    for (std::thread& reader : ending) reader.join();
}

int StreamScheduler::threads() const {
    std::lock_guard lock(mutex_);
    return threads_;
}

void StreamScheduler::wake() {
    {
        std::lock_guard lock(mutex_);
        woken_ = true;
    }
    wake_.notify_one();
}

void StreamScheduler::waitIdle() {
    std::unique_lock lock(mutex_);
    const uint64_t after = passes_;
    woken_ = true;
    wake_.notify_one();
    idle_.wait(lock, [&] { return stop_ || idlePass_ > after; });
}

std::vector<StreamCache::Want> StreamScheduler::wanted() {
    std::shared_ptr<const RenderSnapshot> snap;
    std::shared_ptr<const AudioSource> preview;
    double horizon = 0.0;
    bool locating = false;
    double locateBeat = 0.0;
    {
        std::lock_guard lock(mutex_);
        snap = snapshot_;
        preview = preview_;
        horizon = horizon_;
        locating = std::chrono::steady_clock::now() < locateUntil_;
        locateBeat = locateBeat_;
    }
    std::vector<StreamCache::Want> wants;
    // What the audio thread missed: first of all.
    for (auto& stream : cache_->streams()) {
        if (const int64_t k = stream->takeUrgent(); k >= 0) wants.push_back({stream, k, -1});
    }
    // Every page of `source`'s frames [first, end), which play from `start` samples on, over `length` samples.
    const auto want = [&](const AudioSource& source, int64_t first, int64_t end, int64_t start, int64_t length) {
        const std::shared_ptr<StreamedAudio>& stream = source.sharedStream();
        if (!stream) return;
        first = std::max<int64_t>(first, 0);
        end = std::min(end, stream->frames());
        if (first >= end) return;
        const double perFrame = static_cast<double>(length) / static_cast<double>(end - first);
        for (int64_t k = first >> kStreamPageShift; k <= (end - 1) >> kStreamPageShift; ++k) {
            const int64_t at = std::max(first, k << kStreamPageShift);
            wants.push_back({stream, k, start + std::llround(static_cast<double>(at - first) * perFrame)});
        }
    };
    if (snap) {
        const auto length = static_cast<int64_t>(std::llround(horizon * snap->sampleRate));
        struct Window {
            int64_t from, to, offset;  // timeline [from, to), played `offset` samples from now
        };
        std::vector<Window> windows;
        const auto ahead = [&](int64_t at, int64_t samples) {
            at = std::max<int64_t>(at, 0);
            // The loop's end takes the playhead back to its start (as the renderer has it: from anywhere before it).
            if (snap->loopEnabled && snap->loopEnd > snap->loopStart && at < snap->loopEnd) {
                int64_t offset = 0;
                for (int wraps = 0; samples > 0 && wraps < 4; ++wraps) {
                    const int64_t to = std::min(at + samples, snap->loopEnd);
                    windows.push_back({at, to, offset});
                    offset += to - at;
                    samples -= to - at;
                    at = snap->loopStart;
                }
                return;
            }
            windows.push_back({at, at + samples, 0});
        };
        if (locating) ahead(snap->sampleAt(locateBeat), length / 2);
        ahead(shared_.positionSamples.load(std::memory_order_relaxed), length);
        for (const TrackRender& track : snap->tracks) {
            for (const Window& window : windows) {
                forEachClip(track, window.from, window.to, [&](const ClipRender& clip, int64_t from, int64_t to) {
                    clipSpans(clip, from, to, snap->sampleRate, false,
                              [&](const AudioSource& source, int64_t first, int64_t end) {
                                  want(source, first, end, window.offset + (from - window.from), to - from);
                              });
                });
            }
        }
        if (preview && preview->stream()) {
            const int64_t at = shared_.previewPosition.load(std::memory_order_relaxed);
            want(*preview, at, at + length, 0, length);
        }
    }
    std::stable_sort(wants.begin(), wants.end(),
                     [](const StreamCache::Want& a, const StreamCache::Want& b) { return a.deadline < b.deadline; });
    return wants;
}

void StreamScheduler::planner() {
    platform::raiseNormalPriority();
    std::unique_lock lock(mutex_);
    while (!stop_) {
        wake_.wait_for(lock, kPassInterval, [this] { return stop_ || woken_; });
        if (stop_) break;
        woken_ = false;
        const uint64_t pass = ++passes_;
        const bool busy = inFlight_ > 0;
        lock.unlock();
        const std::vector<StreamCache::Want> requests = cache_->plan(wanted());
        // Neighbouring pages of a file, one read (by the earliest's deadline).
        std::vector<Read> reads;
        std::unordered_map<const StreamedAudio*, std::vector<size_t>> byStream;
        for (const StreamCache::Want& request : requests) {
            std::vector<size_t>& mine = byStream[request.stream.get()];
            const auto joins = std::find_if(mine.begin(), mine.end(), [&](size_t r) {
                const Read& read = reads[r];
                return read.count < kMaxPagesARead && read.first + read.count == request.page;
            });
            if (joins != mine.end()) {
                ++reads[*joins].count;
                continue;
            }
            mine.push_back(reads.size());
            reads.push_back({request.stream, request.page, 1, request.deadline});
        }
        lock.lock();
        std::vector<Read> old = std::exchange(queue_, std::move(reads));
        if (queue_.empty() && !busy && inFlight_ == 0) {
            idlePass_ = pass;
            idle_.notify_all();
        }
        work_.notify_all();
        lock.unlock();
        old.clear();  // (its streams may go with it: not under the lock)
        lock.lock();
    }
}

}  // namespace sub
