// Grooves in the application (model/Grooves.h, editor/EditorGrooves.cpp,
// io/GrooveFiles.h): a MIDI clip's notes and an audio clip's warp markers as
// its groove plays them, Apply Groove (notes, clips, the device), Extract
// Groove, the project's library with undo (a groove removed leaving its users,
// take lanes' clips too, their own copy), the Groove device's source, the default
// groove, and grooves in project files, .gilgroove and .agr files.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "io/GrooveFiles.h"
#include "io/Serialization.h"
#include "model/DeviceState.h"
#include "model/Errors.h"
#include "model/Grooves.h"
#include "model/Notes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include <cmath>

using namespace sub::app;
using sub::TempoMap;
using test::EditorFixture;
namespace groove = sub::groove;

namespace {

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

const groove::Groove kTriplet = groove::swing(groove::kTripletSwing, 0.25);

// A MIDI clip at `start` of 16th notes on `pitch`, `count` of them.
Clip sixteenths(const QString& id, double start, int count, int pitch = 60) {
    std::vector<Note> notes;
    for (int i = 0; i < count; ++i) notes.push_back({pitch, i * 0.25, 0.125, 100});
    return Clip::midi(id, {}, start, count * 0.25, 0.0, notes);
}

ClipGroove clipGroove(const GrooveSource& source, double timing = 1.0) {
    ClipGroove g;
    g.source = source;
    g.amounts.timing = timing;
    return g;
}

// A MIDI track with a clip; (track id, clip id).
std::pair<QString, QString> midiClipOn(EditorFixture& f, const Clip& clip) {
    const QString track = f.editor.addMidiTrack();
    const std::optional<ClipRef> added = f.editor.addMidiClip(track, clip.startBeat, clip.durationBeats);
    Clip placed = clip;
    placed.id = added->clipId;
    f.editor.replaceClip(track, placed, QStringLiteral("Notes"));
    return {track, added->clipId};
}

}  // namespace

class TestGrooves : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    // --- The model -----------------------------------------------------------------------------

    void groovedNotesMoveWhereTheGroovePutsThem() {
        Clip clip = sixteenths(QStringLiteral("c"), 4.0, 4);
        clip.notes[3].velocity = 90;
        const std::vector<PlayedNote> played = grooves::groovedNotes(clip, kTriplet, {}, Tempo(120.0));
        QCOMPARE(played.size(), size_t{4});
        QVERIFY(near(played[0].start, 4.0));
        QVERIFY(near(played[1].start, 4.0 + 1.0 / 3));  // the offbeat on the triplet
        QVERIFY(near(played[2].start, 4.5));
        QVERIFY(near(played[3].start, 4.5 + 1.0 / 3));
        QVERIFY(near(played[1].end - played[1].start, 0.125));  // (Length 0: as long)
        QCOMPARE(played[3].note.velocity, 90);
        // A note running into the next on its key ends where it starts.
        Clip legato = Clip::midi(QStringLiteral("l"), {}, 0.0, 1.0, 0.0, {{60, 0.25, 0.25, 100}, {60, 0.5, 0.25, 100}});
        groove::Groove late = kTriplet;
        late.lanes.front().steps[2].timing = -0.45;  // the next pulled early, into it
        const std::vector<PlayedNote> held = grooves::groovedNotes(legato, late, {}, Tempo(120.0));
        QVERIFY(held[0].end <= held[1].start + 1e-9);
    }

    void notesGroovedAsTheDeviceWouldPlayThem() {
        // Apply Groove (notes::grooved) places each note as groove::place() does, which the Groove device plays.
        const std::vector<Note> notes{{60, 0.0, 0.2, 100}, {62, 0.25, 0.2, 80}, {64, 0.77, 0.1, 64}};
        groove::Amounts amounts;
        amounts.velocity = 0.5;
        amounts.quantize = 0.5;
        groove::Groove accents = kTriplet;
        accents.lanes.front().steps[1].velocity = -0.2;
        const std::vector<Note> grooved = notes::grooved(notes, accents, amounts, Tempo(97.0));
        QCOMPARE(grooved.size(), notes.size());
        for (size_t i = 0; i < notes.size(); ++i) {
            const groove::Placed placed = groove::place(accents, notes[i].start, notes[i].pitch, notes[i].velocity,
                                                        notes[i].length, TempoMap(97.0), amounts);
            QVERIFY(near(grooved[i].start, placed.beat));
            QCOMPARE(grooved[i].velocity, static_cast<int>(std::lround(placed.velocity)));
        }
    }

    void groovedMarkersMoveTheTransients() {
        // A warped loop at 120: a hit every 16th (0.125 s), its transients there.
        Clip clip = test::audioClip(QStringLiteral("a"), 0.0, 1.0);
        clip.sourceDurationSec = 1.0;
        clip.warp = true;
        clip.segmentBpm = 120.0;
        std::vector<double> hits;
        for (int i = 1; i < 8; ++i) hits.push_back(i * 0.125);
        const std::vector<WarpMarker> markers = grooves::groovedMarkers(clip, hits, kTriplet, {}, Tempo(120.0));
        Clip grooved = clip;
        grooved.setWarpMarkers(markers);
        // Each hit on the beat the groove gives it (its own beats from the clip's start, at 120).
        QVERIFY(near(grooved.warpBeatAt(0.125), 1.0 / 3, 1e-9));
        QVERIFY(near(grooved.warpBeatAt(0.25), 0.5, 1e-9));
        QVERIFY(near(grooved.warpBeatAt(0.375), 0.5 + 1.0 / 3, 1e-9));
        QVERIFY(near(grooved.warpBeatAt(0.0), 0.0, 1e-9));
        // Not warped: none.
        clip.warp = false;
        QVERIFY(grooves::groovedMarkers(clip, hits, kTriplet, {}, Tempo(120.0)).empty());
        QVERIFY(grooves::warpedForGroove(clip, 98.0).isWarped());
    }

    void applyGrooveWritesItIntoTheClip() {
        Clip clip = sixteenths(QStringLiteral("c"), 2.0, 4);
        clip.offsetBeats = 0.0;
        clip.groove = clipGroove(grooves::swingSource(groove::kTripletSwing));
        const Clip applied = grooves::applied(clip, kTriplet, {}, Tempo(120.0));
        QVERIFY(!applied.groove);
        QVERIFY(near(applied.notes[1].start, 1.0 / 3));
        QVERIFY(near(applied.notes[3].start, 0.5 + 1.0 / 3));
        // Without one resolved: only cleared.
        QCOMPARE(grooves::applied(clip, std::nullopt, {}, Tempo(120.0)).notes, clip.notes);
    }

    void extractGrooveFromAMidiClip() {
        Clip played = sixteenths(QStringLiteral("c"), 0.0, 16);
        for (size_t i = 1; i < played.notes.size(); i += 2) played.notes[i].start += 0.05;
        played.notes = notes::normalize(played.notes);
        const groove::Groove made = grooves::fromMidiClip(played, 0.25, 1, false);
        QVERIFY(near(made.lanes.front().steps[1].timing, 0.2, 1e-9));
        QVERIFY(near(made.lanes.front().steps[0].timing, 0.0, 1e-9));
    }

    void builtinsAreThere() {
        QVERIFY(grooves::builtins().size() >= 17);
        const LibraryGroove* mpc = grooves::builtin(QStringLiteral("builtin:mpc-16-66"));
        QVERIFY(mpc);
        QCOMPARE(mpc->name, QStringLiteral("MPC 16 Swing 66"));
        QVERIFY(groove::swingOf(mpc->groove).has_value());
        for (const LibraryGroove& g : grooves::builtins()) QCOMPARE(g.groove, groove::normalized(g.groove));
    }

    void sourcesResolve() {
        const std::vector<LibraryGroove> library{{QStringLiteral("g1"), QStringLiteral("One"), kTriplet}};
        QCOMPARE(*grooves::resolve(grooves::librarySource(QStringLiteral("g1")), library, std::nullopt), kTriplet);
        QVERIFY(!grooves::resolve(grooves::librarySource(QStringLiteral("gone")), library, std::nullopt));
        GrooveSource project;
        project.kind = GrooveSource::Kind::Project;
        QVERIFY(!grooves::resolve(project, library, std::nullopt));
        QCOMPARE(*grooves::resolve(project, library, grooves::librarySource(QStringLiteral("g1"))), kTriplet);
        const GrooveSource own = grooves::detached(grooves::librarySource(QStringLiteral("g1")), QStringLiteral("g1"), kTriplet);
        QCOMPARE(own.kind, GrooveSource::Kind::Own);
        QCOMPARE(*grooves::resolve(own, {}, std::nullopt), kTriplet);
        QCOMPARE(grooves::describe(grooves::swingSource(58.0, 0.5), library), QStringLiteral("Swing 58.0 % (1/8)"));
    }

    // --- The editor --------------------------------------------------------------------------

    void theLibraryIsEditedWithUndo() {
        EditorFixture f;
        const QString id = f.editor.addBuiltinGroove(QStringLiteral("builtin:mpc-16-58"));
        QCOMPARE(f.project.grooves().size(), size_t{1});
        QCOMPARE(f.project.grooves()[0].name, QStringLiteral("MPC 16 Swing 58"));
        const QString copy = f.editor.duplicateGroove(id);
        QCOMPARE(f.project.findGroove(copy)->name, QStringLiteral("MPC 16 Swing 58 2"));
        f.editor.renameGroove(copy, QStringLiteral("Mine"));
        QCOMPARE(f.project.findGroove(copy)->name, QStringLiteral("Mine"));
        // A drag in the groove editor is one step.
        groove::Groove edited = f.project.findGroove(copy)->groove;
        const double swung = edited.lanes.front().steps[3].timing;  // (58 %: 0.16 of a step late)
        QVERIFY(near(swung, 0.16));
        for (int i = 1; i <= 3; ++i) {
            edited.lanes.front().steps[3].timing = 0.1 * i;
            f.editor.setGroove(copy, edited, QStringLiteral("drag"));
        }
        QVERIFY(near(f.project.findGroove(copy)->groove.lanes.front().steps[3].timing, 0.3));
        QCOMPARE(f.stack.undoText(), QStringLiteral("Edit Groove"));
        f.stack.undo();
        QVERIFY(near(f.project.findGroove(copy)->groove.lanes.front().steps[3].timing, swung));
        f.stack.undo();
        QCOMPARE(f.project.findGroove(copy)->name, QStringLiteral("MPC 16 Swing 58 2"));
        f.editor.removeGroove(id);
        QCOMPARE(f.project.grooves().size(), size_t{1});
        f.stack.undo();
        QCOMPARE(f.project.grooves().size(), size_t{2});
    }

    void aGrooveRemovedLeavesItsUsersTheirOwnCopy() {
        EditorFixture f;
        const QString id = f.editor.addGroove(QStringLiteral("Shuffle"), kTriplet);
        const auto [track, clip] = midiClipOn(f, sixteenths(QString(), 0.0, 4));
        f.editor.setClipGroove({{track, clip}}, clipGroove(grooves::librarySource(id)));
        const QString device = f.editor.addDevice(track, QStringLiteral("groove"));
        f.editor.setDeviceGrooveSource(track, device, QStringLiteral("library"), id);
        QCOMPARE(grooves::deviceSource(f.project.device(track, device).state).libraryId, id);
        f.editor.setDefaultGrooveId(id);
        QCOMPARE(f.project.defaultGroove()->libraryId, id);
        Clip take = f.project.clip(track, clip);  // (a take of it, on a take lane)
        take.id = QStringLiteral("take1");
        f.project.setTakeLanes(track, {TakeLane{QStringLiteral("lane1"), QStringLiteral("Take 1"), {take}}});
        f.editor.removeGroove(id);
        QVERIFY(f.project.grooves().empty());
        QCOMPARE(f.project.track(track).takes.front().clips.front().groove->source.kind, GrooveSource::Kind::Own);
        // Each keeps playing it.
        const ClipGroove& left = *f.project.clip(track, clip).groove;
        QCOMPARE(left.source.kind, GrooveSource::Kind::Own);
        QCOMPARE(*left.source.own, kTriplet);
        QCOMPARE(f.project.defaultGroove()->kind, GrooveSource::Kind::Own);
        const GrooveSource deviceSource = grooves::deviceSource(f.project.device(track, device).state);
        QCOMPARE(deviceSource.kind, GrooveSource::Kind::Own);
        QCOMPARE(*deviceSource.own, kTriplet);
        // One step.
        f.stack.undo();
        QCOMPARE(f.project.clip(track, clip).groove->source.libraryId, id);
        QCOMPARE(f.project.track(track).takes.front().clips.front().groove->source.libraryId, id);
        QCOMPARE(f.project.defaultGroove()->libraryId, id);
        QCOMPARE(grooves::deviceSource(f.project.device(track, device).state).libraryId, id);
    }

    void aGrooveEditedReachesTheDevicesPlayingIt() {
        EditorFixture f;
        const QString id = f.editor.addGroove(QStringLiteral("Shuffle"), kTriplet);
        const QString track = f.editor.addMidiTrack();
        const QString device = f.editor.addDevice(track, QStringLiteral("groove"));
        f.editor.setDeviceGrooveSource(track, device, QStringLiteral("library"), id);
        const auto played = [&] {
            return groove::fromText(deviceState::fromModel(f.project.device(track, device).state)
                                        .value(QStringLiteral("groove"))
                                        .toStdString());
        };
        QCOMPARE(*played(), kTriplet);
        QVERIFY(grooves::deviceSeed(f.project.device(track, device).state) != 0);
        const groove::Groove straight = groove::swing(50.0, 0.25);
        f.editor.setGroove(id, straight);
        QCOMPARE(*played(), straight);
        f.stack.undo();
        QCOMPARE(*played(), kTriplet);
    }

    void clipGroovesWarpAudioAndApply() {
        EditorFixture f;
        const QString track = f.editor.addAudioTrack();
        Clip audio = test::audioClip(QStringLiteral("a"), 0.0, 1.0);
        audio.sourceDurationSec = 1.0;
        f.project.setClips(track, {audio});
        f.editor.setClipGroove({{track, QStringLiteral("a")}}, clipGroove(grooves::swingSource(60.0)));
        const Clip& grooved = f.project.clip(track, QStringLiteral("a"));
        QVERIFY(grooved.groove);
        QVERIFY(grooved.isWarped());  // (a groove warps it)
        QVERIFY(near(grooved.segmentBpm, 120.0));
        f.editor.setClipGroove({{track, QStringLiteral("a")}}, std::nullopt);
        QVERIFY(!f.project.clip(track, QStringLiteral("a")).groove);
        // MIDI: Apply Groove writes it into the notes.
        const auto [midi, clip] = midiClipOn(f, sixteenths(QString(), 4.0, 4));
        f.editor.setClipGroove({{midi, clip}}, clipGroove(grooves::swingSource(75.0)));
        f.editor.applyClipGrooves({{midi, clip}});
        const Clip& applied = f.project.clip(midi, clip);
        QVERIFY(!applied.groove);
        QVERIFY(near(applied.notes[1].start, 0.375));
        f.stack.undo();
        QVERIFY(f.project.clip(midi, clip).groove);
        QVERIFY(near(f.project.clip(midi, clip).notes[1].start, 0.25));
    }

    void applyGrooveOnTheDevice() {
        EditorFixture f;
        const auto [track, clip] = midiClipOn(f, sixteenths(QString(), 0.0, 4));
        const QString device = f.editor.addDevice(track, QStringLiteral("groove"));
        f.editor.setDeviceParam(track, device, QStringLiteral("swing"), 75.0);
        QVERIFY(f.editor.applyDeviceGroove(track, device));
        QVERIFY(near(f.project.clip(track, clip).notes[1].start, 0.375));
        QVERIFY(!f.project.device(track, device).enabled);
        f.stack.undo();  // (one step)
        QVERIFY(near(f.project.clip(track, clip).notes[1].start, 0.25));
        QVERIFY(f.project.device(track, device).enabled);
        QVERIFY(!f.editor.applyDeviceGroove(track, QStringLiteral("nothing")));
    }

    void applyGrooveOnAPadMovesOnlyItsNotes() {
        EditorFixture f;
        const QString track = f.editor.addMidiTrack();
        const QString rack = f.editor.addDevice(track, kDrumRackKind);
        const QString hats = f.editor.loadDrumPad(track, rack, 42, QStringLiteral("/samples/Hat.wav"));
        QVERIFY(!hats.isEmpty());
        // Hats and a kick, on every 16th.
        Clip both = sixteenths(QString(), 0.0, 4, 42);
        for (const Note& n : sixteenths(QString(), 0.0, 4, 36).notes) both.notes.push_back(n);
        both.notes = notes::normalize(both.notes);
        const QString clip = f.editor.addMidiClip(track, 0.0, 1.0)->clipId;
        both.id = clip;
        f.editor.replaceClip(track, both, QStringLiteral("Notes"));
        // A Groove device on the hats' pad.
        const QString device = f.editor.addDevice(track, QStringLiteral("groove"), 0, hats);
        QVERIFY(!device.isEmpty());
        QCOMPARE(*containerOf(f.track(track).devices, device), hats);
        f.editor.setDeviceParam(track, device, QStringLiteral("swing"), 75.0);
        QVERIFY(f.editor.applyDeviceGroove(track, device));
        std::vector<double> kicks, hatStarts;
        for (const Note& n : f.project.clip(track, clip).notes) (n.pitch == 36 ? kicks : hatStarts).push_back(n.start);
        QCOMPARE(kicks, (std::vector<double>{0.0, 0.25, 0.5, 0.75}));
        QCOMPARE(hatStarts, (std::vector<double>{0.0, 0.375, 0.5, 0.875}));
    }

    void theDefaultGroove() {
        EditorFixture f;
        QVERIFY(!f.project.defaultGroove());
        f.editor.setDefaultSwing(58.0, QStringLiteral("k"));
        f.editor.setDefaultSwing(62.0, QStringLiteral("k"));
        QCOMPARE(f.project.defaultGroove()->kind, GrooveSource::Kind::Swing);
        QVERIFY(near(f.project.defaultGroove()->swing, 62.0));
        f.editor.setDefaultSwing(50.0);  // straight: none
        QVERIFY(!f.project.defaultGroove());
        f.stack.undo();
        f.stack.undo();
        QVERIFY(!f.project.defaultGroove());  // (the drag was one step)
        f.editor.setDefaultGrooveId(QStringLiteral("not there"));
        QVERIFY(!f.project.defaultGroove());
    }

    void extractGrooveIntoTheLibrary() {
        EditorFixture f;
        Clip played = sixteenths(QString(), 0.0, 8);
        for (size_t i = 1; i < played.notes.size(); i += 2) played.notes[i].start += 0.04;
        played.notes = notes::normalize(played.notes);
        const auto [track, clip] = midiClipOn(f, played);
        const QString id = f.editor.extractGroove(track, clip, 0.25, 1, false);
        QVERIFY(!id.isEmpty());
        QCOMPARE(f.project.findGroove(id)->name, QStringLiteral("%1 Groove").arg(f.track(track).name));
        QVERIFY(near(f.project.findGroove(id)->groove.lanes.front().steps[1].timing, 0.16, 1e-9));
        // An audio clip's file not decoded (no transients): nothing.
        const QString audio = f.editor.addAudioTrack();
        f.project.setClips(audio, {test::audioClip(QStringLiteral("a"), 0.0, 1.0)});
        QVERIFY(f.editor.extractGroove(audio, QStringLiteral("a")).isEmpty());
        f.editor.setTransients([](const QString&) {
            return std::vector<std::pair<double, float>>{{0.0, 1.f}, {0.14, 0.5f}, {0.25, 1.f}, {0.39, 0.5f}};
        });
        const QString heard = f.editor.extractGroove(audio, QStringLiteral("a"), 0.25, 1, false);
        QVERIFY(!heard.isEmpty());
        QVERIFY(near(f.project.findGroove(heard)->groove.lanes.front().steps[1].timing, 0.12, 1e-6));
    }

    // --- Files ------------------------------------------------------------------------------------

    void grooveFilesRoundTrip() {
        const test::TempDir dir;
        const LibraryGroove* drunk = grooves::builtin(QStringLiteral("builtin:drunk"));
        QVERIFY(drunk);
        const QString path = dir.path(QStringLiteral("Drunk.gilgroove"));
        saveGrooveFile(*drunk, path);
        const LibraryGroove read = loadGrooveFile(path);
        QCOMPARE(read.name, QStringLiteral("Drunk"));
        QCOMPARE(read.groove, drunk->groove);
        QVERIFY(read.id != drunk->id);
        QFile bad(dir.path(QStringLiteral("bad.gilgroove")));
        bad.open(QIODevice::WriteOnly);
        bad.write("{\"format\": \"something else\"}");
        bad.close();
        QVERIFY_THROWS_EXCEPTION(ProjectFileError, loadGrooveFile(dir.path(QStringLiteral("bad.gilgroove"))));
    }

    void agrFilesAreAbletonsGrooves() {
        // Live's groove: a MIDI clip of 16ths, the offbeats 0.06 of a beat late, its Base 1/16, Timing 50 %.
        QString notes;
        for (int i = 0; i < 16; ++i) {
            notes += QStringLiteral("<MidiNoteEvent Time=\"%1\" Duration=\"0.1\" Velocity=\"%2\" IsEnabled=\"true\" />")
                         .arg(i * 0.25 + (i % 2 ? 0.06 : 0.0))
                         .arg(i % 4 == 0 ? 120 : 90);
        }
        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Ableton MajorVersion=\"5\"><GrooveFile><Clip><Value>"
            "<MidiClip Time=\"0\"><CurrentStart Value=\"0\" /><CurrentEnd Value=\"4\" /><Loop><LoopStart Value=\"0\" />"
            "<LoopEnd Value=\"4\" /></Loop><Name Value=\"Swing 16ths\" /><Notes><KeyTracks><KeyTrack Id=\"0\"><Notes>%1"
            "</Notes><MidiKey Value=\"60\" /></KeyTrack></KeyTracks></Notes></MidiClip></Value></Clip>"
            "<Base Value=\"3\" /><Quantization Value=\"0\" /><Timing Value=\"50\" /><Random Value=\"10\" />"
            "<Velocity Value=\"0\" /></GrooveFile></Ableton>")
                                .arg(notes);
        const test::TempDir dir;
        const QString path = dir.path(QStringLiteral("Swing 16ths.agr"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(xml.toUtf8());
        file.close();
        const LibraryGroove read = loadGrooveFile(path);
        QCOMPARE(read.name, QStringLiteral("Swing 16ths"));
        QVERIFY(near(read.groove.stepBeats, 0.25));
        // Its Timing worked in: half of 0.24 of a step.
        QVERIFY(near(read.groove.lanes.front().steps[1].timing, 0.12, 1e-9));
        QVERIFY(near(read.groove.lanes.front().steps[0].velocity, 0.0));  // (Velocity 0 %)
    }

    void projectFilesKeepGrooves() {
        EditorFixture f;
        const QString id = f.editor.addBuiltinGroove(QStringLiteral("builtin:boom-bap"));
        const auto [track, clip] = midiClipOn(f, sixteenths(QString(), 0.0, 4));
        ClipGroove setting = clipGroove(grooves::librarySource(id), 0.75);
        setting.amounts.quantize = 0.5;
        setting.amounts.random = 0.25;
        setting.amounts.seed = 7;
        f.editor.setClipGroove({{track, clip}}, setting);
        f.editor.setDefaultSwing(66.0);
        const QJsonObject saved = projectToJson(f.project);
        QCOMPARE(saved.value(QStringLiteral("grooves")).toArray().size(), 1);
        Project loaded;
        loadInto(loaded, saved);
        QCOMPARE(loaded.grooves(), f.project.grooves());
        QCOMPARE(loaded.defaultGroove(), f.project.defaultGroove());
        QCOMPARE(*loaded.clip(track, clip).groove, setting);
        // A project without them writes none.
        EditorFixture plain;
        const QJsonObject none = projectToJson(plain.project);
        QVERIFY(!none.contains(QStringLiteral("grooves")));
        QVERIFY(!none.contains(QStringLiteral("default_groove")));
    }
};

QTEST_GUILESS_MAIN(TestGrooves)
#include "test_grooves.moc"
