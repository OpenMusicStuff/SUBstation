// What the engine's edit side costs as a project grows: opening it, one edit,
// dragging every clip, and the files it plays arriving from the decoders.
//
//     snapshot_bench [--tracks 32,128,256] [--notes 256] [--audio-every 4] [--repeats 20] [--json out.json]
//
// A synthetic project of that many tracks: every `--audio-every`th an audio
// track playing a file of its own (a 10-second WAV written for it), the others MIDI
// tracks with a clip of `--notes` notes through the built-in Synth and an EQ; in
// groups of eight; every track sending to two returns; each with volume, pan and
// device on/off envelopes. It is saved (projectToJson) and opened into a project
// with an engine bridge, as File › Open does, and measured:
//
//   open         loading it into the project, until the bridge has given the
//                engine everything (its plug-ins would load after: none here)
//   decoded      then until every file has been decoded and arrived
//   one edit     a change that rebuilds the snapshot once (the loop switched)
//   drag         a drag's preview of every clip a beat later, and its end
//   backup       a backup: the project turned into JSON on the GUI thread (json),
//                then written (write), which Backups does on a thread of its own
//   close        the bridge letting go of every track (closing the project, quitting)
//
// For each, the wall time and how many snapshots the engine built
// (Engine::snapshotBuilds(); "-" when the engine doesn't count them, before it
// could); for the backup, the time alone. The application layer without a UI; QT_QPA_PLATFORM=offscreen is fine.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QUndoStack>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "BridgeTestSupport.h"
#include "Json.h"
#include "TestSupport.h"
#include "io/Json.h"
#include "io/Serialization.h"
#include "model/Automation.h"

namespace {

using sub::bench::Json;
namespace app = sub::app;

struct Options {
    std::vector<int> tracks{32, 128, 256};
    int notes = 256;
    int audioEvery = 4;
    int repeats = 20;
    std::string json;
};

std::vector<int> intList(const std::string& text) {
    std::vector<int> out;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t comma = text.find(',', at);
        out.push_back(std::stoi(text.substr(at, comma - at)));
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--tracks") {
            o.tracks = intList(value());
        } else if (arg == "--notes") {
            o.notes = std::stoi(value());
        } else if (arg == "--audio-every") {
            o.audioEvery = std::max(1, std::stoi(value()));
        } else if (arg == "--repeats") {
            o.repeats = std::max(1, std::stoi(value()));
        } else if (arg == "--json") {
            o.json = value();
        } else if (arg == "--help" || arg == "-h") {
            std::printf("snapshot_bench [--tracks 32,128,256] [--notes 256] [--audio-every 4] [--repeats 20] "
                        "[--json out.json]\n");
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option " + arg);
        }
    }
    return o;
}

// The engine's count of snapshots built, where it has one (-1: it doesn't).
template <class E>
int64_t builds(E& engine) {
    if constexpr (requires { engine.snapshotBuilds(); }) {
        return static_cast<int64_t>(engine.snapshotBuilds());
    } else {
        return -1;
    }
}

std::string count(int64_t n) { return n < 0 ? std::string("-") : std::to_string(n); }

// The project, saved as File › Save writes it.
QJsonObject makeProject(const Options& o, int tracks, const app::test::TempDir& dir) {
    app::Project project;
    QUndoStack stack;
    app::test::Edits edit(project, stack);
    const QString returnA = edit.addReturnTrack();
    const QString returnB = edit.addReturnTrack();
    QStringList ids;
    for (int t = 0; t < tracks; ++t) {
        QString id;
        if (t % o.audioEvery == 0) {
            id = edit.addAudioTrack();
            std::vector<float> samples(app::test::kSampleRate * 10);  // (long enough to take a while)
            for (size_t i = 0; i < samples.size(); ++i) samples[i] = 0.1f * static_cast<float>((i * (t + 1)) % 64) / 64.f;
            const QString wav = app::test::writeWav(dir.path(QStringLiteral("t%1.wav").arg(t)), samples, 1);
            std::vector<app::Clip> clips;
            for (int c = 0; c < 8; ++c) {
                clips.push_back(app::test::audioClip(id + QStringLiteral("c%1").arg(c), wav, 4.0 * c, 0.5));
            }
            edit.setClips(id, clips);
        } else {
            id = edit.addMidiTrack();
            edit.addDevice(id, QStringLiteral("eq"));
            std::vector<app::Note> notes;
            for (int n = 0; n < o.notes; ++n) {
                notes.push_back(app::Note{48 + (n * 7 + t) % 36, 0.25 * n, 0.2, 60 + n % 60});
            }
            std::sort(notes.begin(), notes.end(), [](const app::Note& a, const app::Note& b) {
                return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
            });
            edit.setClips(id, {app::Clip::midi(id + QStringLiteral("m"), QString(), 0.0, 0.25 * o.notes, 0.0, notes)});
        }
        edit.setSend(id, returnA, -12.0);
        edit.setSend(id, returnB, -18.0);
        edit.setEnvelope(id, app::automation::kMixerVolume, {{0.0, 0.8, 0.0}, {16.0, 0.6, 0.0}, {32.0, 0.8, 0.0}});
        edit.setEnvelope(id, app::automation::kMixerPan, {{0.0, 0.3, 0.0}, {32.0, 0.7, 0.0}});
        const app::Track& track = project.track(id);
        if (!track.devices.empty()) {
            edit.setEnvelope(id, app::automation::deviceOnKey(track.devices.back().id), {{0.0, 1.0, 0.0}});
        }
        ids.append(id);
    }
    for (int g = 0; g + 8 <= static_cast<int>(ids.size()); g += 8) edit.groupTracks(ids.mid(g, 8));
    return app::projectToJson(project);
}

struct Result {
    int tracks = 0;
    double openMs = 0.0, decodedMs = 0.0, editMs = 0.0, dragMs = 0.0, closeMs = 0.0;
    double backupJsonMs = 0.0, backupWriteMs = 0.0;
    int64_t openBuilds = -1, decodedBuilds = -1, editBuilds = -1, dragBuilds = -1, closeBuilds = -1;
};

Result measure(const Options& o, int tracks) {
    const app::test::TempDir dir;
    const QJsonObject saved = makeProject(o, tracks, dir);
    Result r;
    r.tracks = tracks;

    app::test::Studio studio;
    QElapsedTimer timer;
    int64_t before = builds(studio.engine);
    timer.start();
    app::loadInto(studio.project, saved);
    r.openMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    int64_t after = builds(studio.engine);
    r.openBuilds = before < 0 ? -1 : after - before;

    // The files: decoded in the bridge's pool, arriving on this thread.
    before = after;
    timer.start();
    const auto loading = [&] {
        for (const app::Track& track : studio.project.tracks()) {
            for (const app::Clip& clip : track.clips) {
                if (clip.isAudio() && studio.bridge->isLoading(clip.path)) return true;
            }
        }
        return false;
    };
    while (loading()) {
        app::BridgeTestAccess::finishDecoding(*studio.bridge);
        QCoreApplication::processEvents();
    }
    r.decodedMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    after = builds(studio.engine);
    r.decodedBuilds = before < 0 ? -1 : after - before;

    // One edit: the loop switched on and off again, `repeats` times.
    before = after;
    timer.start();
    for (int i = 0; i < o.repeats; ++i) studio.engine.setLoop(i % 2 == 0, 0.0, 16.0);
    r.editMs = static_cast<double>(timer.nsecsElapsed()) / 1e6 / o.repeats;
    after = builds(studio.engine);
    r.editBuilds = before < 0 ? -1 : (after - before) / o.repeats;

    // A drag of every clip a beat later: its preview, then its end.
    QMap<QString, std::vector<app::Clip>> moved;
    for (const app::Track& track : studio.project.tracks()) {
        if (track.clips.empty()) continue;
        std::vector<app::Clip> clips = track.clips;
        for (app::Clip& clip : clips) clip.startBeat += 1.0;
        moved.insert(track.id, std::move(clips));
    }
    before = after;
    timer.start();
    for (int i = 0; i < o.repeats; ++i) {
        studio.bridge->previewClips(moved, {});
        studio.bridge->endClipPreview();
    }
    r.dragMs = static_cast<double>(timer.nsecsElapsed()) / 1e6 / o.repeats;
    after = builds(studio.engine);
    r.dragBuilds = before < 0 ? -1 : (after - before) / o.repeats;

    // A backup: the project into JSON (the GUI thread's part), then written (the writer's).
    QJsonObject backup;
    timer.start();
    for (int i = 0; i < o.repeats; ++i) backup = app::projectToJson(studio.project, studio.project.path());
    r.backupJsonMs = static_cast<double>(timer.nsecsElapsed()) / 1e6 / o.repeats;
    timer.start();
    for (int i = 0; i < o.repeats; ++i) app::writeJsonFile(dir.path(QStringLiteral("backup.gilproj")), backup);
    r.backupWriteMs = static_cast<double>(timer.nsecsElapsed()) / 1e6 / o.repeats;

    before = after;
    timer.start();
    studio.bridge->shutdown();
    r.closeMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    after = builds(studio.engine);
    r.closeBuilds = before < 0 ? -1 : after - before;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        const Options o = parse(argc, argv);
        app::test::prepareApplication();
        std::printf("%7s %12s %8s %12s %8s %12s %8s %12s %8s %12s %12s %12s %8s\n", "tracks", "open ms", "builds",
                    "decoded ms", "builds", "edit ms", "builds", "drag ms", "builds", "json ms", "write ms", "close ms",
                    "builds");
        Json results = Json::array();
        for (const int tracks : o.tracks) {
            const Result r = measure(o, tracks);
            std::printf("%7d %12.1f %8s %12.1f %8s %12.3f %8s %12.2f %8s %12.1f %12.1f %12.1f %8s\n", r.tracks,
                        r.openMs, count(r.openBuilds).c_str(), r.decodedMs, count(r.decodedBuilds).c_str(), r.editMs,
                        count(r.editBuilds).c_str(), r.dragMs, count(r.dragBuilds).c_str(), r.backupJsonMs,
                        r.backupWriteMs, r.closeMs, count(r.closeBuilds).c_str());
            std::fflush(stdout);
            Json row = Json::object();
            row.set("tracks", r.tracks);
            row.set("open_ms", sub::bench::rounded(r.openMs, 2));
            row.set("open_builds", r.openBuilds);
            row.set("decoded_ms", sub::bench::rounded(r.decodedMs, 2));
            row.set("decoded_builds", r.decodedBuilds);
            row.set("edit_ms", sub::bench::rounded(r.editMs, 4));
            row.set("edit_builds", r.editBuilds);
            row.set("drag_ms", sub::bench::rounded(r.dragMs, 3));
            row.set("drag_builds", r.dragBuilds);
            row.set("backup_json_ms", sub::bench::rounded(r.backupJsonMs, 2));
            row.set("backup_write_ms", sub::bench::rounded(r.backupWriteMs, 2));
            row.set("close_ms", sub::bench::rounded(r.closeMs, 2));
            row.set("close_builds", r.closeBuilds);
            results.push(std::move(row));
        }
        if (!o.json.empty()) {
            Json report = Json::object();
            report.set("notes", o.notes);
            report.set("audio_every", o.audioEvery);
            report.set("results", std::move(results));
            if (!report.save(o.json, 2)) throw std::runtime_error("could not write " + o.json);
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "snapshot_bench: %s\n", error.what());
        return 1;
    }
}
