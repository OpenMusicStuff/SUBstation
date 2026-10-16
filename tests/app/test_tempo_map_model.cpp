// The project's tempo along the song and its time signature changes: what an
// unwarped clip lasts where it is, bars and positions by the time signatures,
// the editor's changes and their undo, saving and opening them, the engine
// playing them, and recordings placed by them.

#include "BridgeTestSupport.h"
#include "EditorFixture.h"
#include "TestSupport.h"

#include "io/Serialization.h"
#include "model/Commands.h"
#include "model/Edits.h"
#include "model/RecordedTake.h"
#include "model/Timebase.h"

#include <QTest>

#include <cmath>

using namespace sub::app;
using sub::MeterMap;
using sub::TempoMap;
using test::EditorFixture;

namespace {

// 120 BPM for four beats, then 60: beat 4 is at 2 s, beat 6 at 4 s.
Tempo slowsAtBeat4() { return Tempo(TempoMap({{0.0, 120.0}, {4.0, 120.0}, {4.0, 60.0}})); }

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

}  // namespace

class TestTempoMapModel : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    // An unwarped clip lasts its seconds: in beats, what the tempo makes them where it is.
    void anUnwarpedClipsBeatsDependOnWhereItIs() {
        const Tempo tempo = slowsAtBeat4();
        Clip clip = test::audioClip(QStringLiteral("c"), 2.0, 2.0);  // two seconds from beat 2
        QVERIFY(near(clip.endBeat(tempo), 5.0));  // one second at 120 (2 beats), one at 60 (1 beat)
        clip.startBeat = 4.0;
        QVERIFY(near(clip.endBeat(tempo), 6.0));
        clip.startBeat = 0.0;
        QVERIFY(near(clip.endBeat(tempo), 4.0));
        // A fade-out's beats are counted back from the end, where they are.
        clip.startBeat = 2.0;
        clip.fadeOutSec = 0.5;
        QVERIFY(near(clip.fadeOutBeats(tempo), 0.5));  // half a second at 60 BPM
        // A warped clip's beats are its own, wherever it is.
        clip.warp = true;
        clip.segmentBpm = 120.0;
        QVERIFY(near(clip.endBeat(tempo), 6.0));
        // One tempo throughout: as a single BPM always did.
        clip.warp = false;
        QCOMPARE(clip.endBeat(Tempo(90.0)), 2.0 + secondsToBeats(2.0, 90.0));
        // Cut in two across the change, each piece covers its own seconds.
        clip.fadeOutSec = 0.0;
        const auto parts = edits::splitClip(clip, 4.0, tempo);
        QVERIFY(parts.has_value());
        QVERIFY(near(parts->first.durationSec, 1.0));
        QVERIFY(near(parts->second.durationSec, 1.0));
        QVERIFY(near(parts->second.offsetSec, 1.0));
    }

    void barsAndPositionsFollowTheTimeSignatures() {
        const MeterMap meter({{0.0, 4, 4}, {8.0, 3, 4}, {14.0, 6, 8}});
        QCOMPARE(formatPosition(9.0, meter), QStringLiteral("3.2.1"));   // bar 3 is the first 3/4
        QCOMPARE(formatPosition(14.5, meter), QStringLiteral("5.2.1"));  // 6/8: eighth-note beats
        QCOMPARE(formatBarLabel(11.0, meter), QStringLiteral("4"));
        QCOMPARE(*parsePosition(QStringLiteral("5.2"), meter), 14.5);
        QCOMPARE(*parsePosition(QStringLiteral("3"), meter), 8.0);
        QVERIFY(near(barNumberAt(9.5, meter), 3.5));
        QVERIFY(near(beatOfBarNumber(3.5, meter), 9.5));
        // One time signature throughout: as it always counted.
        const MeterMap four(4, 4);
        QCOMPARE(formatPosition(9.0, four), formatPosition(9.0, TimeSignature{4, 4}));
        QCOMPARE(barNumberAt(9.0, four), 1.0 + 9.0 / 4.0);
    }

    void theEditorChangesThemAndUndoRestores() {
        EditorFixture f;
        f.editor.setTempoMap(slowsAtBeat4());
        QVERIFY(!f.project.tempoMap().constant());
        // The tempo box: the whole map in proportion, starting at the new tempo.
        f.editor.setTempo(240.0);
        QCOMPARE(f.project.tempo(), 240.0);
        QVERIFY(near(f.project.tempoMap().bpmAt(5.0), 120.0));
        f.stack.undo();
        QVERIFY(f.project.tempoMap() == slowsAtBeat4());
        f.stack.undo();
        QVERIFY(f.project.tempoMap().constant());
        QCOMPARE(f.project.tempo(), 120.0);

        // A time signature change at the bar a beat is in; removed again; the song's own stays.
        f.editor.setTimeSignatureAt(9.0, 3, 4);  // in bar 3: from beat 8
        QCOMPARE(f.project.meter().changes().size(), size_t{2});
        QCOMPARE(f.project.meter().changes()[1].beat, 8.0);
        f.editor.setTimeSignature(6, 8);  // the song's start
        QCOMPARE(f.project.timeSignature().numerator, 6);
        QCOMPARE(f.project.meter().changes().size(), size_t{2});
        f.editor.removeTimeSignatureAt(8.0);
        QVERIFY(f.project.meter().constant());
        f.stack.undo();
        f.stack.undo();
        f.stack.undo();
        QVERIFY(f.project.meter().constant());
        QCOMPARE(f.project.timeSignature().numerator, 4);
    }

    void aNewTempoMapTrimsUnwarpedClipsThatWouldOverlap() {
        EditorFixture f;
        const QString track = QStringLiteral("t");
        f.project.insertTrack(test::makeTrack(track, QStringLiteral("T")), 0);
        // Two seconds at 120 BPM: beats 0 to 4; the next clip at beat 4.
        f.editor.commitClips(QStringLiteral("Clips"), {{track, {test::audioClip(QStringLiteral("a"), 0.0, 2.0),
                                                                test::audioClip(QStringLiteral("b"), 4.0, 1.0)}}});
        // Slower from beat 2: the first now runs to beat 3 + ... past 4, so it is trimmed there.
        f.editor.setTempoMap(Tempo(TempoMap({{0.0, 120.0}, {2.0, 120.0}, {2.0, 30.0}})));
        const std::vector<Clip>& clips = f.track(track).clips;
        QCOMPARE(clips.size(), size_t{2});
        QVERIFY(clips[0].endBeat(f.project.tempoMap()) <= 4.0 + 1e-9);
        f.stack.undo();
        QVERIFY(near(f.track(track).clips[0].durationSec, 2.0));
    }

    void savedAndOpenedAgain() {
        const test::TempDir dir;
        Project project;
        ProjectContents contents;
        contents.tempo = slowsAtBeat4();
        contents.meter = MeterMap({{0.0, 4, 4}, {8.0, 7, 8}});
        project.replaceContents(contents);
        const QString path = dir.path(QStringLiteral("tempo.gilproj"));
        saveProject(project, path);
        Project opened;
        loadProject(opened, path);
        QVERIFY(opened.tempoMap() == slowsAtBeat4());
        QVERIFY(opened.meter() == contents.meter);
        QCOMPARE(opened.tempo(), 120.0);
        // One tempo throughout writes no map: "tempo" says it, as before.
        const QJsonObject plain = projectToJson(Project());
        QVERIFY(!plain.contains(QStringLiteral("tempo_map")));
        QVERIFY(!plain.contains(QStringLiteral("time_signatures")));
        QVERIFY(projectToJson(opened).contains(QStringLiteral("tempo_map")));
    }

    void theEnginePlaysThem() {
        test::Studio studio;
        studio.edit.setSettings({{SettingsField::TempoMap, slowsAtBeat4()},
                                 {SettingsField::Meter, MeterMap({{0.0, 4, 4}, {4.0, 3, 4}})}});
        QVERIFY(studio.engine.tempoMap() == slowsAtBeat4().map());
        QVERIFY(studio.engine.meter() == MeterMap({{0.0, 4, 4}, {4.0, 3, 4}}));
    }

    // The master's Song Tempo lane: the tempo map's points, its BPMs over the lane's range.
    void theSongTempoLaneIsTheTempoMap() {
        EditorFixture f;
        namespace autom = sub::app::automation;
        QCOMPARE(f.project.tempoRange(), std::make_pair(60.0, 200.0));
        Envelope lane = f.project.envelope(kMaster, autom::kSongTempo);
        QCOMPARE(lane.size(), size_t{1});
        QVERIFY(near(lane[0].value, (120.0 - 60.0) / 140.0));
        QVERIFY(f.project.automation(kMaster).isEmpty());  // (never stored as an envelope)
        // A point added: a ramp to it.
        f.editor.addAutomationPoint(kMaster, autom::kSongTempo, 8.0, 1.0);
        QCOMPARE(f.project.tempoMap().map().points().size(), size_t{2});
        QCOMPARE(f.project.tempoMap().bpmAt(8.0), 200.0);
        QCOMPARE(f.project.tempoMap().bpmAt(4.0), 160.0);
        QVERIFY(f.project.automation(kMaster).isEmpty());
        // Curves don't apply: the map's ramps are straight.
        f.editor.setAutomationCurve(kMaster, autom::kSongTempo, f.project.envelope(kMaster, autom::kSongTempo), 0, 0.5);
        QVERIFY(f.project.envelope(kMaster, autom::kSongTempo)[0].curve == 0.0);
        f.stack.undo();
        QVERIFY(f.project.tempoMap().constant());
        // A tempo beyond the lane's range widens it.
        f.editor.setTempo(300.0);
        QCOMPARE(f.project.tempoRange().second, 300.0);
        QCOMPARE(f.project.envelope(kMaster, autom::kSongTempo)[0].value, 1.0);
        f.stack.undo();
        QCOMPARE(f.project.tempoRange().second, 300.0);  // (never narrowed)
        // Pasted onto: only the master's.
        const CopiedAutomation copied{4.0, {{{kMaster, autom::kSongTempo}, {{0.0, 0.5}}}}};
        QVERIFY(f.editor.automationPasteTargets(copied, {})[0].has_value());
        QVERIFY(!f.editor.automationPasteTargets(copied, {{QStringLiteral("t"), autom::kSongTempo}})[0].has_value());
    }

    // A range moved over it and another lane: one undo step, the tempo and the envelope together.
    void aRangeMovesOverTheTempoLaneAndOthers() {
        EditorFixture f;
        namespace autom = sub::app::automation;
        f.editor.setTempoMap(Tempo(TempoMap({{0.0, 120.0}, {4.0, 120.0}, {8.0, 130.0}})));
        f.editor.setEnvelope(kMaster, autom::kMixerVolume, {{0.0, 0.5}, {8.0, 0.25}});
        const int before = f.stack.count();
        QMap<LaneRef, Envelope> originals{
            {{kMaster, autom::kSongTempo}, f.project.envelope(kMaster, autom::kSongTempo)},
            {{kMaster, autom::kMixerVolume}, f.project.envelope(kMaster, autom::kMixerVolume)}};
        const QString key = QStringLiteral("range-drag");
        f.editor.moveAutomationRange(6.0, 10.0, originals, 0.0, 0.05, key);
        f.editor.moveAutomationRange(6.0, 10.0, originals, 0.0, 0.1, key);
        QCOMPARE(f.stack.count(), before + 1);
        QVERIFY(near(f.project.tempoMap().bpmAt(8.0), 144.0, 0.01));  // 130 + 0.1 of 140
        QVERIFY(near(*autom::valueAt(f.project.envelope(kMaster, autom::kMixerVolume), 8.0), 0.35));
        f.stack.undo();
        QVERIFY(near(f.project.tempoMap().bpmAt(8.0), 130.0));
        QVERIFY(near(*autom::valueAt(f.project.envelope(kMaster, autom::kMixerVolume), 8.0), 0.25));
    }

    // A take's seconds are the timeline's: its beats are where the tempo map has them.
    void recordingsArePlacedByTheTempoMap() {
        EditorFixture f;
        f.editor.setTempoMap(slowsAtBeat4());
        const QString track = QStringLiteral("t");
        f.project.insertTrack(test::makeTrack(track, QStringLiteral("T")), 0);
        RecordedTake take;
        take.trackId = track;
        take.path = QStringLiteral("take.wav");
        take.startSec = 3.0;  // beat 5
        take.durationSec = 1.0;
        f.editor.addRecordings({take}, 0.0, false);
        QVERIFY(near(f.track(track).clips.at(0).startBeat, 5.0));
    }
};

QTEST_GUILESS_MAIN(TestTempoMapModel)
#include "test_tempo_map_model.moc"
