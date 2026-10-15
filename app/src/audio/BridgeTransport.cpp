// The transport (play, stop, locate, the metronome), previews (a file, a note),
// and polling: the playhead every kPositionPollMs; every kMeterPollMs, in this
// order, recording progress, the meters, the plug-ins (the engine's idle() and
// what they reported) and the device's events. None of these wait for a render
// in the background: it renders without the engine's lock.

#include "audio/BridgePrivate.h"

#include <algorithm>
#include <cmath>

namespace sub::app {

bool EngineBridge::isPlaying() const { return engine_.isPlaying(); }

void EngineBridge::play() {
    engine_.play();
    pollPosition();
}

void EngineBridge::stop() {
    engine_.stop();
    if (!d_->recording.isEmpty()) stopRecording();
    pollPosition();
}

void EngineBridge::locate(double beat) {
    engine_.setPositionBeats(std::max(0.0, beat));
    pollPosition();
}

double EngineBridge::position() const { return engine_.positionBeats(); }

bool EngineBridge::isCountingIn() const { return engine_.isCountingIn(); }

bool EngineBridge::metronome() const { return engine_.metronome(); }

void EngineBridge::setMetronome(bool enabled) {
    if (enabled == engine_.metronome()) return;
    engine_.setMetronome(enabled);
    Q_EMIT metronomeChanged(enabled);
}

void EngineBridge::previewFile(const QString& path) {
    const int request = ++d_->previewRequest;
    requestSource(path, [this, path, request] {
        if (request != d_->previewRequest) return;  // stopped, or another file previewed, while this one loaded
        try {
            engine_.preview(path.toStdString());
        } catch (const std::invalid_argument&) {
            // the device changed rate while loading; ignore this click
        }
    });
}

void EngineBridge::stopPreview() {
    ++d_->previewRequest;
    engine_.stopPreview();
}

void EngineBridge::previewNote(const QString& trackId, int pitch, int velocity) {
    if (const auto engineId = engineTrackId(trackId)) engine_.previewNote(*engineId, pitch, velocity);
}

void EngineBridge::pollPosition() {
    const double now = engine_.positionBeats();
    const double last = d_->lastPosition;
    // (As close as math.isclose(abs_tol=1e-9) has it: unmoved.)
    if (std::abs(now - last) > std::max(1e-9 * std::max(std::abs(now), std::abs(last)), 1e-9)) {
        d_->lastPosition = now;
        Q_EMIT positionChanged(now);
    }
    const bool playing = engine_.isPlaying();
    if (playing != d_->lastPlaying) {
        d_->lastPlaying = playing;
        Q_EMIT transportChanged(playing);
    }
    const bool countingIn = engine_.isCountingIn();
    if (countingIn != d_->lastCountingIn) {
        d_->lastCountingIn = countingIn;
        Q_EMIT countingInChanged(countingIn);
    }
}

void EngineBridge::pollMeters() {
    pollRecording();
    Private& d = *d_;
    QHash<quint32, QString> byEngineId;
    for (auto it = d.trackIds.constBegin(); it != d.trackIds.constEnd(); ++it) byEngineId.insert(it.value(), it.key());
    QHash<quint32, QString> byChain;
    for (auto it = d.rackOfChain.constBegin(); it != d.rackOfChain.constEnd(); ++it) {
        if (d.chains.contains(it.key())) byChain.insert(d.chains.value(it.key()), it.key());
    }
    for (const sub::MeterReading& reading : engine_.takeMeters()) {
        MeterLevel level;
        level.left = reading.left;
        level.right = reading.right;
        if (reading.chainId) {
            const auto chain = byChain.constFind(reading.chainId);
            if (chain != byChain.constEnd()) d.chainMeters.insert(*chain, level);
            continue;
        }
        const auto key = byEngineId.constFind(reading.trackId);
        if (key != byEngineId.constEnd()) d.meters.insert(*key, level);
    }
    QHash<quint32, QString> byProcessor;
    for (auto it = d.pids.constBegin(); it != d.pids.constEnd(); ++it) {
        if (it.value()) byProcessor.insert(*it.value(), it.key());
    }
    d.deviceMeters.clear();
    for (const sub::DeviceMeterReading& reading : engine_.takeDeviceMeters()) {
        const auto device = byProcessor.constFind(reading.processorId);
        if (device == byProcessor.constEnd()) continue;
        MeterLevel level;
        level.left = reading.left;
        level.right = reading.right;
        d.deviceMeters.insert(*device, level);
    }
    pollLatencies();
    Q_EMIT metersUpdated();
    pollPlugins();
    if (!d.busy) pollDevice();  // not from a message loop inside a plug-in's or driver's call
}

const QMap<QString, MeterLevel>& EngineBridge::meters() const { return d_->meters; }

const QMap<QString, MeterLevel>& EngineBridge::chainMeters() const { return d_->chainMeters; }

MeterLevel EngineBridge::trackMeter(const QString& trackId) const { return d_->meters.value(trackId); }

MeterLevel EngineBridge::deviceMeter(const QString& deviceId) const { return d_->deviceMeters.value(deviceId); }

int EngineBridge::trackLatency(const QString& trackId) const { return d_->trackLatencies.value(trackId, 0); }

int EngineBridge::outputLatency() const { return d_->outputLatency; }

void EngineBridge::pollLatencies() {
    Private& d = *d_;
    QMap<QString, int> latencies;
    for (auto it = d.trackIds.constBegin(); it != d.trackIds.constEnd(); ++it)
        latencies.insert(it.key(), engine_.trackLatency(it.value()));
    const int output = engine_.outputLatency();
    if (latencies == d.trackLatencies && output == d.outputLatency) return;
    d.trackLatencies = std::move(latencies);
    d.outputLatency = output;
    Q_EMIT latenciesChanged();
}

MeterLevel EngineBridge::chainMeter(const QString& chainId) const { return d_->chainMeters.value(chainId); }

QList<float> EngineBridge::takeInputMeters() {
    const std::vector<float> peaks = engine_.takeInputMeters();
    return QList<float>(peaks.begin(), peaks.end());
}

quint64 EngineBridge::scopeWritten() const { return engine_.masterScopeWritten(); }

QList<float> EngineBridge::scopeSamples(int frames) const {
    const std::vector<float> samples = engine_.masterScope(static_cast<size_t>(std::max(0, frames)));
    return QList<float>(samples.begin(), samples.end());
}

double EngineBridge::cpuLoad() const { return engine_.cpuLoad(); }

void EngineBridge::setMeasuringCpu(bool on) { engine_.setMeasuringCpu(on); }

EngineBridge::CpuTimes EngineBridge::takeCpu() {
    const Private& d = *d_;
    const sub::CpuReading reading = engine_.takeCpu();
    CpuTimes times;
    times.wallNs = reading.wallNs;
    QHash<quint32, QString> byEngineId;
    for (auto it = d.trackIds.constBegin(); it != d.trackIds.constEnd(); ++it) byEngineId.insert(it.value(), it.key());
    for (const auto& [engineId, ns] : reading.tracks) {
        const auto key = byEngineId.constFind(engineId);
        if (key != byEngineId.constEnd()) times.tracks.insert(*key, ns);
    }
    QHash<quint32, QString> byProcessor;
    for (auto it = d.pids.constBegin(); it != d.pids.constEnd(); ++it) {
        if (it.value()) byProcessor.insert(*it.value(), it.key());
    }
    for (const auto& [processor, ns] : reading.processors) {
        const auto device = byProcessor.constFind(processor);
        if (device != byProcessor.constEnd()) times.devices.insert(*device, ns);
    }
    return times;
}

double EngineBridge::sampleRate() const { return engine_.sampleRate(); }

}  // namespace sub::app
