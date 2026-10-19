// Audio clips' warp markers (Clip::warpMarkers): moments of the file pinned to
// beats, the audio between two at the tempo that joins them; a clip's beats and
// seconds through them, adding, moving and removing them, the edits that keep
// them in step (split, stretch, the segment BPM, reversing), saving them, and
// the engine given them.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "audio/EngineDescs.h"
#include "io/Serialization.h"
#include "model/Edits.h"

#include <QJsonArray>
#include <QTest>

#include <cmath>

using namespace sub::app;
using sub::TempoMap;
using test::EditorFixture;

namespace {

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

// Three seconds of a file: its first second on two beats (120 BPM), the next two on two (60 BPM).
Clip markedClip() {
    Clip clip = test::audioClip(QStringLiteral("c"), 8.0, 3.0);
    clip.sourceDurationSec = 3.0;
    clip.warp = true;
    clip.setWarpMarkers({{0.0, 10.0}, {1.0, 12.0}, {3.0, 14.0}});  // (their beats start anywhere)
    return clip;
}

}  // namespace

class TestWarpMarkers : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void theAudioBetweenMarkersPlaysAtTheTempoThatJoinsThem() {
        const Clip clip = markedClip();
        QVERIFY(clip.isWarped());
        QCOMPARE(clip.warpTempos(), (std::vector<double>{120.0, 60.0}));
        QCOMPARE(clip.segmentBpm, 120.0);  // (the first stretch's)
        QVERIFY(clip.sourceMap.has_value());
        // Four beats long whatever the song's tempo; its second marker two beats in.
        QVERIFY(near(clip.lengthBeats(Tempo(90.0)), 4.0));
        QVERIFY(near(clip.endBeat(Tempo(140.0)), 12.0));
        QVERIFY(near(clip.beatsToSource(2.0, Tempo(90.0)), 1.0));
        QVERIFY(near(clip.beatsToSource(3.0, Tempo(90.0)), 2.0));
        QVERIFY(near(clip.sourceToBeatsFrom(10.0, 0.5, Tempo(90.0)), 0.5));  // (at 60 BPM)
        // Its own beats at its file's seconds, and back, as its markers count them.
        QVERIFY(near(clip.warpBeatAt(2.0), 13.0));
        QVERIFY(near(clip.warpSecondsAt(13.0), 2.0));
        QVERIFY(near(clip.warpBeatAt(4.0), 15.0));  // (after the last: the last stretch's tempo)
        QVERIFY(near(clip.warpSecondsAt(9.0), -0.5));  // (before the first: the first's)
        // Played from a second in: the same moments on the same beats.
        Clip later = clip;
        later.offsetSec = 1.0;
        later.durationSec = 2.0;
        QVERIFY(near(later.lengthBeats(Tempo(90.0)), 2.0));
    }

    void markersThatDontGoOnAreLeftOut() {
        Clip clip = markedClip();
        clip.setWarpMarkers({{2.0, 3.0}, {0.0, 0.0}, {1.0, 0.0}, {3.0, 5.0}});  // sorted; (1 s, beat 0) goes back
        QCOMPARE(clip.warpMarkers.size(), size_t{3});
        QCOMPARE(clip.warpMarkers[1], (WarpMarker{2.0, 3.0}));
        clip.setWarpMarkers({{1.0, 1.0}});  // one alone: none
        QVERIFY(clip.warpMarkers.empty());
        QVERIFY(!clip.sourceMap.has_value());
    }

    void splitStretchedAndRetimed() {
        const Tempo tempo(100.0);
        const Clip clip = markedClip();
        // Cut on its third beat (two seconds into the file): each part keeps the markers and its moments.
        const auto parts = edits::splitClip(clip, 11.0, tempo);
        QVERIFY(parts.has_value());
        QVERIFY(near(parts->first.durationSec, 2.0));
        QVERIFY(near(parts->second.offsetSec, 2.0));
        QCOMPARE(parts->second.warpMarkers, clip.warpMarkers);
        QVERIFY(near(parts->second.endBeat(tempo), 12.0));
        // Stretched to twice its length: twice the beats in each stretch's seconds (as a segment BPM
        // doubles when a clip without markers is).
        const Clip stretched = edits::stretchClip(clip, 16.0, false, tempo);
        QCOMPARE(stretched.warpTempos(), (std::vector<double>{240.0, 120.0}));
        QVERIFY(near(stretched.endBeat(tempo), 16.0));
        // Its segment BPM set: every stretch as much, held to the range.
        QCOMPARE(edits::setWarpTempo(clip, 240.0).warpTempos(), (std::vector<double>{240.0, 120.0}));
        const Clip slow = edits::setWarpTempo(clip, 1.0);  // (the slower stretch can't go below 20)
        QVERIFY(near(slow.warpTempos()[1], kMinSegmentBpm));
        QVERIFY(near(slow.warpTempos()[0], 2 * kMinSegmentBpm));
        // Reversed: the same moments, the file the other way round, as long.
        const Clip reversed = edits::reverseClip(clip, QStringLiteral("r.wav"), 3.0);
        QCOMPARE(reversed.warpTempos(), (std::vector<double>{60.0, 120.0}));
        QVERIFY(near(reversed.lengthBeats(tempo), 4.0));
    }

    void markersAddedMovedAndRemoved() {
        const Tempo tempo(100.0);
        // A clip at 120 BPM playing a second and a half from half a second in: marked at its start
        // and end and where asked, on the beats they play now.
        Clip plain = test::audioClip(QStringLiteral("p"), 4.0, 1.5);
        plain.sourceDurationSec = 3.0;
        plain.offsetSec = 0.5;
        plain.warp = true;
        plain.segmentBpm = 120.0;
        const Clip added = edits::addWarpMarker(plain, 1.0);
        QCOMPARE(added.warpMarkers, (std::vector<WarpMarker>{{0.5, 1.0}, {1.0, 2.0}, {2.0, 4.0}}));
        QCOMPARE(added.warpTempos(), (std::vector<double>{120.0, 120.0}));
        QVERIFY(near(added.lengthBeats(tempo), 3.0));
        QCOMPARE(edits::addWarpMarker(plain, 0.5).warpMarkers.size(), size_t{2});  // (at its start: one)
        // Several at once (a clip's transients): those in the file, each once.
        const Clip several = edits::addWarpMarkers(plain, {1.25, 1.0, 1.0005, 5.0});
        QCOMPARE(several.warpMarkers,
                 (std::vector<WarpMarker>{{0.5, 1.0}, {1.0, 2.0}, {1.25, 2.5}, {2.0, 4.0}}));
        QVERIFY(edits::addWarpMarkers(plain, {5.0}).warpMarkers.empty());
        QVERIFY(edits::addWarpMarker(plain, 4.0).warpMarkers.empty());              // (past the file)
        Clip unwarped = plain;
        unwarped.warp = false;
        QVERIFY(edits::addWarpMarker(unwarped, 1.0).warpMarkers.empty());
        Clip frozen = plain;  // (a frozen track's: the tempo it was rendered under)
        frozen.sourceMap = Tempo(TempoMap({{0.0, 120.0}, {2.0, 90.0}}));
        QVERIFY(edits::addWarpMarker(frozen, 1.0).warpMarkers.empty());
        QVERIFY(near(frozen.warpBeatAt(1.0), frozen.sourceMap->beatAt(1.0)));

        // The middle one moved to 1.5 s, still on beat 2: the first stretch slower, the second faster,
        // the clip on the same beats and as long.
        const Clip moved = edits::moveWarpMarker(added, 1, 1.5);
        QCOMPARE(moved.warpMarkers[1], (WarpMarker{1.5, 2.0}));
        QCOMPARE(moved.warpTempos(), (std::vector<double>{60.0, 240.0}));
        QVERIFY(near(moved.offsetSec, 0.5));
        QVERIFY(near(moved.durationSec, 1.5));
        QVERIFY(near(moved.lengthBeats(tempo), 3.0));
        // Held where its stretches stay within the range: the first can't go faster than 999 BPM.
        const Clip fast = edits::moveWarpMarker(added, 1, 0.0);
        QVERIFY(near(fast.warpTempos()[0], kMaxSegmentBpm));
        // The first moved: the clip starts on its beat still, so further into the file.
        const Clip first = edits::moveWarpMarker(added, 0, 0.75);
        QVERIFY(near(first.offsetSec, 0.75));
        QVERIFY(near(first.lengthBeats(tempo), 3.0));

        // Removed: the clip on the same beats; down to one, none (at the tempo the two made).
        const Clip removed = edits::removeWarpMarker(moved, 1);
        QCOMPARE(removed.warpMarkers.size(), size_t{2});
        QCOMPARE(removed.warpTempos(), (std::vector<double>{120.0}));
        QVERIFY(near(removed.lengthBeats(tempo), 3.0));
        const Clip none = edits::removeWarpMarker(removed, 0);
        QVERIFY(none.warpMarkers.empty());
        QVERIFY(!none.sourceMap.has_value());
        QCOMPARE(none.segmentBpm, 120.0);
        QVERIFY(near(none.lengthBeats(tempo), 3.0));
    }

    void savedAndPlayed() {
        EditorFixture f;
        const QString track = QStringLiteral("t");
        f.project.insertTrack(test::makeTrack(track, QStringLiteral("T")), 0);
        f.editor.commitClips(QStringLiteral("Clip"), {{track, {markedClip()}}});
        const QJsonObject saved = projectToJson(f.project);
        QCOMPARE(saved.value(QStringLiteral("version")).toInt(), kProjectVersion);
        Project opened;
        const test::TempDir dir;
        const QString path = dir.path(QStringLiteral("w.gilproj"));
        saveProject(f.project, path);
        loadProject(opened, path);
        const Clip& clip = opened.track(track).clips.at(0);
        QCOMPARE(clip.warpMarkers, markedClip().warpMarkers);
        QVERIFY(clip.sourceMap == markedClip().sourceMap);
        // The engine warps it through them: a source map of its own.
        QVERIFY(clipDesc(clip).sourceMap != nullptr);
        Clip steady = clip;
        steady.setWarpMarkers({{0.0, 0.0}, {1.0, 2.0}});  // (one tempo: none needed)
        QVERIFY(clipDesc(steady).sourceMap == nullptr);
        QCOMPARE(steady.segmentBpm, 120.0);

        // A file whose markers make a stretch faster than 999 BPM (written by hand) is damaged.
        QJsonObject data = projectToJson(f.project);
        QJsonArray tracks = data[QStringLiteral("tracks")].toArray();
        QJsonObject t = tracks[0].toObject();
        QJsonArray clips = t[QStringLiteral("clips")].toArray();
        QJsonObject c = clips[0].toObject();
        c[QStringLiteral("warp_markers")] = QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{1.0, 100.0}};
        clips[0] = c;
        t[QStringLiteral("clips")] = clips;
        tracks[0] = t;
        data[QStringLiteral("tracks")] = tracks;
        Project damaged;
        QVERIFY_THROWS_EXCEPTION(std::exception, loadInto(damaged, data));
        // ... and one whose stretch's tempo can't be worked out (the differences overflow).
        c[QStringLiteral("warp_markers")] = QJsonArray{QJsonArray{-1e308, -1e308}, QJsonArray{1e308, 1e308}};
        clips[0] = c;
        t[QStringLiteral("clips")] = clips;
        tracks[0] = t;
        data[QStringLiteral("tracks")] = tracks;
        QVERIFY_THROWS_EXCEPTION(std::exception, loadInto(damaged, data));
        // ... and one whose time isn't a number (not taken as 0).
        c[QStringLiteral("warp_markers")] = QJsonArray{QJsonArray{QStringLiteral("bad"), 0.0}, QJsonArray{1.0, 2.0}};
        clips[0] = c;
        t[QStringLiteral("clips")] = clips;
        tracks[0] = t;
        data[QStringLiteral("tracks")] = tracks;
        QVERIFY_THROWS_EXCEPTION(std::exception, loadInto(damaged, data));
    }
};

QTEST_GUILESS_MAIN(TestWarpMarkers)
#include "test_warp_markers.moc"
