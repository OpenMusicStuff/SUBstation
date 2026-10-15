// Take lanes: recording keeps each take on a lane of its own, comping a range of
// a take into the track, what of a lane the comp plays, deleting lanes, showing
// them, saving them, and copies of tracks getting lanes of their own.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "editor/ProjectEditor.h"
#include "io/Serialization.h"
#include "model/Edits.h"
#include "model/Ids.h"

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
        Project loaded;
        loadInto(loaded, projectToJson(f.project));
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
