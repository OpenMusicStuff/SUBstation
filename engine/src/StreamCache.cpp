#include "StreamCache.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "AudioSource.h"
#include "platform/Paths.h"

namespace sub {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int64_t kPageMask = kStreamPageFrames - 1;
constexpr int64_t kMaxPagesARead = 8;  // a read: up to this many pages of a file, as one I/O
constexpr int64_t kBlockingReadAhead = 4;  // a lease reads the pages after it too (offline renders read on)

// The gain of a frame `distance` frames from a page that isn't in memory (0: next to it).
inline float missFade(int64_t distance) noexcept {
    return static_cast<float>(distance + 1) / static_cast<float>(StreamedAudio::kMissFade + 1);
}

int64_t headFrames(uint32_t sampleRate) {
    return static_cast<int64_t>(std::ceil(StreamCache::kHeadSeconds * sampleRate));
}

std::string hex(uint64_t v) {
    char text[17];
    std::snprintf(text, sizeof text, "%llx", static_cast<unsigned long long>(v));
    return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// StreamedAudio

StreamedAudio::StreamedAudio(std::shared_ptr<StreamCache> cache, StreamLayout layout, uint32_t sampleRate,
                             std::string sourcePath, std::string peakPath, bool temporary)
    : cache_(std::move(cache)), layout_(std::move(layout)), sampleRate_(sampleRate), sourcePath_(std::move(sourcePath)),
      peakPath_(std::move(peakPath)), temporary_(temporary), pages_(layout_.pages()),
      table_(std::make_unique<std::atomic<StreamPage*>[]>(static_cast<size_t>(pages_))),
      loading_(static_cast<size_t>(pages_), 0), pins_(static_cast<size_t>(pages_), 0),
      pinnedIn_(static_cast<size_t>(pages_), 0), buildPinned_(static_cast<size_t>(pages_), 0) {
    for (int64_t k = 0; k < pages_; ++k) table_[k].store(nullptr, std::memory_order_relaxed);
}

StreamedAudio::~StreamedAudio() {
    cache_->closed(*this);
    file_.close();
    if (temporary_) {
        std::error_code ignored;
        std::filesystem::remove(platform::toPath(layout_.path), ignored);
    }
}

int64_t StreamedAudio::fetch(int64_t start, int64_t n, float* outL, float* outR, bool expected) const noexcept {
    int64_t present = 0, missing = 0;
    bool noted = false;
    int64_t i = 0;
    for (; i < n && start + i < 0; ++i) outL[i] = outR[i] = 0.f;  // before the file
    while (i < n) {
        const int64_t f = start + i;
        if (f >= layout_.frames) {  // past its end
            std::fill(outL + i, outL + n, 0.f);
            std::fill(outR + i, outR + n, 0.f);
            break;
        }
        const int64_t k = f >> kStreamPageShift;
        const int64_t o = f & kPageMask;
        const int64_t m = std::min({n - i, kStreamPageFrames - o, layout_.frames - f});
        const StreamPage* page = table_[k].load(std::memory_order_seq_cst);
        if (!page) {
            std::fill(outL + i, outL + i + m, 0.f);
            std::fill(outR + i, outR + i + m, 0.f);
            missing += m;
            if (!noted) urgent_.store(k, std::memory_order_relaxed);
            noted = true;
            i += m;
            continue;
        }
        const float* left = page->samples.get() + o;
        const float* right = layout_.channels > 1 ? left + kStreamPageFrames : left;
        std::copy_n(left, m, outL + i);
        std::copy_n(right, m, outR + i);
        // Faded next to a page that isn't there, so a miss's edges don't click.
        if (o < kMissFade && k > 0 && !table_[k - 1].load(std::memory_order_seq_cst)) {
            for (int64_t j = 0; j < std::min(m, kMissFade - o); ++j) {
                const float g = missFade(o + j);
                outL[i + j] *= g;
                outR[i + j] *= g;
            }
        }
        if (o + m > kStreamPageFrames - kMissFade && k + 1 < pages_ && !table_[k + 1].load(std::memory_order_seq_cst)) {
            for (int64_t at = std::max(o, kStreamPageFrames - kMissFade); at < o + m; ++at) {
                const float g = missFade(kStreamPageFrames - 1 - at);
                outL[i + at - o] *= g;
                outR[i + at - o] *= g;
            }
        }
        present += m;
        i += m;
    }
    if (missing > 0) (expected ? expected_ : missed_).fetch_add(static_cast<uint64_t>(missing), std::memory_order_relaxed);
    return present;
}

float StreamedAudio::sample(uint32_t side, int64_t frame) const noexcept {
    if (frame < 0 || frame >= layout_.frames) return 0.f;
    const int64_t k = frame >> kStreamPageShift;
    const int64_t o = frame & kPageMask;
    const StreamPage* page = table_[k].load(std::memory_order_seq_cst);
    if (!page) return 0.f;
    float v = page->samples[static_cast<size_t>((layout_.channels > 1 ? side : 0u) * kStreamPageFrames + o)];
    if (o < kMissFade && k > 0 && !table_[k - 1].load(std::memory_order_seq_cst)) {
        v *= missFade(o);
    } else if (o >= kStreamPageFrames - kMissFade && k + 1 < pages_ && !table_[k + 1].load(std::memory_order_seq_cst)) {
        v *= missFade(kStreamPageFrames - 1 - o);
    }
    return v;
}

int64_t StreamedAudio::noteMissing(int64_t start, int64_t n, bool expected) const noexcept {
    const int64_t from = std::max<int64_t>(start, 0);
    const int64_t to = std::min<int64_t>(start + n, layout_.frames);
    int64_t missing = 0;
    bool noted = false;
    for (int64_t f = from; f < to;) {
        const int64_t k = f >> kStreamPageShift;
        const int64_t m = std::min(to - f, kStreamPageFrames - (f & kPageMask));
        if (!table_[k].load(std::memory_order_seq_cst)) {
            missing += m;
            if (!noted) urgent_.store(k, std::memory_order_relaxed);
            noted = true;
        }
        f += m;
    }
    if (missing > 0) (expected ? expected_ : missed_).fetch_add(static_cast<uint64_t>(missing), std::memory_order_relaxed);
    return missing;
}

StreamLease StreamedAudio::lease(int64_t k) const {
    if (k < 0 || k >= pages_) return {};
    // Found and leased under the cache's lock: a page is taken out of its table, reused and freed only
    // under it too, so the one found can't go before its lease counts. (The audio thread alone reads
    // the table without it: the audio epochs keep its pages.)
    for (int attempt = 0; attempt < 1000; ++attempt) {
        {
            std::lock_guard lock(cache_->mutex_);
            if (StreamPage* page = table_[k].load(std::memory_order_seq_cst)) {
                page->leases.fetch_add(1, std::memory_order_seq_cst);
                return StreamLease(page);
            }
        }
        if (!cache_->loadBlocking(*this, k)) return {};
    }
    return {};
}

void StreamedAudio::fetchBlocking(int64_t start, int64_t n, float* outL, float* outR) const {
    int64_t i = 0;
    for (; i < n && start + i < 0; ++i) outL[i] = outR[i] = 0.f;
    while (i < n) {
        const int64_t f = start + i;
        if (f >= layout_.frames) {
            std::fill(outL + i, outL + n, 0.f);
            std::fill(outR + i, outR + n, 0.f);
            break;
        }
        const int64_t k = f >> kStreamPageShift;
        const int64_t o = f & kPageMask;
        const int64_t m = std::min({n - i, kStreamPageFrames - o, layout_.frames - f});
        const StreamLease page = lease(k);
        if (page) {
            std::copy_n(page.channel(0) + o, m, outL + i);
            std::copy_n(page.channel(layout_.channels > 1 ? 1u : 0u) + o, m, outR + i);
        } else {  // (unreadable: silence)
            std::fill(outL + i, outL + i + m, 0.f);
            std::fill(outR + i, outR + i + m, 0.f);
        }
        i += m;
    }
}

bool StreamedAudio::read(uint32_t channel, int64_t start, int64_t count, float* out) const {
    bool ok = true;
    int64_t i = 0;
    for (; i < count && start + i < 0; ++i) out[i] = 0.f;
    while (i < count) {
        const int64_t f = start + i;
        if (f >= layout_.frames || channel >= layout_.channels) {
            std::fill(out + i, out + count, 0.f);
            break;
        }
        const int64_t k = f >> kStreamPageShift;
        const int64_t o = f & kPageMask;
        const int64_t m = std::min({count - i, kStreamPageFrames - o, layout_.frames - f});
        const StreamLease page = lease(k);
        if (page) {
            std::copy_n(page.channel(channel) + o, m, out + i);
        } else {
            std::fill(out + i, out + i + m, 0.f);
            ok = false;
        }
        i += m;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// StreamCache: settings

std::shared_ptr<StreamCache> StreamCache::create() { return std::shared_ptr<StreamCache>(new StreamCache()); }

StreamCache::~StreamCache() = default;  // (every stream holds it: none is open any more)

void StreamCache::setPoolBytes(size_t bytes) {
    std::lock_guard lock(mutex_);
    capacity_ = std::max<size_t>(bytes / kPageBytes, 16);
    reclaimLocked();
}

size_t StreamCache::poolBytes() const {
    std::lock_guard lock(mutex_);
    return capacity_ * kPageBytes;
}

size_t StreamCache::capacityPages() const {
    std::lock_guard lock(mutex_);
    return capacity_;
}

void StreamCache::setFolder(const std::string& folder) {
    std::lock_guard lock(mutex_);
    folder_ = folder;
    rendersCleared_ = false;
}

std::string StreamCache::folder() const {
    std::lock_guard lock(mutex_);
    return folder_.empty() ? defaultFolder() : folder_;
}

std::string StreamCache::defaultFolder() {
    std::string folder = platform::userCacheFolder();
    platform::appendName(folder, std::string_view("SUBstation"));
    platform::appendName(folder, std::string_view("Streaming"));
    return folder;
}

void StreamCache::setFolderBudget(uint64_t bytes) {
    {
        std::lock_guard lock(mutex_);
        folderBudget_ = bytes;
    }
    trimFolder();
}

uint64_t StreamCache::folderBudget() const {
    std::lock_guard lock(mutex_);
    return folderBudget_;
}

void StreamCache::setAudioEpochs(const std::atomic<uint64_t>* begun, const std::atomic<uint64_t>* finished) {
    std::lock_guard lock(mutex_);
    begun_ = begun;
    finished_ = finished;
}

void StreamCache::setReadHook(ReadHook hook) {
    std::lock_guard lock(mutex_);
    readHook_ = std::move(hook);
}

void StreamCache::setWakeHook(std::function<void()> hook) {
    std::lock_guard lock(mutex_);
    wakeHook_ = std::move(hook);
}

void StreamCache::wake() {
    // Under the lock, so a hook taken away (its scheduler going) is never called after. (The scheduler's
    // wake() only takes its own lock, and the scheduler never calls the cache holding that.)
    std::lock_guard lock(mutex_);
    if (wakeHook_) wakeHook_();
}

// ---------------------------------------------------------------------------
// Opening

std::shared_ptr<AudioSource> StreamCache::open(const std::string& path, uint32_t sampleRate, bool mayMake) {
    const AudioFileInfo info = AudioSource::probe(path);
    if (info.frames <= 0) throw std::runtime_error("Audio file '" + path + "' contains no audio");
    const platform::FileStamp stamp = platform::stamp(path).value_or(platform::FileStamp{});
    std::string base = folder();
    platform::appendName(base, std::string_view(cacheFileName(path, stamp, sampleRate)));
    const std::string peakPath = base + kPeakFileExtension;
    uint32_t fileRate = info.sampleRate;
    std::vector<std::vector<float>> peaks;
    bool wrote = false;

    // As it is: a WAV or AIFF at the engine's rate, whose frames the decoder agrees on.
    uint32_t asIsRate = 0;
    const std::optional<StreamLayout> asIs = asIsLayout(path, asIsRate);
    StreamLayout layout;
    if (asIs && asIsRate == sampleRate && asIs->frames == info.frames && asIs->channels == info.channels) {
        layout = *asIs;
        if (auto read = readPeakFile(peakPath, layout.channels, layout.frames)) {
            peaks = std::move(*read);
        } else {
            if (!mayMake) return nullptr;
            peaks = scanPeaks(layout);
            wrote = writePeakFile(peakPath, layout.channels, layout.frames, peaks);
        }
    } else {
        const std::string cachePath = base + kCacheFileExtension;
        // One maker of a cache file at a time: a second open waits for the first, then reads it.
        std::unique_lock lock(mutex_);
        made_.wait(lock, [&] { return !making_.contains(cachePath); });
        making_.insert(cachePath);
        lock.unlock();
        try {
            const std::optional<StreamLayout> cached = readCacheFile(cachePath, sampleRate, stamp, &fileRate);
            std::optional<std::vector<std::vector<float>>> read;
            if (cached) read = readPeakFile(peakPath, cached->channels, cached->frames);
            if (cached && read) {
                layout = *cached;
                peaks = std::move(*read);
            } else if (!mayMake) {
                lock.lock();
                making_.erase(cachePath);
                made_.notify_all();
                return nullptr;
            } else {
                MadeCacheFile made = makeCacheFile(path, sampleRate, cachePath);
                layout = std::move(made.layout);
                fileRate = made.fileRate;
                peaks = std::move(made.peaks);
                writePeakFile(peakPath, layout.channels, layout.frames, peaks);
                wrote = true;
            }
        } catch (...) {
            lock.lock();
            making_.erase(cachePath);
            made_.notify_all();
            throw;
        }
        lock.lock();
        making_.erase(cachePath);
        made_.notify_all();
    }
    // Used now: the folder's budget lets go of the least recently used first.
    if (layout.paged) touch(layout.path);
    touch(peakPath);
    auto stream = std::shared_ptr<StreamedAudio>(
        new StreamedAudio(shared_from_this(), layout, sampleRate, path, peakPath, false));
    {
        std::lock_guard lock(mutex_);
        streams_.push_back(stream.get());
    }
    if (!loadHead(*stream)) throw std::runtime_error("Could not read audio file '" + path + "'");
    if (wrote) trimFolder();
    return AudioSource::streamed(path, sampleRate, fileRate, std::move(stream), std::move(peaks));
}

std::shared_ptr<StreamedAudio> StreamCache::openFile(const StreamLayout& layout, uint32_t sampleRate,
                                                     const std::string& sourcePath, bool temporary) {
    auto stream = std::shared_ptr<StreamedAudio>(
        new StreamedAudio(shared_from_this(), layout, sampleRate, sourcePath, "", temporary));
    {
        std::lock_guard lock(mutex_);
        streams_.push_back(stream.get());
    }
    if (!loadHead(*stream)) throw std::runtime_error("Could not read the file " + layout.path);
    return stream;
}

std::string StreamCache::temporaryFile() {
    std::string renders = folder();
    platform::appendName(renders, std::string_view("renders"));
    std::error_code ignored;
    std::filesystem::create_directories(platform::toPath(renders), ignored);
    bool clear = false;
    {
        std::lock_guard lock(mutex_);
        clear = !std::exchange(rendersCleared_, true);
    }
    if (clear) {  // what a crash left behind (a day old: another SUBstation running may be reading its own)
        const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
        for (const auto& entry : std::filesystem::directory_iterator(platform::toPath(renders), ignored)) {
            std::error_code error;
            if (entry.is_regular_file(error) && entry.last_write_time(error) < old) std::filesystem::remove(entry.path(), error);
        }
    }
    static std::atomic<uint64_t> counter{0};
    std::random_device random;
    std::string path = renders;
    platform::appendName(path, std::string_view(hex((static_cast<uint64_t>(random()) << 24) ^ counter.fetch_add(1)) +
                                                kCacheFileExtension));
    return path;
}

bool StreamCache::loadHead(StreamedAudio& stream) {
    // Pinned for good: playback starts from it at once.
    const int64_t frames = std::max<int64_t>(1, std::min<int64_t>(stream.frames(), headFrames(stream.sampleRate_)));
    const int64_t pages = (frames + kStreamPageFrames - 1) / kStreamPageFrames;
    {
        std::lock_guard lock(mutex_);
        for (int64_t k = 0; k < pages; ++k) ++stream.pins_[static_cast<size_t>(k)];
    }
    platform::AlignedBuffer buffer;
    for (int64_t k = 0; k < pages; k += kMaxPagesARead) {
        if (!load(stream, k, std::min(kMaxPagesARead, pages - k), true, buffer)) return false;
    }
    return true;
}

void StreamCache::touch(const std::string& path) {
    std::error_code ignored;
    std::filesystem::last_write_time(platform::toPath(path), std::filesystem::file_time_type::clock::now(), ignored);
}

// ---------------------------------------------------------------------------
// Pins

void StreamCache::beginPins() {
    std::lock_guard lock(mutex_);
    ++build_;
}

void StreamCache::pin(const AudioSource& source, int64_t from, int64_t to) {
    StreamedAudio* stream = source.sharedStream().get();
    if (!stream || stream->cache_.get() != this) return;
    from = std::max<int64_t>(from, 0);
    to = std::min<int64_t>(to, stream->frames());
    if (from >= to) return;
    std::lock_guard lock(mutex_);
    for (int64_t k = from >> kStreamPageShift; k <= (to - 1) >> kStreamPageShift; ++k)
        stream->pinnedIn_[static_cast<size_t>(k)] = build_;
}

void StreamCache::endPins() {
    bool added = false;
    {
        std::lock_guard lock(mutex_);
        for (StreamedAudio* stream : streams_) {
            for (size_t k = 0; k < stream->pinnedIn_.size(); ++k) {
                const bool asked = stream->pinnedIn_[k] == build_;
                if (asked == (stream->buildPinned_[k] != 0)) continue;
                stream->buildPinned_[k] = asked ? 1 : 0;
                if (asked) {
                    ++stream->pins_[k];
                    added = added || !stream->table_[k].load(std::memory_order_relaxed);
                } else {
                    --stream->pins_[k];
                }
            }
        }
    }
    if (added) wake();
}

// ---------------------------------------------------------------------------
// Pages

bool StreamCache::reclaimableLocked(const StreamPage& page) const {
    if (page.leases.load(std::memory_order_seq_cst) != 0) return false;
    if (!finished_) return true;
    // No callback that began before it was taken out of its table still runs: one finished since,
    // or none runs now (one that begins later reads the table as it is now).
    const uint64_t finished = finished_->load(std::memory_order_seq_cst);
    if (finished > page.retiredAt) return true;
    return begun_->load(std::memory_order_seq_cst) == finished;
}

void StreamCache::retireLocked(StreamPage& page) {
    page.owner->table_[page.index].store(nullptr, std::memory_order_seq_cst);
    page.retiredAt = finished_ ? finished_->load(std::memory_order_seq_cst) : 0;
    page.state = StreamPage::State::Retired;
    page.owner = nullptr;
    page.index = -1;
    retired_.push_back(&page);
}

void StreamCache::freeLocked(StreamPage& page) {
    page.state = StreamPage::State::Free;
    page.owner = nullptr;
    page.index = -1;
    free_.push_back(&page);
}

size_t StreamCache::unpinnedInUseLocked() const {
    size_t count = 0;
    for (const auto& page : pages_) {
        if ((page->state == StreamPage::State::InUse || page->state == StreamPage::State::Loading) &&
            page->owner->pins_[static_cast<size_t>(page->index)] == 0)
            ++count;
    }
    return count;
}

void StreamCache::reclaimLocked() {
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (reclaimableLocked(**it)) {
            freeLocked(**it);
            it = retired_.erase(it);
        } else {
            ++it;
        }
    }
    // More pages than the pool holds (a smaller pool, pages a lease took past it): free ones go.
    size_t pinned = 0;
    for (const auto& page : pages_) {
        if (page->state == StreamPage::State::InUse && page->owner->pins_[static_cast<size_t>(page->index)] > 0) ++pinned;
    }
    while (!free_.empty() && pages_.size() > capacity_ + pinned) {
        StreamPage* page = free_.back();
        free_.pop_back();
        pages_.erase(std::find_if(pages_.begin(), pages_.end(), [&](const auto& p) { return p.get() == page; }));
    }
}

StreamPage* StreamCache::takePageLocked(bool pinned, bool blocking) {
    const auto allocate = [&]() -> StreamPage* {
        try {
            auto page = std::make_unique<StreamPage>();
            page->samples.reset(new float[2 * kStreamPageFrames]);
            pages_.push_back(std::move(page));
            return pages_.back().get();
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
    };
    const auto pop = [&]() {
        StreamPage* page = free_.back();
        free_.pop_back();
        return page;
    };
    reclaimLocked();
    if (!free_.empty()) return pop();
    // Pinned pages come on top of the pool.
    if (pinned || unpinnedInUseLocked() < capacity_) {
        if (StreamPage* page = allocate()) return page;
    }
    // The least recently wanted page, neither pinned nor wanted now, makes room.
    StreamPage* victim = nullptr;
    for (const auto& page : pages_) {
        if (page->state != StreamPage::State::InUse || page->wanted >= pass_) continue;
        if (page->owner->pins_[static_cast<size_t>(page->index)] > 0) continue;
        if (!victim || page->wanted < victim->wanted) victim = page.get();
    }
    if (!victim && !blocking) return nullptr;  // the pool holds only what is wanted now: the horizon shrinks
    if (victim) {
        retireLocked(*victim);
        ++evicted_;
        reclaimLocked();  // (without an audio thread: at once)
        if (!free_.empty()) return pop();
    }
    // Until the page let go of can be reused (once the audio thread is done with it), one more.
    return allocate();
}

bool StreamCache::readPages(StreamedAudio& stream, int64_t first, int64_t count, StreamPage* const* pages,
                            platform::AlignedBuffer& buffer) {
    ReadHook hook;
    {
        std::lock_guard lock(mutex_);
        hook = readHook_;
    }
    {
        std::lock_guard lock(stream.fileMutex_);
        if (!stream.file_.isOpen() && !stream.file_.open(stream.layout_.path)) return false;
    }
    if (hook) hook(stream.sourcePath_, first, count);
    uint64_t offset = 0, bytes = 0;
    stream.layout_.span(first, count, offset, bytes);
    if (buffer.size() < bytes) {
        try {
            buffer = platform::AlignedBuffer(static_cast<size_t>(bytes));
        } catch (const std::bad_alloc&) {
            return false;
        }
    }
    int64_t got = -1;
    {
        std::shared_lock lock(stream.fileMutex_);
        if (stream.file_.isOpen()) got = stream.file_.read(offset, buffer.data(), static_cast<size_t>(bytes));
    }
    if (got < 0) {
        std::lock_guard lock(stream.fileMutex_);
        stream.file_.close();  // (none reads it now; opened again next time: the file may come back)
        return false;
    }
    float* samples[kMaxPagesARead];
    for (int64_t i = 0; i < count; ++i) samples[i] = pages[i]->samples.get();
    return stream.layout_.convert(static_cast<const unsigned char*>(buffer.data()), offset, static_cast<uint64_t>(got),
                                  first, count, samples);
}

bool StreamCache::load(StreamedAudio& stream, int64_t first, int64_t count, bool blocking,
                       platform::AlignedBuffer& buffer) {
    first = std::clamp<int64_t>(first, 0, stream.pages_);
    count = std::min({count, stream.pages_ - first, kMaxPagesARead});
    if (count <= 0) return true;
    StreamPage* taken[kMaxPagesARead];
    int64_t from = first, n = 0;
    {
        std::lock_guard lock(mutex_);
        if (!blocking && Clock::now() < stream.failedUntil_) return false;
        const auto needed = [&](int64_t k) {
            return !stream.table_[k].load(std::memory_order_relaxed) && !stream.loading_[static_cast<size_t>(k)];
        };
        while (from < first + count && !needed(from)) ++from;
        for (int64_t k = from; k < first + count && needed(k); ++k) {
            StreamPage* page = takePageLocked(stream.pins_[static_cast<size_t>(k)] > 0, blocking);
            if (!page) break;
            page->state = StreamPage::State::Loading;
            page->owner = &stream;
            page->index = k;
            stream.loading_[static_cast<size_t>(k)] = 1;
            taken[n++] = page;
        }
        if (n == 0) return true;
    }
    const bool ok = readPages(stream, from, n, taken, buffer);
    {
        std::lock_guard lock(mutex_);
        for (int64_t i = 0; i < n; ++i) {
            const int64_t k = from + i;
            stream.loading_[static_cast<size_t>(k)] = 0;
            StreamPage& page = *taken[i];
            if (ok) {
                page.state = StreamPage::State::InUse;
                page.wanted = pass_;  // (fresh: kept until the next pass)
                stream.table_[k].store(&page, std::memory_order_seq_cst);
            } else {
                freeLocked(page);
            }
        }
        if (ok) {
            ++reads_;
            pagesRead_ += static_cast<uint64_t>(n);
            stream.unreadable_.store(false, std::memory_order_relaxed);
        } else {
            stream.failedUntil_ = Clock::now() + kRetryAfter;
            stream.unreadable_.store(true, std::memory_order_relaxed);
        }
    }
    loaded_.notify_all();
    return ok;
}

bool StreamCache::loadBlocking(const StreamedAudio& stream, int64_t k) {
    {
        std::unique_lock lock(mutex_);
        loaded_.wait(lock, [&] { return !stream.loading_[static_cast<size_t>(k)]; });
        if (stream.table_[k].load(std::memory_order_relaxed)) return true;
    }
    platform::AlignedBuffer buffer;
    return load(const_cast<StreamedAudio&>(stream), k, kBlockingReadAhead, true, buffer);
}

void StreamCache::closed(StreamedAudio& stream) {
    std::lock_guard lock(mutex_);
    streams_.erase(std::remove(streams_.begin(), streams_.end(), &stream), streams_.end());
    // Nothing reads it any more (the audio thread neither: no snapshot holds its source): its pages are free.
    for (const auto& page : pages_) {
        if (page->owner == &stream && page->state == StreamPage::State::InUse) freeLocked(*page);
    }
    reclaimLocked();
}

std::vector<StreamCache::Want> StreamCache::plan(const std::vector<Want>& wanted) {
    std::vector<Want> requests;
    {
        std::lock_guard lock(mutex_);
        ++pass_;
        reclaimLocked();
        const auto now = Clock::now();
        std::unordered_set<uint64_t> seen;  // (a page wanted twice: by two clips, or the loop's two windows)
        const auto first = [&](const StreamedAudio* stream, int64_t page) {
            const uint64_t key = (reinterpret_cast<uintptr_t>(stream) * 0x9E3779B97F4A7C15ull) ^ static_cast<uint64_t>(page);
            return seen.insert(key).second;
        };
        size_t room = capacity_;
        for (const Want& want : wanted) {
            StreamedAudio& stream = *want.stream;
            if (want.page < 0 || want.page >= stream.pages_ || !first(&stream, want.page)) continue;
            const auto k = static_cast<size_t>(want.page);
            if (stream.pins_[k] == 0) {
                if (room == 0) continue;  // the horizon shrinks to what the pool holds
                --room;
            }
            if (StreamPage* page = stream.table_[want.page].load(std::memory_order_relaxed)) {
                page->wanted = pass_;
                continue;
            }
            if (stream.loading_[k] || now < stream.failedUntil_) continue;
            requests.push_back(want);
        }
        // Pinned pages not in memory: where playback may start at once.
        for (StreamedAudio* stream : streams_) {
            if (now < stream->failedUntil_) continue;
            std::shared_ptr<StreamedAudio> held;
            for (int64_t k = 0; k < stream->pages_; ++k) {
                if (stream->pins_[static_cast<size_t>(k)] == 0 || stream->loading_[static_cast<size_t>(k)] ||
                    stream->table_[k].load(std::memory_order_relaxed) || !first(stream, k))
                    continue;
                if (!held) held = stream->weak_from_this().lock();
                if (!held) break;  // (closing)
                requests.push_back({held, k, std::numeric_limits<int64_t>::max()});
            }
        }
    }
    return requests;
}

// ---------------------------------------------------------------------------
// Reports and housekeeping

std::vector<StreamMiss> StreamCache::takeMisses() {
    std::vector<StreamMiss> misses;
    std::lock_guard lock(mutex_);
    for (StreamedAudio* stream : streams_) {
        const uint64_t frames = stream->missed_.exchange(0, std::memory_order_relaxed);
        const bool unreadable = stream->unreadable_.load(std::memory_order_relaxed);
        const bool newlyUnreadable = unreadable && stream->missesTaken_ == 0;
        stream->missesTaken_ = unreadable ? 1 : 0;
        if (frames == 0 && !newlyUnreadable) continue;
        misses.push_back({stream->sourcePath_, frames, unreadable});
    }
    return misses;
}

StreamCache::Stats StreamCache::stats() {
    std::lock_guard lock(mutex_);
    Stats stats;
    stats.sources = streams_.size();
    stats.allocatedPages = pages_.size();
    stats.capacityPages = capacity_;
    stats.reads = reads_;
    stats.pagesRead = pagesRead_;
    stats.evicted = evicted_;
    for (const auto& page : pages_) {
        if (page->state != StreamPage::State::InUse) continue;
        ++stats.pagesInMemory;
        if (page->owner->pins_[static_cast<size_t>(page->index)] > 0) ++stats.pinnedPages;
    }
    return stats;
}

std::vector<std::shared_ptr<StreamedAudio>> StreamCache::streams() {
    std::vector<std::shared_ptr<StreamedAudio>> open;
    std::lock_guard lock(mutex_);
    for (StreamedAudio* stream : streams_) {
        if (auto held = stream->weak_from_this().lock()) open.push_back(std::move(held));
    }
    return open;
}

void StreamCache::trimFolder() {
    std::string folder;
    uint64_t budget = 0;
    std::unordered_set<std::string> inUse;
    {
        std::lock_guard lock(mutex_);
        folder = folder_.empty() ? defaultFolder() : folder_;
        budget = folderBudget_;
        for (const StreamedAudio* stream : streams_) {
            if (stream->layout_.paged) inUse.insert(platform::fileKey(stream->layout_.path));
            if (!stream->peakPath_.empty()) inUse.insert(platform::fileKey(stream->peakPath_));
        }
        for (const std::string& making : making_) inUse.insert(platform::fileKey(making));
    }
    struct File {
        std::filesystem::path path;
        uint64_t size;
        std::filesystem::file_time_type used;
    };
    std::vector<File> files;
    uint64_t total = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(platform::toPath(folder), error)) {
        std::error_code ignored;
        if (!entry.is_regular_file(ignored)) continue;
        const std::string extension = platform::fromPath(entry.path().extension());
        if (extension != kCacheFileExtension && extension != kPeakFileExtension) continue;
        const uint64_t size = entry.file_size(ignored);
        files.push_back({entry.path(), size, entry.last_write_time(ignored)});
        total += size;
    }
    if (total <= budget) return;
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.used < b.used; });
    for (const File& file : files) {
        if (total <= budget) break;
        if (inUse.contains(platform::fileKey(platform::fromPath(file.path)))) continue;
        std::error_code ignored;
        if (std::filesystem::remove(file.path, ignored)) total -= file.size;
    }
}

}  // namespace sub
