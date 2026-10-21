// VST3 plug-ins through the bridge: projects keeping them and their state (a
// backup's taken again only where something changed it), a project's plug-ins loading after it opens (one at a time, the selected track's
// first, a waiting one moved or deleted, renders waiting for them), missing and
// moved plug-ins, devices keeping their processor (and state) as they move,
// what plug-ins report (edits for undo only from a shown editor), editors as far
// as they work without windows (and on Linux an X11 editor, on an X server of
// the test's own, run by Qt's event loop), automating plug-in parameters, and
// effects on the master. The changes are made as the editor makes them.
// Skipped without the test plug-ins.

#include "BridgeTestSupport.h"
#include "TestSupport.h"

#include "io/Serialization.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

#if SUBSTATION_HAS_X11_EDITORS && SUBSTATION_EDITOR_TESTS
#include "../engine/harness/XDisplay.h"
#endif

using namespace sub::app;
using sub::app::test::Studio;

namespace {

QJsonObject readJson(const QString& path) {
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeJson(const QString& path, const QJsonObject& data) {
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(data).toJson());
}

bool close(float a, float b, float tolerance = 1e-6f) { return std::abs(a - b) <= tolerance; }

sub::ProcessorEventRecord report(sub::ProcessorEvent::Type type, quint32 processor, int index = -1,
                                float value = 0.f, float old = 0.f, uint32_t gesture = 0) {
    sub::ProcessorEventRecord record;
    record.type = type;
    record.processorId = processor;
    record.paramIndex = index;
    record.value = value;
    record.oldValue = old;
    record.gesture = gesture;
    return record;
}

}  // namespace

class TestBridgePlugins : public QObject {
    Q_OBJECT

    std::optional<PluginRef> synth_;
    std::optional<PluginRef> effect_;

    // A MIDI track with SUB Test Synth (playing a note), saved as a project with
    // the others; then a new project. The project's path.
    QString saveWithPlugins(Studio& studio, const test::TempDir& dir, const QStringList& names, bool effects = false) {
        for (const QString& name : names) {
            if (effects) {
                const QString track = studio.edit.addAudioTrack(name);
                studio.edit.addDevice(track, kPluginKind, effect_);
                continue;
            }
            const QString track = studio.edit.addMidiTrack(name, std::nullopt, synth_);
            studio.edit.setClips(track, {Clip::midi(name + QStringLiteral("m"), name, 0.0, 2.0, 0.0,
                                                    {Note{60, 0.0, 1.0, 100}})});
        }
        studio.bridge->storePluginStates();  // (as saving does)
        const QString path = dir.path("plugins.gilproj");
        saveProject(studio.project, path);
        studio.project.clear();
        return path;
    }

private Q_SLOTS:
    void initTestCase() {
        test::prepareApplication();
        synth_ = test::testPlugin(test::testPluginsBundle(), QStringLiteral("SUB Test Synth"));
        effect_ = test::testPlugin(test::testPluginsBundle(), QStringLiteral("SUB Test Effect"));
        if (!synth_ || !effect_) QSKIP("test plug-ins not built");
    }

    void projectsKeepPluginsAndTheirState() {
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        QString track = studio.edit.addMidiTrack(QStringLiteral("Keys"), std::nullopt, synth_);
        QString device = studio.project.track(track).devices.front().id;
        studio.engine.setProcessorParam(*bridge.engineDeviceId(track, device), test::kSynthGain, 0.3f);
        bridge.storePluginStates();
        const QString path = dir.path("song.gilproj");
        saveProject(studio.project, path);
        const QJsonObject saved =
            readJson(path)["tracks"].toArray()[0].toObject()["devices"].toArray()[0].toObject();
        QCOMPARE(saved["kind"].toString(), QStringLiteral("plugin"));
        QCOMPARE(saved["plugin"].toObject()["name"].toString(), QStringLiteral("SUB Test Synth"));
        QCOMPARE(QDir::cleanPath(saved["plugin"].toObject()["path"].toString()),
                 QDir::cleanPath(test::testPluginsBundle()));
        QVERIFY(saved["plugin"].toObject()["instrument"].toBool());
        QVERIFY(QByteArray::fromBase64(saved["state"].toString().toLatin1()).startsWith("VST3"));

        studio.project.clear();
        QSignalSpy loading(&bridge, &EngineBridge::pluginsLoading);
        loadProject(studio.project, path);
        // The project is there at once; its plug-ins load after it, one at a time (with their state).
        track = studio.project.tracks().front().id;
        device = studio.project.track(track).devices.front().id;
        QVERIFY(!bridge.engineDeviceId(track, device) && bridge.pluginPending(device));
        QCOMPARE(bridge.pluginsPending(), 1);
        QCOMPARE(loading.first(), (QVariantList{0, 1}));
        QVERIFY(!bridge.devicesReady());
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, 5000);
        QCOMPARE(loading.last(), (QVariantList{0, 0}));  // all done
        QVERIFY(bridge.devicesReady());
        QVERIFY(close(studio.engine.processorParam(*bridge.engineDeviceId(track, device), test::kSynthGain), 0.3f));
    }

    // A VST2 plug-in (SUB VST2 Synth, tests/vst2_plugins) as a device: the project
    // keeps its format and its state (an .fxp of its values) and loads it again.
    void projectsKeepVst2PluginsAndTheirState() {
#ifndef SUBSTATION_TEST_VST2_DIR
        QSKIP("the test VST2 plug-ins are not built");
#else
        PluginRef ref;
        ref.format = QStringLiteral("VST2");
        ref.uid = QStringLiteral("53755332");  // 'SuS2'
        ref.name = QStringLiteral("SUB VST2 Synth");
        ref.vendor = QStringLiteral("SUBstation Tests");
#ifdef _WIN32
        ref.path = QStringLiteral(SUBSTATION_TEST_VST2_DIR "/SUBTestVst2Synth.dll");
#else
        ref.path = QStringLiteral(SUBSTATION_TEST_VST2_DIR "/SUBTestVst2Synth.so");
#endif
        ref.instrument = true;
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        QString track = studio.edit.addMidiTrack(QStringLiteral("Keys"), std::nullopt, ref);
        QString device = studio.project.track(track).devices.front().id;
        QVERIFY(bridge.engineDeviceId(track, device));
        studio.engine.setProcessorParam(*bridge.engineDeviceId(track, device), 0, 0.3f);  // its Level
        bridge.storePluginStates();
        const QString path = dir.path("song.gilproj");
        saveProject(studio.project, path);
        const QJsonObject saved =
            readJson(path)["tracks"].toArray()[0].toObject()["devices"].toArray()[0].toObject();
        QCOMPARE(saved["plugin"].toObject()["format"].toString(), QStringLiteral("VST2"));
        QCOMPARE(saved["plugin"].toObject()["uid"].toString(), QStringLiteral("53755332"));
        QVERIFY(QByteArray::fromBase64(saved["state"].toString().toLatin1()).startsWith("CcnK"));  // an .fxp

        studio.project.clear();
        loadProject(studio.project, path);
        track = studio.project.tracks().front().id;
        device = studio.project.track(track).devices.front().id;
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, 5000);
        QCOMPARE(studio.project.track(track).devices.front().plugin->format, QStringLiteral("VST2"));
        QVERIFY(close(studio.engine.processorParam(*bridge.engineDeviceId(track, device), 0), 0.3f));
#endif
    }

    void missingAndMovedPlugins() {
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        bridge.setKnownPlugins(test::testPluginInfos(test::testPluginsBundle()));  // (the scan's)
        studio.edit.addMidiTrack(QStringLiteral("Keys"), std::nullopt, synth_);
        bridge.storePluginStates();
        const QString path = dir.path("song.gilproj");
        saveProject(studio.project, path);
        QJsonObject data = readJson(path);
        QJsonArray tracks = data["tracks"].toArray();
        QJsonObject first = tracks[0].toObject();
        QJsonArray devices = first["devices"].toArray();
        QJsonObject moved = devices[0].toObject();
        QJsonObject movedPlugin = moved["plugin"].toObject();
        movedPlugin["path"] = dir.path("moved away/SUBTestPlugins.vst3");  // found by its id
        moved["plugin"] = movedPlugin;
        QJsonObject gone = moved;
        gone["id"] = QStringLiteral("gone");
        QJsonObject gonePlugin = movedPlugin;
        gonePlugin["uid"] = QString(32, u'0');
        gonePlugin["name"] = QStringLiteral("Gone Synth");
        gonePlugin["instrument"] = false;
        gone["plugin"] = gonePlugin;
        devices = QJsonArray{moved, gone};
        first["devices"] = devices;
        tracks[0] = first;
        data["tracks"] = tracks;
        writeJson(path, data);

        loadProject(studio.project, path);
        const QString track = studio.project.tracks().front().id;
        const QString movedId = studio.project.track(track).devices[0].id;
        QCOMPARE(bridge.pluginErrors().value("gone"), QStringLiteral("Gone Synth is not installed."));  // (known at once)
        QCOMPARE(bridge.pluginsPending(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, 5000);
        QVERIFY(bridge.engineDeviceId(track, movedId) && !bridge.engineDeviceId(track, "gone"));
        // Saving keeps the missing device (and its settings) for when it's back.
        bridge.storePluginStates();
        saveProject(studio.project, path);
        const QJsonArray kept = readJson(path)["tracks"].toArray()[0].toObject()["devices"].toArray();
        QCOMPARE(kept[1].toObject()["plugin"].toObject()["name"].toString(), QStringLiteral("Gone Synth"));
        QCOMPARE(kept[1].toObject()["state"], gone["state"]);
        QCOMPARE(QDir::cleanPath(kept[0].toObject()["plugin"].toObject()["path"].toString()),
                 QDir::cleanPath(test::testPluginsBundle()));  // remembers where it was found
    }

    void aPluginFoundByALaterScanLoads() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        PluginRef elsewhere = *effect_;
        elsewhere.path = QStringLiteral("/nowhere/SUBTestPlugins.vst3");
        const QString track = studio.edit.addAudioTrack();
        const QString fx = studio.edit.addDevice(track, kPluginKind, elsewhere);
        QVERIFY(!bridge.engineDeviceId(track, fx));
        QVERIFY(bridge.pluginError(fx).contains("is not installed"));
        QVERIFY(!bridge.pluginPath(elsewhere));
        QSignalSpy loaded(&bridge, &EngineBridge::devicesLoaded);
        bridge.setKnownPlugins(test::testPluginInfos(test::testPluginsBundle()));
        QCOMPARE(*bridge.pluginPath(elsewhere), test::testPluginsBundle());
        QVERIFY(bridge.engineDeviceId(track, fx) && bridge.pluginError(fx).isEmpty());
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(loaded.first().first().toString(), track);
    }

    void devicesKeepTheirPluginState() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        sub::Engine& engine = studio.engine;
        const QString track = studio.edit.addMidiTrack(QStringLiteral("Keys"), std::nullopt, synth_);
        const QString synth = studio.project.track(track).devices.front().id;
        const quint32 processor = *bridge.engineDeviceId(track, synth);
        engine.setProcessorParam(processor, test::kSynthGain, 0.3f);  // as if changed in its editor, without an undo step
        const QString utility = studio.edit.addDevice(track, QStringLiteral("utility"));
        studio.edit.moveDevices(track, {utility}, 0);  // an instrument stays first...
        QCOMPARE(studio.project.track(track).devices[0].id, synth);
        QCOMPARE(*bridge.engineDeviceId(track, synth), processor);  // ...and nothing is reloaded

        studio.edit.removeDevices(track, {synth});
        QVERIFY(bridge.engineDeviceId(track, utility));
        studio.stack.undo();  // the plug-in comes back as it was
        const quint32 restored = *bridge.engineDeviceId(track, synth);
        QVERIFY(restored != processor);
        QVERIFY(close(engine.processorParam(restored, test::kSynthGain), 0.3f));

        studio.edit.deleteTracks({track});
        studio.stack.undo();
        QVERIFY(close(engine.processorParam(*bridge.engineDeviceId(track, synth), test::kSynthGain), 0.3f));
        // Its current state, read directly (the model's is stale until stored).
        QVERIFY(bridge.pluginState(track, synth)->startsWith("VST3"));
        QVERIFY(!bridge.pluginState(track, utility));
    }

    void movingADeviceToAnotherTrackMovesItsProcessor() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        sub::Engine& engine = studio.engine;
        const QString a = studio.edit.addAudioTrack();
        const QString b = studio.edit.addAudioTrack();
        const QString utility = studio.edit.addDevice(b, QStringLiteral("utility"));
        const QString effect = studio.edit.addDevice(a, kPluginKind, effect_);
        const quint32 processor = *bridge.engineDeviceId(a, effect);
        engine.setProcessorParam(processor, test::kEffectGain, 0.3f);  // as if changed in its editor: only the plug-in knows
        const QString paramId = QString::fromStdString(engine.processorParams(processor)[test::kEffectGain].id);
        const QString key = automation::deviceKey(effect, paramId);
        studio.edit.setEnvelope(a, key, {{0.0, 0.5, 0.0}});
        const uint32_t chainA = engine.trackChain(*bridge.engineTrackId(a));
        const uint32_t chainB = engine.trackChain(*bridge.engineTrackId(b));

        studio.edit.moveDevicesToTrack(a, {effect}, b);
        QVERIFY(studio.project.track(a).devices.empty());
        QCOMPARE(studio.project.track(b).devices.size(), size_t{2});
        QCOMPARE(studio.project.track(b).devices[1].id, effect);
        // The same processor, moved in the engine: nothing loaded again, nothing lost.
        QCOMPARE(*bridge.engineDeviceId(b, effect), processor);
        QVERIFY(!bridge.engineDeviceId(a, effect));
        QCOMPARE(engine.processorChain(processor), chainB);
        QVERIFY(close(engine.processorParam(processor, test::kEffectGain), 0.3f));
        QVERIFY(!studio.project.envelope(b, key).empty() && studio.project.envelope(a, key).empty());
        QVERIFY(bridge.isAutomated(b, key));  // its automation went along

        studio.stack.undo();  // one step: back where it was
        QCOMPARE(*bridge.engineDeviceId(a, effect), processor);
        QCOMPARE(engine.processorChain(processor), chainA);
        QVERIFY(bridge.isAutomated(a, key) && !bridge.isAutomated(b, key));
        studio.stack.redo();
        QCOMPARE(*bridge.engineDeviceId(b, effect), processor);
    }

    // A backup's plug-in states (gatherPluginStates()): taken without the caller waiting, and those
    // the model holds as they are (nothing changed them since they were taken) not asked again.
    void backupsGatherOnlyChangedPluginStates() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString track = studio.edit.addAudioTrack(QStringLiteral("Bus"));
        const QString device = studio.edit.addDevice(track, kPluginKind, effect_);
        const quint32 processor = *bridge.engineDeviceId(track, device);
        const QString paramId = QString::fromStdString(studio.engine.processorParams(processor)[test::kEffectGain].id);
        const auto state = [&] { return studio.project.device(track, device).state.value_or(QString()); };
        const auto gather = [&] {
            bool done = false;
            bridge.gatherPluginStates([&] { done = true; });
            QVERIFY(!done);  // (its plug-in read at the event loop's next turn)
            QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        };
        gather();  // (never taken: asked)
        const QString first = state();
        QVERIFY(QByteArray::fromBase64(first.toLatin1()).startsWith("VST3"));

        // Changed where only the engine knows: not asked again, the model's kept.
        studio.engine.setProcessorParam(processor, test::kEffectGain, 0.3f);
        gather();
        QCOMPARE(state(), first);
        // Saving asks every one.
        bridge.storePluginStates();
        const QString saved = state();
        QVERIFY(saved != first);
        gather();
        QCOMPARE(state(), saved);

        // A parameter set through the bridge: asked again.
        studio.edit.setDeviceParam(track, device, paramId, 0.6);
        gather();
        const QString edited = state();
        QVERIFY(edited != saved);
        // Something it reported (its state changed in a way no parameter shows): asked again.
        studio.engine.setProcessorParam(processor, test::kEffectGain, 0.1f);
        gather();
        QCOMPARE(state(), edited);
        BridgeTestAccess::injectEvents(bridge, {report(sub::ProcessorEvent::Type::StateDirty, processor)});
        bridge.pollPlugins();
        gather();
        QVERIFY(state() != edited);
        QCOMPARE(QByteArray::fromBase64(state().toLatin1()), *bridge.pluginState(track, device));

        // Gone meanwhile: what waited for it is done all the same.
        studio.edit.setDeviceParam(track, device, paramId, 0.2);
        bool done = false;
        bridge.gatherPluginStates([&] { done = true; });
        studio.edit.removeDevices(track, {device});
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    }

    void aPluginReportingItsOwnChangesMakesNoUndoSteps() {
        // Some plug-ins report their parameters as edited while their state is
        // restored (a track with them duplicated, pasted, undone): only edits made
        // in a shown editor are the user's.
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString track = studio.edit.addAudioTrack(QStringLiteral("Bus"));
        const QString device = studio.edit.addDevice(track, kPluginKind, effect_);
        studio.edit.groupDevices(track, {device});  // (in a rack too)
        const quint32 processor = *bridge.engineDeviceId(track, device);
        QVERIFY(!studio.engine.isEditorOpen(processor));
        QSignalSpy edited(&bridge, &EngineBridge::pluginParamEdited);
        QSignalSpy touched(&bridge, &EngineBridge::pluginParamTouched);
        QSignalSpy changed(&bridge, &EngineBridge::pluginParamsChanged);
        using Type = sub::ProcessorEvent::Type;
        BridgeTestAccess::injectEvents(
            bridge, {report(Type::ParamEdited, processor, test::kEffectGain, 0.4f, 0.5f),
                     report(Type::ParamTouched, processor, test::kEffectGain)});
        bridge.pollPlugins();
        QCOMPARE(edited.count(), 0);
        QCOMPARE(touched.count(), 0);
        QCOMPARE(changed.count(), 1);  // its parameters show anew, once
        QCOMPARE(changed.first(), (QVariantList{track, device}));
        // From a shown editor, it is the user's: an edit for the undo stack, and its lane shows.
        if (!bridge.openPluginEditor(track, device)) QSKIP("plug-in editors don't open here (Windows, or Linux with a display)");
        BridgeTestAccess::injectEvents(
            bridge, {report(Type::ParamTouched, processor, test::kEffectGain),
                     report(Type::ParamEdited, processor, test::kEffectGain, 0.3f, 0.5f, 7)});
        bridge.pollPlugins();
        const QString paramId = QString::fromStdString(studio.engine.processorParams(processor)[test::kEffectGain].id);
        QCOMPARE(touched.count(), 1);
        QCOMPARE(touched.first(), (QVariantList{track, device, paramId}));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.first()[2].toString(), paramId);
        QVERIFY(close(edited.first()[3].toFloat(), 0.3f) && close(edited.first()[4].toFloat(), 0.5f));
        QCOMPARE(edited.first()[5].toUInt(), 7u);
        bridge.closePluginEditor(track, device);
    }

    void whatPluginsReport() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString track = studio.edit.addAudioTrack(QStringLiteral("Bus"));
        const QString device = studio.edit.addDevice(track, kPluginKind, effect_);
        const quint32 processor = *bridge.engineDeviceId(track, device);
        const QVariantList place{track, device};
        QSignalSpy changed(&bridge, &EngineBridge::pluginParamsChanged);
        QSignalSpy rebuilt(&bridge, &EngineBridge::pluginParamsRebuilt);
        QSignalSpy editor(&bridge, &EngineBridge::pluginEditorChanged);
        QSignalSpy dirty(&bridge, &EngineBridge::pluginStateDirty);
        QSignalSpy automationState(&bridge, &EngineBridge::automationStateChanged);
        using Type = sub::ProcessorEvent::Type;
        BridgeTestAccess::injectEvents(bridge, {report(Type::ParamsChanged, processor), report(Type::LatencyChanged, processor),
                                                report(Type::ParamInfoChanged, processor),
                                                report(Type::EditorClosed, processor), report(Type::StateDirty, processor),
                                                report(Type::StateDirty, processor),
                                                report(Type::ParamsChanged, processor + 1000)});  // (no such device)
        bridge.pollPlugins();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first(), place);
        QCOMPARE(rebuilt.count(), 1);
        QCOMPARE(rebuilt.first(), place);
        QVERIFY(automationState.count() >= 1);  // (pushed again: its parameters may be elsewhere now)
        QCOMPARE(editor.count(), 1);
        QCOMPARE(dirty.count(), 1);  // the project has changes no edit shows

        // Another preset: said for its device (what it is called is the engine's to say; this one has none).
        QSignalSpy preset(&bridge, &EngineBridge::pluginPresetChanged);
        BridgeTestAccess::injectEvents(bridge, {report(Type::PresetChanged, processor)});
        bridge.pollPlugins();
        QCOMPARE(preset.count(), 1);
        QCOMPARE(preset.first(), place);
        QCOMPARE(bridge.pluginPresetName(track, device), std::optional<QString>(QString()));  // loaded, none told
        QVERIFY(!bridge.pluginPresetName(track, QStringLiteral("nope")));                    // not loaded

        // Not while a plug-in's call runs a message loop: the reports wait.
        BridgeTestAccess::injectEvents(bridge, {report(Type::StateDirty, processor)});
        BridgeTestAccess::setBusy(bridge, true);
        bridge.pollPlugins();
        QCOMPARE(dirty.count(), 1);
        BridgeTestAccess::setBusy(bridge, false);
        bridge.pollPlugins();
        QCOMPARE(dirty.count(), 2);

        // A plug-in asking for its editor gets it when its track is shown, not before.
        bridge.showPluginEditors(QStringLiteral("another track"));
        BridgeTestAccess::injectEvents(bridge, {report(Type::EditorRequested, processor)});
        bridge.pollPlugins();
        QVERIFY(!bridge.isPluginEditorOpen(track, device));
        bridge.showPluginEditors(track);  // (here, with no editor windows, it can't open: nothing said)
        bridge.showPluginEditors(QString());
        bridge.closeAllEditors();
    }

    void pluginEditorsWithoutWindows() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        int asked = 0;
        bridge.setOwnerWindow([&] {
            ++asked;
            return uintptr_t{0x1234};
        });
        const QString track = studio.edit.addAudioTrack(QStringLiteral("Keys"));
        const QString device = studio.edit.addDevice(track, kPluginKind, effect_);
        QSignalSpy messages(&bridge, &EngineBridge::statusMessage);
        QSignalSpy changed(&bridge, &EngineBridge::pluginEditorChanged);
        const bool opened = bridge.openPluginEditor(track, device);
        QCOMPARE(asked, 1);  // the main window owns the editors
        QCOMPARE(changed.count(), 1);
        QCOMPARE(bridge.isPluginEditorOpen(track, device), opened);
        if (opened) {
            bridge.closePluginEditor(track, device);
            QVERIFY(!bridge.isPluginEditorOpen(track, device));
            QSKIP("plug-in editors open here: their following the selected track needs a look at their windows");
        }
        QCOMPARE(messages.last().first().toString(), QStringLiteral("SUB Test Effect has no editor."));
        QVERIFY(!bridge.openPluginEditor(QStringLiteral("nope"), device));
        // Asked for while its track isn't shown: wanted, not opened (nor said to have none).
        const int said = messages.count();
        bridge.requestPluginEditor(track, device);
        QCOMPARE(messages.count(), said);
        bridge.showPluginEditors(track);
        QCOMPARE(messages.count(), said);  // (having none is fine there)
        bridge.closePluginEditor(track, device);
        QVERIFY(!bridge.isPluginEditorOpen(track, device));
    }

#if SUBSTATION_HAS_X11_EDITORS && SUBSTATION_EDITOR_TESTS
    // Linux: the editor's window, and the plug-in's run loop (its connection, its timer), on Qt's event loop.
    void anX11EditorRunsOnQtsEventLoop() {
        subtest::XDisplay display;
        if (!display.running()) QSKIP(display.whyNot().c_str());
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString track = studio.edit.addAudioTrack(QStringLiteral("Keys"));
        const QString device = studio.edit.addDevice(track, kPluginKind, effect_);
        QVERIFY(bridge.openPluginEditor(track, device));
        subtest::XClient x;
        // (Its connection's requests and this one's in order each, not across them: waited for.)
        const std::string title = BridgeTestAccess::editorTitle(bridge, track, device).toStdString();
        xcb_window_t frame = 0;
        QTRY_VERIFY((frame = x.topLevel(title)) != 0 && !x.children(frame).empty());
        const xcb_window_t view = x.children(frame).front();
        QTRY_VERIFY(x.cardinal(view, "SUB_TEST_TICKS") > 2);  // with nothing but Qt's event loop running
        // A knob turned in it: the user's edit.
        QSignalSpy edited(&bridge, &EngineBridge::pluginParamEdited);
        x.send(view, x.atom("SUB_TEST_EDIT_GAIN"));
        QTRY_VERIFY((bridge.pollPlugins(), edited.count() > 0));
        // Its close button.
        x.close(frame);
        QTRY_VERIFY((bridge.pollPlugins(), !bridge.isPluginEditorOpen(track, device)));
        QTRY_COMPARE(x.topLevel(BridgeTestAccess::editorTitle(bridge, track, device).toStdString()), xcb_window_t{0});
    }
#endif

    void automatingPluginParameters() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        sub::Engine& engine = studio.engine;
        const QString track = studio.edit.addMidiTrack(QStringLiteral("Keys"), std::nullopt, synth_);
        const QString device = studio.project.track(track).devices.front().id;
        const quint32 processor = *bridge.engineDeviceId(track, device);
        const QString paramId = QString::fromStdString(engine.processorParams(processor)[test::kSynthGain].id);
        const QString key = automation::deviceKey(device, paramId);
        QCOMPARE(bridge.paramSpec(track, key)->name, QStringLiteral("Gain"));
        QVERIFY(bridge.canAutomate(track, key));
        // An envelope drives it: the plug-in follows.
        studio.edit.setEnvelope(track, key, {{0.0, 0.25, 0.0}});
        engine.renderOffline(0.0, 256);
        bridge.pollPlugins();
        QVERIFY(close(engine.processorParam(processor, test::kSynthGain), 0.25f));
        QCOMPARE(*bridge.currentValue(track, key, 0.0), 0.25);
        // Turning it by hand overrides the envelope, until automation is re-enabled.
        studio.edit.setDeviceParam(track, device, paramId, 0.6);
        QVERIFY(bridge.isOverridden(track, key));
        engine.renderOffline(0.0, 256);
        bridge.pollPlugins();
        QVERIFY(close(engine.processorParam(processor, test::kSynthGain), 0.6f));
        QVERIFY(close(static_cast<float>(*bridge.ownValue(track, key)), 0.6f));  // (read from the plug-in)
        bridge.reEnableAutomation();
        engine.renderOffline(0.0, 256);
        bridge.pollPlugins();
        QVERIFY(close(engine.processorParam(processor, test::kSynthGain), 0.25f));
    }

    void aProjectsPluginsLoadAfterItOpensTheSelectedTracksFirst() {
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString path =
            saveWithPlugins(studio, dir, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
        QSignalSpy loading(&bridge, &EngineBridge::pluginsLoading);
        loadProject(studio.project, path);
        BridgeTestAccess::stopPluginTimer(bridge);  // (one at a time, by hand here)
        QStringList tracks;
        for (const Track& t : studio.project.tracks()) tracks.append(t.id);
        const auto loaded = [&](int i) {
            const QString& id = tracks[i];
            return bridge.engineDeviceId(id, studio.project.track(id).devices.front().id).has_value();
        };
        QCOMPARE(bridge.pluginsPending(), 3);
        QCOMPARE(loading.last(), (QVariantList{0, 3}));
        bridge.prioritizePlugins(tracks[2]);  // (the window does, for the selected track)
        BridgeTestAccess::loadNextPlugin(bridge);
        QVERIFY(!loaded(0) && !loaded(1) && loaded(2));
        QCOMPARE(loading.last(), (QVariantList{1, 3}));
        // Its editor asked for: that one now.
        bridge.requestPluginEditor(tracks[1], studio.project.track(tracks[1]).devices.front().id);
        QVERIFY(loaded(1));
        QCOMPARE(bridge.pluginsPending(), 1);
        BridgeTestAccess::loadNextPlugin(bridge);
        QVERIFY(loaded(0) && bridge.pluginsPending() == 0);
        QCOMPARE(loading.last(), (QVariantList{0, 0}));
    }

    void aPluginWaitingToLoadMayMoveOrGo() {
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString path = saveWithPlugins(studio, dir, {QStringLiteral("A"), QStringLiteral("B")}, true);
        loadProject(studio.project, path);
        BridgeTestAccess::stopPluginTimer(bridge);
        const QString a = studio.project.tracks()[0].id;
        const QString b = studio.project.tracks()[1].id;
        const QString fxA = studio.project.track(a).devices.front().id;
        const QString fxB = studio.project.track(b).devices.front().id;
        studio.edit.removeDevices(b, {fxB});
        studio.edit.moveDevicesToTrack(a, {fxA}, b);
        bridge.loadPendingPlugins();  // A's loads where it is now; B's went
        QVERIFY(bridge.engineDeviceId(b, fxA) && bridge.pluginsPending() == 0);
        QVERIFY(!bridge.engineDeviceId(b, fxB));
        studio.stack.undo();  // (A's goes back, its processor along)
        QVERIFY(bridge.engineDeviceId(a, fxA));
        studio.stack.undo();  // B's is back: it loads now, as any device added
        QVERIFY(bridge.engineDeviceId(b, fxB));
    }

    void rendersWaitForThePluginsStillLoading() {
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString path = saveWithPlugins(studio, dir, {QStringLiteral("A")});
        loadProject(studio.project, path);
        QCOMPARE(bridge.pluginsPending(), 1);
        QVERIFY(!bridge.devicesReady());
        bridge.waitForDeviceStates();
        QVERIFY(bridge.pluginsPending() == 0 && bridge.devicesReady());
        QVERIFY(test::peak(studio.render(test::kSampleRate / 2)) > 0.01f);  // the synth played
    }

    void savedBeforeItsPluginsLoadAProjectKeepsTheirState() {
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString path = saveWithPlugins(studio, dir, {QStringLiteral("A")});
        const QJsonValue saved =
            readJson(path)["tracks"].toArray()[0].toObject()["devices"].toArray()[0].toObject()["state"];
        loadProject(studio.project, path);
        BridgeTestAccess::stopPluginTimer(bridge);
        bridge.storePluginStates();
        const QString again = dir.path("again.gilproj");
        saveProject(studio.project, again);
        QCOMPARE(readJson(again)["tracks"].toArray()[0].toObject()["devices"].toArray()[0].toObject()["state"], saved);
    }

    void effectsOnTheMaster() {
        test::TempDir dir;
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString wav = test::writeWav(dir.path("dc.wav"), std::vector<float>(2 * test::kSampleRate, 0.5f), 2);
        studio.clipTrack(wav);
        const QString utility = studio.edit.addDevice(kMaster, QStringLiteral("utility"));
        QVERIFY(bridge.engineDeviceId(kMaster, utility));
        studio.edit.setDeviceParam(kMaster, utility, QStringLiteral("gain"), -6.0206);
        studio.render(8000);
        QVERIFY(std::abs(studio.render(8000)[2 * 6000] - 0.25f) < 0.25f * 1e-3f);
        const QString effect = studio.edit.addDevice(kMaster, kPluginKind, effect_);
        QVERIFY(bridge.engineDeviceId(kMaster, effect));
        bridge.pollPlugins();
        QVERIFY(bridge.pluginErrors().isEmpty());
        // Its devices can be automated, in the master's lanes.
        const std::vector<ParamGroup> groups = bridge.paramGroups(kMaster);
        QCOMPARE(groups.size(), size_t{4});
        QCOMPARE(groups[0].id, QStringLiteral("mixer"));
        QCOMPARE(groups[1].id, QStringLiteral("song"));  // the Song Tempo lane
        QCOMPARE(groups[2].id, utility);
        QCOMPARE(groups[3].id, effect);
        QCOMPARE(groups[0].specs[0].name, QStringLiteral("Master Volume"));
        const QString key = automation::deviceKey(utility, QStringLiteral("gain"));
        studio.edit.setEnvelope(kMaster, key, {{0.0, 0.0, 0.0}});
        QVERIFY(bridge.isAutomated(kMaster, key));
        QVERIFY(studio.render(8000)[2 * 6000] < 0.01f);  // the utility's lowest gain
        // Saved and opened again: with its plug-in's state.
        bridge.storePluginStates();
        const QString path = dir.path("master.gilproj");
        saveProject(studio.project, path);
        studio.project.clear();
        QVERIFY(studio.project.master().devices.empty() && !bridge.engineDeviceId(kMaster, utility));
        loadProject(studio.project, path);
        QCOMPARE(studio.project.master().devices.size(), size_t{2});
        QTRY_VERIFY_WITH_TIMEOUT(bridge.pluginsPending() == 0, 5000);
        QVERIFY(bridge.engineDeviceId(kMaster, effect));
        QVERIFY(bridge.isAutomated(kMaster, key));
        // Undo takes a device off the master again.
        studio.edit.removeDevices(kMaster, {effect});
        QVERIFY(!bridge.engineDeviceId(kMaster, effect));
        studio.stack.undo();
        QVERIFY(bridge.engineDeviceId(kMaster, effect));
    }

    void shutdownUnloadsEveryPlugin() {
        Studio studio;
        EngineBridge& bridge = *studio.bridge;
        const QString track = studio.edit.addAudioTrack();
        const QString fx = studio.edit.addDevice(track, kPluginKind, effect_);
        studio.edit.addDevice(kMaster, kPluginKind, effect_);
        QVERIFY(bridge.engineDeviceId(track, fx));
        bridge.shutdown();
        QVERIFY(!bridge.engineDeviceId(track, fx));
        QVERIFY(studio.engine.chainProcessors(studio.engine.trackChain(sub::Engine::kMaster)).empty());
    }
};

QTEST_GUILESS_MAIN(TestBridgePlugins)
#include "test_bridge_plugins.moc"
