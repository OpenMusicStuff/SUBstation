// The built-in Vital in the application layer: Vital's presets (.vital) read
// into a device and written from one (io/VitalPresets.h), as presets anywhere
// a preset loads (loadPreset, the preset library, files dragged in); its
// modulation routing read from its state (model/Vital.h) and edited through the
// editor (setVitalModulation, addVitalModulation: one undo step each, the
// slot's settings with it); its parameters' texts as Vital shows them.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "browser/BrowserMime.h"
#include "builtin/VitalPatch.h"
#include "io/Presets.h"
#include "io/Serialization.h"
#include "io/VitalPresets.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Errors.h"
#include "model/Vital.h"

#include <QFile>
#include <QMimeData>
#include <QTest>
#include <QUrl>

using namespace sub::app;
using test::EditorFixture;
using test::kinds;

namespace {

// A MIDI track with a Vital: (track, device).
std::pair<QString, QString> vitalTrack(EditorFixture& f) {
    const QString track = f.editor.addMidiTrack(-1, {}, QStringLiteral("vital"));
    return {track, f.track(track).devices.front().id};
}

void writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

}  // namespace

class TestVital : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void isABuiltInInstrument() {
        const BuiltinDevice* vital = builtinDevice(QStringLiteral("vital"));
        QVERIFY(vital != nullptr);
        QVERIFY(vital->instrument);
        QCOMPARE(vital->name, QStringLiteral("Vital"));
        QCOMPARE(vital->defaults.size(), qsizetype(sub::vitalpatch::params().size()));
        QVERIFY(builtinParamInfo(QStringLiteral("vital"), QStringLiteral("osc_1_level"))->ownText);
        EditorFixture f;
        const auto [track, device] = vitalTrack(f);
        QCOMPARE(kinds(f.track(track).devices), QStringList{QStringLiteral("vital")});
        QCOMPARE(f.track(track).name, QStringLiteral("1 Vital"));
    }

    void presetsRoundTrip() {
        test::TempDir dir;
        EditorFixture f;
        const auto [track, device] = vitalTrack(f);
        f.editor.setDeviceParam(track, device, QStringLiteral("osc_1_level"), 0.25);
        QVERIFY(f.editor.addVitalModulation(track, device, QStringLiteral("lfo_1"), QStringLiteral("filter_1_cutoff")) == 0);
        const QString path = dir.path(QStringLiteral("Bright Pad.vital"));
        saveVitalPreset(f.project.device(track, device), path);
        QVERIFY(QFile::exists(path));

        const Device loaded = loadVitalPreset(path);
        QCOMPARE(loaded.kind, QStringLiteral("vital"));
        QCOMPARE(loaded.params.value(QStringLiteral("osc_1_level")), 0.25);
        QCOMPARE(loaded.params.size(), qsizetype(sub::vitalpatch::params().size()));
        const std::vector<VitalModulation> modulations = vitalModulations(loaded);
        QCOMPARE(modulations.size(), size_t(1));
        QCOMPARE(modulations.front().source, QStringLiteral("lfo_1"));
        QCOMPARE(modulations.front().destination, QStringLiteral("filter_1_cutoff"));
        QVERIFY(!vitalStateValue(loaded, QStringLiteral("wavetables")).isEmpty());
        // Named by its file (the device had no preset name).
        QCOMPARE(vitalStateValue(loaded, QStringLiteral("preset_name")), QStringLiteral("Bright Pad"));

        // loadPreset reads it too (as any preset loads), and it loads into a Vital in place, one undo step.
        const Device preset = loadPreset(path);
        QCOMPARE(preset.kind, QStringLiteral("vital"));
        const auto [other, otherDevice] = vitalTrack(f);
        QVERIFY(loadsInto(preset, f.project.device(other, otherDevice)));
        const int steps = f.stack.count();
        QVERIFY(f.editor.loadPresetInto(other, otherDevice, preset));
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(f.project.device(other, otherDevice).params.value(QStringLiteral("osc_1_level")), 0.25);
        QCOMPARE(vitalModulations(f.project.device(other, otherDevice)).size(), size_t(1));
        f.stack.undo();
        QCOMPARE(vitalModulations(f.project.device(other, otherDevice)).size(), size_t(0));
        // Not into another kind of device.
        QVERIFY(!loadsInto(preset, newDevice(QStringLiteral("synth"))));
    }

    void badPresetsRefused() {
        test::TempDir dir;
        const QString text = dir.path(QStringLiteral("notes.vital"));
        writeFile(text, "hello");
        QVERIFY_THROWS_EXCEPTION(ProjectFileError, loadVitalPreset(text));
        QVERIFY_THROWS_EXCEPTION(ProjectFileError, loadPreset(text));
        QVERIFY_THROWS_EXCEPTION(ProjectFileError, loadVitalPreset(dir.path(QStringLiteral("missing.vital"))));
    }

    void presetsLibraryAndDragsTakeVitalPresets() {
        test::TempDir dir;
        QVERIFY(QDir().mkpath(dir.path(QStringLiteral("Vital"))));
        writeFile(dir.path(QStringLiteral("Vital/Lead.vital")),
                  QByteArray::fromStdString(sub::vitalpatch::writePreset({})));
        writeFile(dir.path(QStringLiteral("Vital/readme.txt")), "x");
        const std::vector<PresetFile> found = listPresets(dir.path());
        QCOMPARE(found.size(), size_t(1));
        QCOMPARE(found.front().name, QStringLiteral("Lead"));
        QCOMPARE(found.front().group, QStringLiteral("Vital"));

        // A .vital file dragged in from a file manager is a preset; other files aren't.
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(dir.path(QStringLiteral("Vital/Lead.vital"))),
                      QUrl::fromLocalFile(dir.path(QStringLiteral("Vital/readme.txt")))});
        QCOMPARE(presetPaths(&mime), QStringList{dir.path(QStringLiteral("Vital/Lead.vital"))});
    }

    void modulationsEditedSlotBySlot() {
        EditorFixture f;
        const auto [track, device] = vitalTrack(f);
        const auto slots = [&] { return vitalModulations(f.project.device(track, device)); };
        const auto value = [&](const QString& id) { return f.project.device(track, device).params.value(id); };
        QVERIFY(slots().empty());

        // Added in the first free slot, one undo step; an LFO's made bipolar, as Vital makes it.
        int steps = f.stack.count();
        QCOMPARE(f.editor.addVitalModulation(track, device, QStringLiteral("lfo_1"), QStringLiteral("osc_1_level")), 0);
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(value(vitalSlotParam(0, QStringLiteral("bipolar"))), 1.0);
        QCOMPARE(f.editor.addVitalModulation(track, device, QStringLiteral("env_2"), QStringLiteral("filter_1_cutoff")), 1);
        QCOMPARE(value(vitalSlotParam(1, QStringLiteral("bipolar"))), 0.0);
        // The same pair again: its slot.
        QCOMPARE(f.editor.addVitalModulation(track, device, QStringLiteral("lfo_1"), QStringLiteral("osc_1_level")), 0);
        QCOMPARE(slots().size(), size_t(2));
        // Unknown sources and destinations refused.
        QCOMPARE(f.editor.addVitalModulation(track, device, QStringLiteral("nothing"), QStringLiteral("osc_1_level")), -1);
        QCOMPARE(f.editor.addVitalModulation(track, device, QStringLiteral("lfo_1"), QStringLiteral("nothing")), -1);

        // Removed: its slot's settings back to their defaults, one undo step; a new one takes the slot freed.
        f.editor.setDeviceParam(track, device, vitalSlotParam(0, QStringLiteral("amount")), 0.7);
        steps = f.stack.count();
        QVERIFY(f.editor.setVitalModulation(track, device, 0, QString(), QString()));
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(slots().size(), size_t(1));
        QCOMPARE(slots().front().slot, 1);
        QCOMPARE(value(vitalSlotParam(0, QStringLiteral("amount"))), 0.0);
        QCOMPARE(value(vitalSlotParam(0, QStringLiteral("bipolar"))), 0.0);
        f.stack.undo();
        QCOMPARE(slots().size(), size_t(2));
        QCOMPARE(value(vitalSlotParam(0, QStringLiteral("amount"))), 0.7);
        f.stack.redo();
        QCOMPARE(f.editor.addVitalModulation(track, device, QStringLiteral("macro_control_1"), QStringLiteral("volume")), 0);

        // A slot's destination changed in place.
        QVERIFY(f.editor.setVitalModulation(track, device, 1, QStringLiteral("env_2"), QStringLiteral("filter_2_cutoff")));
        QCOMPARE(vitalModulationSlots(f.project.device(track, device))[1].destination, QStringLiteral("filter_2_cutoff"));
        // Not on another kind of device, nor past the 64 slots.
        const QString synthTrack = f.editor.addMidiTrack(-1, {}, QStringLiteral("synth"));
        QCOMPARE(f.editor.addVitalModulation(synthTrack, f.track(synthTrack).devices.front().id, QStringLiteral("lfo_1"),
                                             QStringLiteral("volume")),
                 -1);
        QVERIFY(!f.editor.setVitalModulation(track, device, 64, QStringLiteral("lfo_1"), QStringLiteral("volume")));

        // Every slot taken: none more.
        for (int i = 0; i < kVitalModulationSlots; ++i)
            f.editor.setVitalModulation(track, device, i, QStringLiteral("lfo_2"), vitalDestinations().at(i));
        QCOMPARE(slots().size(), size_t(kVitalModulationSlots));
        QCOMPARE(f.editor.addVitalModulation(track, device, QStringLiteral("lfo_3"), QStringLiteral("volume")), -1);
    }

    void wavetablesAndLfoShapes() {
        EditorFixture f;
        const auto [track, device] = vitalTrack(f);
        const auto wavetables = [&] { return vitalWavetableNames(f.project.device(track, device)); };
        QCOMPARE(wavetables(), (QStringList{QStringLiteral("Init"), QStringLiteral("Init"), QStringLiteral("Init")}));
        QCOMPARE(vitalShapeNames().at(1), QStringLiteral("Sine"));
        const int steps = f.stack.count();
        QVERIFY(f.editor.setVitalWavetableShape(track, device, 1, 1));  // a sine on oscillator 2, one undo step
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(wavetables().at(1), QStringLiteral("Sine"));
        f.stack.undo();
        QCOMPARE(wavetables().at(1), QStringLiteral("Init"));
        QVERIFY(!f.editor.setVitalWavetableShape(track, device, 3, 1));  // (three oscillators)
        QVERIFY(!f.editor.setVitalWavetableShape(track, device, 0, 99));
        QVERIFY(f.editor.setVitalLfoShape(track, device, 7, 5));
        QCOMPARE(vitalLfoNames(f.project.device(track, device)).at(7), QStringLiteral("Square"));
        QCOMPARE(vitalLfoNames(f.project.device(track, device)).at(0), QStringLiteral("Triangle"));

        // From Vital's own wavetable files, named after them; not from what isn't one.
        test::TempDir dir;
        const QString round = dir.path(QStringLiteral("Round.vitaltable"));
        writeFile(round, QByteArray::fromStdString(sub::vitalpatch::wavetableOfShape(sub::vitalpatch::Shape::Sine)));
        QVERIFY(f.editor.setVitalWavetable(track, device, 2, vitalWavetableFromFile(round)));
        QCOMPARE(wavetables().at(2), QStringLiteral("Round"));
        QVERIFY_THROWS_EXCEPTION(ProjectFileError, vitalWavetableFromFile(dir.path(QStringLiteral("missing.wav"))));
        const QString text = dir.path(QStringLiteral("text.vitaltable"));
        writeFile(text, "hello");
        QVERIFY_THROWS_EXCEPTION(ProjectFileError, vitalWavetableFromFile(text));
    }

    void sourcesAndDestinationsNamed() {
        QVERIFY(vitalSources().contains(QStringLiteral("lfo_8")));
        QVERIFY(vitalDestinations().contains(QStringLiteral("osc_3_wave_frame")));
        QCOMPARE(vitalSourceName(QStringLiteral("random_2")), QStringLiteral("Random 2"));
        QCOMPARE(vitalDestinationName(QStringLiteral("osc_1_level")), QStringLiteral("Oscillator 1 Level"));
        QCOMPARE(vitalSlotParam(63, QStringLiteral("amount")), QStringLiteral("modulation_64_amount"));
        QVERIFY(!vitalDestinations().contains(QStringLiteral("beats_per_minute")));  // (the host's)
    }
};

QTEST_MAIN(TestVital)
#include "test_vital.moc"
