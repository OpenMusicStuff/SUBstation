// Audio files' tempo, key and chords on the application's side: which added
// files are warped at the tempo heard (detectedWarp), warping clips as they
// were added (ProjectEditor::warpToDetectedTempo), a file's chords along a clip
// (AudioAnalysis::clipChords: its window, the tempo, transposing, Re-Pitch),
// requests and their results, and audio clips' chords in the song's (Harmony).
// The analysis itself is the intelligence module's (tests/intelligence).

#include "EditorFixture.h"
#include "TestSupport.h"

#include "editor/ProjectEditor.h"
#include "intelligence/AudioAnalysis.h"
#include "intelligence/Harmony.h"
#include "model/Keys.h"

#include <QSignalSpy>
#include <QTest>

using namespace sub::app;
using test::EditorFixture;
using test::Spans;
using test::spans;
namespace harmony = sub::intelligence::harmony;

namespace {

AudioAnalysis::Result heardWith(std::vector<AudioAnalysis::Chord> chords, std::optional<double> bpm = 120.0) {
    AudioAnalysis::Result result;
    result.bpm = bpm;
    result.chords = std::move(chords);
    return result;
}

QStringList names(const std::vector<AudioAnalysis::ClipChord>& chords) {
    QStringList list;
    for (const auto& c : chords) list << QString::fromStdString(c.chord.name());
    return list;
}

}  // namespace

class TestAudioAnalysis : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void whichFilesAreWarpedAtTheTempoHeard() {
        // A tempo in the name wins; none heard, nothing.
        QVERIFY(!detectedWarp("Loop_128.wav", 7.5, 120.0));
        QVERIFY(!detectedWarp("loop.wav", 7.5, std::nullopt));
        // Long: warped at what was heard.
        QCOMPARE(*detectedWarp("song.wav", 200.0, 97.0), 97.0);
        // Short: only whole bars at that tempo (a 2-bar loop at 128: 3.75 s).
        QCOMPARE(*detectedWarp("loop.wav", 3.75, 128.0), 128.0);
        QCOMPARE(*detectedWarp("loop.wav", 3.76, 128.0), 128.0);  // (within 2 %)
        QVERIFY(!detectedWarp("loop.wav", 3.0, 128.0));            // 1.6 bars
        QVERIFY(!detectedWarp("kick.wav", 0.4, 128.0));            // a one-shot
    }

    void addedClipsAreWarpedAsTheyWereAdded() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        // Two 2-bar loops at 100 BPM (4.8 s each), placed end to end unwarped: 9.6 beats each at 120.
        const ClipRefs refs = f.editor.addClips(track, 4.0, {{"a.wav", 4.8}, {"b.wav", 4.8}});
        std::vector<Clip> added;
        for (const ClipRef& ref : refs) added.push_back(f.project.clip(ref.trackId, ref.clipId));
        QVERIFY((spans(f.track(track).clips) == Spans{{4, 13.6}, {13.6, 23.2}}));
        QVERIFY(f.editor.warpToDetectedTempo(track, added, {{added[0].id, 100.0}, {added[1].id, 100.0}}));
        QCOMPARE(f.stack.undoText(), QStringLiteral("Detect Tempo"));
        // Warped at 100: 8 beats each, still end to end.
        QVERIFY((spans(f.track(track).clips) == Spans{{4, 12}, {12, 20}}));
        QVERIFY(f.track(track).clips[0].isWarped());
        QCOMPARE(f.track(track).clips[1].segmentBpm, 100.0);
        f.stack.undo();
        QVERIFY((spans(f.track(track).clips) == Spans{{4, 13.6}, {13.6, 23.2}}));
        QVERIFY(!f.editor.warpToDetectedTempo(track, added, {}));  // (nothing to warp: the same)

        // A clip changed since it was added stays as it is, and so do those after it.
        Clip moved = f.track(track).clips[0];
        moved.gainDb = -6.0;
        f.editor.commitClips(QStringLiteral("Gain"), {{track, {moved, f.track(track).clips[1]}}});
        QVERIFY(!f.editor.warpToDetectedTempo(track, added, {{added[0].id, 100.0}, {added[1].id, 100.0}}));
        QCOMPARE(f.stack.undoText(), QStringLiteral("Gain"));
    }

    void aFilesChordsAlongAClip() {
        using Q = harmony::Quality;
        const AudioAnalysis::Result heard = heardWith({{0.0, 2.0, {0, Q::Major}},     // C, 0..2 s
                                                       {2.0, 4.0, {9, Q::Minor}},     // Am
                                                       {4.0, 6.0, {5, Q::Major, 9}}});  // F/A
        // Unwarped at 120 (a beat each half second), from 1 s of the file to 5 s, at beat 8.
        Clip clip = Clip::audio("c", "song.wav", "song", 8.0, 4.0, 1.0, 6.0);
        auto chords = AudioAnalysis::clipChords(heard, clip, 120.0);
        QCOMPARE(names(chords), (QStringList{"C", "Am", "F/A"}));
        QCOMPARE(chords[0].startBeat, 8.0);
        QCOMPARE(chords[0].endBeat, 10.0);
        QCOMPARE(chords[1].endBeat, 14.0);
        QCOMPARE(chords[2].endBeat, 16.0);
        // Transposed up 2: D, Bm, G/B.
        clip.transpose = 2;
        QCOMPARE(names(AudioAnalysis::clipChords(heard, clip, 120.0)), (QStringList{"D", "Bm", "G/B"}));
        // Warped at 60 BPM: a second is a beat.
        clip.transpose = 0;
        clip.warp = true;
        clip.segmentBpm = 60.0;
        chords = AudioAnalysis::clipChords(heard, clip, 120.0);
        QCOMPARE(chords[0].endBeat, 9.0);
        QCOMPARE(AudioAnalysis::clipShift(clip, 120.0), 0);
        // Re-Pitch, twice as fast: an octave up (and transposing does nothing).
        clip.warpMode = QStringLiteral("Re-Pitch");
        clip.transpose = 3;
        QCOMPARE(AudioAnalysis::clipShift(clip, 120.0), 12);
        QCOMPARE(names(AudioAnalysis::clipChords(heard, clip, 127.0)).first(), QStringLiteral("C#"));
    }

    void requestsHearOfTheirResults() {
        AudioAnalysis analysis(nullptr);
        int analysed = 0;
        analysis.setAnalyser([&](const QString& path) {
            ++analysed;
            AudioAnalysis::Result result;
            if (path.contains("loop")) result.bpm = 128.0;
            result.key = Key{9, true};
            return result;
        });
        QSignalSpy spy(&analysis, &AudioAnalysis::analysed);
        std::optional<double> heard;
        analysis.request("C:/x/loop.wav", [&](const AudioAnalysis::Result* r) { heard = r ? r->bpm : std::nullopt; });
        QCOMPARE(heard, std::optional<double>(128.0));
        QCOMPARE(spy.count(), 1);
        analysis.request("C:/x/loop.wav");  // (known: not again)
        QCOMPARE(analysed, 1);
        QCOMPARE(analysis.fileInfo("C:/x/loop.wav").value("bpm").toDouble(), 128.0);
        QCOMPARE(analysis.fileInfo("C:/x/loop.wav").value("key").toString(), QStringLiteral("A Minor"));
        QVERIFY(!analysis.fileInfo("C:/x/other.wav").value("analysed").toBool());

        AudioAnalysis off(nullptr);
        off.setEnabled(false);
        bool called = false;
        off.request("C:/x/loop.wav", [&](const AudioAnalysis::Result* r) { called = r == nullptr; });
        QVERIFY(called);
    }

    void audioClipsChordsAreHeardInTheSongs() {
        using Q = harmony::Quality;
        EditorFixture f;
        AudioAnalysis analysis(nullptr);
        analysis.setAnalyser([](const QString&) {
            // 4 s each (2 bars at 120): C, G, Am, F.
            return heardWith({{0, 4, {0, Q::Major}}, {4, 8, {7, Q::Major}}, {8, 12, {9, Q::Minor}}, {12, 16, {5, Q::Major}}});
        });
        Harmony harmony(&f.project);
        harmony.setAudioAnalysis(&analysis);
        const QString keys = f.editor.addAudioTrack(-1, QStringLiteral("Keys"));
        f.editor.addClips(keys, 0.0, {{"keys.wav", 16.0}});
        QVERIFY(harmony.chords().empty());  // (not analysed yet)
        analysis.request("keys.wav");
        QStringList chords;
        for (const auto& span : harmony.chords()) chords << QString::fromStdString(span.chord.name());
        QCOMPARE(chords, (QStringList{"C", "G", "Am", "F"}));
        QCOMPARE(harmony.chords()[1].start, 8.0);
        QVERIFY(harmony.key() == (Key{0, false}));
        // A drum track's audio is no harmony; a muted track's isn't heard.
        f.editor.renameTrack(keys, QStringLiteral("Drums"));
        QVERIFY(harmony.chords().empty());
    }
};

QTEST_GUILESS_MAIN(TestAudioAnalysis)
#include "test_audio_analysis.moc"
