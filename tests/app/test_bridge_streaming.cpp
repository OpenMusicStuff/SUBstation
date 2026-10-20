// Streaming long files from disk, as the application has it (docs/engine/streaming.md):
// the preferences (StreamingPreferences), saved and applied by the bridge, files
// fetched again as the settings now say when what streams changes, a waveform
// read from disk, and what the disk came late for said in the status line and
// counted for the transport bar.

#include "BridgeTestSupport.h"
#include "TestSupport.h"

#include "audio/AudioSettings.h"
#include "audio/EngineBridge.h"
#include "audio/Waveform.h"

#include "AudioSource.h"
#include "StreamCache.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTest>

using namespace sub::app;
using sub::app::test::Studio;

class TestBridgeStreaming : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }
    void init() { QSettings().clear(); }

    void preferencesHaveDefaultsAndRoundTrip() {
        const StreamingPreferences defaults = StreamingPreferences::load();
        QVERIFY(defaults == StreamingPreferences{});
        QVERIFY(!defaults.enabled);  // (until it has had a Canary release)
        QCOMPARE(defaults.thresholdSeconds, 30.0);
        QCOMPARE(defaults.memoryMb, 256);
        QCOMPARE(defaults.cacheGb, 20);
        QCOMPARE(defaults.diskThreads, 2);

        StreamingPreferences other;
        other.enabled = true;
        other.thresholdSeconds = 10.0;
        other.memoryMb = 1024;
        other.cacheFolder = QStringLiteral("/somewhere/cache");
        other.cacheGb = 50;
        other.diskThreads = 4;
        other.save();
        QVERIFY(StreamingPreferences::load() == other);
        QCOMPARE(QSettings().value("audio/streaming").toString(), QStringLiteral("true"));

        // Out of range: the nearest that isn't.
        QSettings().setValue("audio/stream_memory_mb", 1);
        QSettings().setValue("audio/stream_disk_threads", 99);
        QCOMPARE(StreamingPreferences::load().memoryMb, 16);
        QCOMPARE(StreamingPreferences::load().diskThreads, 8);
    }

    void theBridgeSavesAndAppliesThem() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        QSignalSpy changed(&bridge, &EngineBridge::streamingChanged);
        test::TempDir dir;
        bridge.setStreamCacheFolder(dir.path(QStringLiteral("cache")));
        bridge.setStreamThreshold(2.0);
        bridge.setStreamMemory(512);
        bridge.setStreamCacheSize(5);
        bridge.setStreamDiskThreads(1);
        bridge.setStreaming(true);
        QVERIFY(changed.count() >= 6);
        const sub::StreamingSettings settings = studio.engine.streaming();
        QVERIFY(settings.enabled);
        QCOMPARE(settings.thresholdSeconds, 2.0);
        QCOMPARE(settings.poolBytes, size_t{512} << 20);
        QCOMPARE(QString::fromStdString(settings.cacheFolder), dir.path(QStringLiteral("cache")));
        QCOMPARE(settings.cacheBudgetBytes, uint64_t{5} << 30);
        QCOMPARE(settings.diskThreads, 1);
        QVERIFY(bridge.streaming());
        QCOMPARE(bridge.streamMemory(), 512);
        const StreamingPreferences saved = StreamingPreferences::load();
        QVERIFY(saved.enabled);
        QCOMPARE(saved.memoryMb, 512);
        // The default folder chosen again: none of the user's own.
        bridge.setStreamCacheFolder(EngineBridge::defaultStreamCacheFolder());
        QCOMPARE(bridge.streamCacheFolder(), QString());
        QVERIFY(!EngineBridge::defaultStreamCacheFolder().isEmpty());
        // Applied again (the program starting), from what was saved.
        sub::StreamingSettings off;
        studio.engine.setStreaming(off);
        bridge.applyStreaming();
        QVERIFY(studio.engine.streaming().enabled);
    }

    void aLongFileStreamsAndItsWaveformReadsFromDisk() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        test::TempDir dir;
        bridge.setStreamCacheFolder(dir.path(QStringLiteral("cache")));
        bridge.setStreamThreshold(1.0);
        bridge.setStreaming(true);
        std::vector<float> ramp(static_cast<size_t>(3 * test::kSampleRate));
        for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i % 1000) / 1000.f - 0.5f;
        const QString path = test::writeWav(dir.path(QStringLiteral("long.wav")), ramp);
        const QString shortPath = test::writeWav(dir.path(QStringLiteral("short.wav")), std::vector<float>(1000, 0.25f));
        bool ready = false;
        bridge.requestSource(path, [&] { ready = true; });
        bridge.requestSource(shortPath);
        QVERIFY(QTest::qWaitFor([&] { return ready && bridge.source(shortPath) != nullptr; }));
        const Waveform waveform = bridge.waveform(path);
        QVERIFY(!waveform.resident());
        QVERIFY(waveform.channelData(0) == nullptr);
        QVERIFY(waveform.peakCount(0) > 0);  // (its peaks in memory, from its peak file)
        std::vector<float> read(100);
        QVERIFY(waveform.read(0, 2 * test::kSampleRate + 5, 100, read.data()));
        QCOMPARE(read[0], ramp[static_cast<size_t>(2 * test::kSampleRate + 5)]);
        QCOMPARE(waveform.samples(0).size(), ramp.size());
        QVERIFY(bridge.waveform(shortPath).resident());  // (short: in memory)

        // Streaming switched off: the files in use are fetched again, into memory.
        bridge.setStreaming(false);
        QVERIFY(QTest::qWaitFor([&] {
            const auto source = bridge.source(path);
            return source && source->resident();
        }));
        QVERIFY(studio.engine.cachedSource(path.toStdString())->resident());
    }

    void whatTheDiskCameLateForIsSaidAndCounted() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        test::TempDir dir;
        bridge.setStreamCacheFolder(dir.path(QStringLiteral("cache")));
        bridge.setStreamThreshold(0.0);
        bridge.setStreaming(true);
        const QString path = test::writeWav(dir.path(QStringLiteral("late.wav")),
                                            std::vector<float>(static_cast<size_t>(10 * test::kSampleRate), 0.5f));
        const auto source = studio.engine.loadSource(path.toStdString());
        QVERIFY(!source->resident());
        QSignalSpy messages(&bridge, &EngineBridge::statusMessage);
        QSignalSpy late(&bridge, &EngineBridge::diskLateChanged);
        // As the audio thread meets them: frames of a page not in memory (past the 2 s head), 4800 of them (100 ms).
        std::vector<float> left(4800), right(4800);
        source->stream()->fetch(8 * test::kSampleRate, 4800, left.data(), right.data());
        sub::app::BridgeTestAccess::pollMeters(bridge);  // (the idle poll)
        QCOMPARE(late.count(), 1);
        QCOMPARE(bridge.diskLate(), 1);
        QCOMPARE(bridge.diskLateMs(), 100.0);
        QCOMPARE(messages.count(), 1);
        QVERIFY(messages.at(0).at(0).toString().contains(QStringLiteral("late.wav")));
        QVERIFY(messages.at(0).at(0).toString().contains(QStringLiteral("100 ms")));
        // Again at once: counted, not said again so soon.
        source->stream()->fetch(8 * test::kSampleRate, 4800, left.data(), right.data());
        sub::app::BridgeTestAccess::pollMeters(bridge);
        QCOMPARE(bridge.diskLate(), 2);
        QCOMPARE(messages.count(), 1);
        // Expected misses (the playhead dragged) are no problem: not counted.
        source->stream()->fetch(8 * test::kSampleRate, 4800, left.data(), right.data(), true);
        sub::app::BridgeTestAccess::pollMeters(bridge);
        QCOMPARE(bridge.diskLate(), 2);
    }
};

QTEST_GUILESS_MAIN(TestBridgeStreaming)
#include "test_bridge_streaming.moc"
