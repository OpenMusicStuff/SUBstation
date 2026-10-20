#include "audio/AudioSettings.h"

#include "AudioDevice.h"  // sub::kDefaultDriver, sub::kSystemDriver, sub::AudioDevice::driverTypes

#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace sub::app {

namespace {

// Channel numbers saved as "2,3" (none if any of them isn't a number).
std::vector<int> channelsFrom(const QVariant& value) {
    std::vector<int> channels;
    for (const QString& part : value.toString().split(u',')) {
        if (part.trimmed().isEmpty()) continue;
        bool ok = false;
        const int channel = part.trimmed().toInt(&ok);
        if (!ok) return {};
        channels.push_back(channel);
    }
    return channels;
}

QString channelsText(const std::vector<int>& channels) {
    QStringList parts;
    for (int channel : channels) parts.append(QString::number(channel));
    return parts.join(u',');
}

int intValue(const QSettings& settings, const QString& key, int fallback) {
    bool ok = false;
    const int value = settings.value(key, fallback).toInt(&ok);
    return ok ? value : fallback;
}

}  // namespace

QString defaultDriver() { return QString::fromUtf8(sub::kDefaultDriver); }

QString systemDriver() { return QString::fromUtf8(sub::kSystemDriver); }

QStringList audioDrivers() {
#ifdef _WIN32
    return {defaultDriver(), QStringLiteral("ASIO")};
#else
    QStringList drivers;
    for (const std::string& driver : sub::AudioDevice::driverTypes()) drivers.append(QString::fromStdString(driver));
    return drivers;
#endif
}

AudioSettings AudioSettings::load() {
    const QSettings s;
    AudioSettings settings;
    const QString driver = s.value(QStringLiteral("audio/driver"), defaultDriver()).toString();
    settings.driver = audioDrivers().contains(driver) ? driver : defaultDriver();
    settings.deviceName = s.value(QStringLiteral("audio/device"), QString()).toString();
    settings.sampleRate = intValue(s, QStringLiteral("audio/sample_rate"), 0);
    settings.bufferFrames = intValue(s, QStringLiteral("audio/buffer_frames"), 256);
    settings.exclusive = s.value(QStringLiteral("audio/exclusive"), QStringLiteral("false")).toString().toLower() ==
                         u"true";
    settings.outputChannels = channelsFrom(s.value(QStringLiteral("audio/output_channels"), QString()));
    settings.inputChannels = channelsFrom(s.value(QStringLiteral("audio/input_channels"), QString()));
    return settings;
}

void AudioSettings::save() const {
    QSettings s;
    s.setValue(QStringLiteral("audio/driver"), driver);
    s.setValue(QStringLiteral("audio/device"), deviceName);
    s.setValue(QStringLiteral("audio/sample_rate"), sampleRate);
    s.setValue(QStringLiteral("audio/buffer_frames"), bufferFrames);
    s.setValue(QStringLiteral("audio/exclusive"), exclusive ? QStringLiteral("true") : QStringLiteral("false"));
    s.setValue(QStringLiteral("audio/output_channels"), channelsText(outputChannels));
    s.setValue(QStringLiteral("audio/input_channels"), channelsText(inputChannels));
}

int audioThreads() {
    bool ok = false;
    const int value = QSettings().value(kAudioThreadsKey, 0).toInt(&ok);
    return ok ? std::max(0, value) : 0;
}

void setAudioThreads(int threads) { QSettings().setValue(kAudioThreadsKey, std::max(0, threads)); }

bool midiImmediate() { return QSettings().value(kMidiImmediateKey, false).toBool(); }

void setMidiImmediate(bool immediate) {
    QSettings().setValue(kMidiImmediateKey, immediate ? QStringLiteral("true") : QStringLiteral("false"));
}

bool constrainLatency() { return QSettings().value(kConstrainLatencyKey, false).toBool(); }

void setConstrainLatency(bool on) {
    QSettings().setValue(kConstrainLatencyKey, on ? QStringLiteral("true") : QStringLiteral("false"));
}

double constrainThresholdMs() {
    bool ok = false;
    const double value = QSettings().value(kConstrainThresholdKey, kDefaultConstrainThresholdMs).toDouble(&ok);
    return ok && std::isfinite(value) ? std::clamp(value, 0.0, kMaxConstrainThresholdMs) : kDefaultConstrainThresholdMs;
}

void setConstrainThresholdMs(double ms) {
    QSettings().setValue(kConstrainThresholdKey, std::isfinite(ms) ? std::clamp(ms, 0.0, kMaxConstrainThresholdMs)
                                                                    : kDefaultConstrainThresholdMs);
}

StreamingPreferences StreamingPreferences::load() {
    const QSettings s;
    StreamingPreferences prefs;
    prefs.enabled = s.value(QStringLiteral("audio/streaming"), QStringLiteral("false")).toString().toLower() == u"true";
    bool ok = false;
    const double threshold = s.value(QStringLiteral("audio/stream_threshold_s"), kDefaultThresholdSeconds).toDouble(&ok);
    prefs.thresholdSeconds = ok && std::isfinite(threshold) ? std::clamp(threshold, 0.0, 3600.0) : kDefaultThresholdSeconds;
    prefs.memoryMb = std::clamp(intValue(s, QStringLiteral("audio/stream_memory_mb"), kDefaultMemoryMb), 16, 65536);
    prefs.cacheFolder = s.value(QStringLiteral("audio/stream_cache_folder"), QString()).toString();
    prefs.cacheGb = std::clamp(intValue(s, QStringLiteral("audio/stream_cache_gb"), kDefaultCacheGb), 1, 100000);
    prefs.diskThreads = std::clamp(intValue(s, QStringLiteral("audio/stream_disk_threads"), kDefaultDiskThreads), 1, 8);
    return prefs;
}

void StreamingPreferences::save() const {
    QSettings s;
    s.setValue(QStringLiteral("audio/streaming"), enabled ? QStringLiteral("true") : QStringLiteral("false"));
    s.setValue(QStringLiteral("audio/stream_threshold_s"), thresholdSeconds);
    s.setValue(QStringLiteral("audio/stream_memory_mb"), memoryMb);
    s.setValue(QStringLiteral("audio/stream_cache_folder"), cacheFolder);
    s.setValue(QStringLiteral("audio/stream_cache_gb"), cacheGb);
    s.setValue(QStringLiteral("audio/stream_disk_threads"), diskThreads);
}

QSet<QString> disabledMidiInputs() {
    const QVariant value = QSettings().value(kMidiDisabledKey);
    QSet<QString> names;
    // (QSettings gives a one-item list back as a string.)
    for (const QString& name : value.toStringList()) {
        if (!name.isEmpty()) names.insert(name);
    }
    return names;
}

void setMidiInputDisabled(const QString& name, bool disabled) {
    QSet<QString> names = disabledMidiInputs();
    if (disabled) {
        names.insert(name);
    } else {
        names.remove(name);
    }
    QStringList sorted(names.cbegin(), names.cend());
    sorted.sort();
    QSettings().setValue(kMidiDisabledKey, sorted);
}

const std::vector<std::pair<QString, double>>& recordQuantizeChoices() {
    static const std::vector<std::pair<QString, double>> choices{
        {QStringLiteral("No Quantization"), 0.0}, {QStringLiteral("1/4"), 1.0},
        {QStringLiteral("1/8"), 0.5},             {QStringLiteral("1/8 Triplet"), 1.0 / 3.0},
        {QStringLiteral("1/16"), 0.25},           {QStringLiteral("1/16 Triplet"), 1.0 / 6.0},
        {QStringLiteral("1/32"), 0.125}};
    return choices;
}

double recordQuantize() {
    bool ok = false;
    const double value = QSettings().value(kRecordQuantizeKey, 0.0).toDouble(&ok);
    if (!ok) return 0.0;
    const auto& choices = recordQuantizeChoices();
    const bool known = std::any_of(choices.begin(), choices.end(),
                                   [&](const auto& choice) { return std::abs(value - choice.second) < 1e-9; });
    return known ? value : 0.0;
}

void setRecordQuantize(double grid) { QSettings().setValue(kRecordQuantizeKey, grid); }

bool recordQuantizeEnds() { return QSettings().value(kRecordQuantizeEndsKey, false).toBool(); }

void setRecordQuantizeEnds(bool ends) { QSettings().setValue(kRecordQuantizeEndsKey, ends); }

}  // namespace sub::app
