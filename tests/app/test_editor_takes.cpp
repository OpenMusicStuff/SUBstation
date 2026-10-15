// Take lanes: recording keeps each take on a lane of its own, comping a range of
// a take into the track, what of a lane the comp plays, deleting lanes, showing
// them, saving them, and copies of tracks getting lanes of their own.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "editor/ProjectEditor.h"
#include "io/Serialization.h"
#include "model/Edits.h"
#include "model/Ids.h"
#include "model/RecordedTake.h"

#include <QSet>
#include <QTest>

using namespace sub::app;
using test::EditorFixture;
using test::Spans;
using test::spans;

namespace {

// An audio take of `sec` seconds at `startSec` (at 120 BPM a beat is half a second).
RecordedTake audioTake(const QString& trackId, const QString& path, double startSec, double sec) {
    return {trackId, path, startSec, sec, {}, false};
}

QStringList laneNames(const Track& track) {
    QStringList names;
    for (const TakeLane& lane : track.takes) names.append(lane.name);
    return names;
}

}  // namespace

class TestEditorTakes : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void recordingKeepsEachTakeOnALane() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        f.editor.addClips(track, 0.0, {{"C:/x/loop.wav", 4.0}});  // beats 0..8
        f.editor.addRecordings({audioTake(track, "C:/rec/one.wav", 1.0, 1.5)});  // beats 2..5
        QCOMPARE(f.stack.undoText(), QStringLiteral("Record"));
        QVERIFY((spans(f.track(track).clips) == Spans{{0, 2}, {2, 5}, {5, 8}}));  // overdubbed, as always
        QCOMPARE(laneNames(f.track(track)), QStringList{"Take 1"});
        const TakeLane& first = f.track(track).takes[0];
        QVERIFY((spans(first.clips) == Spans{{2, 5}}));
        QCOMPARE(first.clips[0].path, QStringLiteral("C:/rec/one.wav"));
        QVERIFY(first.clips[0].id != f.track(track).clips[1].id);  // a copy, with an id of its own

        f.editor.addRecordings({audioTake(track, "C:/rec/two.wav", 0.5, 2.0)});  // beats 1..5
        QCOMPARE(laneNames(f.track(track)), (QStringList{"Take 1", "Take 2"}));
        f.stack.undo();  // the clips and the lane go in one step
        QCOMPARE(laneNames(f.track(track)), QStringList{"Take 1"});
        QVERIFY((spans(f.track(track).clips) == Spans{{0, 2}, {2, 5}, {5, 8}}));
        f.stack.undo();
        QVERIFY(f.track(track).takes.empty());
        f.stack.redo();
        f.stack.redo();
        QCOMPARE(laneNames(f.track(track)), (QStringList{"Take 1", "Take 2"}));

        // Numbered past the highest, whatever was deleted.
        f.editor.deleteTakeLane(track, f.track(track).takes[0].id);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Delete Take Lane"));
        f.editor.addRecordings({audioTake(track, "C:/rec/three.wav", 0.0, 1.0)});
        QCOMPARE(laneNames(f.track(track)), (QStringList{"Take 2", "Take 3"}));
    }

    void midiTakesHaveLanesToo() {
        EditorFixture f;
        const QString track = f.editor.addMidiTrack();
        RecordedTake take{track, {}, 0.0, 2.0, {{0.0, 0.5, 60, 100, {}}, {1.0, 1.5, 64, 90, {}}}, true};
        f.editor.addRecordings({take});
        QCOMPARE(f.track(track).takes.size(), size_t(1));
        const Clip& kept = f.track(track).takes[0].clips.at(0);
        QVERIFY(kept.isMidi());
        QCOMPARE(kept.notes, f.track(track).clips.at(0).notes);
    }

    void aTakeRoundTheLoopIsATakeForEachPass() {
        // Recorded from beat 1 (0.5 s) with the loop over beats 0..4 (0..2 s): round it twice and a half.
        RecordedTake take = audioTake("t", "C:/rec/loop.wav", 0.5, 4.5);  // recording time 0.5..5 s
        take.wraps = {{2.0, 2.0}, {4.0, 2.0}};
        const std::vector<RecordedTake> passes = loopPasses(take);
        QCOMPARE(passes.size(), size_t(3));
        QCOMPARE(passes[0].startSec, 0.5);
        QCOMPARE(passes[0].durationSec, 1.5);
        QCOMPARE(passes[0].offsetSec, 0.0);
        QCOMPARE(passes[1].startSec, 0.0);  // round again from the loop's start
        QCOMPARE(passes[1].durationSec, 2.0);
        QCOMPARE(passes[1].offsetSec, 1.5);  // its stretch of the file
        QCOMPARE(passes[2].startSec, 0.0);
        QCOMPARE(passes[2].durationSec, 1.0);
        QCOMPARE(passes[2].offsetSec, 3.5);
        for (const RecordedTake& pass : passes) QCOMPARE(pass.fileSec, 4.5);

        // A pass a moment long (stopped just after the loop wrapped) is none.
        take.durationSec = 3.52;  // to 4.02 s
        QCOMPARE(loopPasses(take).size(), size_t(2));
        // Without wraps: the take itself.
        QCOMPARE(loopPasses(audioTake("t", "a.wav", 1.0, 2.0)).size(), size_t(1));

        // MIDI: the notes starting in each pass, at their place round the loop, cut at its end.
        RecordedTake midi{"t", {}, 0.0, 4.0, {{0.5, 1.0, 60, 100, {}}, {1.5, 2.5, 62, 100, {}}, {2.5, 3.0, 64, 90, {}}},
                          true};
        midi.wraps = {{2.0, 2.0}};
        const auto notes = loopPasses(midi);
        QCOMPARE(notes.size(), size_t(2));
        QCOMPARE(notes[0].notes.size(), size_t(2));
        QCOMPARE(notes[0].notes[1].end, 2.0);  // held over the wrap: cut there
        QCOMPARE(notes[1].notes.size(), size_t(1));
        QCOMPARE(notes[1].notes[0].start, 0.5);
        QCOMPARE(notes[1].notes[0].pitch, 64);
    }

    void punchingKeepsOnlyTheLoopsStretch() {
        // Played in from 0.5 s, the loop 1..3 s (beats 2..6), round it once more and stopped at 2.5 s
        // (recording time 0.5..4.5 s: at 3 s the timeline went back 2 s).
        RecordedTake take = audioTake("t", "C:/rec/punch.wav", 0.5, 4.0);
        take.wraps = {{3.0, 2.0}};
        take.punchInSec = 1.0;
        std::vector<RecordedTake> passes = loopPasses(take);
        QCOMPARE(passes.size(), size_t(2));
        QCOMPARE(passes[0].startSec, 1.0);  // from the loop's start: the run-in left out
        QCOMPARE(passes[0].durationSec, 2.0);
        QCOMPARE(passes[0].offsetSec, 0.5);
        QCOMPARE(passes[1].startSec, 1.0);
        QCOMPARE(passes[1].offsetSec, 2.5);
        QCOMPARE(passes[1].durationSec, 1.5);

        // Punch Out, the loop off: what was played after the loop's end is left out.
        RecordedTake straight = audioTake("t", "C:/rec/out.wav", 0.0, 5.0);
        straight.punchInSec = 1.0;
        straight.punchOutSec = 3.0;
        passes = loopPasses(straight);
        QCOMPARE(passes.size(), size_t(1));
        QCOMPARE(passes[0].startSec, 1.0);
        QCOMPARE(passes[0].durationSec, 2.0);
        QCOMPARE(passes[0].offsetSec, 1.0);
        // Recorded only before the punch region: nothing.
        straight.durationSec = 0.8;
        QVERIFY(loopPasses(straight).empty());

        // MIDI: the notes started inside it, cut at Punch Out.
        RecordedTake midi{"t", {}, 0.0, 5.0, {{0.5, 1.5, 60, 100, {}}, {2.5, 3.5, 62, 100, {}}, {4.0, 4.5, 64, 90, {}}},
                          true};
        midi.punchInSec = 1.0;
        midi.punchOutSec = 3.0;
        passes = loopPasses(midi);
        QCOMPARE(passes.size(), size_t(1));
        QCOMPARE(passes[0].notes.size(), size_t(1));
        QCOMPARE(passes[0].notes[0].pitch, 62);
        QCOMPARE(passes[0].notes[0].end, 3.0);

        // On a track: its clips before the punch region stay as they were.
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        f.editor.addClips(track, 0.0, {{"C:/x/old.wav", 4.0}});  // beats 0..8
        RecordedTake punched = audioTake(track, "C:/rec/p.wav", 0.0, 4.0);
        punched.punchInSec = 1.0;
        punched.punchOutSec = 3.0;
        f.editor.addRecordings({punched});
        QVERIFY((spans(f.track(track).clips) == Spans{{0, 2}, {2, 6}, {6, 8}}));
        QCOMPARE(f.track(track).clips[1].path, QStringLiteral("C:/rec/p.wav"));
        QCOMPARE(f.track(track).takes.size(), size_t(1));
    }

    void recordingRoundTheLoopMakesALaneForEachPass() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        RecordedTake take = audioTake(track, "C:/rec/loop.wav", 0.0, 5.0);  // the loop over beats 0..4, round 2.5 times
        take.wraps = {{2.0, 2.0}, {4.0, 2.0}};
        f.editor.addRecordings({take});
        QCOMPARE(f.stack.undoText(), QStringLiteral("Record"));
        QCOMPARE(laneNames(f.track(track)), (QStringList{"Take 1", "Take 2", "Take 3"}));
        // The track plays the last time round where it got to, the one before for the rest.
        const auto& clips = f.track(track).clips;
        QVERIFY((spans(clips) == Spans{{0, 2}, {2, 4}}));
        QCOMPARE(clips[0].offsetSec, 4.0);  // the third pass
        QCOMPARE(clips[1].offsetSec, 3.0);  // the second's last second
        QVERIFY((edits::compedSpans(clips, f.track(track).takes[2].clips, 120.0) ==
                 std::vector<edits::Interval>{{0, 2}}));
        QVERIFY(edits::compedSpans(clips, f.track(track).takes[0].clips, 120.0).empty());
        // Comping the first pass back: its audio, from the file's start.
        QVERIFY(f.editor.compTake(track, f.track(track).takes[0].id, 0.0, 4.0));
        QVERIFY((spans(f.track(track).clips) == Spans{{0, 4}}));
        QCOMPARE(f.track(track).clips[0].offsetSec, 0.0);
        f.stack.undo();
        f.stack.undo();
        QVERIFY(f.track(track).clips.empty() && f.track(track).takes.empty());
    }

    void compingARangeReplacesWhatPlaysThere() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        f.editor.addRecordings({audioTake(track, "C:/rec/one.wav", 0.0, 4.0)});  // beats 0..8
        f.editor.addRecordings({audioTake(track, "C:/rec/two.wav", 1.0, 2.0)});  // beats 2..6
        const QString one = f.track(track).takes[0].id;
        const QString two = f.track(track).takes[1].id;
        // The track plays one, then two over 2..6, then one.
        QVERIFY((spans(f.track(track).clips) == Spans{{0, 2}, {2, 6}, {6, 8}}));
        QVERIFY((edits::compedSpans(f.track(track).clips, f.track(track).takes[0].clips, 120.0) ==
                 std::vector<edits::Interval>{{0, 2}, {6, 8}}));
        QVERIFY((edits::compedSpans(f.track(track).clips, f.track(track).takes[1].clips, 120.0) ==
                 std::vector<edits::Interval>{{2, 6}}));

        // Take one's 3..5 back.
        QVERIFY(f.editor.compTake(track, one, 3.0, 5.0));
        QCOMPARE(f.stack.undoText(), QStringLiteral("Comp Take"));
        QVERIFY((spans(f.track(track).clips) == Spans{{0, 2}, {2, 3}, {3, 5}, {5, 6}, {6, 8}}));
        const Clip& middle = f.track(track).clips[2];
        QCOMPARE(middle.path, QStringLiteral("C:/rec/one.wav"));
        QCOMPARE(middle.offsetSec, 1.5);  // where take one is at beat 3
        QVERIFY((edits::compedSpans(f.track(track).clips, f.track(track).takes[0].clips, 120.0) ==
                 std::vector<edits::Interval>{{0, 2}, {3, 5}, {6, 8}}));
        QVERIFY((edits::compedSpans(f.track(track).clips, f.track(track).takes[1].clips, 120.0) ==
                 std::vector<edits::Interval>{{2, 3}, {5, 6}}));
        // The comp's clips are copies: the lanes are as they were.
        QVERIFY((spans(f.track(track).takes[0].clips) == Spans{{0, 8}}));
        QSet<QString> ids;
        for (const Clip& c : f.track(track).clips) ids.insert(c.id);
        for (const TakeLane& lane : f.track(track).takes) {
            for (const Clip& c : lane.clips) QVERIFY(!ids.contains(c.id));
        }

        // Where a take has nothing, comping it leaves silence.
        QVERIFY(f.editor.compTake(track, two, 0.0, 1.0));
        QVERIFY((spans(f.track(track).clips) == Spans{{1, 2}, {2, 3}, {3, 5}, {5, 6}, {6, 8}}));
        // Again: nothing changes.
        QVERIFY(!f.editor.compTake(track, two, 0.0, 1.0));
        QVERIFY(!f.editor.compTake(track, "nope", 0.0, 1.0));
        QVERIFY(!f.editor.compTake(track, two, 2.0, 2.0));
        f.stack.undo();
        f.stack.undo();
        QVERIFY((spans(f.track(track).clips) == Spans{{0, 2}, {2, 6}, {6, 8}}));
    }

    void deletingAndShowingLanes() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        f.editor.addRecordings({audioTake(track, "C:/rec/one.wav", 0.0, 1.0)});
        f.editor.addRecordings({audioTake(track, "C:/rec/two.wav", 0.0, 1.0)});
        const auto clips = f.track(track).clips;
        f.editor.setTakesShown(track, true);
        QVERIFY(f.track(track).takesShown);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Record"));  // (view state: no undo step)
        f.editor.deleteTakeLanes(track);
        QVERIFY(f.track(track).takes.empty());
        QVERIFY(f.track(track).clips == clips);  // what plays stays
        f.stack.undo();
        QCOMPARE(f.track(track).takes.size(), size_t(2));

        const QString group = f.editor.groupTracks({f.editor.addAudioTrack()});
        f.editor.setTakesShown(group, true);  // (a group has no takes)
        QVERIFY(!f.track(group).takesShown);
    }

    void lanesAreSavedAndCopiedWithTheirTracks() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        f.editor.addRecordings({audioTake(track, "C:/rec/one.wav", 0.0, 1.0)});
        f.editor.setTakesShown(track, true);
        f.editor.setPunchIn(true);
        QVERIFY(!projectToJson(f.project)["loop"].toObject().contains("punch_out"));  // (only when on)
        Project loaded;
        loadInto(loaded, projectToJson(f.project));
        QVERIFY(loaded.punchIn() && !loaded.punchOut());
        QVERIFY(loaded.track(track).takes == f.track(track).takes);
        QVERIFY(loaded.track(track).takesShown);

        const QStringList copies = f.editor.duplicateTracks({track});
        QCOMPARE(copies.size(), 1);
        const Track& copy = f.track(copies[0]);
        QCOMPARE(copy.takes.size(), size_t(1));
        QVERIFY(copy.takes[0].id != f.track(track).takes[0].id);
        QVERIFY(copy.takes[0].clips[0].id != f.track(track).takes[0].clips[0].id);
        QCOMPARE(copy.takes[0].clips[0].path, QStringLiteral("C:/rec/one.wav"));
    }
};

QTEST_GUILESS_MAIN(TestEditorTakes)
#include "test_editor_takes.moc"
