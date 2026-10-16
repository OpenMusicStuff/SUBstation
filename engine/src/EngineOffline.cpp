// Engine: offline rendering, exporting (WAV, MP3), and rendering one track
// (freezing it), in the caller's thread or in the background (RenderJob.h).
#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "AudioFileWriter.h"
#include "AudioSource.h"
#include "MiniaudioFiles.h"
#include "miniaudio.h"
#include "plugins/sandbox/PluginSandboxes.h"

namespace sub {
namespace {

// Runs a callable when the scope ends (used to resume live output).
template <typename F>
class ScopeExit {
public:
    explicit ScopeExit(F f) : f_(std::move(f)) {}
    ~ScopeExit() { f_(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    F f_;
};

// Frames rendered at a time in the background: a cancel is heard this soon.
constexpr int64_t kJobChunk = 4096;

// Renders `frames` that lag the timeline and drops them, as many at a time as
// `scratch` (stereo) holds: render(out, n) for each piece.
template <typename Render>
void dropFrames(int64_t frames, std::vector<float>& scratch, Render&& render) {
    const auto chunk = static_cast<int64_t>(scratch.size() / 2);
    while (frames > 0) {
        const int64_t n = std::min(chunk, frames);
        render(scratch.data(), n);
        frames -= n;
    }
}

// A stereo WAV file being written. Unless kept, it is deleted when this goes:
// a render cancelled, or failed, leaves nothing behind.
class WavWriter final : public AudioFileWriter {
public:
    WavWriter(std::string path, ma_format format, double sampleRate) : AudioFileWriter(std::move(path)), format_(format) {
        ma_encoder_config config =
            ma_encoder_config_init(ma_encoding_format_wav, format, 2, static_cast<ma_uint32>(sampleRate));
        if (initEncoderFile(path_, config, &encoder_) != MA_SUCCESS) {
            throw std::runtime_error("Could not create " + path_);
        }
    }
    ~WavWriter() override {
        close();
        deleteUnlessKept();
    }
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    // Interleaved float frames, in the file's format (dithered to 16 bits). A
    // short write (a full disk) throws: the file would be shorter than the render.
    void write(const float* samples, int64_t frames) override {
        const void* data = samples;
        if (format_ != ma_format_f32) {
            converted_.resize(static_cast<size_t>(frames) * 2 * ma_get_bytes_per_sample(format_));
            ma_pcm_convert(converted_.data(), format_, samples, ma_format_f32, static_cast<ma_uint64>(frames * 2),
                           format_ == ma_format_s16 ? ma_dither_mode_triangle : ma_dither_mode_none);
            data = converted_.data();
        }
        ma_uint64 written = 0;
        if (ma_encoder_write_pcm_frames(&encoder_, data, static_cast<ma_uint64>(frames), &written) != MA_SUCCESS ||
            written != static_cast<ma_uint64>(frames)) {
            throw std::runtime_error("Could not write " + path_);
        }
    }
    void keep() override {  // done: the file stays
        close();
        kept_ = true;
    }

private:
    void close() {
        if (open_) ma_encoder_uninit(&encoder_);
        open_ = false;
    }

    ma_format format_;
    ma_encoder encoder_{};
    bool open_ = true;
    std::vector<uint8_t> converted_;
};

// Freezing: a track's render on its way into its file. Its lag (what feeds it
// and its devices' latency) is dropped, the range written as it comes, and the
// tail kept back until it is known where it falls below -100 dB: it ends after
// its last frame above that. The render stops early once the track has been
// silent long enough past the range (`quietEnough`, quietWindow()) and nothing
// that feeds it starts again later (`settled`, frames from the range's start);
// had it gone on, that tail would have been cut there just the same. What it
// writes it keeps too, planar, for the source decoding the file would make.
class TrackCapture {
public:
    TrackCapture(std::shared_ptr<AudioFileWriter> writer, std::string path, int64_t lag, int64_t frames, int64_t tail,
                 int64_t quietEnough, int64_t settled)
        : writer_(std::move(writer)), path_(std::move(path)), lag_(lag), frames_(frames), tail_(tail),
          quietEnough_(quietEnough), settled_(settled) {}

    // The render's next `n` frames (interleaved stereo).
    void take(const float* rendered, int64_t n) {
        int64_t i = std::clamp<int64_t>(lag_ - taken_, 0, n);  // (its lag: dropped)
        taken_ += n;
        int64_t at = taken_ - n + i - lag_;  // where frame i falls, from the range's start
        if (const int64_t inRange = std::clamp<int64_t>(frames_ - at, 0, n - i); inRange > 0) {
            write(rendered + i * 2, inRange);
            for (const int64_t end = i + inRange; i < end; ++i) quiet_ = silent(rendered + i * 2) ? quiet_ + 1 : 0;
            at += inRange;
        }
        const int64_t inTail = std::clamp<int64_t>(frames_ + tail_ - at, 0, n - i);
        if (inTail > 0 && tailSamples_.empty()) tailSamples_.reserve(static_cast<size_t>(std::min(tail_, kJobChunk * 64)) * 2);
        tailSamples_.insert(tailSamples_.end(), rendered + i * 2, rendered + (i + inTail) * 2);
        for (int64_t f = 0; f < inTail; ++f) {
            const bool quiet = silent(rendered + (i + f) * 2);
            quiet_ = quiet ? quiet_ + 1 : 0;
            if (!quiet) tailKept_ = at - frames_ + f + 1;
        }
        at += inTail;
        done_ = at >= frames_ + tail_ ||
                (quietEnough_ >= 0 && at >= std::max(frames_, settled_) && quiet_ >= quietEnough_);
    }
    // Nothing it renders from here on is kept.
    bool done() const noexcept { return done_; }
    // The tail as far as it sounds, and the file kept: the frames written.
    int64_t finish() {
        if (tailKept_ > 0) write(tailSamples_.data(), tailKept_);
        writer_->keep();
        return frames_ + tailKept_;
    }
    // Once finished: what the file holds, as decoding it would give it (made here,
    // on the job's thread, instead of on the caller's: it needn't read the file).
    std::shared_ptr<AudioSource> source(double sampleRate) {
        left_.insert(left_.end(), right_.begin(), right_.end());
        right_ = {};
        return AudioSource::fromSamples(path_, static_cast<uint32_t>(sampleRate), 2, std::move(left_));
    }

private:
    void write(const float* samples, int64_t n) {
        writer_->write(samples, n);
        if (left_.empty()) {
            // Room for all it may keep (the range and the whole tail), and in the
            // left channel's for the right channel too: source() never grows it.
            left_.reserve(static_cast<size_t>(frames_ + tail_) * 2);
            right_.reserve(static_cast<size_t>(frames_ + tail_));
        }
        for (int64_t f = 0; f < n; ++f) {
            left_.push_back(samples[f * 2]);
            right_.push_back(samples[f * 2 + 1]);
        }
    }

    static bool silent(const float* frame) noexcept {
        constexpr float kSilence = 1e-5f;  // -100 dB
        return std::abs(frame[0]) < kSilence && std::abs(frame[1]) < kSilence;
    }

    std::shared_ptr<AudioFileWriter> writer_;
    std::string path_;
    std::vector<float> left_, right_;  // what it wrote
    int64_t lag_, frames_, tail_;
    int64_t quietEnough_;  // -1: the whole tail is rendered
    int64_t settled_;
    int64_t taken_ = 0;  // frames of the render so far
    int64_t quiet_ = 0;  // frames it has been silent for, up to there
    std::vector<float> tailSamples_;
    int64_t tailKept_ = 0;  // the tail up to its last frame above -100 dB
    bool done_ = false;
};

// How long a track's render must have stayed silent past the range before it is
// taken to have ended (TrackCapture): at least kQuietSeconds, and as long as
// every device that feeds it, its own and those of the tracks upstream, says it
// rings on (Processor::tailSamples(), plus its latency), all added up, so the
// silence between a delay's echoes isn't taken for its end. -1 (the whole tail
// is rendered) where something may sound again after any silence: a MIDI effect
// (an arpeggiator holding its notes plays on), a plug-in that declares no tail
// (many don't, delays among them). On the main thread (tails are asked there).
constexpr double kQuietSeconds = 1.0;

int64_t quietWindow(const RenderSnapshot& snap, const std::vector<char>& upstream) {
    int64_t window = std::llround(kQuietSeconds * snap.sampleRate);
    bool unknown = false;
    const std::function<void(const StripRender&)> add = [&](const StripRender& strip) {
        for (size_t i = 0; i < strip.inserts.size(); ++i) {
            const Processor& insert = *strip.inserts[i];
            if (insert.isMidiEffect()) unknown = true;
            const int tail = insert.tailSamples();
            if (tail <= 0 && !insert.typeId().starts_with("builtin:") && insert.typeId() != "rack") unknown = true;
            window += std::max(0, insert.latencySamples()) + std::max(0, tail);
            if (i < strip.racks.size() && strip.racks[i]) {
                for (const ChainRender& chain : strip.racks[i]->chains) add(chain);
            }
        }
    };
    for (size_t t = 0; t < upstream.size(); ++t) {
        if (upstream[t]) add(snap.tracks[t]);
    }
    return unknown ? -1 : window;
}

// Where the last clip or note that starts before `to` ends (timeline samples),
// at most `to`; 0 if none does: on these tracks, and on those whose notes a
// device on them takes (a MidiFeed: no edge, so not upstream, yet it plays them).
int64_t lastSounding(const RenderSnapshot& snap, const std::vector<char>& upstream, int64_t to) {
    std::vector<char> sounding = upstream;
    std::vector<const MidiFeed*> feeds;
    const std::function<void(const StripRender&)> add = [&](const StripRender& strip) {
        for (const auto& feed : strip.midiFeeds) {
            if (feed) feeds.push_back(feed.get());
        }
        for (const auto& rack : strip.racks) {
            if (!rack) continue;
            for (const ChainRender& chain : rack->chains) add(chain);
        }
    };
    for (size_t t = 0; t < upstream.size(); ++t) {
        if (upstream[t]) add(snap.tracks[t]);
    }
    for (const MidiFeedRender& feed : snap.midiFeeds) {
        if (feed.source >= 0 && std::find(feeds.begin(), feeds.end(), feed.feed.get()) != feeds.end())
            sounding[static_cast<size_t>(feed.source)] = 1;
    }
    int64_t last = 0;
    for (size_t t = 0; t < sounding.size(); ++t) {
        if (!sounding[t]) continue;
        for (const ClipRender& clip : snap.tracks[t].clips) {
            if (clip.start < to) last = std::max(last, std::min(to, clip.start + clip.length));
        }
        for (const NoteRender& note : snap.tracks[t].notes) {
            if (note.start < to) last = std::max(last, std::min(to, note.end));
        }
    }
    return last;
}

// A writer on a thread of its own (an MP3 export's: LAME's encoding is a fifth
// of the export's time on the render's thread, which goes on rendering
// meanwhile). The render hands it each piece (a copy; at most kQueued wait, then
// the render waits for the encoder); it writes them in order, as `inner` would
// have. What went wrong comes back from the next write() or keep(). Let go of
// unkept, it drops what waits, and `inner` deletes its file.
class WriterThread final : public AudioFileWriter {
public:
    explicit WriterThread(std::shared_ptr<AudioFileWriter> inner, std::string path)
        : AudioFileWriter(std::move(path)), inner_(std::move(inner)), thread_([this] { run(); }) {}
    ~WriterThread() override {
        {
            const std::lock_guard lock(mutex_);
            stop_ = true;
        }
        changed_.notify_all();
        thread_.join();
    }
    WriterThread(const WriterThread&) = delete;
    WriterThread& operator=(const WriterThread&) = delete;

    void write(const float* samples, int64_t frames) override {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return queue_.size() < kQueued || error_; });
        if (error_) std::rethrow_exception(error_);
        std::vector<float> piece = std::move(spare_);
        piece.assign(samples, samples + frames * 2);
        queue_.push_back(std::move(piece));
        changed_.notify_all();
    }
    void keep() override {  // once all of it is written
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return (queue_.empty() && !writing_) || error_; });
        if (error_) std::rethrow_exception(error_);
        inner_->keep();
    }

private:
    static constexpr size_t kQueued = 2;

    void run() {
        std::unique_lock lock(mutex_);
        for (;;) {
            changed_.wait(lock, [this] { return stop_ || (!queue_.empty() && !error_); });
            if (stop_) return;
            std::vector<float> piece = std::move(queue_.front());
            queue_.pop_front();
            writing_ = true;
            lock.unlock();
            std::exception_ptr error;
            try {
                inner_->write(piece.data(), static_cast<int64_t>(piece.size() / 2));
            } catch (...) {
                error = std::current_exception();
            }
            lock.lock();
            writing_ = false;
            if (error) error_ = error;
            spare_ = std::move(piece);
            changed_.notify_all();
        }
    }

    std::shared_ptr<AudioFileWriter> inner_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::vector<float>> queue_;
    std::vector<float> spare_;  // a piece written, its memory for the next
    bool writing_ = false;
    bool stop_ = false;
    std::exception_ptr error_;
    std::thread thread_;  // (last: it starts once the rest is made)
};

ma_format exportFormat(int bitDepth) {
    switch (bitDepth) {
        case 16: return ma_format_s16;
        case 24: return ma_format_s24;
        case 32: return ma_format_f32;
        default: throw std::invalid_argument("Bit depth must be 16, 24 or 32");
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Offline rendering

void Engine::suspendLiveLocked() {
    finishRecordingLocked();  // its input would have a hole
    if (deviceRunning_) {
        liveSuspended_.store(true, std::memory_order_seq_cst);
        // Once the epoch advances, any callback that started before the flag was
        // visible has finished; later callbacks output silence.
        waitForCallbackLocked();
    }
    // Offline renders hear every device, bypassed live or not (setConstrainLatency()).
    if (!constrainHeld_) {
        constrainHeld_ = true;
        if (constrainLatency_) rebuildSnapshotLocked();
    }
}

void Engine::resumeLiveLocked() { liveSuspended_.store(false, std::memory_order_seq_cst); }

void Engine::resetProcessorsLocked(bool rendering) {
    // Offline renders share the processors with live playback. Resetting them
    // before keeps live notes (and a plug-in's reverb tail) out of the render;
    // after, keeps the render's last notes from hanging in live playback.
    for (auto& [id, entry] : processors_) {
        entry.processor->resetOffline(rendering);
        entry.processor->requestReset();
    }
}

void Engine::checkNotRenderingLocked() const {
    if (job_ != nullptr) throw std::runtime_error("Wait for the render to finish");
}

void Engine::prepareOfflineLocked(const RenderSnapshot& snap, Renderer& offline, OfflineLines& lines,
                                  double startBeat) {
    // Fresh stretchers, as many as live playback has, so an offline render
    // starts from a clean state and leaves the live voices alone. Likewise
    // delay-compensation lines.
    for (int c = 0; c < kNumStretchConfigs; ++c) {
        for (size_t i = 0; i < snap.warpVoices[c].size(); ++i) {
            lines.voices[c].push_back(std::make_shared<WarpVoice>(static_cast<StretchConfig>(c), sampleRate_));
        }
    }
    const auto line = [](int samples) { return samples > 0 ? std::make_shared<DelayLine>(samples + 1) : nullptr; };
    for (const EdgeRender& edge : snap.edges) {
        lines.delays.push_back(line(edge.compensation));
        lines.deviceDelays.push_back(line(edge.deviceDelay));
    }
    for (const int samples : snap.chainDelays) lines.chainDelays.push_back(line(samples));
    for (const int samples : snap.switchDelays) lines.switchDelays.push_back(line(samples));
    offline.setScheduler(scheduler_.get());
    offline.setCostOrdering(renderer_.costOrdering());
    offline.prepare(sampleRate_);
    offline.setWarpVoices(&lines.voices);
    offline.setDelayLines(&lines.delays, &lines.deviceDelays, &lines.chainDelays, &lines.switchDelays);
    offline.syncTempo(snap);
    offline.setPosition(std::llround(std::max(0.0, startBeat) * snap.samplesPerBeat()));
    offline.setPlaying(true);
}

void Engine::renderOfflineLocked(double startBeat, int64_t frames, float* out, bool loop, bool metronome) {
    checkNotRenderingLocked();
    ScopedNoDenormals noDenormals;
    suspendLiveLocked();
    resetProcessorsLocked(true);
    ScopeExit resume([this] { endOfflineLocked(); });
    const std::shared_ptr<const RenderSnapshot> snap = currentSnapshotLocked();
    Renderer offline;
    OfflineLines lines;
    prepareOfflineLocked(*snap, offline, lines, startBeat);
    // With delay compensation the output lags the timeline: render the lag first and drop it.
    if (const int64_t lag = snap->outputLatency(); lag > 0) {
        std::vector<float> discarded(static_cast<size_t>(lag) * 2);
        offline.renderOffline(*snap, discarded.data(), lag, loop, metronome, frames > 0);
    }
    offline.renderOffline(*snap, out, frames, loop, metronome);
}

std::vector<float> Engine::renderOffline(double startBeat, int64_t frames, bool loop, bool metronome) {
    if (frames < 0) throw std::invalid_argument("frames must be >= 0");
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    std::lock_guard lock(mutex_);
    renderOfflineLocked(startBeat, frames, out.data(), loop, metronome);
    return out;
}

void Engine::exportWav(const std::string& path, double startBeat, double endBeat, int bitDepth) {
    startExport(path, startBeat, endBeat, bitDepth)->finish();
}

void Engine::exportFile(const std::string& path, double startBeat, double endBeat, const ExportFormat& format) {
    startExport(path, startBeat, endBeat, format)->finish();
}

// ---------------------------------------------------------------------------
// One track (freezing)

void Engine::renderTrackLocked(uint32_t trackId, double startBeat, int64_t frames,
                               const std::function<void(const float*, int64_t)>& sink) {
    checkNotRenderingLocked();
    arrangementTrackLocked(trackId);  // (throws for an unknown track)
    ScopedNoDenormals noDenormals;
    suspendLiveLocked();  // (before the snapshot is taken: it may make a new one)
    resetProcessorsLocked(true);
    ScopeExit resume([this] { endOfflineLocked(); });
    const std::shared_ptr<const RenderSnapshot> held = currentSnapshotLocked();
    const RenderSnapshot& snap = *held;
    const TrackRender* found = snap.findTrack(trackId);
    if (!found) throw std::invalid_argument("Unknown track id " + std::to_string(trackId));
    const int track = static_cast<int>(found - snap.tracks.data());
    Renderer offline;
    OfflineLines lines;
    prepareOfflineLocked(snap, offline, lines, startBeat);
    offline.setCaptures(snap, {track}, !pruneTrackRenders_);
    // Its signal before its fader lags the timeline by what feeds it (a group's
    // bus hears its tracks that late) and its own devices: render that first and drop it.
    constexpr int64_t kChunk = 16384;
    std::vector<float> rendered(kChunk * 2);
    dropFrames(found->inputLatency + found->latency, rendered,
               [&](float* out, int64_t n) { offline.renderCaptures(snap, &out, n, true); });
    for (int64_t done = 0; done < frames;) {
        const int64_t n = std::min(kChunk, frames - done);
        float* out = rendered.data();
        offline.renderCaptures(snap, &out, n, done + n < frames);
        sink(out, n);
        done += n;
    }
}

std::vector<float> Engine::renderTrackOffline(uint32_t trackId, double startBeat, int64_t frames) {
    if (frames < 0) throw std::invalid_argument("frames must be >= 0");
    std::vector<float> out;
    out.reserve(static_cast<size_t>(frames) * 2);
    std::lock_guard lock(mutex_);
    renderTrackLocked(trackId, startBeat, frames,
                      [&out](const float* samples, int64_t n) { out.insert(out.end(), samples, samples + n * 2); });
    return out;
}

int64_t Engine::renderTrackToWav(uint32_t trackId, const std::string& path, double startBeat, double endBeat,
                                 double tailSeconds) {
    return startTrackRender(trackId, path, startBeat, endBeat, tailSeconds)->finish().value_or(0);
}

std::vector<int64_t> Engine::renderTracksToWav(const std::vector<TrackFile>& tracks, double startBeat, double endBeat,
                                               double tailSeconds) {
    const std::shared_ptr<RenderJob> job = startTracksRender(tracks, startBeat, endBeat, tailSeconds);
    job->finish();
    return job->written();
}

// ---------------------------------------------------------------------------
// In the background

std::shared_ptr<Engine::OfflineRender> Engine::beginOfflineLocked(double startBeat) {
    suspendLiveLocked();
    try {
        resetProcessorsLocked(true);
        auto render = std::make_shared<OfflineRender>();
        render->snapshot = currentSnapshotLocked();
        prepareOfflineLocked(*render->snapshot, render->renderer, render->lines, startBeat);
        return render;
    } catch (...) {
        endOfflineLocked();
        throw;
    }
}

void Engine::endOfflineLocked() {
    resetProcessorsLocked(false);
    if (constrainHeld_) {
        constrainHeld_ = false;
        if (constrainLatency_) rebuildSnapshotLocked();
    }
    resumeLiveLocked();
}

std::shared_ptr<RenderJob> Engine::startJobLocked(std::vector<std::string> paths, int64_t total,
                                                  std::function<std::optional<int64_t>(RenderJob&)> body) {
    std::shared_ptr<RenderJob> job(new RenderJob(this, std::move(paths), total));
    try {
        job->start(std::move(body));
    } catch (...) {
        job->engine_ = nullptr;  // (nothing to give back: the render ends here)
        endOfflineLocked();
        throw;
    }
    job_ = job.get();
    sandboxes_->holdIdle(true);  // (in sandboxes too: a plug-in restarting would leave a gap in the render)
    return job;
}

bool Engine::isRendering() {
    std::lock_guard lock(mutex_);
    return job_ != nullptr;
}

void Engine::endJob(RenderJob& job) {
    std::lock_guard lock(mutex_);
    if (job_ == &job) job_ = nullptr;
    sandboxes_->holdIdle(false);
    endOfflineLocked();
}

std::shared_ptr<RenderJob> Engine::startExport(const std::string& path, double startBeat, double endBeat,
                                               int bitDepth) {
    return startExport(path, startBeat, endBeat, ExportFormat{ExportFormat::Kind::Wav, bitDepth});
}

std::shared_ptr<RenderJob> Engine::startExport(const std::string& path, double startBeat, double endBeat,
                                               const ExportFormat& format) {
    if (endBeat <= startBeat) throw std::invalid_argument("Export range is empty");
    const bool mp3 = format.kind == ExportFormat::Kind::Mp3;
    const ma_format wavFormat = mp3 ? ma_format_unknown : exportFormat(format.bitDepth);
    if (mp3) checkMp3Bitrate(format.bitrate);
    std::lock_guard lock(mutex_);
    checkNotRenderingLocked();
    std::shared_ptr<AudioFileWriter> writer;
    if (mp3) {
        writer = std::make_shared<WriterThread>(makeMp3Writer(path, sampleRate_, format.bitrate), path);
    } else {
        writer = std::make_shared<WavWriter>(path, wavFormat, sampleRate_);
    }
    auto render = beginOfflineLocked(startBeat);
    const int64_t total = std::llround((endBeat - startBeat) * render->snapshot->samplesPerBeat());
    return startJobLocked({path}, total, [render, writer, total](RenderJob& job) -> std::optional<int64_t> {
        ScopedNoDenormals noDenormals;
        const RenderSnapshot& snap = *render->snapshot;
        std::vector<float> rendered(kJobChunk * 2);
        // With delay compensation the output lags the timeline: render the lag first and drop it.
        // (Each piece but the last followed by the next: the workers spin for it while this one is written.)
        dropFrames(snap.outputLatency(), rendered,
                   [&](float* out, int64_t n) { render->renderer.renderOffline(snap, out, n, false, false, true); });
        for (int64_t done = 0; done < total;) {
            if (job.stopping()) return std::nullopt;
            const int64_t n = std::min(kJobChunk, total - done);
            render->renderer.renderOffline(snap, rendered.data(), n, false, false, done + n < total);
            writer->write(rendered.data(), n);
            done += n;
            job.advance(n);
        }
        writer->keep();
        return total;
    });
}

std::shared_ptr<RenderJob> Engine::startTrackRender(uint32_t trackId, const std::string& path, double startBeat,
                                                    double endBeat, double tailSeconds) {
    return startTracksRender({{trackId, path}}, startBeat, endBeat, tailSeconds);
}

std::shared_ptr<RenderJob> Engine::startTracksRender(const std::vector<TrackFile>& files, double startBeat,
                                                     double endBeat, double tailSeconds) {
    if (files.empty()) throw std::invalid_argument("No track to render");
    if (endBeat <= startBeat) throw std::invalid_argument("Render range is empty");
    std::lock_guard lock(mutex_);
    checkNotRenderingLocked();
    for (size_t i = 0; i < files.size(); ++i) {
        arrangementTrackLocked(files[i].trackId);  // (throws for an unknown track)
        for (size_t j = 0; j < i; ++j) {
            if (files[j].trackId == files[i].trackId)
                throw std::invalid_argument("Track " + std::to_string(files[i].trackId) + " is rendered twice");
            if (files[j].path == files[i].path)
                throw std::invalid_argument("Two tracks rendered to " + files[i].path);
        }
    }
    std::vector<std::shared_ptr<AudioFileWriter>> writers;
    std::vector<std::string> paths;
    for (const TrackFile& file : files) {
        writers.push_back(std::make_shared<WavWriter>(file.path, ma_format_f32, sampleRate_));
        paths.push_back(file.path);
    }
    auto render = beginOfflineLocked(startBeat);
    const RenderSnapshot& snap = *render->snapshot;
    std::vector<int> tracks;  // by snapshot index
    for (const TrackFile& file : files) {
        const TrackRender* found = snap.findTrack(file.trackId);
        if (!found) {
            endOfflineLocked();
            throw std::invalid_argument("Unknown track id " + std::to_string(file.trackId));
        }
        tracks.push_back(static_cast<int>(found - snap.tracks.data()));
    }
    const bool whole = !pruneTrackRenders_;
    render->renderer.setCaptures(snap, tracks, whole);
    const int64_t frames = std::llround((endBeat - startBeat) * snap.samplesPerBeat());
    const int64_t tail = std::llround(std::clamp(tailSeconds, 0.0, 600.0) * sampleRate_);
    const int64_t start = render->renderer.position();
    auto captures = std::make_shared<std::vector<TrackCapture>>();
    int64_t longest = 0;  // the longest lag
    for (size_t i = 0; i < tracks.size(); ++i) {
        // Its signal before its fader lags the timeline by what feeds it (a group's
        // bus hears its tracks that late) and its own devices: that is rendered first and dropped.
        const TrackRender& track = snap.tracks[static_cast<size_t>(tracks[i])];
        const int64_t lag = track.inputLatency + track.latency;
        const std::vector<char> upstream = Renderer::upstream(snap, {tracks[i]});
        captures->emplace_back(writers[i], files[i].path, lag, frames, tail, quietWindow(snap, upstream),
                               lastSounding(snap, upstream, start + frames + tail) - start);
        longest = std::max(longest, lag);
    }
    return startJobLocked(std::move(paths), frames + tail, [render, captures, tracks, whole, longest, frames, tail,
                                                            rate = sampleRate_](RenderJob& job) -> std::optional<int64_t> {
        ScopedNoDenormals noDenormals;
        const RenderSnapshot& snap = *render->snapshot;
        const size_t count = tracks.size();
        std::vector<float> rendered(static_cast<size_t>(kJobChunk) * 2 * count);
        std::vector<float*> out(count);
        std::vector<size_t> rendering(count);  // the captures still rendering, as setCaptures() was given them
        for (size_t i = 0; i < count; ++i) rendering[i] = i;
        const int64_t total = longest + frames + tail;
        int64_t reported = 0;  // (progress: of the range and tail, the lag counted in)
        const auto report = [&](int64_t done) {
            const int64_t upTo = std::min(done, frames + tail);
            job.advance(upTo - reported);
            reported = upTo;
        };
        for (int64_t done = 0; done < total && !rendering.empty();) {
            if (job.stopping()) return std::nullopt;
            const int64_t n = std::min(kJobChunk, total - done);
            for (size_t k = 0; k < rendering.size(); ++k) out[k] = rendered.data() + k * kJobChunk * 2;
            render->renderer.renderCaptures(snap, out.data(), n, done + n < total);
            for (size_t k = 0; k < rendering.size(); ++k) (*captures)[rendering[k]].take(out[k], n);
            done += n;
            report(done);
            // Those done render no more, nor what only they need.
            const auto finished = [&](size_t i) { return (*captures)[i].done(); };
            if (std::any_of(rendering.begin(), rendering.end(), finished)) {
                rendering.erase(std::remove_if(rendering.begin(), rendering.end(), finished), rendering.end());
                std::vector<int> still;
                for (const size_t i : rendering) still.push_back(tracks[i]);
                if (!still.empty()) render->renderer.setCaptures(snap, still, whole);
            }
        }
        std::vector<int64_t> written;
        for (TrackCapture& capture : *captures) written.push_back(capture.finish());
        std::vector<std::shared_ptr<AudioSource>> sources;
        for (TrackCapture& capture : *captures) sources.push_back(capture.source(rate));
        report(total);  // (what is left is known to be silent)
        job.written_ = written;
        job.sources_ = std::move(sources);
        return written.front();
    });
}

// ---------------------------------------------------------------------------
// RenderJob

void RenderJob::start(std::function<std::optional<int64_t>(RenderJob&)> body) {
    thread_ = std::thread([this, body = std::move(body)]() mutable {
        try {
            frames_ = body(*this);
        } catch (...) {
            error_ = std::current_exception();
        }
        body = nullptr;  // what it holds goes now (an unfinished file is deleted)
        rendered_.store(true, std::memory_order_release);
    });
}

double RenderJob::progress() const noexcept {
    if (total_ <= 0) return done() ? 1.0 : 0.0;
    return std::min(1.0, static_cast<double>(done_.load(std::memory_order_relaxed)) / static_cast<double>(total_));
}

void RenderJob::end() {
    if (thread_.joinable()) thread_.join();
    if (Engine* engine = std::exchange(engine_, nullptr)) engine->endJob(*this);
}

std::optional<int64_t> RenderJob::finish() {
    end();
    if (error_) std::rethrow_exception(std::exchange(error_, nullptr));
    return frames_;
}

RenderJob::~RenderJob() {
    cancel();
    try {
        end();
    } catch (...) {
    }
}

}  // namespace sub
