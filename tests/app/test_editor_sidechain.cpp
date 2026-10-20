// Sidechains through the editor: a device taking a track's (a group's, a
// return's) signal into its sidechain input, after its fader, before it,
// before its devices or after one of them; cycles refused, sidechains that
// would close one dropped when tracks move into groups or devices to other
// tracks, a source going takes the sidechains from it along (one undo step),
// saving; and sidechains taken from a Drum Rack's pad (its own Pre FX, Post FX,
// Post Mixer): refused from what isn't a pad of the source, saved, copied with
// their source, keeping it from being frozen.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "io/Serialization.h"
#include "model/Devices.h"
#include "model/Errors.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace sub::app;
using test::EditorFixture;
using test::ids;

namespace {

// A device to take a sidechain (the model doesn't ask whether it has a sidechain input).
QString device(EditorFixture& f, const QString& trackId) { return f.editor.addDevice(trackId, "utility"); }

std::optional<Sidechain> sidechain(const QString& trackId, const QString& tap = kPostFader) {
    return Sidechain{trackId, tap};
}

}  // namespace

class TestEditorSidechain : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void aSidechainAndItsUndo() {
        EditorFixture f;
        Project& p = f.project;
        const QString kick = f.editor.addAudioTrack(-1, "Kick");
        const QString bass = f.editor.addAudioTrack(-1, "Bass");
        const QString comp = device(f, bass);
        const QString tapped = device(f, kick);
        const int steps = f.stack.count();
        f.editor.setDeviceSidechain(bass, comp, sidechain(kick));
        QVERIFY(p.device(bass, comp).sidechain == sidechain(kick, kPostFader));
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Change Sidechain"));
        f.editor.setDeviceSidechain(bass, comp, sidechain(kick, kPreFader));
        f.editor.setDeviceSidechain(bass, comp, sidechain(kick, tapped));
        QCOMPARE(p.device(bass, comp).sidechain->tapDevice(), std::optional<QString>(tapped));
        f.editor.setDeviceSidechain(bass, comp, std::nullopt);
        QVERIFY(!p.device(bass, comp).sidechain);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Remove Sidechain"));
        f.editor.setDeviceSidechain(bass, comp, std::nullopt);  // (nothing changes: no step)
        QCOMPARE(f.stack.count(), steps + 4);
        for (const QString& tap : {tapped, kPreFader, kPostFader}) {
            f.stack.undo();
            QVERIFY(p.device(bass, comp).sidechain == sidechain(kick, tap));
        }
        f.stack.undo();
        QVERIFY(!p.device(bass, comp).sidechain);

        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(bass, comp, sidechain(bass)));  // its own track
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(bass, comp, sidechain(kMaster)));
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(bass, comp, sidechain("gone")));
        // Not one of the kick's devices.
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(bass, comp, sidechain(kick, comp)));
        const QString onMaster = device(f, kMaster);
        f.editor.setDeviceSidechain(kMaster, onMaster, sidechain(bass));  // the master's devices take any track's
        QVERIFY(p.master().devices.back().sidechain == sidechain(bass));
        // From QML.
        QVERIFY(!f.editor.trySetDeviceSidechain(bass, comp, bass));
        QCOMPARE(f.messages.size(), 1);
        QVERIFY(f.editor.trySetDeviceSidechain(bass, comp, kick, kPreFader));
        QVERIFY(p.device(bass, comp).sidechain == sidechain(kick, kPreFader));
        QVERIFY(f.editor.trySetDeviceSidechain(bass, comp, {}));
        QVERIFY(!p.device(bass, comp).sidechain);
    }

    void whatCanBeASource() {
        EditorFixture f;
        Project& p = f.project;
        const QString a = f.editor.addAudioTrack();
        const QString b = f.editor.addAudioTrack();
        const QString group = f.editor.groupTracks({b});
        const QString ret = f.editor.addReturnTrack();
        f.editor.setSend(a, ret, 0.0);
        QCOMPARE(ids(p.sidechainSources(b)), (QStringList{a, group, ret}));
        QCOMPARE(ids(p.sidechainSources(kMaster)), (QStringList{a, group, b, ret}));
        QVERIFY(p.sidechainWouldCycle(b, group));  // what its track goes into
        QVERIFY(p.sidechainWouldCycle(a, ret));  // a return it sends to
        QVERIFY(!p.sidechainWouldCycle(group, b));  // what goes into the group: fine
        for (const Track* t : p.sidechainSources(kMaster)) QVERIFY(!p.sidechainWouldCycle(kMaster, t->id));
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(b, device(f, b), sidechain(group)));
        f.editor.setDeviceSidechain(group, device(f, group), sidechain(b));
    }

    // Sends and inputs that would close a cycle with a sidechain are refused.
    void sidechainsAreRoutingEdges() {
        EditorFixture f;
        Project& p = f.project;
        const QString a = f.editor.addAudioTrack();
        const QString b = f.editor.addAudioTrack();
        const QString ret = f.editor.addReturnTrack();
        f.editor.setDeviceSidechain(a, device(f, a), sidechain(ret));  // ret -> a
        QVERIFY(p.wouldCycle(a, ret) && !ids(p.sendTargets(a)).contains(ret));
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setSend(a, ret, 0.0));
        f.editor.setDeviceSidechain(b, device(f, b), sidechain(a));  // a -> b
        QVERIFY(p.inputWouldCycle(a, b));
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setTrackInputTrack(a, b));
    }

    void movingATrackIntoItsSourceDropsTheSidechain() {
        EditorFixture f;
        Project& p = f.project;
        const QString a = f.editor.addAudioTrack();
        const QString b = f.editor.addAudioTrack();
        const QString group = f.editor.groupTracks({a});
        const QString comp = device(f, b);
        f.editor.setDeviceSidechain(b, comp, sidechain(group));
        f.editor.setDeviceSidechain(group, device(f, group), sidechain(a));  // stays: no cycle
        const int steps = f.stack.count();
        QVERIFY(f.editor.moveTracks({b}, p.trackIndex(a) + 1, group));
        QVERIFY(f.track(b).parent == std::optional<QString>(group) && !p.device(b, comp).sidechain);
        QVERIFY(f.track(group).devices[0].sidechain == sidechain(a));
        QCOMPARE(f.stack.count(), steps + 1);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Move Track"));
        f.stack.undo();
        QVERIFY(!f.track(b).parent && p.device(b, comp).sidechain == sidechain(group));
    }

    void aSourceGoingTakesTheSidechainsFromIt() {
        EditorFixture f;
        Project& p = f.project;
        const QString kick = f.editor.addAudioTrack();
        const QString bass = f.editor.addAudioTrack();
        const QString group = f.editor.groupTracks({bass});
        const QString ret = f.editor.addReturnTrack();
        const QString comp = device(f, bass);
        const QString onMaster = device(f, kMaster);
        const QString onReturn = device(f, ret);
        f.editor.setDeviceSidechain(bass, comp, sidechain(kick));
        f.editor.setDeviceSidechain(kMaster, onMaster, sidechain(kick, kPreFader));
        f.editor.setDeviceSidechain(ret, onReturn, sidechain(group));
        const int steps = f.stack.count();
        f.editor.deleteTracks({kick});
        QVERIFY(!p.device(bass, comp).sidechain && !p.device(kMaster, onMaster).sidechain);
        QCOMPARE(f.stack.count(), steps + 1);
        f.stack.undo();
        QVERIFY(p.device(bass, comp).sidechain == sidechain(kick));
        QVERIFY(p.device(kMaster, onMaster).sidechain == sidechain(kick, kPreFader));
        // A group ungrouped is a source no more; what was in it stays, and so do its sidechains.
        f.editor.ungroup({group});
        QVERIFY(!p.device(ret, onReturn).sidechain);
        QVERIFY(p.device(bass, comp).sidechain == sidechain(kick));
        f.stack.undo();
        QVERIFY(p.device(ret, onReturn).sidechain == sidechain(group));
        // A device keyed by a track going with it (in its group) keeps its sidechain: they come back together.
        f.editor.deleteTracks({group});
        QVERIFY(!p.hasTrack(bass) && !p.device(ret, onReturn).sidechain);
        f.stack.undo();
        QVERIFY(p.device(bass, comp).sidechain == sidechain(kick));
    }

    void movingADeviceDropsASidechainThatWouldCloseACycle() {
        EditorFixture f;
        Project& p = f.project;
        const QString kick = f.editor.addAudioTrack();
        const QString bass = f.editor.addAudioTrack();
        const QString pad = f.editor.addAudioTrack();
        const QString comp = device(f, bass);
        f.editor.setDeviceSidechain(bass, comp, sidechain(kick));
        QVERIFY(f.editor.moveDevicesToTrack(bass, {comp}, pad));
        QVERIFY(p.device(pad, comp).sidechain == sidechain(kick));  // it goes with the device
        QVERIFY(f.editor.moveDevicesToTrack(pad, {comp}, kMaster));
        QVERIFY(p.device(kMaster, comp).sidechain == sidechain(kick));
        QVERIFY(f.editor.moveDevicesToTrack(kMaster, {comp}, kick));  // onto its source: a cycle
        QVERIFY(!p.device(kick, comp).sidechain);
        f.stack.undo();
        QVERIFY(p.device(kMaster, comp).sidechain == sidechain(kick));
    }

    void sidechainsAreSavedAndLoaded() {
        EditorFixture f;
        Project& p = f.project;
        const QString a = f.editor.addAudioTrack();
        const QString b = f.editor.addAudioTrack();
        const QString c = f.editor.addAudioTrack();
        const QString ret = f.editor.addReturnTrack();
        const QString tapped = device(f, c);
        const QString onA = device(f, a);
        const QString onB = device(f, b);
        const QString onRet = device(f, ret);
        const QString onMaster = device(f, kMaster);
        f.editor.setDeviceSidechain(a, onA, sidechain(c, tapped));  // a track listed after it
        f.editor.setDeviceSidechain(b, onB, sidechain(ret, kPreFader));
        f.editor.setDeviceSidechain(ret, onRet, sidechain(a));
        f.editor.setDeviceSidechain(kMaster, onMaster, sidechain(ret));
        const QJsonObject data = projectToJson(p);
        const QJsonObject saved = data["tracks"].toArray()[0].toObject()["devices"].toArray()[0].toObject()["sidechain"].toObject();
        QCOMPARE(saved["track"].toString(), c);
        QCOMPARE(saved["tap"].toString(), tapped);
        Project loaded;
        loadInto(loaded, data);
        QVERIFY(loaded.track(a).devices.back().sidechain == sidechain(c, tapped));
        QVERIFY(loaded.track(b).devices.back().sidechain == sidechain(ret, kPreFader));
        QVERIFY(loaded.track(ret).devices.back().sidechain == sidechain(a));
        QVERIFY(loaded.track(kMaster).devices.back().sidechain == sidechain(ret));
    }

    void aSidechainFromAPad() {
        EditorFixture f;
        Project& p = f.project;
        const QString drums = f.editor.addMidiTrack(-1, "Drums", kDrumRackKind);
        const QString rack = f.track(drums).devices.at(0).id;
        const QString kick = f.editor.loadDrumPad(drums, rack, 36, QStringLiteral("/s/Kick.wav"));
        const QString snare = f.editor.loadDrumPad(drums, rack, 38, QStringLiteral("/s/Snare.wav"));
        const QString bass = f.editor.addAudioTrack(-1, "Bass");
        const QString comp = device(f, bass);
        QVERIFY(!p.freezeProblem(drums));
        for (const QString& tap : {kPostFader, kPreFader, kPreFx}) {
            f.editor.setDeviceSidechain(bass, comp, Sidechain{drums, tap, kick});
            QCOMPARE(p.device(bass, comp).sidechain, std::optional<Sidechain>(Sidechain{drums, tap, kick}));
        }
        QCOMPARE(f.stack.undoText(), QStringLiteral("Change Sidechain"));
        f.editor.setDeviceSidechain(bass, comp, Sidechain{drums, kPostFader, snare});
        f.stack.undo();
        QCOMPARE(p.device(bass, comp).sidechain, std::optional<Sidechain>(Sidechain{drums, kPreFx, kick}));
        // Refused: no such pad on the source, a pad of another track, a pad's tap after a device.
        const QString other = f.editor.addMidiTrack(-1, "Other", kDrumRackKind);
        const QString otherPad =
            f.editor.loadDrumPad(other, f.track(other).devices.at(0).id, 36, QStringLiteral("/s/Kick.wav"));
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(bass, comp, Sidechain{drums, kPostFader, "gone"}));
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(bass, comp, Sidechain{drums, kPostFader, otherPad}));
        const QString sampler = padSampler(*findChain(f.track(drums).devices, kick).chain)->id;
        QVERIFY_THROWS_EXCEPTION(EditError, f.editor.setDeviceSidechain(bass, comp, Sidechain{drums, sampler, kick}));
        f.editor.setDeviceSidechain(bass, comp, Sidechain{drums, sampler});  // (after its Sampler, as a device)
        QVERIFY(f.editor.trySetDeviceSidechain(bass, comp, drums, kPreFader, snare));
        QCOMPARE(p.device(bass, comp).sidechain, std::optional<Sidechain>(Sidechain{drums, kPreFader, snare}));
        QVERIFY(!f.editor.trySetDeviceSidechain(bass, comp, drums, kPreFader, otherPad));
        // Its frozen audio has no pads.
        QVERIFY(p.freezeProblem(drums));
        QVERIFY(p.freezeProblem(drums)->contains(QStringLiteral("pads")));
        // The pad deleted: the sidechain keeps it (the bridge takes the track's own Post FX meanwhile), undone: back.
        f.editor.removeRackChains(drums, {snare});
        QCOMPARE(p.device(bass, comp).sidechain, std::optional<Sidechain>(Sidechain{drums, kPreFader, snare}));
        f.stack.undo();
        QVERIFY(findChain(f.track(drums).devices, snare).chain != nullptr);

        // Saved with its pad, and loaded.
        const QJsonObject data = projectToJson(p);
        QJsonObject saved;
        for (const QJsonValue track : data["tracks"].toArray()) {
            if (track.toObject()["id"].toString() == bass)
                saved = track.toObject()["devices"].toArray()[0].toObject()["sidechain"].toObject();
        }
        QCOMPARE(saved["track"].toString(), drums);
        QCOMPARE(saved["tap"].toString(), kPreFader);
        QCOMPARE(saved["chain"].toString(), snare);
        Project loaded;
        loadInto(loaded, data);
        QCOMPARE(loaded.device(bass, comp).sidechain, std::optional<Sidechain>(Sidechain{drums, kPreFader, snare}));
        // A track's own taps save no chain.
        f.editor.setDeviceSidechain(bass, comp, Sidechain{drums, kPreFader});
        QJsonObject plain;
        for (const QJsonValue track : projectToJson(p)["tracks"].toArray()) {
            if (track.toObject()["id"].toString() == bass)
                plain = track.toObject()["devices"].toArray()[0].toObject()["sidechain"].toObject();
        }
        QVERIFY(!plain.contains("chain"));
    }

    void copiesTakeTheirCopiedSourcesPadsAndDevices() {
        // Copied together, the keyed track's copy takes the source's copy's pad
        // (and a tap after a device, the copy's device); copied alone, its own source's.
        EditorFixture f;
        const QString drums = f.editor.addMidiTrack(-1, "Drums", kDrumRackKind);
        const QString rack = f.track(drums).devices.at(0).id;
        const QString kick = f.editor.loadDrumPad(drums, rack, 36, QStringLiteral("/s/Kick.wav"));
        const QString eq = f.editor.addDevice(drums, "utility");
        const QString bass = f.editor.addAudioTrack(-1, "Bass");
        const QString onPad = device(f, bass);
        const QString onEq = device(f, bass);
        f.editor.setDeviceSidechain(bass, onPad, Sidechain{drums, kPostFader, kick});
        f.editor.setDeviceSidechain(bass, onEq, Sidechain{drums, eq});
        const QStringList both = f.editor.duplicateTracks({drums, bass});
        QCOMPARE(both.size(), 2);
        const Track& drumsCopy = f.track(both[0]);
        const Track& bassCopy = f.track(both[1]);
        const QString kickCopy = drumsCopy.devices.at(0).chains.at(0).id;
        QVERIFY(kickCopy != kick);
        QCOMPARE(bassCopy.devices.at(0).sidechain, std::optional<Sidechain>(Sidechain{both[0], kPostFader, kickCopy}));
        QCOMPARE(bassCopy.devices.at(1).sidechain,
                 std::optional<Sidechain>(Sidechain{both[0], drumsCopy.devices.at(1).id}));
        const QStringList alone = f.editor.duplicateTracks({bass});
        QCOMPARE(f.track(alone[0]).devices.at(0).sidechain, std::optional<Sidechain>(Sidechain{drums, kPostFader, kick}));
        QCOMPARE(f.track(alone[0]).devices.at(1).sidechain, std::optional<Sidechain>(Sidechain{drums, eq}));
    }
};

QTEST_GUILESS_MAIN(TestEditorSidechain)
#include "test_editor_sidechain.moc"
