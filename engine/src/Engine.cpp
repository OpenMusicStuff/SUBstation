// The engine's lifetime, sources, transport, browser preview, audio threads and
// housekeeping. The rest of Engine is implemented by area: EngineDevice.cpp,
// EngineTracks.cpp, EngineInput.cpp, EngineChains.cpp, EngineSnapshot.cpp and
// EngineOffline.cpp.
#include "EditorHost.h"
#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

#include "platform/Paths.h"
#include "plugins/sandbox/PluginSandboxes.h"

namespace sub {

namespace {

std::mutex defaultStreamingMutex;
StreamingSettings defaultStreamingSettings;

}  // namespace

Engine::Engine()
    : midiDevices_([this](uint16_t port, const uint8_t* message, int size, int64_t hostTime) {
          midiInput(port, message, size, hostTime);
      }) {
    std::lock_guard lock(mutex_);
    streamCache_ = StreamCache::create();
    // A page the disk threads let go of is reused once no audio callback that may have read it still runs.
    streamCache_->setAudioEpochs(&callbacksBegun_, &audioEpoch_);
    warpCache_.setStreamCache(streamCache_);
    streaming_ = defaultStreaming();
    applyStreamingLocked();
    sandboxes_ = std::make_unique<sandbox::PluginSandboxes>();
    master_.chainId = addChainLocked(kMaster, 0);
    scheduler_ = std::make_unique<Scheduler>(defaultAudioThreads());
    renderer_.setScheduler(scheduler_.get());
    renderer_.prepare(sampleRate_);
    rebuildSnapshotLocked();
}

Engine::~Engine() {
    std::lock_guard lock(mutex_);
    if (RenderJob* job = std::exchange(job_, nullptr)) {  // its thread mustn't outlive the engine
        job->cancel();
        if (job->thread_.joinable()) job->thread_.join();
        job->engine_ = nullptr;
    }
    midiDevices_.closeAll();
    closeDeviceLocked();
    streamScheduler_.reset();  // (its threads end: what they hold goes with them)
}

std::string Engine::sourceKey(const std::string& path) { return platform::fileKey(path); }

// ---------------------------------------------------------------------------
// Sources

std::shared_ptr<AudioSource> Engine::loadSource(const std::string& path) { return loadSourceImpl(path, true); }

std::shared_ptr<AudioSource> Engine::decodeSource(const std::string& path) { return loadSourceImpl(path, false); }

std::shared_ptr<AudioSource> Engine::loadSourceImpl(const std::string& path, bool publish) {
    const std::string key = sourceKey(path);
    uint32_t rate = 0;
    bool streaming = false;
    {
        std::lock_guard lock(mutex_);
        rate = static_cast<uint32_t>(sampleRate_);
        streaming = streaming_.enabled;
        auto it = sources_.find(key);
        // Cached at the engine's rate, and kept as the settings would keep it now.
        if (it != sources_.end() && it->second->sampleRate() == rate &&
            it->second->resident() == !streamsLocked(it->second->duration()))
            return it->second;
    }
    // Decode (or open for streaming) without holding the lock; retry if the device changed rate meanwhile.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const bool streams = streaming && [&] {
            const AudioFileInfo info = AudioSource::probe(path);
            std::lock_guard lock(mutex_);
            return streamsLocked(info.duration);
        }();
        auto source = streams ? streamCache_->open(path, rate) : AudioSource::load(path, rate);
        std::lock_guard lock(mutex_);
        const auto current = static_cast<uint32_t>(sampleRate_);
        if (current != rate) {
            rate = current;
            continue;
        }
        auto& slot = sources_[key];
        if (slot && slot->sampleRate() == rate && slot->resident() == source->resident()) return slot;
        slot = std::move(source);
        if (!slot->resident()) ensureStreamSchedulerLocked();
        if (publish) {
            rebuildSnapshotLocked();  // clips waiting for this file become audible
        } else {
            snapshotStale_ = true;  // (they do when the decoder publishes)
        }
        return slot;
    }
    throw std::runtime_error("The sample rate kept changing while loading " + path);
}

std::shared_ptr<AudioSource> Engine::loadResidentSource(const std::string& path) {
    const std::string key = sourceKey(path);
    uint32_t rate = 0;
    bool enabled = false;
    {
        std::lock_guard lock(mutex_);
        enabled = streaming_.enabled;
    }
    if (!enabled) return loadSource(path);  // (in memory anyway: the clips' cache; decoded without the lock)
    if (const AudioFileInfo info = AudioSource::probe(path); [&] {
            std::lock_guard lock(mutex_);
            return !streamsLocked(info.duration);
        }()) {
        return loadSource(path);
    }
    {
        std::lock_guard lock(mutex_);
        rate = static_cast<uint32_t>(sampleRate_);
        if (auto it = sources_.find(key); it != sources_.end() && it->second->sampleRate() == rate && it->second->resident())
            return it->second;
        if (auto it = residentSources_.find(key); it != residentSources_.end() && it->second->sampleRate() == rate)
            return it->second;
    }
    auto source = AudioSource::load(path, rate);
    std::lock_guard lock(mutex_);
    auto& slot = residentSources_[key];
    if (slot && slot->sampleRate() == source->sampleRate()) return slot;
    slot = std::move(source);
    return slot;
}

std::shared_ptr<AudioSource> Engine::cachedSource(const std::string& path) {
    std::lock_guard lock(mutex_);
    auto it = sources_.find(sourceKey(path));
    return it == sources_.end() ? nullptr : it->second;
}

void Engine::cacheSource(std::shared_ptr<AudioSource> source) {
    std::lock_guard lock(mutex_);
    const auto rate = static_cast<uint32_t>(sampleRate_);
    if (!source || source->sampleRate() != rate) return;
    auto& slot = sources_[sourceKey(source->path())];
    if (slot && slot->sampleRate() == rate) return;  // (still cached, or decoded again since)
    slot = std::move(source);
    if (!slot->resident()) ensureStreamSchedulerLocked();
    rebuildSnapshotLocked();  // clips waiting for this file become audible
}

void Engine::releaseUnusedSources() {
    std::lock_guard lock(mutex_);
    std::unordered_set<std::string> used;
    for (const auto& track : tracks_) used.insert(track.clipKeys.begin(), track.clipKeys.end());
    std::erase_if(sources_, [&](const auto& entry) {
        return !used.contains(entry.first) && entry.second != previewHold_;
    });
    // Devices hold the samples they use themselves.
    std::erase_if(residentSources_, [](const auto& entry) { return entry.second.use_count() == 1; });
}

void Engine::reloadSourcesLocked() {
    std::unordered_map<std::string, std::shared_ptr<AudioSource>> reloaded;
    for (const auto& [key, source] : sources_) {
        try {
            if (source->resident()) {
                reloaded[key] = AudioSource::load(source->path(), static_cast<uint32_t>(sampleRate_));
            } else if (auto streamed = streamCache_->open(source->path(), static_cast<uint32_t>(sampleRate_), false)) {
                reloaded[key] = std::move(streamed);
            }
            // (A streamed file whose cache file at this rate isn't made yet is let go: whoever loads its
            // sources, the engine bridge, has it made again in the background, and its clips wait meanwhile.)
        } catch (const std::exception&) {
            // The file vanished or became unreadable; its clips go silent.
        }
    }
    sources_ = std::move(reloaded);
    residentSources_.clear();  // (the devices keep theirs at the rate they loaded them)
}

// ---------------------------------------------------------------------------
// Streaming

void Engine::setStreaming(const StreamingSettings& settings) {
    std::lock_guard lock(mutex_);
    streaming_ = settings;
    applyStreamingLocked();
}

StreamingSettings Engine::streaming() {
    std::lock_guard lock(mutex_);
    return streaming_;
}

void Engine::setDefaultStreaming(const StreamingSettings& settings) {
    std::lock_guard lock(defaultStreamingMutex);
    defaultStreamingSettings = settings;
}

StreamingSettings Engine::defaultStreaming() {
    std::lock_guard lock(defaultStreamingMutex);
    return defaultStreamingSettings;
}

void Engine::applyStreamingLocked() {
    streaming_.thresholdSeconds = std::max(0.0, streaming_.thresholdSeconds);
    streaming_.diskThreads = std::clamp(streaming_.diskThreads, 1, StreamScheduler::kMaxThreads);
    streamCache_->setPoolBytes(streaming_.poolBytes);
    streamCache_->setFolder(streaming_.cacheFolder);
    if (streamCache_->folderBudget() != streaming_.cacheBudgetBytes) streamCache_->setFolderBudget(streaming_.cacheBudgetBytes);
    if (streamScheduler_) {
        streamScheduler_->setThreads(streaming_.diskThreads);
        streamScheduler_->setHorizon(streaming_.horizonSeconds);
    }
}

void Engine::ensureStreamSchedulerLocked() {
    if (streamScheduler_) return;
    streamScheduler_ = std::make_unique<StreamScheduler>(streamCache_, shared_, streaming_.diskThreads);
    streamScheduler_->setHorizon(streaming_.horizonSeconds);
    streamScheduler_->setSnapshot(snapshotHold_);
    snapshotStale_ = true;  // (the next build pins its clips' starts)
}

std::vector<StreamMiss> Engine::takeStreamMisses() { return streamCache_->takeMisses(); }

StreamCache::Stats Engine::streamingStats() { return streamCache_->stats(); }

void Engine::waitForStreams() {
    StreamScheduler* scheduler = nullptr;
    {
        std::lock_guard lock(mutex_);
        publishPendingLocked();  // (its pins)
        scheduler = streamScheduler_.get();
    }
    if (scheduler) scheduler->waitIdle();
}

void Engine::setStreamReadHook(StreamCache::ReadHook hook) { streamCache_->setReadHook(std::move(hook)); }

// ---------------------------------------------------------------------------
// Transport

bool Engine::pushCommandLocked(const TransportCommand& command) {
    const bool pushed = shared_.commands.push(command);  // 256 slots; a full queue only drops UI requests
    serviceTransportIfIdleLocked();
    return pushed;
}

void Engine::serviceTransportIfIdleLocked() {
    // Without a running device nobody else consumes transport commands, so the
    // edit side applies them to keep the state consistent.
    if (deviceRunning_) return;
    const auto& snap = currentSnapshotLocked();
    if (!snap) return;
    renderer_.syncTempo(*snap);
    renderer_.drainCommands(shared_);
    renderer_.publishTransport(shared_);
    Renderer::discardPreviewNotes(shared_);  // nobody would hear them, and a stale note-on could hang
    discardMidiInputLocked();
}

void Engine::play(double countInBeats) {
    std::lock_guard lock(mutex_);
    if (streamScheduler_) streamScheduler_->wake();  // (playback may wait for the pages where it starts)
    requestedPlaying_.store(true);
    pushCommandLocked({TransportCommand::Type::Play, 0.0, std::max(0.0, countInBeats)});
}

void Engine::stop() {
    std::lock_guard lock(mutex_);
    requestedPlaying_.store(false);
    pushCommandLocked({TransportCommand::Type::Stop});
}

void Engine::setPositionBeats(double beat) {
    std::lock_guard lock(mutex_);
    beat = std::max(0.0, beat);
    if (streamScheduler_) streamScheduler_->locate(beat);  // its pages first
    // Said at once (positionBeats()) until the audio thread confirms it: a block it is
    // rendering from where it was publishes that position before it takes the locate.
    locateBeat_.store(beat);
    locatesPushed_.fetch_add(1);
    if (!pushCommandLocked({TransportCommand::Type::Locate, beat})) locatesPushed_.fetch_sub(1);
}

double Engine::positionBeats() const {
    if (shared_.locatesPublished.load(std::memory_order_acquire) < locatesPushed_.load(std::memory_order_acquire))
        return locateBeat_.load();
    return shared_.positionBeats.load(std::memory_order_relaxed);
}

void Engine::setTempo(double bpm) { setTempoMap(TempoMap(bpm)); }

double Engine::tempo() {
    std::lock_guard lock(mutex_);
    return tempoMap_->points().front().bpm;
}

void Engine::setTempoMap(const TempoMap& map) {
    std::lock_guard lock(mutex_);
    if (map == *tempoMap_) return;  // (the settings pushed again: nothing to build)
    tempoMap_ = std::make_shared<const TempoMap>(map);
    ++tempoVersion_;
    rebuildSnapshotLocked();
}

TempoMap Engine::tempoMap() {
    std::lock_guard lock(mutex_);
    return *tempoMap_;
}

void Engine::setTimeSignature(int numerator, int denominator) {
    if (!MeterMap::valid(numerator, denominator)) throw std::invalid_argument("Unsupported time signature");
    setMeter(MeterMap(numerator, denominator));
}

void Engine::setMeter(const MeterMap& meter) {
    std::lock_guard lock(mutex_);
    if (meter == *meter_) return;
    meter_ = std::make_shared<const MeterMap>(meter);
    rebuildSnapshotLocked();
}

MeterMap Engine::meter() {
    std::lock_guard lock(mutex_);
    return *meter_;
}

void Engine::setSongKey(int tonic, bool minor) {
    if (tonic < -1 || tonic > 11) throw std::invalid_argument("Unknown key");
    std::lock_guard lock(mutex_);
    if (tonic == keyTonic_ && (tonic < 0 || minor == keyMinor_)) return;
    keyTonic_ = tonic;
    keyMinor_ = tonic >= 0 && minor;
    rebuildSnapshotLocked();
}

void Engine::setLoop(bool enabled, double startBeat, double endBeat) {
    std::lock_guard lock(mutex_);
    const double start = std::max(0.0, startBeat);
    const double end = std::max(start, endBeat);
    if (enabled == loopEnabled_ && start == loopStartBeat_ && end == loopEndBeat_) return;
    loopEnabled_ = enabled;
    loopStartBeat_ = start;
    loopEndBeat_ = end;
    rebuildSnapshotLocked();
}

void Engine::setPunch(bool in, bool out) {
    std::lock_guard lock(mutex_);
    if (in == punchIn_ && out == punchOut_) return;
    punchIn_ = in;
    punchOut_ = out;
    rebuildSnapshotLocked();
}

void Engine::setMetronome(bool enabled) { shared_.metronome.store(enabled); }

void Engine::setClipFadeMs(double ms) {
    std::lock_guard lock(mutex_);
    clipFadeMs_ = std::clamp(ms, 0.0, 100.0);
    rebuildSnapshotLocked();
}

void Engine::waitForWarpRenders() { warpCache_.waitIdle(); }

// ---------------------------------------------------------------------------
// Preview

void Engine::preview(const std::string& path) {
    std::lock_guard lock(mutex_);
    auto it = sources_.find(sourceKey(path));
    if (it == sources_.end() || it->second->sampleRate() != static_cast<uint32_t>(sampleRate_)) {
        throw std::invalid_argument("Audio file is not loaded: " + path);
    }
    std::shared_ptr<const AudioSource> old = std::move(previewHold_);
    previewHold_ = it->second;
    if (streamScheduler_) streamScheduler_->setPreview(previewHold_->resident() ? nullptr : previewHold_);
    shared_.previewSource.store(previewHold_.get(), std::memory_order_seq_cst);
    shared_.previewActive.store(true);
    shared_.previewSerial.fetch_add(1, std::memory_order_seq_cst);
    releasePool_.retire(std::move(old), audioEpoch_.load(std::memory_order_seq_cst));
}

void Engine::stopPreview() {
    std::lock_guard lock(mutex_);
    if (streamScheduler_) streamScheduler_->setPreview(nullptr);
    std::shared_ptr<const AudioSource> old = std::move(previewHold_);
    shared_.previewSource.store(nullptr, std::memory_order_seq_cst);
    shared_.previewActive.store(false);
    shared_.previewSerial.fetch_add(1, std::memory_order_seq_cst);
    releasePool_.retire(std::move(old), audioEpoch_.load(std::memory_order_seq_cst));
}

// ---------------------------------------------------------------------------
// Audio threads

int Engine::defaultAudioThreads() {
    const auto cores = static_cast<int>(std::thread::hardware_concurrency());
    return std::clamp(cores - 1, 1, kMaxAudioThreads);
}

void Engine::setAudioThreads(int threads) {
    threads = std::clamp(threads, 1, kMaxAudioThreads);
    std::lock_guard lock(mutex_);
    if (scheduler_->threads() == threads) return;
    checkNotRenderingLocked();  // (a render in the background uses the scheduler)
    // The audio thread mustn't be inside the scheduler (or the renderer's
    // scratch) while they change: it outputs silence meanwhile. A recording goes
    // on (the playhead waits as well).
    const bool suspended = liveSuspended_.exchange(true, std::memory_order_seq_cst);
    if (!waitForCallbackLocked()) {  // (it may be in the scheduler: leave it be)
        if (!suspended) resumeLiveLocked();
        throw std::runtime_error(kAudioThreadBusy);
    }
    auto old = std::exchange(scheduler_, std::make_unique<Scheduler>(threads));
    scheduler_->setWorkgroup(device_.workgroup());
    renderer_.setScheduler(scheduler_.get());
    old.reset();  // its workers end
    if (!suspended) resumeLiveLocked();
}

int Engine::audioThreads() {
    std::lock_guard lock(mutex_);
    return scheduler_->threads();
}

uint64_t Engine::nodesOnWorkers() {
    std::lock_guard lock(mutex_);
    return scheduler_->nodesOnWorkers();
}

int Engine::realtimeWorkers() {
    std::lock_guard lock(mutex_);
    return scheduler_->realtimeWorkers();
}

void Engine::setCostOrdering(bool on) { renderer_.setCostOrdering(on); }  // offline renders copy it

void Engine::setPruneTrackRenders(bool on) {
    std::lock_guard lock(mutex_);
    pruneTrackRenders_ = on;
}

std::vector<TrackCost> Engine::trackCosts() {
    std::lock_guard lock(mutex_);
    std::vector<TrackCost> costs;
    costs.reserve(tracks_.size());
    for (const TrackModel& track : tracks_) {
        costs.push_back({track.id, track.buffers->cost.load(std::memory_order_relaxed)});
    }
    return costs;
}

// ---------------------------------------------------------------------------
// Housekeeping

void Engine::collectGarbageLocked() {
    releasePool_.collect(audioEpoch_.load(std::memory_order_seq_cst), !deviceRunning_);
}

void Engine::idle(bool releaseAll) {
    runEditorEvents();  // plug-in editors' windows, file descriptors and timers (Linux), until the application runs them
    std::vector<std::shared_ptr<Processor>> live;
    std::vector<std::shared_ptr<Processor>> dead;
    {
        std::lock_guard lock(mutex_);
        const bool rendering = job_ != nullptr;  // processors wait (a plug-in restarting would leave a gap in it)
        if (deviceRunning_ && (pendingDeviceEvents_.load() & static_cast<uint32_t>(DeviceEvent::Stopped))) {
            closeDeviceLocked();  // the backend lost the device; the UI reports it via takeDeviceEvent()
        }
        collectGarbageLocked();
        // Sources decoded since the last edit that nobody published (decodeSource()).
        if (!rendering && batchDepth_ == 0) publishPendingLocked();
        serviceTransportIfIdleLocked();
        if (!rendering) {
            for (const auto& [id, entry] : processors_) live.push_back(entry.processor);
        }
        // Removed processors that no snapshot holds any more.
        for (auto it = graveyard_.begin(); it != graveyard_.end();) {
            if (it->use_count() == 1) {
                dead.push_back(std::move(*it));
                it = graveyard_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Plug-ins may take their time to go: not under the lock, and a few at a
    // time; those left wait for the next call.
    constexpr auto kReleaseBudget = std::chrono::milliseconds(20);
    const auto started = std::chrono::steady_clock::now();
    size_t released = 0;
    while (released < dead.size() && (releaseAll || std::chrono::steady_clock::now() - started < kReleaseBudget)) {
        dead[released++].reset();
    }
    if (released < dead.size()) {
        std::lock_guard lock(mutex_);
        graveyard_.insert(graveyard_.begin(), std::make_move_iterator(dead.begin() + static_cast<std::ptrdiff_t>(released)),
                          std::make_move_iterator(dead.end()));
    }
    dead.clear();

    sandboxes_->idle();  // (unused ones quit)
    bool realign = false;
    for (const auto& p : live) realign |= p->idle();
    if (live.empty()) return;
    std::lock_guard lock(mutex_);
    // A processor's idle() compares its latency with what it last reported, which may not be what the
    // snapshot was aligned to (a change, another edit rebuilding the snapshot, then the change undone):
    // check against the snapshot too.
    for (auto it = processors_.begin(); !realign && it != processors_.end(); ++it) {
        realign = !it->second.rack && ownLatency(*it->second.processor) != it->second.alignedLatency;
    }
    if (realign && job_ == nullptr) rebuildSnapshotLocked();  // a latency changed: new delay compensation
}

}  // namespace sub
