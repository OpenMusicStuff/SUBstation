// Drum Racks and the Sampler's samples through the editor: a Drum Rack is an
// instrument rack even while empty; samples loaded onto its pads (a pad a key,
// each with a Sampler, named after its sample), pads added, duplicated and
// moved to other keys, locked; saving and loading them; samples loaded by
// hand, and swapped for similar sounds, with the reference those come from
// (model/SamplerState.h); the stepping itself.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "io/Serialization.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/SamplerState.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace sub::app;
using test::EditorFixture;

namespace {

QString sampleOf(const Device& device) { return samplerState::sample(deviceState::fromModel(device.state)); }
QString referenceOf(const Device& device) { return samplerState::reference(deviceState::fromModel(device.state)); }

// A MIDI track with a Drum Rack: (track, rack).
std::pair<QString, QString> drums(EditorFixture& f) {
    const QString track = f.editor.addMidiTrack(-1, {}, kDrumRackKind);
    return {track, f.track(track).devices.at(0).id};
}

}  // namespace

class TestEditorDrumRacks : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void aDrumRackIsAnInstrumentRackEvenEmpty() {
        EditorFixture f;
        const auto [track, rackId] = drums(f);
        const Device& rack = f.project.device(track, rackId);
        QVERIFY(rack.isRack() && rack.isDrumRack());
        QVERIFY(rack.chains.empty());
        QVERIFY(deviceIsInstrument(rack));
        QVERIFY(isInstrument(kDrumRackKind));
        QVERIFY(isDeviceKind(kDrumRackKind));
        QCOMPARE(kindName(rack), QStringLiteral("Drum Rack"));
        QCOMPARE(deviceName(rack), QStringLiteral("Drum Rack"));
        QCOMPARE(macroCount(rack), kDefaultMacroCount);
        // Added to a MIDI track, it goes first (and replaces the instrument there), as instruments do.
        const QString keys = f.editor.addMidiTrack(-1, {}, QStringLiteral("synth"));
        const QString added = f.editor.addDevice(keys, kDrumRackKind);
        QCOMPARE(f.track(keys).devices.size(), size_t(1));
        QCOMPARE(f.track(keys).devices[0].id, added);
        QVERIFY(f.editor.addDevice(f.editor.addAudioTrack(), kDrumRackKind).isEmpty());
        // A preset of one loads into a Drum Rack only.
        Device instrumentRack = newRack({newChain("A", {newDevice("synth")})});
        QVERIFY(!loadsInto(instrumentRack, rack));
        QVERIFY(loadsInto(newDrumRack(), rack));
        QCOMPARE(drumNoteName(36), QStringLiteral("C1"));
        QCOMPARE(drumNoteName(60), QStringLiteral("C3"));
        QCOMPARE(drumNoteName(39), QStringLiteral("D#1"));
    }

    void samplesLoadOntoPads() {
        EditorFixture f;
        const auto [track, rackId] = drums(f);
        const int before = f.stack.count();
        const QString kick = f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/samples/Kick 1.wav"));
        QVERIFY(!kick.isEmpty());
        QCOMPARE(f.stack.count(), before + 1);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Load Pad"));
        const Chain& pad = f.project.chain(track, kick);
        QCOMPARE(pad.receiveNote, 36);
        QCOMPARE(pad.name, QStringLiteral("Kick 1"));
        QCOMPARE(pad.devices.size(), size_t(1));
        QCOMPARE(pad.devices[0].kind, kSamplerKind);
        QCOMPARE(sampleOf(pad.devices[0]), QStringLiteral("/samples/Kick 1.wav"));
        QVERIFY(!pad.devices[0].params.contains(QStringLiteral("start")) ||
                pad.devices[0].params.value(QStringLiteral("start")) == 0.0);
        // -1: the first key without a pad from C1 up; a part of a file sets Start and End.
        const QString snare = f.editor.loadDrumPad(track, rackId, -1, QStringLiteral("/samples/Snare.wav"), 0.25, 0.5);
        const Chain& second = f.project.chain(track, snare);
        QCOMPARE(second.receiveNote, 37);
        QCOMPARE(second.devices[0].params.value(QStringLiteral("start")), 25.0);
        QCOMPARE(second.devices[0].params.value(QStringLiteral("end")), 50.0);
        // In key order.
        const QString low = f.editor.loadDrumPad(track, rackId, 30, QStringLiteral("/samples/Low.wav"));
        QCOMPARE(f.project.device(track, rackId).chains[0].id, low);
        // Onto a pad with a sample: its Sampler takes the new one, and the pad its name (it followed the sample).
        QCOMPARE(f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/samples/Kick 2.wav")), kick);
        QCOMPARE(sampleOf(f.project.chain(track, kick).devices[0]), QStringLiteral("/samples/Kick 2.wav"));
        QCOMPARE(f.project.chain(track, kick).name, QStringLiteral("Kick 2"));
        QCOMPARE(f.project.chain(track, kick).devices.size(), size_t(1));
        f.stack.undo();  // (one step, the name too)
        QCOMPARE(sampleOf(f.project.chain(track, kick).devices[0]), QStringLiteral("/samples/Kick 1.wav"));
        QCOMPARE(f.project.chain(track, kick).name, QStringLiteral("Kick 1"));
        f.stack.redo();
        // A pad named by hand keeps its name.
        f.editor.renameChain(track, kick, QStringLiteral("Big Kick"));
        f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/samples/Kick 3.wav"));
        QCOMPARE(f.project.chain(track, kick).name, QStringLiteral("Big Kick"));
        // Not a Drum Rack, or no path: nothing.
        const QString synthTrack = f.editor.addMidiTrack(-1, {}, QStringLiteral("synth"));
        const QString rack = f.editor.groupDevices(synthTrack, {f.track(synthTrack).devices[0].id});
        QVERIFY(f.editor.loadDrumPad(synthTrack, rack, 36, QStringLiteral("/a.wav")).isEmpty());
        QVERIFY(f.editor.loadDrumPad(track, rackId, 40, QString()).isEmpty());
    }

    void padsAddedDuplicatedMovedAndLocked() {
        EditorFixture f;
        const auto [track, rackId] = drums(f);
        const QString kick = f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/s/Kick.wav"));
        const QString added = f.editor.addRackChain(track, rackId);  // + Chain: a pad on the next free key
        QCOMPARE(f.project.chain(track, added).receiveNote, 37);
        QCOMPARE(f.project.chain(track, added).name, QStringLiteral("C#1"));
        const QString copy = f.editor.duplicateRackChain(track, kick);  // onto the next key without a pad
        QCOMPARE(f.project.chain(track, copy).receiveNote, 38);
        QCOMPARE(sampleOf(f.project.chain(track, copy).devices[0]), QStringLiteral("/s/Kick.wav"));
        // Moved to another key: the pad there takes this one's.
        f.editor.setChainParam(track, kick, QStringLiteral("receive_note"), 38);
        QCOMPARE(f.project.chain(track, kick).receiveNote, 38);
        QCOMPARE(f.project.chain(track, copy).receiveNote, 36);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Move Pad"));
        f.stack.undo();
        QCOMPARE(f.project.chain(track, kick).receiveNote, 36);
        // Locked, undoably.
        f.editor.setChainParam(track, kick, QStringLiteral("locked"), 1.0);
        QVERIFY(f.project.chain(track, kick).locked);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Lock Pad"));
        f.stack.undo();
        QVERIFY(!f.project.chain(track, kick).locked);
        // Every key a pad: no more.
        Device full = f.project.device(track, rackId);
        for (int note = 0; note < 128; ++note) {
            if (drumPad(full, note) == nullptr) full.chains.push_back(newDrumPad(note));
        }
        QCOMPARE(ProjectEditor::freePadNote(full), -1);
        QCOMPARE(ProjectEditor::freePadNote(f.project.device(track, rackId), 126), 126);
    }

    void drumRacksAreSavedAndLoaded() {
        EditorFixture f;
        const auto [track, rackId] = drums(f);
        const QString kick = f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/s/Kick.wav"));
        f.editor.loadDrumPad(track, rackId, 42, QStringLiteral("/s/Hat.wav"));
        f.editor.setChainParam(track, kick, QStringLiteral("locked"), 1.0);
        const QJsonObject data = projectToJson(f.project);
        QCOMPARE(data.value(QStringLiteral("version")).toInt(), kProjectVersion);
        Project loaded;
        loadInto(loaded, data);
        QCOMPARE(projectToJson(loaded), data);
        const Device& again = loaded.device(track, rackId);
        QVERIFY(again.isDrumRack());
        QCOMPARE(again.chains.size(), size_t(2));
        QCOMPARE(again.chains[0].receiveNote, 36);
        QVERIFY(again.chains[0].locked);
        QCOMPARE(again.chains[1].receiveNote, 42);
        QVERIFY(!again.chains[1].locked);
        // A pad without a key, or a key taken twice, gets the first free one from C1 up; another rack's chains have none.
        QJsonObject edited = data;
        QJsonArray tracks = edited[QStringLiteral("tracks")].toArray();
        QJsonObject t = tracks[0].toObject();
        QJsonArray devices = t[QStringLiteral("devices")].toArray();
        QJsonObject rack = devices[0].toObject();
        QJsonArray chains = rack[QStringLiteral("chains")].toArray();
        QJsonObject second = chains[1].toObject();
        second[QStringLiteral("receive_note")] = 36;
        chains[1] = second;
        rack[QStringLiteral("chains")] = chains;
        devices[0] = rack;
        t[QStringLiteral("devices")] = devices;
        tracks[0] = t;
        edited[QStringLiteral("tracks")] = tracks;
        Project fixed;
        loadInto(fixed, edited);
        QCOMPARE(fixed.device(track, rackId).chains[0].receiveNote, 36);
        QCOMPARE(fixed.device(track, rackId).chains[1].receiveNote, 37);
        // A pad with a key of its own keeps it: the one taking another's gets a key no pad has.
        {
            QJsonArray three = chains;
            QJsonObject a = chains[0].toObject(), b = chains[1].toObject(), c = chains[0].toObject();
            b[QStringLiteral("receive_note")] = 36;
            c[QStringLiteral("id")] = QStringLiteral("third");
            c[QStringLiteral("receive_note")] = 37;
            three = QJsonArray{a, b, c};
            QJsonObject r = rack;
            r[QStringLiteral("chains")] = three;
            QJsonArray d = devices;
            d[0] = r;
            QJsonObject tt = t;
            tt[QStringLiteral("devices")] = d;
            QJsonArray ts = tracks;
            ts[0] = tt;
            QJsonObject e = edited;
            e[QStringLiteral("tracks")] = ts;
            Project kept;
            loadInto(kept, e);
            const auto& pads = kept.device(track, rackId).chains;
            QCOMPARE(pads[0].receiveNote, 36);
            QCOMPARE(pads[1].receiveNote, 38);
            QCOMPARE(pads[2].receiveNote, 37);
        }
        rack.remove(QStringLiteral("drum"));
        devices[0] = rack;
        t[QStringLiteral("devices")] = devices;
        tracks[0] = t;
        edited[QStringLiteral("tracks")] = tracks;
        Project plain;
        loadInto(plain, edited);
        QVERIFY(!plain.device(track, rackId).isDrumRack());
        QCOMPARE(plain.device(track, rackId).chains[0].receiveNote, -1);
        QVERIFY(!plain.device(track, rackId).chains[0].locked);
    }

    void samplesLoadedByHandAndTheirReference() {
        EditorFixture f;
        const QString track = f.editor.addMidiTrack(-1, {}, kSamplerKind);
        const QString sampler = f.track(track).devices[0].id;
        f.editor.loadSample(track, sampler, QStringLiteral("/s/Kick 1.wav"));
        QCOMPARE(f.stack.undoText(), QStringLiteral("Load Sample"));
        QCOMPARE(sampleOf(f.project.device(track, sampler)), QStringLiteral("/s/Kick 1.wav"));
        QCOMPARE(referenceOf(f.project.device(track, sampler)), QString());
        // Swapped for a similar sound: the sample it came from is the reference.
        f.editor.swapSamples(track, {{sampler, QStringLiteral("/s/Kick 2.wav")}});
        QCOMPARE(f.stack.undoText(), QStringLiteral("Swap Similar Sample"));
        QCOMPARE(sampleOf(f.project.device(track, sampler)), QStringLiteral("/s/Kick 2.wav"));
        QCOMPARE(referenceOf(f.project.device(track, sampler)), QStringLiteral("/s/Kick 1.wav"));
        f.editor.swapSamples(track, {{sampler, QStringLiteral("/s/Kick 3.wav")}});
        QCOMPARE(referenceOf(f.project.device(track, sampler)), QStringLiteral("/s/Kick 1.wav"));  // (kept)
        // Loaded by hand: a reference stepping started from goes with it...
        f.editor.loadSample(track, sampler, QStringLiteral("/s/Snare.wav"));
        QCOMPARE(referenceOf(f.project.device(track, sampler)), QString());
        // ...a saved one stays.
        f.editor.setSimilarReference(track, sampler);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Save as Similar Reference"));
        QCOMPARE(referenceOf(f.project.device(track, sampler)), QStringLiteral("/s/Snare.wav"));
        f.editor.loadSample(track, sampler, QStringLiteral("/s/Clap.wav"));
        QCOMPARE(referenceOf(f.project.device(track, sampler)), QStringLiteral("/s/Snare.wav"));
        f.editor.swapSamples(track, {{sampler, QStringLiteral("/s/Clap 2.wav")}});
        QCOMPARE(referenceOf(f.project.device(track, sampler)), QStringLiteral("/s/Snare.wav"));
        // A part of a file: Start and End; cleared: no sample.
        f.editor.loadSample(track, sampler, QStringLiteral("/s/Loop.wav"), 0.5, 0.75);
        QCOMPARE(f.project.device(track, sampler).params.value(QStringLiteral("start")), 50.0);
        QCOMPARE(f.project.device(track, sampler).params.value(QStringLiteral("end")), 75.0);
        f.stack.undo();  // (one step)
        QCOMPARE(sampleOf(f.project.device(track, sampler)), QStringLiteral("/s/Clap 2.wav"));
        f.editor.loadSample(track, sampler, QString());
        QCOMPARE(f.stack.undoText(), QStringLiteral("Clear Sample"));
        QCOMPARE(sampleOf(f.project.device(track, sampler)), QString());
    }

    void severalPadsSwappedInOneStep() {
        EditorFixture f;
        const auto [track, rackId] = drums(f);
        const QString kick = f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/s/Kick 1.wav"));
        const QString hat = f.editor.loadDrumPad(track, rackId, 42, QStringLiteral("/s/Hat 1.wav"));
        const QString kickSampler = padSampler(f.project.chain(track, kick))->id;
        const QString hatSampler = padSampler(f.project.chain(track, hat))->id;
        const int count = f.stack.count();
        f.editor.swapSamples(track, {{kickSampler, QStringLiteral("/s/Kick 2.wav")}, {hatSampler, QStringLiteral("/s/Hat 2.wav")}},
                             QStringLiteral("Swap Similar Samples"));
        QCOMPARE(f.stack.count(), count + 1);
        QCOMPARE(f.project.chain(track, kick).name, QStringLiteral("Kick 2"));
        QCOMPARE(f.project.chain(track, hat).name, QStringLiteral("Hat 2"));
        f.stack.undo();
        QCOMPARE(f.project.chain(track, kick).name, QStringLiteral("Kick 1"));
        QCOMPARE(sampleOf(f.project.device(track, hatSampler)), QStringLiteral("/s/Hat 1.wav"));
    }

    void steppingThroughSimilarSounds() {
        const QStringList best{QStringLiteral("/s/B.wav"), QStringLiteral("/s/C.wav"), QStringLiteral("/s/D.wav")};
        const QString reference = QStringLiteral("/s/A.wav");
        using samplerState::step;
        QCOMPARE(step(best, reference, reference, 1), best[0]);  // from the reference: the best
        QCOMPARE(step(best, reference, best[0], 1), best[1]);
        QCOMPARE(step(best, reference, best[2], 1), QString());  // no more
        QCOMPARE(step(best, reference, best[1], -1), best[0]);
        QCOMPARE(step(best, reference, best[0], -1), reference);  // back to the reference
        QCOMPARE(step(best, reference, reference, -1), QString());
        QCOMPARE(step(best, reference, QStringLiteral("/s/Other.wav"), 1), best[0]);  // a sound not among them
        QCOMPARE(step({}, reference, reference, 1), QString());
    }
};

QTEST_MAIN(TestEditorDrumRacks)
#include "test_editor_drum_racks.moc"
