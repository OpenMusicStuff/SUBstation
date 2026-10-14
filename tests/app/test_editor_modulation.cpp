// Modulation through the editor and the bridge: modulators (the LFO, the
// Envelope) mapped to parameters of devices on their track, over a range and
// upside down, one modulator a parameter, undone and redone; what can't be
// mapped; the mappings following their devices (deleted, moved, copied with
// them, a track duplicated) and saved; Map (ModulationLearn) taking the next
// parameter touched; and the engine playing it all as the model says (a
// modulator switched off, a mapping gone, giving the parameter its own value
// back).

#include "EditorFixture.h"
#include "SessionFixture.h"
#include "TestSupport.h"

#include "io/Serialization.h"
#include "model/Devices.h"
#include "model/Errors.h"
#include "session/ModulationLearn.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace sub::app;
using test::EditorFixture;
using test::SessionFixture;
using test::TempDir;
namespace autom = sub::app::automation;

namespace {

constexpr int kRate = 48000;

QString dcWav(const TempDir& dir) {
    return test::writeWav(dir.path(QStringLiteral("dc.wav")), std::vector<float>(2 * kRate, 0.5f), 2);
}

bool near(double a, double b, double tolerance = 2e-3) { return std::abs(a - b) <= tolerance; }

// The modulations of a device on a track.
const std::vector<ModulationMapping>& modulationsOf(EditorFixture& f, const QString& track, const QString& device) {
    return f.project.device(track, device).modulations;
}

// The normalized value of the Utility's gain for `db`.
double gainAt(double db) { return (db + 60.0) / 84.0; }

}  // namespace

class TestEditorModulation : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void modulatorsAreBuiltInDevicesOfTheirOwn() {
        QVERIFY(isModulator("lfo") && isModulator("envelope"));
        QVERIFY(!isModulator("utility") && !isModulator("synth") && !isModulator("nothing"));
        QVERIFY(!isInstrument("lfo"));
        QCOMPARE(builtinDevice("lfo")->category, QStringLiteral("Modulators"));
        const auto categories = builtinCategories();
        QCOMPARE(categories.back().first, QStringLiteral("Modulators"));
        QCOMPARE(categories.back().second, (QStringList{"envelope", "lfo"}));
        QCOMPARE(ModulationMapping({"d", "p", 0.2, 0.6, false}).target(0.5), 0.4);
        QCOMPARE(ModulationMapping({"d", "p", 0.2, 0.6, true}).target(0.25), 0.5);
        QCOMPARE(ModulationMapping({"d", "p", 0.8, 0.2, false}).target(1.5), 0.2);
    }

    void mappingAParameterAndItsRange() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        const QString lfo = f.editor.addDevice(track, "lfo");
        const QString utility = f.editor.addDevice(track, "utility");
        const int steps = f.stack.count();
        f.editor.mapModulation(track, lfo, utility, "gain");
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Map Modulation"));
        QCOMPARE(modulationsOf(f, track, lfo), (std::vector<ModulationMapping>{{utility, "gain", 0.0, 1.0, false}}));
        QCOMPARE(f.editor.modulatorOf(track, utility, "gain"), lfo);
        QCOMPARE(f.editor.modulatorOf(track, utility, "pan"), QString());
        QCOMPARE(f.editor.modulators(track), (QStringList{lfo}));
        // The range: a drag is one undo step; held to 0..1; the other way round is allowed.
        f.editor.setModulationRange(track, lfo, utility, "gain", 0.2, 0.5, "drag");
        f.editor.setModulationRange(track, lfo, utility, "gain", -1.0, 0.7, "drag");
        QCOMPARE(f.stack.count(), steps + 2);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Change Modulation Range"));
        QCOMPARE(modulationsOf(f, track, lfo).front().low, 0.0);
        QCOMPARE(modulationsOf(f, track, lfo).front().high, 0.7);
        f.editor.setModulationRange(track, lfo, utility, "gain", 0.9, 0.1, "other");
        QCOMPARE(modulationsOf(f, track, lfo).front().low, 0.9);
        f.editor.setModulationInverted(track, lfo, utility, "gain", true);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Invert Modulation"));
        QVERIFY(modulationsOf(f, track, lfo).front().invert);
        f.stack.undo();
        f.stack.undo();
        QCOMPARE(modulationsOf(f, track, lfo).front(), (ModulationMapping{utility, "gain", 0.0, 0.7, false}));
        // Unmapped; and everything at once.
        f.editor.mapModulation(track, lfo, utility, "pan");
        f.editor.unmapModulation(track, utility, "gain");
        QCOMPARE(f.stack.undoText(), QStringLiteral("Remove Modulation"));
        QCOMPARE(modulationsOf(f, track, lfo), (std::vector<ModulationMapping>{{utility, "pan", 0.0, 1.0, false}}));
        f.editor.mapModulation(track, lfo, utility, "width");
        f.editor.clearModulations(track, lfo);
        QVERIFY(modulationsOf(f, track, lfo).empty());
        f.stack.undo();
        QCOMPARE(modulationsOf(f, track, lfo).size(), size_t(2));
    }

    void theAmountAndPolarityFromTheKnobsHandle() {
        // The maths: unipolar up from the base, bipolar either side of it, held to 0..1.
        ModulationMapping m{"d", "p"};
        m.setAmount(0.3, 0.4);
        QCOMPARE(m.low, 0.3);
        QVERIFY(near(m.high, 0.7, 1e-12));
        QVERIFY(near(m.amount(), 0.4, 1e-12));
        m.setAmount(0.3, -0.5);  // the other way: down from it
        QCOMPARE(m.low, 0.3);
        QCOMPARE(m.high, 0.0);
        m.bipolar = true;
        m.setAmount(0.5, 0.2);
        QVERIFY(near(m.low, 0.3, 1e-12) && near(m.high, 0.7, 1e-12));
        QVERIFY(near(m.amount(), 0.2, 1e-12));
        QVERIFY(near(m.target(0.5), 0.5, 1e-12));  // the modulator's middle: the knob's own value

        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        const QString lfo = f.editor.addDevice(track, "lfo");
        const QString utility = f.editor.addDevice(track, "utility");
        f.editor.mapModulation(track, lfo, utility, "gain");
        const int steps = f.stack.count();
        // A drag of the handle: one undo step, from the knob's value.
        f.editor.setModulationAmount(track, lfo, utility, "gain", 0.1, 0.4, "drag");
        f.editor.setModulationAmount(track, lfo, utility, "gain", 0.25, 0.4, "drag");
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Change Modulation Amount"));
        QCOMPARE(modulationsOf(f, track, lfo).front().low, 0.4);
        QVERIFY(near(modulationsOf(f, track, lfo).front().high, 0.65, 1e-12));
        // Alt+Shift-click: bipolar round the knob's value, the amount kept; and back.
        f.editor.setModulationBipolar(track, lfo, utility, "gain", true, 0.5);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Bipolar Modulation"));
        const ModulationMapping bipolar = modulationsOf(f, track, lfo).front();
        QVERIFY(bipolar.bipolar);
        QVERIFY(near(bipolar.low, 0.25, 1e-12) && near(bipolar.high, 0.75, 1e-12));
        f.editor.setModulationBipolar(track, lfo, utility, "gain", true, 0.5);  // (already: nothing)
        QCOMPARE(f.stack.count(), steps + 2);
        f.editor.setModulationBipolar(track, lfo, utility, "gain", false, 0.5);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Unipolar Modulation"));
        QVERIFY(!modulationsOf(f, track, lfo).front().bipolar);
        QVERIFY(near(modulationsOf(f, track, lfo).front().low, 0.5, 1e-12));
        QVERIFY(near(modulationsOf(f, track, lfo).front().high, 0.75, 1e-12));
        f.stack.undo();
        QCOMPARE(modulationsOf(f, track, lfo).front(), bipolar);
        // Saved and read back.
        const QJsonObject saved = deviceToJson(f.project.device(track, lfo));
        QVERIFY(saved.value("modulations").toArray().at(0).toObject().value("bipolar").toBool());
        QCOMPARE(deviceFromJson(saved).modulations.front(), bipolar);
    }

    void aParameterFollowsOneModulator() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        const QString lfo = f.editor.addDevice(track, "lfo");
        const QString envelope = f.editor.addDevice(track, "envelope");
        const QString utility = f.editor.addDevice(track, "utility");
        f.editor.mapModulation(track, lfo, utility, "gain");
        const int steps = f.stack.count();
        f.editor.mapModulation(track, envelope, utility, "gain", 0.1, 0.9, true);  // one step: it moves over
        QCOMPARE(f.stack.count(), steps + 1);
        QVERIFY(modulationsOf(f, track, lfo).empty());
        QCOMPARE(modulationsOf(f, track, envelope), (std::vector<ModulationMapping>{{utility, "gain", 0.1, 0.9, true}}));
        QCOMPARE(f.editor.modulatorOf(track, utility, "gain"), envelope);
        f.stack.undo();
        QCOMPARE(f.editor.modulatorOf(track, utility, "gain"), lfo);
        // A modulator moves another one's parameters too.
        f.editor.mapModulation(track, envelope, lfo, "rate");
        QCOMPARE(f.editor.modulatorOf(track, lfo, "rate"), envelope);
    }

    void whatCantBeModulated() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        const QString other = f.editor.addAudioTrack();
        const QString lfo = f.editor.addDevice(track, "lfo");
        const QString utility = f.editor.addDevice(track, "utility");
        const QString far = f.editor.addDevice(other, "utility");
        const QString sidechain = f.editor.addDevice(track, "sidechain");
        const int steps = f.stack.count();
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.mapModulation(track, utility, lfo, "rate"));  // not a modulator
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.mapModulation(track, lfo, lfo, "rate"));      // itself
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.mapModulation(track, lfo, far, "gain"));      // another track's
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.mapModulation(track, lfo, utility, "nothing"));
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.mapModulation(track, lfo, sidechain, "p1_x"));       // hidden
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.mapModulation(track, lfo, sidechain, "lookahead"));  // not automatable
        const QString rack = f.editor.groupDevices(track, {utility});
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.mapModulation(track, lfo, rack, "macro1"));
        QCOMPARE(f.stack.count(), steps + 1);  // (the grouping)
        QVERIFY(!f.editor.canModulate(track, rack, "macro1"));
        QVERIFY(f.editor.canModulate(track, utility, "gain"));
        // The Q_INVOKABLE form says why on `refused`.
        QVERIFY(!f.editor.tryMapModulation(track, lfo, sidechain, "p1_x"));
        QVERIFY(!f.messages.isEmpty());
        // A device in a rack can be.
        f.editor.mapModulation(track, lfo, utility, "gain");
        QCOMPARE(f.editor.modulatorOf(track, utility, "gain"), lfo);
    }

    void mappingsFollowTheirDevices() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        const QString other = f.editor.addAudioTrack();
        const QString lfo = f.editor.addDevice(track, "lfo");
        const QString a = f.editor.addDevice(track, "utility");
        const QString b = f.editor.addDevice(track, "utility");
        f.editor.mapModulation(track, lfo, a, "gain");
        f.editor.mapModulation(track, lfo, b, "pan");
        // Its target deleted: its mapping goes in the same step, and comes back with it.
        f.editor.removeDevice(track, a);
        QCOMPARE(modulationsOf(f, track, lfo), (std::vector<ModulationMapping>{{b, "pan", 0.0, 1.0, false}}));
        f.stack.undo();
        QCOMPARE(modulationsOf(f, track, lfo).size(), size_t(2));
        // Grouped into a rack, still moved.
        f.editor.groupDevices(track, {a});
        QCOMPARE(modulationsOf(f, track, lfo).size(), size_t(2));
        // Its target moved to another track: unmapped.
        QVERIFY(f.editor.moveDevicesToTrack(track, {b}, other, -1));
        QCOMPARE(modulationsOf(f, track, lfo), (std::vector<ModulationMapping>{{a, "gain", 0.0, 1.0, false}}));
        f.stack.undo();
        // The modulator moved to another track: it moves nothing there.
        QVERIFY(f.editor.moveDevicesToTrack(track, {lfo}, other, -1));
        QVERIFY(modulationsOf(f, other, lfo).empty());
    }

    void copiesTakeTheirMappingsAlong() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        const QString lfo = f.editor.addDevice(track, "lfo");
        const QString utility = f.editor.addDevice(track, "utility");
        f.editor.mapModulation(track, lfo, utility, "gain", 0.25, 0.75, true);
        // Pasted together: the copy moves the copy.
        const QStringList pasted = f.editor.pasteDevices(track, f.editor.copyDevices(track, {lfo, utility}));
        QCOMPARE(pasted.size(), 2);
        QCOMPARE(modulationsOf(f, track, pasted[0]),
                 (std::vector<ModulationMapping>{{pasted[1], "gain", 0.25, 0.75, true}}));
        QCOMPARE(f.editor.modulatorOf(track, utility, "gain"), lfo);
        // Alone: it moves nothing yet.
        const QStringList alone = f.editor.pasteDevices(track, f.editor.copyDevices(track, {lfo}));
        QVERIFY(modulationsOf(f, track, alone[0]).empty());
        // A track duplicated: its modulators move its copies.
        const QStringList copies = f.editor.duplicateTracks({track});
        QCOMPARE(copies.size(), 1);
        const Track& copy = f.track(copies[0]);
        QCOMPARE(copy.devices.size(), size_t(5));
        const Device& lfoCopy = copy.devices[0];
        QCOMPARE(lfoCopy.modulations.size(), size_t(1));
        QCOMPARE(lfoCopy.modulations[0].deviceId, copy.devices[1].id);
        QVERIFY(lfoCopy.modulations[0].invert);
        QVERIFY(modulationsOf(f, track, lfo).front().deviceId == utility);  // (the original's own)
    }

    void savedAndLoaded() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        const QString envelope = f.editor.addDevice(track, "envelope");
        const QString utility = f.editor.addDevice(track, "utility");
        f.editor.mapModulation(track, envelope, utility, "width", 0.1, 0.6, true);
        const QJsonObject saved = projectToJson(f.project);
        QCOMPARE(saved[QStringLiteral("version")].toInt(), kProjectVersion);
        QCOMPARE(kProjectVersion, 23);
        const QJsonObject device = saved[QStringLiteral("tracks")].toArray()[0].toObject()[QStringLiteral("devices")]
                                       .toArray()[0].toObject();
        QCOMPARE(device[QStringLiteral("modulations")].toArray().size(), 1);
        Project loaded;
        loadInto(loaded, saved);
        QCOMPARE(loaded.device(track, envelope).modulations,
                 (std::vector<ModulationMapping>{{utility, "width", 0.1, 0.6, true}}));
        // A file naming a device that isn't there, the modulator itself, a parameter twice, or of a
        // device that isn't a modulator: those go.
        QJsonObject edited = saved;
        QJsonArray tracks = edited[QStringLiteral("tracks")].toArray();
        QJsonObject first = tracks[0].toObject();
        QJsonArray devices = first[QStringLiteral("devices")].toArray();
        QJsonObject mod = devices[0].toObject();
        QJsonArray modulations = mod[QStringLiteral("modulations")].toArray();
        modulations.append(QJsonObject{{"device", "gone"}, {"param", "gain"}});
        modulations.append(QJsonObject{{"device", envelope}, {"param", "depth"}});
        modulations.append(QJsonObject{{"device", utility}, {"param", "width"}, {"low", 2.0}});
        mod[QStringLiteral("modulations")] = modulations;
        devices[0] = mod;
        QJsonObject util = devices[1].toObject();
        util[QStringLiteral("modulations")] = QJsonArray{QJsonObject{{"device", envelope}, {"param", "depth"}}};
        devices[1] = util;
        first[QStringLiteral("devices")] = devices;
        tracks[0] = first;
        edited[QStringLiteral("tracks")] = tracks;
        Project repaired;
        loadInto(repaired, edited);
        QCOMPARE(repaired.device(track, envelope).modulations,
                 (std::vector<ModulationMapping>{{utility, "width", 0.1, 0.6, true}}));
        QVERIFY(repaired.device(track, utility).modulations.empty());
        // A modulator's preset moves nothing (its targets are another track's business).
        const Device preset = presetDevice(deviceToPreset(f.project.device(track, envelope)));
        QVERIFY(preset.modulations.empty());
    }

    void mapTakesTheNextParameterTouched() {
        EditorFixture f;
        ModulationLearn learn(&f.project, &f.editor);
        QSignalSpy said(&learn, &ModulationLearn::statusMessage);
        const QString track = f.editor.addAudioTrack();
        const QString other = f.editor.addAudioTrack();
        const QString lfo = f.editor.addDevice(track, "lfo");
        const QString utility = f.editor.addDevice(track, "utility");
        const QString far = f.editor.addDevice(other, "utility");
        learn.start(track, utility);  // not a modulator: nothing
        QVERIFY(!learn.active());
        learn.toggle(track, lfo);
        QVERIFY(learn.active() && learn.isLearning(lfo));
        // Its own controls, another track's, a switch: it keeps waiting.
        f.editor.setDeviceParam(track, lfo, "rate", 2.0);
        f.editor.touchParameter(other, autom::deviceKey(far, "gain"));
        f.editor.touchParameter(track, autom::deviceOnKey(utility));
        f.editor.touchParameter(track, autom::kMixerVolume);
        QVERIFY(learn.active());
        QVERIFY(modulationsOf(f, track, lfo).empty());
        // A knob of the track's: mapped, and done.
        f.editor.touchParameter(track, autom::deviceKey(utility, "pan"));
        QVERIFY(!learn.active());
        QCOMPARE(modulationsOf(f, track, lfo), (std::vector<ModulationMapping>{{utility, "pan", 0.0, 1.0, false}}));
        QVERIFY(said.last().at(0).toString().contains(QStringLiteral("Pan")));
        // Toggled off; given up when its modulator goes.
        learn.toggle(track, lfo);
        learn.toggle(track, lfo);
        QVERIFY(!learn.active());
        learn.start(track, lfo);
        f.editor.removeDevice(track, lfo);
        QVERIFY(!learn.active());
    }

    // --- The engine plays it ---

    void theEngineMovesWhatTheModelMaps() {
        SessionFixture f;
        TempDir dir;
        const QString track = f.clipTrack(dcWav(dir));
        const QString lfo = f.editor().addDevice(track, "lfo");
        const QString utility = f.editor().addDevice(track, "utility");
        QVERIFY(near(f.level(), 0.5));
        // A still LFO (Depth 0: the middle) mapped over -6 dB..-6 dB holds the gain there.
        f.editor().setDeviceParam(track, lfo, "depth", 0.0);
        f.editor().mapModulation(track, lfo, utility, "gain", gainAt(-6.0206), gainAt(-6.0206));
        QVERIFY(f.bridge().isModulated(track, autom::deviceKey(utility, "gain")));
        QVERIFY(near(f.level(), 0.25));
        // Over 0 dB at its middle: -60 dB at 0, +24 dB at 1, inverted the same.
        f.editor().setModulationRange(track, lfo, utility, "gain", gainAt(-24.0), gainAt(24.0));
        QVERIFY(near(f.level(), 0.5));
        f.editor().setModulationInverted(track, lfo, utility, "gain", true);
        QVERIFY(near(f.level(), 0.5));
        f.editor().setDeviceParam(track, lfo, "offset", 100.0);  // at 1: inverted, the low end
        QVERIFY(near(f.level(), 0.5 * std::pow(10.0, -24.0 / 20.0), 1e-4));
        // Its own value counts again once it is unmapped, or its modulator switched off.
        f.editor().setDeviceEnabled(track, lfo, false);
        QVERIFY(!f.bridge().isModulated(track, autom::deviceKey(utility, "gain")));
        QVERIFY(near(f.level(), 0.5));
        f.stack().undo();
        QVERIFY(near(f.level(), 0.5 * std::pow(10.0, -24.0 / 20.0), 1e-4));
        f.editor().unmapModulation(track, utility, "gain");
        QVERIFY(near(f.level(), 0.5));
        // Modulation wins over automation, which plays again after.
        f.editor().setEnvelope(track, autom::deviceKey(utility, "gain"), test::env({{0.0, gainAt(-6.0206)}}));
        QVERIFY(near(f.level(), 0.25));
        f.stack().undo();  // (the envelope)
        f.stack().undo();  // (unmapping: mapped again)
        f.editor().setEnvelope(track, autom::deviceKey(utility, "gain"), test::env({{0.0, gainAt(-6.0206)}}));
        QVERIFY(near(f.level(), 0.5 * std::pow(10.0, -24.0 / 20.0), 1e-4));
        f.editor().unmapModulation(track, utility, "gain");
        QVERIFY(near(f.level(), 0.25));
    }
};

QTEST_GUILESS_MAIN(TestEditorModulation)
#include "test_editor_modulation.moc"
