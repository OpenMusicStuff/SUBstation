#include "WarpCache.h"

#include <algorithm>
#include <cmath>
#include <new>

#include "CacheFile.h"
#include "Snapshot.h"
#include "StreamCache.h"
#include "platform/Threads.h"
#include "rt/RtUtils.h"

namespace sub {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kRenderBlock = 1024;  // frames per render() call: fixed, so a render is the same every time

}  // namespace

bool WarpCache::Entry::matches(const ClipRender& clip, double rate) const noexcept {
    return source.get() == clip.source.get() && sourceOffset == clip.sourceOffset && length == clip.length &&
           this->rate == clip.rate && config == clip.stretchConfig && transpose == clip.transpose &&
           formants == clip.preserveFormants && sampleRate == rate &&
           (curve == clip.curve || (curve && clip.curve && *curve == *clip.curve));
}

WarpCache::WarpCache(size_t budget, std::chrono::milliseconds settle)
    : budget_(budget), settle_(settle), thread_([this] { run(); }) {}

WarpCache::~WarpCache() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        for (Entry& entry : entries_) entry.render->wanted_.store(false, std::memory_order_relaxed);
    }
    wake_.notify_all();
    thread_.join();
}

void WarpCache::beginBuild() {
    std::lock_guard lock(mutex_);
    for (Entry& entry : entries_) entry.askedThisBuild = false;
    buildBytes_ = 0;
}

std::shared_ptr<const WarpRender> WarpCache::request(const ClipRender& clip, double sampleRate) {
    std::lock_guard lock(mutex_);
    for (Entry& entry : entries_) {
        if (!entry.matches(clip, sampleRate)) continue;
        if (!entry.askedThisBuild) {
            if (buildBytes_ + entry.bytes() > budget_) return nullptr;  // past the budget: forgotten, stretched live
            buildBytes_ += entry.bytes();
        }
        entry.askedThisBuild = true;
        return entry.render;
    }
    Entry entry;
    entry.source = clip.source;
    entry.sourceOffset = clip.sourceOffset;
    entry.length = clip.length;
    entry.rate = clip.rate;
    entry.curve = clip.curve;
    entry.config = clip.stretchConfig;
    entry.transpose = clip.transpose;
    entry.formants = clip.preserveFormants;
    entry.sampleRate = sampleRate;
    if (clip.length <= 0 || buildBytes_ + entry.bytes() > budget_) return nullptr;
    entry.render = std::make_shared<WarpRender>();
    entry.render->length_ = entry.length;
    entry.render->sourceOffset_ = entry.sourceOffset;
    entry.render->rate_ = entry.rate;
    entry.render->curve_ = entry.curve;
    entry.asked = Clock::now();
    entry.askedThisBuild = true;
    buildBytes_ += entry.bytes();
    entries_.push_back(std::move(entry));
    wake_.notify_one();
    return entries_.back().render;
}

void WarpCache::endBuild() {
    std::lock_guard lock(mutex_);
    const auto forgotten = std::remove_if(entries_.begin(), entries_.end(), [](const Entry& entry) {
        if (entry.askedThisBuild) return false;
        entry.render->wanted_.store(false, std::memory_order_relaxed);
        return true;
    });
    entries_.erase(forgotten, entries_.end());
    idle_.notify_all();
}

void WarpCache::waitIdle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] {
        return stop_ || std::all_of(entries_.begin(), entries_.end(),
                                    [](const Entry& entry) { return entry.render->ready(); });
    });
}

void WarpCache::run() {
    platform::enterBackgroundMode();
    std::unique_lock lock(mutex_);
    while (!stop_) {
        // The longest waiting, once its clip has settled.
        Entry* next = nullptr;
        for (Entry& entry : entries_) {
            if (!entry.started && (!next || entry.asked < next->asked)) next = &entry;
        }
        if (!next) {
            wake_.wait(lock);
            continue;
        }
        if (const auto due = next->asked + settle_; Clock::now() < due) {
            wake_.wait_until(lock, due);
            continue;
        }
        next->started = true;
        const Entry job = *next;  // (endBuild() may drop the entry meanwhile: the job holds what it needs)
        lock.unlock();
        const bool rendered = renderEntry(job);
        lock.lock();
        if (!rendered) {  // out of memory: the clip goes on stretching live
            entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                          [&](const Entry& entry) { return entry.render == job.render; }),
                           entries_.end());
        }
        idle_.notify_all();
    }
}

void WarpCache::setStreamCache(std::shared_ptr<StreamCache> cache) {
    std::lock_guard lock(mutex_);
    streamCache_ = std::move(cache);
}

bool WarpCache::renderEntry(const Entry& entry) {
    WarpRender& render = *entry.render;
    ClipRender clip;
    clip.source = entry.source;
    clip.length = entry.length;
    clip.sourceOffset = entry.sourceOffset;
    clip.rate = entry.rate;
    clip.curve = entry.curve;
    clip.playback = ClipRender::Playback::Stretch;
    clip.stretchConfig = entry.config;
    clip.transpose = entry.transpose;
    clip.preserveFormants = entry.formants;
    // A clip of a streamed source is rendered to a file, streamed back as its source is.
    std::shared_ptr<StreamCache> streams;
    if (!entry.source->resident()) {
        std::lock_guard lock(mutex_);
        streams = streamCache_;
    }
    const auto rate = static_cast<uint32_t>(std::lround(entry.sampleRate));
    const StreamReads reads{true, false};  // its source read waiting for the disk (this thread isn't real-time)
    try {
        WarpVoice voice(entry.config, entry.sampleRate);
        ScopedNoDenormals noDenormals;  // as the audio thread renders
        if (streams) {
            CacheFileWriter writer(streams->temporaryFile(), 2, rate, SampleEncoding::Float32);
            std::vector<float> left(kRenderBlock), right(kRenderBlock);
            const float* channels[2] = {left.data(), right.data()};
            for (int64_t at = 0; at < entry.length; at += kRenderBlock) {
                if (!render.wanted_.load(std::memory_order_relaxed)) return true;  // (the writer drops its file)
                const auto frames = static_cast<int>(std::min<int64_t>(kRenderBlock, entry.length - at));
                voice.render(clip, at, frames, at > 0, left.data(), right.data(), reads);
                writer.writePlanar(channels, frames);
            }
            const StreamLayout layout = writer.finish();
            render.audio_ = AudioSource::streamed(entry.source->path(), rate, rate,
                                                  streams->openFile(layout, rate, entry.source->path(), true), {});
        } else {
            std::vector<float> planar(static_cast<size_t>(entry.length) * 2);
            float* left = planar.data();
            float* right = planar.data() + entry.length;
            for (int64_t at = 0; at < entry.length; at += kRenderBlock) {
                if (!render.wanted_.load(std::memory_order_relaxed)) return true;
                const auto frames = static_cast<int>(std::min<int64_t>(kRenderBlock, entry.length - at));
                voice.render(clip, at, frames, at > 0, left + at, right + at, reads);
            }
            render.audio_ = AudioSource::fromSamples(entry.source->path(), rate, 2, std::move(planar), false);
        }
    } catch (const std::bad_alloc&) {
        render.audio_.reset();
        return false;
    } catch (const std::exception&) {  // (its file couldn't be written: a full disk)
        render.audio_.reset();
        return false;
    }
    if (!render.wanted_.load(std::memory_order_relaxed)) {  // forgotten meanwhile: its memory goes now
        render.audio_.reset();
        return true;
    }
    render.ready_.store(true, std::memory_order_release);
    return true;
}

}  // namespace sub
