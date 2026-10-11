// The engine bridge on PipeWire (Linux), against a daemon of the test's own
// (tests/engine/harness/PipeWireDaemon.h): a device that went away opens again
// when it is back, and a track's input opens the device again with it. Skipped
// where the engine was built without PipeWire, or the daemon isn't installed.

#include "BridgeTestSupport.h"
#include "TestSupport.h"

#include "SessionFixture.h"

#include "audio/AudioSettings.h"
#include "audio/EngineBridge.h"
#include "session/AudioPreferences.h"
#include "session/Session.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

#include <functional>
#include <memory>

using namespace sub::app;
using sub::app::test::Studio;

#if SUBSTATION_HAS_PIPEWIRE
#include "../engine/harness/PipeWireDaemon.h"

namespace {

AudioSettings pipeWire(const QString& name = {}) {
    AudioSettings settings;
    settings.driver = QStringLiteral("PipeWire");
    settings.deviceName = name;
    return settings;
}

// Polls the bridge (and the engine's housekeeping) as the application's timers do, until `done`.
bool pollUntil(Studio& studio, const std::function<bool()>& done, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        studio.engine.idle();
        BridgeTestAccess::pollMeters(*studio.bridge);
        if (done()) return true;
        QTest::qWait(10);
    }
    return false;
}

}  // namespace
#endif

class TestBridgePipeWire : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }
    void init() { QSettings().clear(); }

#if SUBSTATION_HAS_PIPEWIRE
    void aDeviceThatWentAwayOpensWhenItIsBack() {
        auto daemon = std::make_unique<subtest::PipeWireDaemon>();
        if (!daemon->running()) QSKIP(daemon->whyNot().c_str());
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        QCOMPARE(bridge.openDevice(pipeWire()), QString());
        QSignalSpy messages(&bridge, &EngineBridge::statusMessage);
        auto said = [&](const QString& text) {
            for (const auto& message : messages) {
                if (message.first().toString() == text) return true;
            }
            return false;
        };
        daemon->stop();  // (the sound server restarting)
        QVERIFY(pollUntil(studio, [&] { return !bridge.deviceStatus().open; }));
        QVERIFY(said(QStringLiteral("The audio device stopped. Choose a device in Options > Preferences.")));
        daemon = std::make_unique<subtest::PipeWireDaemon>();
        QVERIFY(daemon->running());
        QVERIFY(pollUntil(studio, [&] { return bridge.deviceStatus().open; }));
        QVERIFY(said(QStringLiteral("The audio device is back: Test Duplex.")));
        QCOMPARE(bridge.deviceStatus().backend, QStringLiteral("PipeWire"));
        // Another device chosen meanwhile is kept: nothing is looked for once one opens.
        daemon->stop();
        QVERIFY(pollUntil(studio, [&] { return !bridge.deviceStatus().open; }));
        daemon = std::make_unique<subtest::PipeWireDaemon>();
        QCOMPARE(bridge.openDevice(pipeWire(QStringLiteral("Test Sink"))), QString());
        QTest::qWait(static_cast<int>(EngineBridge::kLostDeviceInterval) + 200);
        QVERIFY(pollUntil(studio, [] { return true; }));
        QCOMPARE(bridge.deviceStatus().name, QStringLiteral("Test Sink"));
    }

    void aTracksInputOpensTheDeviceWithIt() {
        subtest::PipeWireDaemon daemon;
        if (!daemon.running()) QSKIP(daemon.whyNot().c_str());
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        QCOMPARE(bridge.openDevice(pipeWire()), QString());
        QVERIFY(bridge.deviceStatus().inputChannels.isEmpty());
        QCOMPARE(bridge.inputNames(), QStringList({QStringLiteral("Front Left"), QStringLiteral("Front Right")}));
        const QString track = studio.edit.addAudioTrack();
        studio.edit.setInput(track, {1}, std::nullopt);
        const AudioDeviceStatus status = bridge.deviceStatus();
        QVERIFY(status.open);
        QCOMPARE(status.inputChannels, QList<int>({1}));
        QCOMPARE(status.name, QStringLiteral("Test Duplex"));
        const AudioSettings saved = AudioSettings::load();
        QCOMPARE(saved.driver, QStringLiteral("PipeWire"));
        QCOMPARE(saved.deviceName, QString());  // still the default device, whichever it is
        QCOMPARE(saved.inputChannels, std::vector<int>{1});
        // What was asked for, not what the graph runs now: requests, kept as they were.
        QCOMPARE(saved.sampleRate, 0);
        QCOMPARE(saved.bufferFrames, 256);
    }

    void thePreferencesShowDropoutsAsTheyCome() {
        subtest::PipeWireDaemon daemon;
        if (!daemon.running()) QSKIP(daemon.whyNot().c_str());
        test::SessionFixture f;
        QCOMPARE(f.bridge().openDevice(pipeWire()), QString());
        AudioPreferences& prefs = *f.s().audioPreferences();
        prefs.open();
        QVERIFY(!prefs.status().contains(QStringLiteral("dropout")));
        // The device runs: callbacks come (the scope fills).
        const quint64 written = f.bridge().scopeWritten();
        QTRY_VERIFY(f.bridge().scopeWritten() > written);
        // A freeze makes the device's clock skip only now and then (when it stops,
        // or when it starts again, by a cycle or all it missed): again until it has.
        for (int i = 0; i < 20 && f.bridge().deviceStatus().dropouts == 0; ++i) {
            daemon.freeze(std::chrono::milliseconds(200));
            QTest::qWait(200);
        }
        QVERIFY(f.bridge().deviceStatus().dropouts > 0);
        // No setting changed and no device event: the open dialog looks again by itself.
        QTRY_VERIFY_WITH_TIMEOUT(prefs.status().contains(QStringLiteral("since it opened")),
                                 3 * AudioPreferences::kDropoutsInterval);
        prefs.close();
    }
#endif
};

QTEST_GUILESS_MAIN(TestBridgePipeWire)
#include "test_bridge_pipewire.moc"
