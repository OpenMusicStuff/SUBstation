#pragma once
// The stream cache: the pages of streamed sources that are in memory
// (docs/engine/streaming.md). A streamed source (AudioSource::stream()) is a
// file on disk, read a page at a time (kStreamPageFrames frames of each
// channel) into a pool of pages; a table of atomic pointers, one per page of
// the file, says which are in memory. The audio thread loads an entry and reads
// the page, or finds it missing and plays silence there (faded at its edges):
// it never waits for the disk, never locks and never allocates.
//
// Pages come in on the disk threads (StreamScheduler.h), which ask for what the
// snapshot plays soon, and on readers off the audio thread that wait for them
// (offline renders, the warp cache's thread, the waveform at sample level:
// leases). They go least recently wanted first when the pool is full, never one
// pinned: the head of each file and the start of each clip (which playback
// starts from at once), nor one the disk threads want now. A page taken out of
// a table is reused only once no audio callback that may have read it is still
// running (the engine's epochs, as retired snapshots), and no lease holds it.
//
// What a source's frames are on disk, and the cache and peak files, are
// CacheFile.h's; the cache folder (and its budget) is this cache's.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "CacheFile.h"
#include "platform/StreamFile.h"

namespace sub {

class AudioSource;
class StreamCache;
class StreamedAudio;

// A page of a streamed source in memory: room for kStreamPageFrames frames of two channels.
struct StreamPage {
    enum class State : uint8_t { Free, Loading, InUse, Retired };

    std::unique_ptr<float[]> samples;  // channel c at c * kStreamPageFrames
    // Readers off the audio thread copying from it now (StreamLease): it isn't reused meanwhile.
    std::atomic<int> leases{0};
    // The cache's own, under its mutex.
    State state = State::Free;
    StreamedAudio* owner = nullptr;
    int64_t index = -1;
    uint64_t wanted = 0;     // the planner's pass that last wanted it (StreamCache::plan())
    uint64_t retiredAt = 0;  // the audio epoch (callbacks finished) when it was taken out of its table
};

// A page held for a reader off the audio thread (StreamedAudio::lease()): it
// stays as it is while the lease lives, even if the cache lets it go meanwhile.
class StreamLease {
public:
    StreamLease() noexcept = default;
    explicit StreamLease(const StreamPage* page) noexcept : page_(page) {}
    ~StreamLease() { release(); }
    StreamLease(StreamLease&& other) noexcept : page_(other.page_) { other.page_ = nullptr; }
    StreamLease& operator=(StreamLease&& other) noexcept {
        if (this != &other) {
            release();
            page_ = other.page_;
            other.page_ = nullptr;
        }
        return *this;
    }
    StreamLease(const StreamLease&) = delete;
    StreamLease& operator=(const StreamLease&) = delete;

    explicit operator bool() const noexcept { return page_ != nullptr; }
    const float* channel(uint32_t c) const noexcept { return page_->samples.get() + c * kStreamPageFrames; }

private:
    void release() noexcept {
        if (page_) const_cast<StreamPage*>(page_)->leases.fetch_sub(1, std::memory_order_seq_cst);
        page_ = nullptr;
    }
    const StreamPage* page_ = nullptr;
};

// One streamed file: where its frames are (its layout), and which of its pages
// are in memory. Made by StreamCache::open(); an AudioSource holds it.
class StreamedAudio : public std::enable_shared_from_this<StreamedAudio> {
public:
    // Frames this close to a page that isn't in memory are faded (the miss's edges).
    static constexpr int64_t kMissFade = 64;

    ~StreamedAudio();
    StreamedAudio(const StreamedAudio&) = delete;
    StreamedAudio& operator=(const StreamedAudio&) = delete;

    const StreamLayout& layout() const noexcept { return layout_; }
    uint32_t channels() const noexcept { return layout_.channels; }
    int64_t frames() const noexcept { return layout_.frames; }
    int64_t pages() const noexcept { return pages_; }
    const std::string& sourcePath() const noexcept { return sourcePath_; }
    uint32_t sampleRate() const noexcept { return sampleRate_; }
    StreamCache& cache() const noexcept { return *cache_; }

    // --- Real-time (the audio thread and its workers): never wait, lock or allocate.
    // Page `k`, if it is in memory.
    bool loaded(int64_t k) const noexcept { return table_[k].load(std::memory_order_seq_cst) != nullptr; }
    // Frames [start, start + n) of both sides as played (a mono file's one channel on both) into outL/outR.
    // Frames outside the file are silence; frames of pages not in memory are silence too, faded over
    // kMissFade frames at each edge, and counted as missed (`expected`: where misses are no problem,
    // the playhead dragged), the first such page noted for the disk threads. Returns the frames that were there.
    int64_t fetch(int64_t start, int64_t n, float* outL, float* outR, bool expected = false) const noexcept;
    // One frame of side `side` as fetch() has it (no counting: noteMissing() first).
    float sample(uint32_t side, int64_t frame) const noexcept;
    // Counts the frames of [start, start + n) not in memory as fetch() does, without reading them.
    int64_t noteMissing(int64_t start, int64_t n, bool expected) const noexcept;

    // --- Off the audio thread: these wait for the disk.
    // Page `k` held, read first if it isn't in memory; empty if the file can't be read.
    StreamLease lease(int64_t k) const;
    // fetch() waiting for every page (offline renders): the same frames, never missing.
    void fetchBlocking(int64_t start, int64_t n, float* outL, float* outR) const;
    // Frames [start, start + count) of channel `channel` (AudioSource::read()). False if the file
    // couldn't be read (those frames are 0).
    bool read(uint32_t channel, int64_t start, int64_t count, float* out) const;

    // What the audio thread found missing since the cache last took it (StreamCache::takeMisses()).
    uint64_t missedFrames() const noexcept { return missed_.load(std::memory_order_relaxed); }
    uint64_t expectedMisses() const noexcept { return expected_.load(std::memory_order_relaxed); }
    // The last page the audio thread found missing (-1: none since asked), taken by the disk threads.
    int64_t takeUrgent() const noexcept { return urgent_.exchange(-1, std::memory_order_relaxed); }
    // Whether a read of it failed lately (the file gone, or changed).
    bool unreadable() const noexcept { return unreadable_.load(std::memory_order_relaxed); }

private:
    friend class StreamCache;
    StreamedAudio(std::shared_ptr<StreamCache> cache, StreamLayout layout, uint32_t sampleRate, std::string sourcePath,
                  std::string peakPath, bool temporary);

    std::shared_ptr<StreamCache> cache_;
    StreamLayout layout_;
    uint32_t sampleRate_;
    std::string sourcePath_;
    std::string peakPath_;  // beside its cache file (none: "")
    bool temporary_;        // its file goes when it does (a stretched clip's render)
    int64_t pages_ = 0;
    std::unique_ptr<std::atomic<StreamPage*>[]> table_;

    mutable std::atomic<uint64_t> missed_{0};
    mutable std::atomic<uint64_t> expected_{0};
    mutable std::atomic<int64_t> urgent_{-1};
    mutable std::atomic<bool> unreadable_{false};

    // The cache's own, under its mutex: per page, whether it is on its way in, its pins (the head's,
    // the snapshot's), and the snapshot build that pinned it last.
    std::vector<uint8_t> loading_;
    std::vector<uint16_t> pins_;
    std::vector<uint64_t> pinnedIn_;
    std::vector<uint8_t> buildPinned_;
    std::chrono::steady_clock::time_point failedUntil_{};  // a failed read isn't tried again before this
    uint64_t missesTaken_ = 0;  // (reported unreadable already)

    // The file, opened by the first read.
    mutable std::mutex fileMutex_;
    mutable platform::StreamFile file_;
};

// What the audio thread found missing of a source since the last report (Engine::takeStreamMisses()).
struct StreamMiss {
    std::string path;          // the source's file
    uint64_t frames = 0;       // frames played as silence: the disk was late
    bool unreadable = false;   // reading it failed (it went away, or changed)
};

class StreamCache : public std::enable_shared_from_this<StreamCache> {
public:
    // The head of each file, and the start of each clip, stay in memory: this long.
    static constexpr double kHeadSeconds = 2.0;
    static constexpr size_t kPageBytes = 2 * kStreamPageFrames * sizeof(float);
    static constexpr size_t kDefaultPoolBytes = size_t{256} << 20;
    static constexpr uint64_t kDefaultFolderBudget = uint64_t{20} << 30;
    // A read that failed is tried again after this long.
    static constexpr std::chrono::milliseconds kRetryAfter{1000};

    static std::shared_ptr<StreamCache> create();
    ~StreamCache();
    StreamCache(const StreamCache&) = delete;
    StreamCache& operator=(const StreamCache&) = delete;

    // --- Settings (any thread).
    // How much memory the pages playback streams through may take (pinned pages come on top).
    void setPoolBytes(size_t bytes);
    size_t poolBytes() const;
    size_t capacityPages() const;
    // Where cache files and peak files go ("": defaultFolder()), and how much they may take there: the
    // least recently used go first past it.
    void setFolder(const std::string& folder);
    std::string folder() const;
    static std::string defaultFolder();  // the user's cache folder's SUBstation/Streaming
    void setFolderBudget(uint64_t bytes);
    uint64_t folderBudget() const;
    // The audio callbacks begun and finished (the engine's): a page taken out of a table is reused only
    // once no callback that may have read it still runs. Without them (tests), at once.
    void setAudioEpochs(const std::atomic<uint64_t>* begun, const std::atomic<uint64_t>* finished);
    // Called on the reading thread before each read from disk (tests slow the disk down with it).
    using ReadHook = std::function<void(const std::string& path, int64_t firstPage, int64_t pages)>;
    void setReadHook(ReadHook hook);

    // --- Opening (a decoding thread, never the audio thread: it reads, and may decode).
    // The file streamed at `sampleRate`: as it is (a WAV or AIFF at that rate), else from its cache file,
    // made first if there is none; its peaks from its peak file, made first if there is none. Its head
    // is read before it returns. Throws std::runtime_error if the file can't be read or decoded.
    // Without `mayMake`, null where a cache file or peak file would have to be made first.
    std::shared_ptr<AudioSource> open(const std::string& path, uint32_t sampleRate, bool mayMake = true);
    // A cache file written already (a stretched clip's render: WarpCache.h): streamed from `layout`,
    // its head read first; `temporary`: the file goes when the stream does.
    std::shared_ptr<StreamedAudio> openFile(const StreamLayout& layout, uint32_t sampleRate, const std::string& sourcePath,
                                            bool temporary);
    // A fresh name for a temporary file in the folder's renders/ (a stretched clip's render).
    std::string temporaryFile();

    // --- Pins (a snapshot build, under the engine's mutex): the pages playback may start in at once.
    // beginPins(), pin() for each region, endPins(): pins not asked for again are let go; the disk
    // threads read the new ones first.
    void beginPins();
    void pin(const AudioSource& source, int64_t from, int64_t to);  // frames [from, to)
    void endPins();

    // --- The disk threads (StreamScheduler.h).
    struct Want {
        std::shared_ptr<StreamedAudio> stream;
        int64_t page = 0;
        int64_t deadline = 0;  // samples until playback reaches it (-1: missed already)
    };
    // A pass of the planner over `wanted` (in the order wanted: earliest deadline first): the first
    // capacityPages() of them are marked wanted (the horizon shrinks to what the pool holds) and kept
    // from eviction until the next pass; pinned pages not in memory are added (missed and due ones
    // first). Returns those not in memory, not on their way and not failing, in order.
    std::vector<Want> plan(const std::vector<Want>& wanted);
    // Reads pages [first, first + count) of `stream` (those of them not in memory nor on their way:
    // the run from the first) into the pool, through `buffer` (grown if it must be), and publishes them.
    // `blocking` (a lease): its pages always come, past the pool's size if they must; else a full
    // pool of pages wanted now reads none. False if the file couldn't be read.
    bool load(StreamedAudio& stream, int64_t first, int64_t count, bool blocking, platform::AlignedBuffer& buffer);
    // Called (on whichever thread) when the disk threads have reading to do: new pins, a page read.
    void setWakeHook(std::function<void()> hook);

    // --- Reports and housekeeping.
    // What each source missed since the last call (and those that became unreadable).
    std::vector<StreamMiss> takeMisses();
    struct Stats {
        size_t sources = 0;        // streamed sources open
        size_t pagesInMemory = 0;  // in use (pinned or not)
        size_t pinnedPages = 0;
        size_t allocatedPages = 0;
        size_t capacityPages = 0;
        uint64_t reads = 0;        // reads from disk so far
        uint64_t pagesRead = 0;
        uint64_t evicted = 0;
    };
    Stats stats();
    // The streamed sources open now (the planner's urgent misses).
    std::vector<std::shared_ptr<StreamedAudio>> streams();
    // Deletes the least recently used cache and peak files past the folder's budget (never one in use).
    void trimFolder();

private:
    StreamCache() = default;
    friend class StreamedAudio;

    // Under mutex_.
    StreamPage* takePageLocked(bool pinned, bool blocking);
    void reclaimLocked();
    bool reclaimableLocked(const StreamPage& page) const;
    void retireLocked(StreamPage& page);
    void freeLocked(StreamPage& page);
    size_t unpinnedInUseLocked() const;
    bool loadBlocking(const StreamedAudio& stream, int64_t k);
    bool readPages(StreamedAudio& stream, int64_t first, int64_t count, StreamPage* const* pages,
                   platform::AlignedBuffer& buffer);
    void wake();
    void closed(StreamedAudio& stream);  // ~StreamedAudio
    bool loadHead(StreamedAudio& stream);
    void touch(const std::string& path);

    mutable std::mutex mutex_;
    std::condition_variable loaded_;  // a load finished (or failed)
    std::vector<std::unique_ptr<StreamPage>> pages_;
    std::vector<StreamPage*> free_;
    std::vector<StreamPage*> retired_;
    std::vector<StreamedAudio*> streams_;
    size_t capacity_ = kDefaultPoolBytes / kPageBytes;
    uint64_t pass_ = 1;
    uint64_t build_ = 0;
    uint64_t reads_ = 0, pagesRead_ = 0, evicted_ = 0;
    std::string folder_;
    uint64_t folderBudget_ = kDefaultFolderBudget;
    bool rendersCleared_ = false;
    std::set<std::string> making_;  // cache files being made (a second open waits for the first)
    std::condition_variable made_;
    const std::atomic<uint64_t>* begun_ = nullptr;
    const std::atomic<uint64_t>* finished_ = nullptr;
    ReadHook readHook_;
    std::function<void()> wakeHook_;
};

}  // namespace sub
