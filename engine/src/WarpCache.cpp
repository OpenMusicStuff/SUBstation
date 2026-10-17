#include "WarpCache.h"

#include <algorithm>
#include <new>

#include "Snapshot.h"
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
    try {
        WarpVoice voice(entry.config, entry.sampleRate);
        render.left_.resize(static_cast<size_t>(entry.length));
        render.right_.resize(static_cast<size_t>(entry.length));
        ScopedNoDenormals noDenormals;  // as the audio thread renders
        for (int64_t at = 0; at < entry.length; at += kRenderBlock) {
            if (!render.wanted_.load(std::memory_order_relaxed)) break;
            const auto frames = static_cast<int>(std::min<int64_t>(kRenderBlock, entry.length - at));
            voice.render(clip, at, frames, at > 0, render.left_.data() + at, render.right_.data() + at);
        }
    } catch (const std::bad_alloc&) {
        std::vector<float>().swap(render.left_);
        std::vector<float>().swap(render.right_);
        return false;
    }
    if (!render.wanted_.load(std::memory_order_relaxed)) {  // forgotten meanwhile: its memory goes now
        std::vector<float>().swap(render.left_);
        std::vector<float>().swap(render.right_);
        return true;
    }
    render.ready_.store(true, std::memory_order_release);
    return true;
}

}  // namespace sub
