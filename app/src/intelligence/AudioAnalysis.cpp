#include "intelligence/AudioAnalysis.h"

#include <QThread>

#include <algorithm>
#include <cmath>

#include "audio/AudioAnalysis.h"  // (the intelligence module's)
#include "audio/EngineBridge.h"
#include "audio/Waveform.h"
#include "model/Clip.h"
#include "model/Paths.h"

namespace sub::app {

namespace {

// A decoded file analysed (on the analysis thread: the source is immutable).
std::optional<AudioAnalysis::Result> analyseWaveform(const Waveform& waveform) {
    std::vector<const float*> channels;
    std::vector<std::vector<float>> read;  // (a file streamed from disk: read for the while)
    read.reserve(static_cast<size_t>(std::max(0, waveform.channels())));
    for (int c = 0; c < waveform.channels(); ++c) {
        const float* samples = waveform.channelData(c);
        if (!samples) samples = read.emplace_back(waveform.samples(c)).data();
        channels.push_back(samples);
    }
    if (channels.empty() || waveform.frames() <= 0 || waveform.sampleRate() <= 0) return std::nullopt;
    const intelligence::audio::Analysis heard = intelligence::audio::analyze(
        channels.data(), static_cast<int>(channels.size()), static_cast<std::size_t>(waveform.frames()),
        static_cast<double>(waveform.sampleRate()));
    AudioAnalysis::Result result;
    if (heard.tempo) {
        result.bpm = heard.tempo->bpm;
        result.tempoConfidence = heard.tempo->confidence;
        result.firstBeatSec = heard.tempo->firstBeatSec;
    }
    if (heard.key) result.key = Key{heard.key->tonic, heard.key->minor};
    for (const auto& span : heard.chords) result.chords.push_back({span.start, span.end, span.chord});
    return result;
}

}  // namespace

AudioAnalysis::AudioAnalysis(EngineBridge* bridge, QObject* parent) : QObject(parent), bridge_(bridge) {
    pool_.setMaxThreadCount(1);
    pool_.setThreadPriority(QThread::LowPriority);
    if (bridge) {
        connect(bridge, &EngineBridge::sourceFailed, this, [this](const QString& path) {
            const QString key = pathIdentity(path);
            if (waiting_.contains(key)) finish(key, std::nullopt);
        });
    }
}

AudioAnalysis::~AudioAnalysis() {
    *alive_ = false;
    pool_.waitForDone();
}

void AudioAnalysis::waitForDone() { pool_.waitForDone(); }

const AudioAnalysis::Result* AudioAnalysis::result(const QString& path) const {
    const auto it = results_.constFind(pathIdentity(path));
    return it == results_.constEnd() ? nullptr : &*it;
}

bool AudioAnalysis::isPending(const QString& path) const { return waiting_.contains(pathIdentity(path)); }

void AudioAnalysis::request(const QString& path, std::function<void(const Result*)> then) {
    const QString key = pathIdentity(path);
    if (const Result* known = result(path)) {
        if (then) then(known);
        return;
    }
    if (failed_.contains(key) || !enabled_) {
        if (then) then(nullptr);
        return;
    }
    const bool started = waiting_.contains(key);
    if (then) waiting_[key].push_back(std::move(then));
    else waiting_[key];  // (pending all the same)
    if (!started) start(path);
}

void AudioAnalysis::start(const QString& path) {
    const QString key = pathIdentity(path);
    if (analyser_) {
        finish(key, analyser_(path));
        return;
    }
    if (!bridge_) {
        finish(key, std::nullopt);
        return;
    }
    // Decoded first (the bridge's pool), then analysed here, in the background.
    bridge_->requestSource(path, [this, path, key, alive = alive_] {
        if (!*alive || !bridge_) return;
        const Waveform waveform = bridge_->waveform(path);
        if (waveform.isNull()) {
            finish(key, std::nullopt);
            return;
        }
        pool_.start([this, waveform, key, alive] {
            std::optional<Result> result = analyseWaveform(waveform);
            QMetaObject::invokeMethod(
                this,
                [this, key, alive, result = std::move(result)]() mutable {
                    if (*alive) finish(key, std::move(result));
                },
                Qt::QueuedConnection);
        });
    });
}

void AudioAnalysis::finish(const QString& key, std::optional<Result> result) {
    const Result* stored = nullptr;
    if (result) {
        stored = &*results_.insert(key, std::move(*result));
    } else {
        failed_.insert(key);
    }
    const auto callbacks = waiting_.take(key);
    for (const auto& callback : callbacks) callback(stored);
    if (stored) Q_EMIT analysed(key);
}

int AudioAnalysis::clipShift(const Clip& clip, double tempo) {
    if (clip.isWarped() && clip.warpMode == QStringLiteral("Re-Pitch") && clip.segmentBpm > 0 && tempo > 0)
        return static_cast<int>(std::lround(12.0 * std::log2(tempo / clip.segmentBpm)));
    return clip.transpose + static_cast<int>(std::lround(clip.detune / 100.0));
}

std::vector<AudioAnalysis::ClipChord> AudioAnalysis::clipChords(const Result& result, const Clip& clip, double tempo) {
    std::vector<ClipChord> chords;
    if (!clip.isAudio()) return chords;
    const int shift = clipShift(clip, tempo);
    const double from = clip.offsetSec, to = clip.offsetSec + clip.durationSec;
    for (const Chord& c : result.chords) {
        const double start = std::max(from, c.start), end = std::min(to, c.end);
        if (end <= start) continue;
        intelligence::harmony::Chord chord = c.chord;
        chord.root = intelligence::harmony::pitchClass(chord.root + shift);
        if (chord.bass >= 0) chord.bass = intelligence::harmony::pitchClass(chord.bass + shift);
        chords.push_back({clip.startBeat + clip.sourceToBeats(start - from, tempo),
                          clip.startBeat + clip.sourceToBeats(end - from, tempo), chord});
    }
    return chords;
}

QVariantMap AudioAnalysis::fileInfo(const QString& path) const {
    const Result* r = result(path);
    return {{QStringLiteral("bpm"), r && r->bpm ? *r->bpm : 0.0},
            {QStringLiteral("key"), r && r->key ? r->key->label() : QString()},
            {QStringLiteral("analysed"), r != nullptr},
            {QStringLiteral("pending"), isPending(path)}};
}

}  // namespace sub::app
