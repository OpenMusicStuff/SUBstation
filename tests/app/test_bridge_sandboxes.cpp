// Plug-ins in sandboxes through the bridge (docs/engine/sandboxes.md): a
// project opens without waiting for its plug-ins, which load in their sandboxes
// meanwhile (progress, the selected track's first, states as saved, automation
// and sidechains once they have loaded); a plug-in that crashes says so and
// loads again with Reload, as it was last saved; Reload Plug-ins moves those
// loaded to where the hosting preference says now; the preference itself; a
// backup's answer from a sandbox not taking the place of a state set meanwhile.
// With the test plug-ins and the plug-in host program built with the tests.

#include "BridgeTestSupport.h"
#include "TestSupport.h"

#include "io/Serialization.h"
#include "model/Commands.h"
#include "session/PluginHostingPreferences.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

using namespace sub::app;
using sub::app::test::Studio;

namespace {

constexpr int kLoadTimeoutMs = 20000;

}  // namespace

class TestBridgeSandboxes : public QObject {
    Q_OBJECT

    std::optional<PluginRef> effect_;
    std::optional<PluginRef> crasher_;
    std::optional<PluginRef> sidechain_;

    // A studio whose plug-ins run in sandboxes, grouped as `mode` says.
    static void sandboxed(Studio& studio, PluginHostingMode mode = PluginHostingMode::Together) {
        studio.bridge->setPluginHostProgram(QStringLiteral(SUBSTATION_PLUGIN_HOST));
        studio.bridge->setPluginHosting(mode);
        studio.engine.setSandboxRetireDelay(0);
    }

    // Audio tracks with `plugins`, each with a constant clip, saved as a project; then a new one. Its path.
    QString saveProjectWith(Studio& studio, const test::TempDir& dir, const std::vector<PluginRef>& plugins,
                            const QString& wav) {
        for (const PluginRef& plugin : plugins) {
            const QString track = studio.clipTrack(wav, 2.0, plugin.name);
            studio.edit.addDevice(track, kPluginKind, plugin);
        }
        studio.bridge->storePluginStates();  // (as saving does)
        QTest::qWaitFor([&] { return !studio.bridge->isLoading(wav); }, kLoadTimeoutMs);  // (decoded before it goes)
        const QString path = dir.path(QStringLiteral("sandboxed.gilproj"));
        saveProject(studio.project, path);
        studio.project.clear();
        return path;
    }

    static QString deviceOf(const Studio& studio, int track) {
        return studio.project.tracks()[track].devices.front().id;
    }
    static QString trackOf(const Studio& studio, int track) { return studio.project.tracks()[track].id; }

private Q_SLOTS:
    void initTestCase() {
        test::prepareApplication();
        effect_ = test::testPlugin(test::testPluginsBundle(), QStringLiteral("SUB Test Effect"));
        crasher_ = test::testPlugin(test::testPluginsBundle(), QStringLiteral("SUB Test Crasher"));
        sidechain_ = test::testPlugin(test::testPluginsBundle(), QStringLiteral("SUB Test Sidechain"));
        if (!effect_ || !crasher_ || !sidechain_) QSKIP("test plug-ins not built");
    }

    void aProjectOpensWithoutWaitingForItsPlugins() {
        test::TempDir dir;
        const test::ScopedEnv slow("SUB_TEST_PLUGIN_SLOW_LOAD_MS", QStringLiteral("400"));
        Studio studio;
        sandboxed(studio, PluginHostingMode::Individually);  // (they load side by side)
        EngineBridge& bridge = *studio.bridge;
        const QString wav = test::writeWav(dir.path(QStringLiteral("dc.wav")), std::vector<float>(48000, 0.25f));
        const QString path = saveProjectWith(studio, dir, {*crasher_, *crasher_, *crasher_}, wav);
        QSignalSpy loading(&bridge, &EngineBridge::pluginsLoading);
        QElapsedTimer timer;
        timer.start();
        loadProject(studio.project, path);
        QVERIFY2(timer.elapsed() < 1000, "the project waited for its plug-ins");  // (each takes 400 ms to load)
        QCOMPARE(bridge.pluginsPending(), 3);
        QCOMPARE(loading.last(), (QVariantList{0, 3}));
        const QString first = deviceOf(studio, 0);
        QVERIFY(bridge.pluginPending(first));
        QVERIFY(!bridge.pluginRunning(trackOf(studio, 0), first));
        QVERIFY(!bridge.devicesReady());
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, kLoadTimeoutMs);
        QCOMPARE(loading.last(), (QVariantList{0, 0}));
        QVERIFY(bridge.devicesReady());
        for (int i = 0; i < 3; ++i) QVERIFY(bridge.pluginRunning(trackOf(studio, i), deviceOf(studio, i)));
        QCOMPARE(bridge.pluginSandboxes().size(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.isLoading(wav), kLoadTimeoutMs);
        QVERIFY(test::peak(studio.render(1000)) > 1.4f);  // 0.25 doubled, on three tracks
    }

    void savedStatesAutomationAndSidechainsOnceLoaded() {
        test::TempDir dir;
        Studio studio;
        sandboxed(studio);
        EngineBridge& bridge = *studio.bridge;
        const QString wav = test::writeWav(dir.path(QStringLiteral("dc.wav")), std::vector<float>(48000, 0.5f));
        // The effect at gain 0.25, saved so; the sidechain keyed from the effect's track.
        const QString fxTrack = studio.clipTrack(wav, 2.0, QStringLiteral("Fx"));
        const QString fx = studio.edit.addDevice(fxTrack, kPluginKind, *effect_);
        QVERIFY(bridge.engineDeviceId(fxTrack, fx));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, kLoadTimeoutMs);
        studio.edit.setDeviceParam(fxTrack, fx, QStringLiteral("0"), 0.25);
        const QString keyed = studio.clipTrack(wav, 2.0, QStringLiteral("Keyed"));
        const QString sc = studio.edit.addDevice(keyed, kPluginKind, *sidechain_);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, kLoadTimeoutMs);
        QVERIFY(bridge.hasSidechainInput(keyed, sc));
        studio.edit.setDeviceSidechain(keyed, sc, Sidechain{fxTrack, kPostFader});
        bridge.storePluginStates();
        const QString path = dir.path(QStringLiteral("states.gilproj"));
        saveProject(studio.project, path);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.isLoading(wav), kLoadTimeoutMs);  // (decoded before it goes)
        studio.project.clear();

        loadProject(studio.project, path);
        // Saved while still loading: the states as they were in the file.
        bridge.storePluginStates();
        const QString again = dir.path(QStringLiteral("again.gilproj"));
        saveProject(studio.project, again);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, kLoadTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.isLoading(wav), kLoadTimeoutMs);
        const QString fxAgain = studio.project.tracks()[0].id;
        const QString keyedAgain = studio.project.tracks()[1].id;
        const QString fxDevice = deviceOf(studio, 0);
        const QString scDevice = deviceOf(studio, 1);
        QCOMPARE(bridge.deviceParamValue(fxAgain, fxDevice, 0).value_or(-1.0), 0.25);
        // The sidechain, pushed once the plug-in said it has one: the keyed track hears 0.5 and the
        // effect's track (0.5 at gain 0.25: 0.25).
        QVERIFY(bridge.hasSidechainInput(keyedAgain, scDevice));
        const std::vector<float> out = studio.render(1000);
        QVERIFY(std::abs(out[1000] - (0.25f + 0.5f + 0.25f)) < 1e-4f);  // the effect's track, the keyed one
        Project reread;
        loadProject(reread, again);
        QCOMPARE(reread.tracks()[0].devices.front().state, studio.project.tracks()[0].devices.front().state);
    }

    void aStateSetWhileABackupAsksIsNotUndoneByTheAnswer() {
        // A backup asks the sandbox for the effect's state; an undo sets another before the answer is
        // taken. The answer (from before) doesn't take the place of the state the model was set to.
        Studio studio;
        sandboxed(studio);
        EngineBridge& bridge = *studio.bridge;
        const QString track = studio.edit.addAudioTrack(QStringLiteral("Fx"));
        const QString fx = studio.edit.addDevice(track, kPluginKind, *effect_);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, kLoadTimeoutMs);
        const auto state = [&] { return studio.project.device(track, fx).state; };
        studio.edit.setDeviceParam(track, fx, QStringLiteral("0"), 0.25);
        bridge.storePluginStates();
        const std::optional<QString> quarter = state();
        studio.edit.setDeviceParam(track, fx, QStringLiteral("0"), 0.75);
        bridge.storePluginStates();
        const std::optional<QString> threeQuarters = state();
        QVERIFY(quarter != threeQuarters);
        studio.edit.setDeviceParam(track, fx, QStringLiteral("0"), 0.5);  // (changed: asked again)
        bool done = false;
        bridge.gatherPluginStates([&] { done = true; });  // (asked at 0.5)
        studio.stack.push(new SetDeviceStateCommand(&studio.project, track, fx, threeQuarters, quarter,
                                                    QStringLiteral("Load Preset")));
        QTRY_VERIFY_WITH_TIMEOUT(done, kLoadTimeoutMs);
        QCOMPARE(state(), quarter);
        // The next backup asks again, and takes what it says now.
        done = false;
        bridge.gatherPluginStates([&] { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, kLoadTimeoutMs);
        QCOMPARE(QByteArray::fromBase64(state().value_or(QString()).toLatin1()), *bridge.pluginState(track, fx));
    }

    void aCrashedPluginSaysSoAndReloadBringsItBack() {
        test::TempDir dir;
        Studio studio;
        sandboxed(studio, PluginHostingMode::ByPlugin);
        EngineBridge& bridge = *studio.bridge;
        const QString wav = test::writeWav(dir.path(QStringLiteral("dc.wav")), std::vector<float>(48000, 0.25f));
        const QString track = studio.clipTrack(wav, 2.0, QStringLiteral("Crashing"));
        const QString crasher = studio.edit.addDevice(track, kPluginKind, *crasher_);
        const QString other = studio.clipTrack(wav, 2.0, QStringLiteral("Other"));
        const QString fx = studio.edit.addDevice(other, kPluginKind, *effect_);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, kLoadTimeoutMs);
        QSignalSpy messages(&bridge, &EngineBridge::statusMessage);
        QSignalSpy loaded(&bridge, &EngineBridge::devicesLoaded);
        // Note 0 crashes it: in a sandbox of its own, the effect goes on.
        const quint32 engineTrack = *bridge.engineTrackId(track);
        studio.engine.setTrackNotes(engineTrack, {{0.0, 1.0, 0, 100}});
        studio.render(1000);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginReloadable(track, crasher), kLoadTimeoutMs);
        QVERIFY(!bridge.pluginRunning(track, crasher));
        QVERIFY(bridge.pluginError(crasher).contains(QStringLiteral("crashed")));
        QVERIFY(!messages.isEmpty() && messages.last().at(0).toString().contains(QStringLiteral("SUB Test Crasher crashed")));
        QVERIFY(loaded.contains(QVariantList{track}));
        QVERIFY(bridge.pluginRunning(other, fx));
        // Reload: back, in a new sandbox.
        studio.engine.setTrackNotes(engineTrack, {});
        bridge.reloadPlugin(track, crasher);
        QVERIFY(bridge.pluginPending(crasher));
        QVERIFY(bridge.pluginError(crasher).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginRunning(track, crasher), kLoadTimeoutMs);
        QVERIFY(!bridge.pluginReloadable(track, crasher));
        QCOMPARE(bridge.pluginsPending(), 0);
    }

    void reloadPluginsMovesThemWhereThePreferenceSays() {
        test::TempDir dir;
        Studio studio;  // (in this process, as the tests run)
        studio.bridge->setPluginHostProgram(QStringLiteral(SUBSTATION_PLUGIN_HOST));
        studio.engine.setSandboxRetireDelay(0);
        EngineBridge& bridge = *studio.bridge;
        const QString wav = test::writeWav(dir.path(QStringLiteral("dc.wav")), std::vector<float>(48000, 0.5f));
        const QString track = studio.clipTrack(wav, 2.0, QStringLiteral("Fx"));
        const QString fx = studio.edit.addDevice(track, kPluginKind, *effect_);
        studio.edit.setDeviceParam(track, fx, QStringLiteral("0"), 0.25);
        QVERIFY(bridge.pluginRunning(track, fx));
        QVERIFY(bridge.pluginSandboxes().isEmpty());
        bridge.setPluginHosting(PluginHostingMode::Together);
        QVERIFY(bridge.pluginSandboxes().isEmpty());  // (loaded already: it stays until reloaded)
        bridge.reloadPlugins();
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginRunning(track, fx), kLoadTimeoutMs);
        QCOMPARE(bridge.pluginSandboxes().size(), 1);
        QCOMPARE(bridge.pluginSandboxes().front().label, QStringLiteral("All plug-ins"));
        QCOMPARE(bridge.deviceParamValue(track, fx, 0).value_or(-1.0), 0.25);  // as it was
        QVERIFY(std::abs(studio.level() - 0.25f) < 1e-5f);
        // Back within, and the sandbox nothing uses goes.
        bridge.setPluginHosting(PluginHostingMode::Within);
        bridge.reloadPlugins();
        QVERIFY(bridge.pluginRunning(track, fx));
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginSandboxes().isEmpty(), kLoadTimeoutMs);
        QCOMPARE(bridge.deviceParamValue(track, fx, 0).value_or(-1.0), 0.25);
    }

    void thePreference() {
        QSettings().remove(PluginHostingPreferences::kKey);
        Studio studio;
        {
            const test::ScopedEnv unset("SUBSTATION_PLUGIN_HOSTING", std::nullopt);
            PluginHostingPreferences prefs(studio.bridge.get(), QStringLiteral(SUBSTATION_PLUGIN_HOST));
            QCOMPARE(prefs.mode(), QStringLiteral("together"));  // (Bitwig's default too)
            QCOMPARE(studio.bridge->pluginHosting(), PluginHostingMode::Together);
            QStringList modes;
            for (const QVariant& mode : prefs.modes()) modes << mode.toMap().value(QStringLiteral("value")).toString();
            QCOMPARE(modes, (QStringList{QStringLiteral("within"), QStringLiteral("together"), QStringLiteral("manufacturer"),
                                         QStringLiteral("plugin"), QStringLiteral("instance")}));
            QSignalSpy changed(&prefs, &PluginHostingPreferences::modeChanged);
            prefs.setMode(QStringLiteral("manufacturer"));
            QCOMPARE(changed.size(), 1);
            QCOMPARE(studio.bridge->pluginHosting(), PluginHostingMode::ByManufacturer);
            QCOMPARE(QSettings().value(PluginHostingPreferences::kKey).toString(), QStringLiteral("manufacturer"));
            prefs.setMode(QStringLiteral("nonsense"));
            QCOMPARE(prefs.mode(), QStringLiteral("manufacturer"));
            QCOMPARE(prefs.status(), QStringLiteral("No plug-in runs in a sandbox now."));
        }
        // Read back as it was set; the tests' own default without one.
        PluginHostingPreferences again(studio.bridge.get(), QStringLiteral(SUBSTATION_PLUGIN_HOST));
        QCOMPARE(again.mode(), QStringLiteral("manufacturer"));
        QSettings().remove(PluginHostingPreferences::kKey);
        PluginHostingPreferences tests(studio.bridge.get(), QStringLiteral(SUBSTATION_PLUGIN_HOST));
        QCOMPARE(tests.mode(), QStringLiteral("within"));
        QCOMPARE(tests.status(), QStringLiteral("Plug-ins run within SUBstation."));
    }
};

QTEST_GUILESS_MAIN(TestBridgeSandboxes)
#include "test_bridge_sandboxes.moc"
