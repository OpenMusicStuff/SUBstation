// Streaming long files from disk (docs/engine/streaming.md): the preferences
// pushed to the engine, and what the disk came late for: counted for the
// transport bar, and said in the status line (at most every kDiskLateSaidMs).

#include <QFileInfo>

#include <algorithm>
#include <cmath>

#include "audio/BridgePrivate.h"

namespace sub::app {

namespace {

constexpr int kDiskLateSaidMs = 2000;

sub::StreamingSettings engineSettings(const StreamingPreferences& prefs) {
    sub::StreamingSettings settings;
    settings.enabled = prefs.enabled;
    settings.thresholdSeconds = prefs.thresholdSeconds;
    settings.poolBytes = static_cast<size_t>(prefs.memoryMb) << 20;
    settings.cacheFolder = prefs.cacheFolder.toStdString();
    settings.cacheBudgetBytes = static_cast<uint64_t>(prefs.cacheGb) << 30;
    settings.diskThreads = prefs.diskThreads;
    return settings;
}

}  // namespace

void EngineBridge::applyStreaming() {
    engine_.setStreaming(engineSettings(StreamingPreferences::load()));
    Q_EMIT streamingChanged();
}

void EngineBridge::changeStreaming(const std::function<void(StreamingPreferences&)>& change) {
    StreamingPreferences prefs = StreamingPreferences::load();
    const StreamingPreferences before = prefs;
    change(prefs);
    if (prefs == before) return;
    prefs.save();
    guarded("streaming", [&] { applyStreaming(); });
    // What streams changed: the files in use are fetched again, each kept as the settings say now
    // (meanwhile their clips play on as they were).
    if (prefs.enabled != before.enabled || (prefs.enabled && prefs.thresholdSeconds != before.thresholdSeconds))
        refreshSources();
}

bool EngineBridge::streaming() const { return engine_.streaming().enabled; }

void EngineBridge::setStreaming(bool on) {
    changeStreaming([&](StreamingPreferences& prefs) { prefs.enabled = on; });
}

double EngineBridge::streamThreshold() const { return engine_.streaming().thresholdSeconds; }

void EngineBridge::setStreamThreshold(double seconds) {
    if (!std::isfinite(seconds)) return;
    changeStreaming([&](StreamingPreferences& prefs) { prefs.thresholdSeconds = std::clamp(seconds, 0.0, 3600.0); });
}

int EngineBridge::streamMemory() const { return static_cast<int>(engine_.streaming().poolBytes >> 20); }

void EngineBridge::setStreamMemory(int megabytes) {
    changeStreaming([&](StreamingPreferences& prefs) { prefs.memoryMb = std::clamp(megabytes, 16, 65536); });
}

QString EngineBridge::streamCacheFolder() const { return QString::fromStdString(engine_.streaming().cacheFolder); }

void EngineBridge::setStreamCacheFolder(const QString& folder) {
    // (The default one given back: none of the user's own.)
    const QString chosen = folder == defaultStreamCacheFolder() ? QString() : folder;
    changeStreaming([&](StreamingPreferences& prefs) { prefs.cacheFolder = chosen; });
}

QString EngineBridge::defaultStreamCacheFolder() { return QString::fromStdString(sub::StreamCache::defaultFolder()); }

int EngineBridge::streamCacheSize() const { return static_cast<int>(engine_.streaming().cacheBudgetBytes >> 30); }

void EngineBridge::setStreamCacheSize(int gigabytes) {
    changeStreaming([&](StreamingPreferences& prefs) { prefs.cacheGb = std::clamp(gigabytes, 1, 100000); });
}

int EngineBridge::streamDiskThreads() const { return engine_.streaming().diskThreads; }

void EngineBridge::setStreamDiskThreads(int threads) {
    changeStreaming([&](StreamingPreferences& prefs) { prefs.diskThreads = std::clamp(threads, 1, 8); });
}

int EngineBridge::diskLate() const { return d_->diskLate; }

double EngineBridge::diskLateMs() const { return d_->diskLateMs; }

void EngineBridge::pollStreams() {
    const std::vector<sub::StreamMiss> misses = engine_.takeStreamMisses();
    if (misses.empty()) return;
    const double rate = std::max(1.0, engine_.sampleRate());
    double lateMs = 0.0;
    QString late;
    for (const sub::StreamMiss& miss : misses) {
        const QString name = QFileInfo(QString::fromStdString(miss.path)).fileName();
        if (miss.unreadable) {
            // (A file that is gone is the File Manager's to list as missing.)
            Q_EMIT statusMessage(QStringLiteral("%1 can't be read from disk any more: it plays as silence").arg(name));
        }
        if (miss.frames > 0) {
            lateMs += static_cast<double>(miss.frames) * 1000.0 / rate;
            late = name;
        }
    }
    if (lateMs <= 0.0) return;
    ++d_->diskLate;
    d_->diskLateMs += lateMs;
    Q_EMIT diskLateChanged();
    if (d_->diskLateSaid.isValid() && d_->diskLateSaid.elapsed() < kDiskLateSaidMs) return;
    d_->diskLateSaid.start();
    Q_EMIT statusMessage(QStringLiteral("The disk was late: %1 ms of %2 played as silence (Preferences > Audio: "
                                        "streaming)")
                             .arg(QString::number(lateMs, 'f', 0), late));
}

}  // namespace sub::app
