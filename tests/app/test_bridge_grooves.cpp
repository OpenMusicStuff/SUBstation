// Grooves through the engine bridge: a MIDI clip's groove moving the notes the
// engine plays (and back as it goes), the library's grooves and the project's
// default reaching what plays them, an audio loop's transients heard on the
// grooved beats (its ClipDesc's warp markers) and back on its own when the
// groove is removed, and the Groove device playing the project's default.

#include "BridgeTestSupport.h"
#include "TestSupport.h"

#include "audio/EngineDescs.h"
#include "model/Commands.h"
#include "model/Grooves.h"

#include <QCoreApplication>
#include <QTest>

#include <cmath>

using namespace sub::app;
using test::Studio;
namespace groove = sub::groove;

namespace {

constexpr int64_t kBeat = test::kSampleRate / 2;  // at 120 BPM
constexpr int64_t kSixteenth = kBeat / 4;         // 6000

// Where sound starts in the left channel of interleaved stereo, from `from` (-1: none).
int64_t onset(const std::vector<float>& stereo, int64_t from = 0, float above = 1e-3f) {
    for (size_t i = static_cast<size_t>(from) * 2; i < stereo.size(); i += 2) {
        if (std::abs(stereo[i]) > above) return static_cast<int64_t>(i / 2);
    }
    return -1;
}

// Whether sound starts at `at` (within the synth's first few samples).
bool startsAt(const std::vector<float>& stereo, int64_t at) {
    const int64_t found = onset(stereo);
    return found >= at && found <= at + 64;
}

ClipGroove swingOf(double percent) {
    ClipGroove g;
    g.source = grooves::swingSource(percent, 0.25);
    return g;
}

void setClipGroove(Studio& studio, const QString& trackId, const std::optional<ClipGroove>& groove) {
    std::vector<Clip> clips = studio.project.track(trackId).clips;
    for (Clip& clip : clips) clip.groove = groove;
    studio.edit.setClips(trackId, clips);
}

// A loop at 120 BPM: a click on each 16th, a second long.
std::vector<float> clicks() {
    std::vector<float> samples(test::kSampleRate, 0.f);
    for (int64_t at = 0; at < test::kSampleRate; at += kSixteenth) {
        for (int i = 0; i < 48; ++i) samples[static_cast<size_t>(at + i)] = 0.8f * std::exp(-i / 8.f) * (i % 2 ? -1.f : 1.f);
    }
    return samples;
}

// The loudest sample's place within `around` of `at` (stereo, left).
int64_t loudestNear(const std::vector<float>& stereo, int64_t at, int64_t around) {
    int64_t best = -1;
    float most = 0.f;
    for (int64_t i = std::max<int64_t>(0, at - around); i < at + around && 2 * i < static_cast<int64_t>(stereo.size()); ++i) {
        const float x = std::abs(stereo[static_cast<size_t>(2 * i)]);
        if (x > most) {
            most = x;
            best = i;
        }
    }
    return best;
}

}  // namespace

class TestBridgeGrooves : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void aMidiClipsGrooveMovesItsNotes() {
        Studio studio;
        const QString keys = studio.edit.addMidiTrack();
        // One offbeat 16th.
        studio.edit.setClips(keys, {Clip::midi(QStringLiteral("m"), {}, 0.0, 1.0, 0.0, {Note{60, 0.25, 0.1, 100}})});
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth));
        setClipGroove(studio, keys, swingOf(75.0));  // half a 16th late
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth + kSixteenth / 2));
        studio.stack.undo();
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth));
    }

    void aLibraryGrooveEditedIsHeardWhereItPlays() {
        Studio studio;
        const QString keys = studio.edit.addMidiTrack();
        studio.edit.setClips(keys, {Clip::midi(QStringLiteral("m"), {}, 0.0, 1.0, 0.0, {Note{60, 0.25, 0.1, 100}})});
        const LibraryGroove library{QStringLiteral("g"), QStringLiteral("G"), groove::swing(75.0, 0.25)};
        studio.stack.push(new SetGroovesCommand(&studio.project, {}, {{library}},
                                                QStringLiteral("Add Groove")));
        ClipGroove fromLibrary;
        fromLibrary.source = grooves::librarySource(QStringLiteral("g"));
        setClipGroove(studio, keys, fromLibrary);
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth + kSixteenth / 2));
        // The library's groove changed: the clip plays it.
        LibraryGroove triplet = library;
        triplet.groove = groove::swing(groove::kTripletSwing, 0.25);
        studio.stack.push(new SetGroovesCommand(&studio.project, {{library}},
                                                {{triplet}}, QStringLiteral("Edit Groove")));
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth + kSixteenth / 3));
        // From the project's default.
        ClipGroove fromProject;
        fromProject.source.kind = GrooveSource::Kind::Project;
        setClipGroove(studio, keys, fromProject);
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth));  // (none yet: straight)
        studio.stack.push(new SetDefaultGrooveCommand(&studio.project, std::nullopt, grooves::swingSource(75.0)));
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth + kSixteenth / 2));
    }

    void theGrooveDevicePlaysTheProjectsGroove() {
        Studio studio;
        const QString keys = studio.edit.addMidiTrack();
        studio.edit.setClips(keys, {Clip::midi(QStringLiteral("m"), {}, 0.0, 1.0, 0.0, {Note{60, 0.25, 0.1, 100}})});
        Device device = test::makeDevice(QStringLiteral("gd"), grooves::kDeviceKind);
        device.state = grooves::deviceState(std::nullopt, GrooveSource{GrooveSource::Kind::Project}, {});
        std::vector<Device> devices = studio.project.track(keys).devices;
        devices.push_back(device);
        studio.stack.push(new SetDevicesCommand(&studio.project, keys, studio.project.track(keys).devices, devices,
                                                QStringLiteral("Add Groove")));
        studio.bridge->waitForDeviceStates();  // (its source, set on the bridge's state pool)
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth));  // (no default: straight)
        studio.stack.push(new SetDefaultGrooveCommand(&studio.project, std::nullopt, grooves::swingSource(75.0)));
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth + kSixteenth / 2));
        studio.stack.undo();
        QVERIFY(startsAt(studio.render(kBeat), kSixteenth));
    }

    void anAudioLoopsTransientsGoOnTheGroovedBeats() {
        test::TempDir dir;
        Studio studio;
        const QString wav = test::writeWav(dir.path("clicks.wav"), clicks());
        const QString loop = studio.clipTrack(wav, 1.0, QStringLiteral("Loop"));
        BridgeTestAccess::finishDecoding(*studio.bridge);
        QTRY_VERIFY(!studio.bridge->isLoading(wav));
        QVERIFY(!studio.bridge->transients(wav).empty());
        std::vector<Clip> clips = studio.project.track(loop).clips;
        clips.front().warp = true;
        clips.front().segmentBpm = 120.0;
        clips.front().warpMode = QStringLiteral("Re-Pitch");  // (resampled: each click lands where it goes)
        studio.edit.setClips(loop, clips);
        const std::vector<float> straight = studio.render(kBeat * 2);
        QVERIFY(std::abs(loudestNear(straight, kSixteenth, 1000) - kSixteenth) < 20);
        // Swung 75 %: the offbeat clicks half a 16th late, the downbeats where they were.
        setClipGroove(studio, loop, swingOf(75.0));
        const std::vector<float> swung = studio.render(kBeat * 2);
        QVERIFY(std::abs(loudestNear(swung, kSixteenth + kSixteenth / 2, 1500) - (kSixteenth + kSixteenth / 2)) < 40);
        QVERIFY(std::abs(loudestNear(swung, 2 * kSixteenth, 1000) - 2 * kSixteenth) < 40);
        QVERIFY(std::abs(loudestNear(swung, 3 * kSixteenth + kSixteenth / 2, 1500) - (3 * kSixteenth + kSixteenth / 2)) < 40);
        // The description the engine gets: the clip's own markers stay none, its grooved ones there.
        const GrooveContext context{&studio.project.grooves(), std::nullopt, studio.project.tempoMap(),
                                    [&](const QString& path) {
                                        std::vector<double> seconds;
                                        for (const auto& [sec, strength] : studio.bridge->transients(path)) {
                                            seconds.push_back(sec);
                                        }
                                        return seconds;
                                    }};
        QVERIFY(studio.project.track(loop).clips.front().warpMarkers.empty());
        QVERIFY(groovedClip(studio.project.track(loop).clips.front(), context).warpMarkers.size() > 2);
        // Removed: back on its own beats.
        setClipGroove(studio, loop, std::nullopt);
        const std::vector<float> back = studio.render(kBeat * 2);
        QVERIFY(std::abs(loudestNear(back, kSixteenth, 1000) - kSixteenth) < 20);
    }
};

QTEST_GUILESS_MAIN(TestBridgeGrooves)
#include "test_bridge_grooves.moc"
