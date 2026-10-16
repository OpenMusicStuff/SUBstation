#pragma once
// A render running on a thread of its own (Engine::startExport,
// Engine::startTracksRender): exporting the arrangement, freezing tracks. The
// UI polls its progress, may cancel it, and finishes it from the thread that
// started it, which gives live output back (it is silent meanwhile).
//
// The render takes the engine's snapshot as it was when it started (holding it,
// and so its processors, sources and buffers) and renders it without the
// engine's lock, so the UI's calls (meters, the playhead) don't wait for it.

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sub {

class AudioSource;
class Engine;

class RenderJob {
public:
    ~RenderJob();  // not finished: cancelled, and finished (its errors dropped)
    RenderJob(const RenderJob&) = delete;
    RenderJob& operator=(const RenderJob&) = delete;

    const std::string& path() const noexcept { return paths_.front(); }  // (its first file)
    const std::vector<std::string>& paths() const noexcept { return paths_; }  // the files it writes
    // How far it got: 0..1 of what it renders.
    double progress() const noexcept;
    // The thread rendering has ended (finish() returns at once).
    bool done() const noexcept { return rendered_.load(std::memory_order_acquire); }
    // Stops it soon. Its file goes (what was written of it).
    void cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }
    bool cancelled() const noexcept { return cancel_.load(std::memory_order_relaxed); }
    // Waits for the render, gives live output back, and returns the frames
    // written (to its first file): none (nullopt) if it was cancelled before the
    // end. Throws what went wrong (its files gone then too). From the thread that
    // started it.
    std::optional<int64_t> finish();
    // Once finished: the frames written to each of its files, in order (none if
    // it was cancelled or failed).
    const std::vector<int64_t>& written() const noexcept { return written_; }
    // Once finished, a render of tracks (Engine::startTracksRender): each file's
    // samples as decoding it would give them (AudioSource::fromSamples(), made on
    // the job's thread), so the caller needn't decode the file.
    const std::vector<std::shared_ptr<AudioSource>>& sources() const noexcept { return sources_; }

private:
    friend class Engine;
    RenderJob(Engine* engine, std::vector<std::string> paths, int64_t total)
        : engine_(engine), paths_(std::move(paths)), total_(total) {}
    // `body` renders on the job's thread: it reports its frames with advance(),
    // stops (returning nullopt) when stopping(), and returns the frames written
    // (to its first file; to each in written_, if it writes several).
    void start(std::function<std::optional<int64_t>(RenderJob&)> body);
    void advance(int64_t frames) noexcept { done_.fetch_add(frames, std::memory_order_relaxed); }
    bool stopping() const noexcept { return cancelled(); }
    void end();  // waits for the thread, and gives the engine back (once)

    Engine* engine_;  // null once ended (or once the engine went first)
    std::vector<std::string> paths_;
    int64_t total_;
    std::atomic<int64_t> done_{0};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> rendered_{false};
    std::optional<int64_t> frames_;
    std::vector<int64_t> written_;
    std::vector<std::shared_ptr<AudioSource>> sources_;
    std::exception_ptr error_;
    std::thread thread_;
};

}  // namespace sub
