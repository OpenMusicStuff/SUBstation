// The audio device: opening it (WASAPI or ASIO; PipeWire on Linux, the
// system's backend elsewhere), resetting it when its driver asks, its control
// panel, and what happens to it (stopped and back again, rerouted).

#include "audio/BridgePrivate.h"

#include <algorithm>

namespace sub::app {

namespace {

template <typename T>
QList<int> intList(const std::vector<T>& values) {
    QList<int> list;
    for (const T value : values) list.append(static_cast<int>(value));
    return list;
}

}  // namespace

QStringList EngineBridge::driverTypes() { return stringList(sub::Engine::driverTypes()); }

QList<AudioDeviceEntry> EngineBridge::listDevices(const QString& driver) {
    QList<AudioDeviceEntry> entries;
    try {
        for (const sub::AudioDeviceInfo& info : engine_.devices(driver.toStdString())) {
            AudioDeviceEntry entry;
            entry.name = QString::fromStdString(info.name);
            entry.isDefault = info.isDefault;
            entries.append(entry);
        }
    } catch (const std::exception&) {
        return {};  // (a driver type that can't list its devices: none)
    }
    return entries;
}

AudioDeviceStatus EngineBridge::deviceStatus() const {
    const sub::DeviceStatus status = engine_.deviceStatus();
    AudioDeviceStatus out;
    out.open = status.open;
    out.name = QString::fromStdString(status.name);
    out.backend = QString::fromStdString(status.backend);
    out.sampleRate = static_cast<int>(status.sampleRate);
    out.bufferFrames = static_cast<int>(status.bufferFrames);
    out.latencyMs = status.latencyMs;
    out.inputLatencyMs = status.inputLatencyMs;
    out.inputChannels = intList(status.inputChannels);
    out.outputChannels = intList(status.outputChannels);
    out.exclusive = status.exclusive;
    out.dropouts = static_cast<qint64>(status.dropouts);
    return out;
}

AudioDeviceCaps EngineBridge::deviceCapabilities() const {
    const sub::DeviceCaps caps = engine_.deviceCapabilities();
    AudioDeviceCaps out;
    out.inputNames = stringList(caps.inputNames);
    out.outputNames = stringList(caps.outputNames);
    out.sampleRates = intList(caps.sampleRates);
    out.bufferSizes = intList(caps.bufferSizes);
    out.preferredBufferFrames = static_cast<int>(caps.preferredBufferFrames);
    out.hasControlPanel = caps.hasControlPanel;
    return out;
}

QString EngineBridge::openDevice(const AudioSettings& settings) {
    sub::DeviceConfig config;
    config.driver = settings.driver.toStdString();
    config.name = settings.deviceName.toStdString();
    config.sampleRate = static_cast<uint32_t>(std::max(0, settings.sampleRate));
    config.bufferFrames = static_cast<uint32_t>(std::max(0, settings.bufferFrames));
    config.exclusive = settings.exclusive;
    config.inputChannels = settings.inputChannels;
    config.outputChannels = settings.outputChannels;
    config.window = d_->ownerWindow();  // ASIO drivers own their dialogs by it
    const QString error = changeDevice([&] { engine_.openDevice(config); });
    if (error.isEmpty()) {
        d_->opened = settings;
        d_->lost.reset();  // (another device is open now)
    }
    return error;
}

QString EngineBridge::resetDevice() {
    const QString error = changeDevice([&] { engine_.reopenDevice(); });
    if (!error.isEmpty()) {
        Q_EMIT statusMessage(QStringLiteral("The audio device could not restart: ") + error +
                             QStringLiteral(". Choose a device in Options > Preferences."));
    } else {
        Q_EMIT statusMessage(QStringLiteral("The audio driver restarted with its new settings."));
    }
    return error;
}

bool EngineBridge::showDeviceControlPanel() {
    const BusyScope busy(d_->busy);  // its dialog may run a message loop that calls us back
    try {
        return engine_.showDeviceControlPanel();
    } catch (const std::exception& error) {
        qWarning("EngineBridge (control panel): %s", error.what());
    }
    return false;
}

QString EngineBridge::changeDevice(const std::function<void()>& change) {
    const double oldRate = engine_.sampleRate();
    QString error;
    {
        const BusyScope busy(d_->busy);  // a driver may show a dialog while it opens
        try {
            change();
        } catch (const std::exception& failure) {
            error = QString::fromStdString(failure.what());
            if (error.isEmpty()) error = QStringLiteral("The audio device could not be opened");
        }
    }
    if (engine_.sampleRate() != oldRate) refreshSources();
    Q_EMIT deviceChanged();
    return error;
}

void EngineBridge::closeDevice() {
    engine_.closeDevice();
    Q_EMIT deviceChanged();
    if (isRecording()) stopRecording();
}

bool EngineBridge::startAudio() {
    guarded("audio threads", [&] { applyAudioThreads(); });
    guarded("Constrain Delay Compensation", [&] { applyConstrainLatency(); });
    applyMidiImmediate();
    openMidiInputs();
    const AudioSettings settings = AudioSettings::load();
    const QString error = openDevice(settings);
    if (error.isEmpty()) return true;
    // The device may not take the saved settings any more (an ASIO driver
    // locked to another clock), or be gone: its own settings, then the system default.
    AudioSettings own = settings;
    own.sampleRate = 0;
    own.bufferFrames = 0;
    own.outputChannels.clear();
    own.inputChannels.clear();
    AudioSettings system;
    system.bufferFrames = settings.bufferFrames;
    std::vector<std::pair<AudioSettings, QString>> fallbacks{
        {own, QStringLiteral("Using the device's own settings instead.")},
        {system, QStringLiteral("Using the system default output instead.")},
    };
    // Where PipeWire doesn't run: miniaudio's choice of the system's backends.
    if (defaultDriver() != systemDriver()) {
        AudioSettings miniaudio = system;
        miniaudio.driver = systemDriver();
        fallbacks.emplace_back(miniaudio, QStringLiteral("Using the %1 driver instead.").arg(systemDriver()));
    }
    for (const auto& [fallback, note] : fallbacks) {
        if (fallback != settings && openDevice(fallback).isEmpty()) {
            Q_EMIT statusMessage(error + QStringLiteral(". ") + note);
            return true;
        }
    }
    Q_EMIT statusMessage(QStringLiteral("Audio is off: ") + error +
                         QStringLiteral(". Choose a device in Options > Preferences."));
    return false;
}

// What happened to the device: one event a poll (not while rendering in the
// background: the device can't be reopened then, so the event waits).
void EngineBridge::pollDevice() {
    if (engine_.isRendering()) return;
    const std::string event = engine_.takeDeviceEvent();
    if (event.empty() && d_->lost) {
        lookForLostDevice();
    } else if (event == "stopped") {
        // Looked for from now on: it opens again when it is back (plugged in again, the sound server restarted).
        d_->lost = d_->opened;
        d_->lostChecked.start();
        Q_EMIT statusMessage(QStringLiteral("The audio device stopped. Choose a device in Options > Preferences."));
        Q_EMIT deviceChanged();
    } else if (event == "rerouted") {
        Q_EMIT statusMessage(QStringLiteral("Audio output was rerouted to another device."));
        Q_EMIT deviceChanged();
    } else if (event == "reset") {
        resetDevice();
    } else if (event == "latency") {
        Q_EMIT deviceChanged();
    }
}

// The device that went away, once a second: opened again when its driver
// lists it again (or, the default device, any).
void EngineBridge::lookForLostDevice() {
    if (engine_.deviceStatus().open) {
        d_->lost.reset();
        return;
    }
    if (d_->lostChecked.isValid() && d_->lostChecked.elapsed() < kLostDeviceInterval) return;
    d_->lostChecked.start();
    const AudioSettings lost = *d_->lost;
    const QList<AudioDeviceEntry> devices = listDevices(lost.driver);
    const bool back = lost.deviceName.isEmpty()
                          ? !devices.isEmpty()
                          : std::any_of(devices.begin(), devices.end(),
                                        [&](const AudioDeviceEntry& entry) { return entry.name == lost.deviceName; });
    if (!back || !openDevice(lost).isEmpty()) return;
    const QString name = QString::fromStdString(engine_.deviceStatus().name);
    Q_EMIT statusMessage(QStringLiteral("The audio device is back: %1.").arg(name));
}

}  // namespace sub::app
