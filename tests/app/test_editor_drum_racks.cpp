// Drum Racks and the Sampler's samples through the editor: a Drum Rack is an
// instrument rack even while empty; samples loaded onto its pads (a pad a key,
// each with a Sampler, named after its sample), pads added, duplicated and
// moved to other keys, locked; saving and loading them; samples loaded by
// hand, and swapped for similar sounds, with the reference those come from
// (model/SamplerState.h); the stepping itself; pads as tracks of their own
// (sidechains taken from them taken from their tracks).

#include "EditorFixture.h"
#include "TestSupport.h"

#include "io/Serialization.h"
#include "model/Automation.h"
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
        // No macros (a rack of a pad's devices has them), and none to add.
        QCOMPARE(macroCount(rack), 0);
        QVERIFY(rack.params.isEmpty());
        f.editor.setMacroCount(track, rackId, 4);
        QCOMPARE(macroCount(f.project.device(track, rackId)), 0);
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
        QCOMPARE(pad.devices[0].params.value(QStringLiteral("mode")), kSamplerOneShot);  // drums: 1-Shot
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
        // A pad playing another instrument: the Sampler takes its place (a chain has one instrument).
        const QString synthPad = f.editor.addRackChain(track, rackId);
        const QString synth = f.editor.addDevice(track, QStringLiteral("synth"), -1, synthPad);
        const QString utility = f.editor.addDevice(track, QStringLiteral("utility"), -1, synthPad);
        QVERIFY(!synth.isEmpty() && !utility.isEmpty());
        const int synthKey = f.project.chain(track, synthPad).receiveNote;
        QCOMPARE(f.editor.loadDrumPad(track, rackId, synthKey, QStringLiteral("/samples/Clap.wav")), synthPad);
        const Chain& replaced = f.project.chain(track, synthPad);
        QCOMPARE(replaced.devices.size(), size_t(2));
        QCOMPARE(replaced.devices[0].kind, kSamplerKind);
        QCOMPARE(replaced.devices[1].id, utility);  // (its effects stay)
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
        // (Saved with macros, as Drum Racks were at first: they go.)
        rack[QStringLiteral("macro_names")] = QJsonArray{QString(), QStringLiteral("Punch"), QString(), QString()};
        rack[QStringLiteral("params")] = QJsonObject{{QStringLiteral("macro1"), 0.5}, {QStringLiteral("macro2"), 0.0}};
        devices[0] = rack;
        t[QStringLiteral("devices")] = devices;
        tracks[0] = t;
        edited[QStringLiteral("tracks")] = tracks;
        Project fixed;
        loadInto(fixed, edited);
        QCOMPARE(fixed.device(track, rackId).chains[0].receiveNote, 36);
        QCOMPARE(fixed.device(track, rackId).chains[1].receiveNote, 37);
        QCOMPARE(macroCount(fixed.device(track, rackId)), 0);
        QVERIFY(!fixed.device(track, rackId).params.contains(QStringLiteral("macro1")));
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

    // Every pad a track: the track a group (of the same id) with the effects
    // after the rack and its mixer; each pad's track has its devices, fader,
    // automation and the notes of its key, at C3. One undo step.
    void padsSeparateIntoTracks() {
        EditorFixture f;
        const auto [track, rackId] = drums(f);
        const QString kick = f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/s/Kick.wav"));
        const QString snare = f.editor.loadDrumPad(track, rackId, 38, QStringLiteral("/s/Snare.wav"));
        const QString utility = f.editor.addDevice(track, QStringLiteral("utility"));
        f.editor.setChainParam(track, snare, ChainField::VolumeDb, -6.0);
        f.editor.setTrackParam(track, TrackField::VolumeDb, -3.0);
        const QString snareSampler = padSampler(*findChain(f.track(track).devices, snare).chain)->id;
        const Envelope ramp{{0.0, 0.2}, {4.0, 0.8}};
        f.editor.setEnvelope(track, automation::chainKey(rackId, snare, automation::kChainVolume), ramp);
        f.editor.setEnvelope(track, automation::deviceKey(snareSampler, QStringLiteral("volume")), ramp);
        f.editor.setEnvelope(track, automation::deviceKey(utility, QStringLiteral("gain")), ramp);
        const auto clip = f.editor.addMidiClip(track, 0.0, 4.0);
        QVERIFY(clip);
        Clip played = f.track(track).clips.at(0);
        played.notes = {{36, 0.0, 0.25, 100}, {38, 1.0, 0.25, 90}, {36, 2.0, 0.25, 110}, {50, 3.0, 0.25, 100}};
        f.editor.replaceClip(track, played, QStringLiteral("Notes"));
        // Sidechains taken from the pads: from their tracks next.
        const QString bass = f.editor.addAudioTrack();
        const QString onKick = f.editor.addDevice(bass, QStringLiteral("utility"));
        const QString onSnare = f.editor.addDevice(bass, QStringLiteral("utility"));
        const QString onDrums = f.editor.addDevice(bass, QStringLiteral("utility"));
        f.editor.setDeviceSidechain(bass, onKick, Sidechain{track, kPreFx, kick});
        f.editor.setDeviceSidechain(bass, onSnare, Sidechain{track, kPostFader, snare});
        f.editor.setDeviceSidechain(bass, onDrums, Sidechain{track, kPreFader});
        const std::vector<Track> before = f.project.tracks();
        const int steps = f.stack.count();

        const QStringList made = f.editor.separatePads(track, rackId);
        QCOMPARE(made.size(), 2);
        QCOMPARE(f.stack.count(), steps + 1);
        const Track& group = f.track(track);
        QVERIFY(group.isGroup());
        QCOMPARE(group.volumeDb, -3.0);
        QCOMPARE(group.devices.size(), size_t(1));
        QCOMPARE(group.devices[0].id, utility);
        QVERIFY(group.automation.contains(automation::deviceKey(utility, QStringLiteral("gain"))));
        QCOMPARE(group.automation.size(), 1);
        const Track& kickTrack = f.track(made[0]);
        const Track& snareTrack = f.track(made[1]);
        QCOMPARE(kickTrack.name, QStringLiteral("Kick"));
        QCOMPARE(snareTrack.name, QStringLiteral("Snare"));
        for (const Track* t : {&kickTrack, &snareTrack}) {
            QVERIFY(t->isMidi());
            QCOMPARE(t->parent, std::optional<QString>(track));
            QCOMPARE(t->devices.size(), size_t(1));
            QCOMPARE(t->devices[0].kind, QString(kSamplerKind));
            QCOMPARE(t->clips.size(), size_t(1));
        }
        QCOMPARE(f.project.trackIndex(made[0]), f.project.trackIndex(track) + 1);
        QCOMPARE(kickTrack.clips[0].notes, (std::vector<Note>{{60, 0.0, 0.25, 100}, {60, 2.0, 0.25, 110}}));
        QCOMPARE(snareTrack.clips[0].notes, (std::vector<Note>{{60, 1.0, 0.25, 90}}));
        QCOMPARE(snareTrack.devices[0].id, snareSampler);
        QCOMPARE(snareTrack.volumeDb, -6.0);
        QCOMPARE(snareTrack.automation.value(automation::kMixerVolume), ramp);
        QCOMPARE(snareTrack.automation.value(automation::deviceKey(snareSampler, QStringLiteral("volume"))), ramp);
        QVERIFY(kickTrack.automation.isEmpty());
        QCOMPARE(f.project.device(bass, onKick).sidechain, std::optional<Sidechain>(Sidechain{made[0], kPreFx}));
        QCOMPARE(f.project.device(bass, onSnare).sidechain, std::optional<Sidechain>(Sidechain{made[1], kPostFader}));
        QCOMPARE(f.project.device(bass, onDrums).sidechain, std::optional<Sidechain>(Sidechain{track, kPreFader}));
        f.stack.undo();
        QCOMPARE(f.project.tracks(), before);
        // A track's own Drum Rack only.
        QVERIFY(f.editor.separatePads(track, QStringLiteral("nothing")).isEmpty());
    }

    // One pad a track of its own, where it was dropped: the pad leaves the
    // rack, its notes the clips.
    void aPadBecomesATrack() {
        EditorFixture f;
        const auto [track, rackId] = drums(f);
        f.editor.loadDrumPad(track, rackId, 36, QStringLiteral("/s/Kick.wav"));
        const QString snare = f.editor.loadDrumPad(track, rackId, 38, QStringLiteral("/s/Snare.wav"));
        const QString other = f.editor.addAudioTrack();
        f.editor.addMidiClip(track, 0.0, 4.0);
        Clip played = f.track(track).clips.at(0);
        played.notes = {{36, 0.0, 0.25, 100}, {38, 1.0, 0.25, 90}};
        f.editor.replaceClip(track, played, QStringLiteral("Notes"));
        const QString keyed = f.editor.addDevice(other, QStringLiteral("utility"));
        f.editor.setDeviceSidechain(other, keyed, Sidechain{track, kPreFader, snare});
        const std::vector<Track> before = f.project.tracks();

        const QString made = f.editor.padToTrack(track, snare, other);
        QVERIFY(!made.isEmpty());
        QCOMPARE(f.project.device(other, keyed).sidechain, std::optional<Sidechain>(Sidechain{made, kPreFader}));
        QCOMPARE(f.project.trackIndex(made), f.project.trackIndex(other) + 1);
        const Track& snareTrack = f.track(made);
        QCOMPARE(snareTrack.name, QStringLiteral("Snare"));
        QCOMPARE(snareTrack.clips.size(), size_t(1));
        QCOMPARE(snareTrack.clips[0].notes, (std::vector<Note>{{60, 1.0, 0.25, 90}}));
        const Device& rack = f.project.device(track, rackId);
        QCOMPARE(rack.chains.size(), size_t(1));
        QVERIFY(drumPad(rack, 38) == nullptr);
        QCOMPARE(f.track(track).clips[0].notes, (std::vector<Note>{{36, 0.0, 0.25, 100}}));
        f.stack.undo();
        QCOMPARE(f.project.tracks(), before);
        // Without a track to go after: last.
        const QString last = f.editor.padToTrack(track, snare);
        QCOMPARE(f.project.trackIndex(last), int(f.project.tracks().size()) - 1);
    }
};

QTEST_MAIN(TestEditorDrumRacks)
#include "test_editor_drum_racks.moc"
