#pragma once
// Persistent audio and MIDI preferences (QSettings), per user, under the
// application's organisation and name ("SUBstation"; the tests use "SUBstation
// Tests"):
//
//   audio/driver           the driver type: the default one (sub::kDefaultDriver:
//                          "WASAPI" on Windows, "PipeWire" on Linux),
//                          "ASIO" or "System"                     AudioSettings
//   audio/device           device or driver name; "": the system default (WASAPI),
//                          the first driver (ASIO)
//   audio/sample_rate      0: the rate the device runs at
//   audio/buffer_frames    0: the device's preferred size; default 256
//   audio/exclusive        WASAPI exclusive mode ("true" / "false")
//   audio/output_channels  "2,3": the pair the master plays on; "": the first two
//   audio/input_channels   ASIO inputs to open; none until something records
//   audio/threads          render threads; 0: the engine's default   audioThreads()
//   audio/constrain_latency   Constrain Delay Compensation ("true" / "false")   constrainLatency()
//   audio/constrain_threshold_ms   the latency it bypasses devices over; default 3   constrainThresholdMs()
//   audio/streaming        stream long files from disk ("true" / "false")   StreamingPreferences
//   audio/stream_threshold_s   files at least this long stream; default 30
//   audio/stream_memory_mb     what streaming's pages may take; default 256
//   audio/stream_cache_folder  where decoded files go; "": the user's cache folder's SUBstation/Streaming
//   audio/stream_cache_gb      what they may take there; default 20
//   audio/stream_disk_threads  reads at once: 1 for a spinning disk, more for an SSD; default 2
//   midi/disabled_inputs   MIDI inputs turned off                     disabledMidiInputs()
//   midi/immediate         MIDI input played at once ("true" / "false")   midiImmediate()
//   record/quantize        record quantization grid in beats; 0: as played

#include <QSet>
#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace sub::app {

// The driver type a build plays through by default (sub::kDefaultDriver):
// "WASAPI" on Windows, "PipeWire" on Linux (built with it), miniaudio's default
// backend ("System") elsewhere.
QString defaultDriver();
// miniaudio's, which every build has (sub::kSystemDriver): "WASAPI" on
// Windows, "System" elsewhere (after PipeWire, where it doesn't run).
QString systemDriver();
// The driver types the preferences offer: WASAPI and ASIO (Windows, whether
// built with it or not); the engine's elsewhere (PipeWire, System).
QStringList audioDrivers();
inline const std::vector<int> kBufferSizes{64, 128, 256, 512, 1024, 2048};
inline const std::vector<int> kSampleRates{0, 44100, 48000, 88200, 96000};  // 0 = device default

struct AudioSettings {
    QString driver = defaultDriver();
    QString deviceName;  // empty = the system default (WASAPI), the first driver (ASIO)
    int sampleRate = 0;  // 0 = the rate the device runs at
    int bufferFrames = 256;  // 0 = the device's preferred size
    bool exclusive = false;  // WASAPI
    // Device channels (0-based). The master plays on the first two outputs; {} = the device's first two.
    std::vector<int> outputChannels;
    // Inputs to open (ASIO); none until something records.
    std::vector<int> inputChannels;

    // The saved settings (an unknown driver type: the default one).
    static AudioSettings load();
    void save() const;

    friend bool operator==(const AudioSettings&, const AudioSettings&) = default;
};

// Streaming long files from disk instead of decoding them into memory
// (sub::Engine::setStreaming(), docs/engine/streaming.md). Off by default
// until it has had a Canary release.
struct StreamingPreferences {
    static constexpr double kDefaultThresholdSeconds = 30.0;
    static constexpr int kDefaultMemoryMb = 256;
    static constexpr int kDefaultCacheGb = 20;
    static constexpr int kDefaultDiskThreads = 2;

    bool enabled = false;
    double thresholdSeconds = kDefaultThresholdSeconds;  // files at least this long stream
    int memoryMb = kDefaultMemoryMb;                     // the pages they stream through
    QString cacheFolder;                                 // "": the default
    int cacheGb = kDefaultCacheGb;
    int diskThreads = kDefaultDiskThreads;

    // The saved settings (out of range: the nearest that is in it).
    static StreamingPreferences load();
    void save() const;

    friend bool operator==(const StreamingPreferences&, const StreamingPreferences&) = default;
};

inline const QString kAudioThreadsKey = QStringLiteral("audio/threads");
inline const QString kMidiDisabledKey = QStringLiteral("midi/disabled_inputs");
inline const QString kRecordQuantizeKey = QStringLiteral("record/quantize");
inline const QString kRecordQuantizeEndsKey = QStringLiteral("record/quantize_ends");
inline const QString kMidiImmediateKey = QStringLiteral("midi/immediate");
inline const QString kConstrainLatencyKey = QStringLiteral("audio/constrain_latency");
inline const QString kConstrainThresholdKey = QStringLiteral("audio/constrain_threshold_ms");
inline constexpr double kDefaultConstrainThresholdMs = 3.0;
inline constexpr double kMaxConstrainThresholdMs = 100.0;

// The threads chosen to render audio (the audio thread and its workers); 0:
// the engine's default (one per core but one).
int audioThreads();
void setAudioThreads(int threads);

// Constrain Delay Compensation (sub::Engine::setConstrainLatency()): whether
// it is on, and the latency (ms, 0-100) over which it bypasses a device.
bool constrainLatency();
void setConstrainLatency(bool on);
double constrainThresholdMs();
void setConstrainThresholdMs(double ms);

// MIDI input played at once (sub::Engine::setMidiImmediate()): sooner, less steady.
bool midiImmediate();
void setMidiImmediate(bool immediate);

// MIDI inputs the user turned off. Every other input is used, so a device
// plugged in for the first time just plays.
QSet<QString> disabledMidiInputs();
void setMidiInputDisabled(const QString& name, bool disabled);

// Record quantization choices: (label, grid in beats); 0 = off.
const std::vector<std::pair<QString, double>>& recordQuantizeChoices();
// The grid recorded MIDI notes start on, in beats (0: as played).
double recordQuantize();
void setRecordQuantize(double grid);
// Whether record quantization puts recorded notes' ends on its grid too (as
// Ableton's "Quantize Note End"), not only their starts: a note whose end would
// land where it starts lasts a grid step. Off by default.
bool recordQuantizeEnds();
void setRecordQuantizeEnds(bool ends);

}  // namespace sub::app
