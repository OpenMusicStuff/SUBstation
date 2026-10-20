#include "model/Grooves.h"

#include "model/DeviceState.h"
#include "model/Edits.h"
#include "model/Notes.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <functional>

namespace sub::app::grooves {

namespace {

// A step's name, as the device and the clip view show it (or its beats).
QString stepName(double stepBeats) {
    for (const groove::StepChoice& step : groove::kSteps) {
        if (std::abs(step.beats - stepBeats) < 1e-9) return QString::fromLatin1(step.name);
    }
    return QString::number(stepBeats, 'g', 4);
}

}  // namespace

QString kindName(GrooveSource::Kind kind) {
    switch (kind) {
        case GrooveSource::Kind::Project: return QStringLiteral("project");
        case GrooveSource::Kind::Library: return QStringLiteral("library");
        case GrooveSource::Kind::Own: return QStringLiteral("own");
        case GrooveSource::Kind::Swing: break;
    }
    return QStringLiteral("swing");
}

std::optional<GrooveSource::Kind> kindFromName(const QString& name) {
    if (name == QLatin1String("swing")) return GrooveSource::Kind::Swing;
    if (name == QLatin1String("project")) return GrooveSource::Kind::Project;
    if (name == QLatin1String("library")) return GrooveSource::Kind::Library;
    if (name == QLatin1String("own")) return GrooveSource::Kind::Own;
    return std::nullopt;
}

GrooveSource swingSource(double percent, double stepBeats) {
    GrooveSource source;
    source.kind = GrooveSource::Kind::Swing;
    source.swing = std::clamp(percent, groove::kMinSwing, groove::kMaxSwing);
    source.stepBeats = groove::kSteps[static_cast<size_t>(groove::stepIndex(stepBeats))].beats;
    return source;
}

GrooveSource librarySource(const QString& libraryId) {
    GrooveSource source;
    source.kind = GrooveSource::Kind::Library;
    source.libraryId = libraryId;
    return source;
}

std::optional<groove::Groove> resolve(const GrooveSource& source, const std::vector<LibraryGroove>& library,
                                      const std::optional<GrooveSource>& projectDefault) {
    switch (source.kind) {
        case GrooveSource::Kind::Swing: return groove::swing(source.swing, source.stepBeats);
        case GrooveSource::Kind::Project:
            // (The default is never Project itself; were it, it would play none.)
            if (!projectDefault || projectDefault->kind == GrooveSource::Kind::Project) return std::nullopt;
            return resolve(*projectDefault, library, std::nullopt);
        case GrooveSource::Kind::Library:
            for (const LibraryGroove& entry : library) {
                if (entry.id == source.libraryId) return groove::normalized(entry.groove);
            }
            return std::nullopt;
        case GrooveSource::Kind::Own:
            if (source.own) return groove::normalized(*source.own);
            return std::nullopt;
    }
    return std::nullopt;
}

QString describe(const GrooveSource& source, const std::vector<LibraryGroove>& library) {
    switch (source.kind) {
        case GrooveSource::Kind::Swing:
            return QStringLiteral("Swing %1 % (%2)").arg(source.swing, 0, 'f', 1).arg(stepName(source.stepBeats));
        case GrooveSource::Kind::Project: return QStringLiteral("Project");
        case GrooveSource::Kind::Library:
            for (const LibraryGroove& entry : library) {
                if (entry.id == source.libraryId) return entry.name;
            }
            return QStringLiteral("(missing groove)");
        case GrooveSource::Kind::Own: return QStringLiteral("(own groove)");
    }
    return {};
}

GrooveSource detached(const GrooveSource& source, const QString& id, const groove::Groove& groove) {
    if (source.kind != GrooveSource::Kind::Library || source.libraryId != id) return source;
    GrooveSource own = source;
    own.kind = GrooveSource::Kind::Own;
    own.libraryId.clear();
    own.own = groove;
    return own;
}

// --- MIDI clips ------------------------------------------------------------------------------------------------

std::vector<PlayedNote> groovedNotes(const Clip& clip, const groove::Groove& groove, const groove::Amounts& amounts,
                                     const Tempo& tempo) {
    std::vector<PlayedNote> notes = clip.heardNotes();
    const TempoMap& map = tempo.map();
    for (PlayedNote& played : notes) {
        const groove::Placed placed =
            groove::place(groove, played.start, played.note.pitch, played.note.velocity, played.end - played.start, map,
                          amounts);
        const double moved = placed.beat - played.start;
        played.start = placed.beat;
        played.end = placed.beat + std::max(notes::kMinNoteBeats, placed.lengthBeats);
        played.note.start += moved;
        played.note.length = played.end - played.start;
        played.note.velocity = static_cast<int>(std::clamp(std::lround(placed.velocity), 1L, 127L));
    }
    // A note running into the next on its key ends where that one starts (place() keeps their order).
    std::stable_sort(notes.begin(), notes.end(), [](const PlayedNote& a, const PlayedNote& b) {
        return a.note.pitch != b.note.pitch ? a.note.pitch < b.note.pitch : a.start < b.start;
    });
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
        PlayedNote& note = notes[i];
        const PlayedNote& next = notes[i + 1];
        if (next.note.pitch != note.note.pitch || next.start >= note.end) continue;
        note.end = std::max(note.start + notes::kEps, next.start);
        note.note.length = note.end - note.start;
    }
    std::stable_sort(notes.begin(), notes.end(), [](const PlayedNote& a, const PlayedNote& b) {
        return a.start != b.start ? a.start < b.start : a.note.pitch < b.note.pitch;
    });
    return notes;
}

// --- Audio clips -----------------------------------------------------------------------------------------------

std::vector<WarpMarker> groovedMarkers(const Clip& clip, const std::vector<double>& transientSeconds,
                                       const groove::Groove& groove, const groove::Amounts& amounts,
                                       const Tempo& tempo) {
    if (!clip.isWarped()) return {};
    const double from = clip.offsetSec, to = clip.offsetSec + clip.durationSec;
    std::vector<double> seconds{from, to};
    for (const WarpMarker& marker : clip.warpMarkers) seconds.push_back(marker.sec);
    for (const double sec : transientSeconds) {
        if (sec > from && sec < to) seconds.push_back(sec);
    }
    std::sort(seconds.begin(), seconds.end());
    const TempoMap& map = tempo.map();
    const double origin = clip.warpBeatAt(from);
    // Where the groove puts a timeline beat (and the clip's start, which stays).
    const auto placed = [&](double beat) { return groove::place(groove, beat, 60, 100.0, 0.0, map, amounts).beat; };
    const double start = placed(clip.startBeat);
    std::vector<WarpMarker> markers;
    for (const double sec : seconds) {
        const double beat = clip.startBeat + clip.warpBeatAt(sec) - origin;
        const WarpMarker marker{sec, origin + placed(beat) - start};
        if (!markers.empty()) {
            const WarpMarker& last = markers.back();
            const double gap = marker.sec - last.sec;
            if (gap < edits::kMinWarpMarkerGapSec || marker.beat <= last.beat + 1e-9) continue;
            const double bpm = 60.0 * (marker.beat - last.beat) / gap;
            if (bpm < kMinSegmentBpm || bpm > kMaxSegmentBpm) continue;
        }
        markers.push_back(marker);
    }
    return markers;
}

bool takesGroove(const Clip& clip) { return clip.isMidi() || !clip.warpMarkers.empty() || !clip.sourceMap; }

Clip warpedForGroove(const Clip& clip, double tempo) {
    if (!clip.isAudio() || clip.isWarped()) return clip;
    Clip warped = clip;
    warped.warp = true;
    if (!(warped.segmentBpm > 0)) warped.segmentBpm = std::clamp(tempo, kMinSegmentBpm, kMaxSegmentBpm);
    return warped;
}

// --- Apply Groove ------------------------------------------------------------------------------------------------

Clip applied(const Clip& clip, const std::optional<groove::Groove>& groove, const std::vector<double>& transientSeconds,
             const Tempo& tempo) {
    Clip changed = clip;
    const std::optional<ClipGroove> setting = clip.groove;
    changed.groove.reset();
    if (!groove || !setting) return changed;
    if (clip.isMidi()) {
        // Every note, as the clip's timeline has it.
        std::vector<Note> onTimeline = clip.notes;
        for (Note& note : onTimeline) note.start = clip.toTimeline(note.start);
        std::vector<Note> moved = notes::grooved(onTimeline, *groove, setting->amounts, tempo);
        for (Note& note : moved) note.start = std::max(0.0, note.start - clip.toTimeline(0.0));
        changed.notes = notes::normalize(moved);
        return changed;
    }
    const std::vector<WarpMarker> markers = groovedMarkers(clip, transientSeconds, *groove, setting->amounts, tempo);
    if (markers.size() >= 2) changed.setWarpMarkers(markers);
    return changed;
}

// --- Making grooves --------------------------------------------------------------------------------------------

std::vector<groove::NoteTime> folded(std::vector<groove::NoteTime> notes, const groove::Groove& groove) {
    const double cycle = groove::cycleBeats(groove);
    for (groove::NoteTime& note : notes) note.beat -= std::floor(note.beat / cycle) * cycle;
    std::sort(notes.begin(), notes.end(), [](const groove::NoteTime& a, const groove::NoteTime& b) {
        return a.beat != b.beat ? a.beat < b.beat : a.key < b.key;
    });
    return notes;
}

groove::Groove fromMidiClip(const Clip& clip, double stepBeats, int bars, bool perKeyLanes,
                            std::vector<groove::NoteTime>* notes) {
    std::vector<groove::NoteTime> heard;
    for (const PlayedNote& played : clip.heardNotes()) {
        heard.push_back({played.start, played.note.pitch, static_cast<double>(played.note.velocity),
                         played.end - played.start});
    }
    groove::Groove made = groove::fromNotes(heard, stepBeats, bars, perKeyLanes);
    if (notes) *notes = folded(std::move(heard), made);
    return made;
}

groove::Groove fromAudioClip(const Clip& clip, const std::vector<std::pair<double, float>>& transients, double tempo,
                             double stepBeats, int bars, std::vector<groove::NoteTime>* notes) {
    std::vector<double> beats;
    std::vector<float> levels;
    const double from = clip.offsetSec, to = clip.offsetSec + clip.durationSec;
    const double origin = clip.isWarped() ? clip.warpBeatAt(from) : 0.0;
    for (const auto& [sec, strength] : transients) {
        if (sec < from || sec >= to) continue;
        const double own = clip.isWarped() ? clip.warpBeatAt(sec) - origin : (sec - from) * tempo / 60.0;
        beats.push_back(clip.startBeat + own);
        levels.push_back(strength);
    }
    groove::Groove made = groove::fromOnsets(beats, levels, stepBeats, bars);
    if (notes) {
        std::vector<groove::NoteTime> hits;
        for (size_t i = 0; i < beats.size(); ++i) hits.push_back({beats[i], 60, 1.0 + 126.0 * levels[i], 0.0});
        *notes = folded(std::move(hits), made);
    }
    return made;
}

// --- The Groove device ----------------------------------------------------------------------------------

GrooveSource deviceSource(const std::optional<QString>& state) {
    const deviceState::Values values = deviceState::fromModel(state);
    GrooveSource source;
    const QString kind = values.value(QStringLiteral("source"));
    if (kind == QLatin1String("project")) {
        source.kind = GrooveSource::Kind::Project;
    } else if (kind == QLatin1String("groove")) {
        const QString id = values.value(QStringLiteral("groove_id"));
        const std::optional<groove::Groove> own = groove::fromText(values.value(QStringLiteral("groove")).toStdString());
        if (!id.isEmpty()) {
            source = librarySource(id);
        } else {
            source.kind = GrooveSource::Kind::Own;
        }
        source.own = own;  // (a library groove's copy too: what it plays)
        if (source.kind == GrooveSource::Kind::Library) source.own.reset();
        if (source.kind == GrooveSource::Kind::Own && !source.own) source.kind = GrooveSource::Kind::Swing;
    }
    return source;
}

std::optional<QString> deviceState(const std::optional<QString>& state, const GrooveSource& source,
                                   const std::vector<LibraryGroove>& library, const QString& seedFrom) {
    deviceState::Values values = deviceState::fromModel(state);
    values.remove(QStringLiteral("groove_id"));
    values.remove(QStringLiteral("groove"));
    switch (source.kind) {
        case GrooveSource::Kind::Swing: values.insert(QStringLiteral("source"), QStringLiteral("swing")); break;
        case GrooveSource::Kind::Project: values.insert(QStringLiteral("source"), QStringLiteral("project")); break;
        case GrooveSource::Kind::Library:
        case GrooveSource::Kind::Own: {
            values.insert(QStringLiteral("source"), QStringLiteral("groove"));
            if (source.kind == GrooveSource::Kind::Library) values.insert(QStringLiteral("groove_id"), source.libraryId);
            if (const std::optional<groove::Groove> played = resolve(source, library, std::nullopt)) {
                values.insert(QStringLiteral("groove"), QString::fromStdString(groove::toText(*played)));
            }
            break;
        }
    }
    if (values.value(QStringLiteral("seed")).isEmpty() && !seedFrom.isEmpty()) {
        values.insert(QStringLiteral("seed"), QString::number(qHash(seedFrom) & 0x7fffffffU));
    }
    return deviceState::toModel(values);
}

uint32_t deviceSeed(const std::optional<QString>& state) {
    return deviceState::fromModel(state).value(QStringLiteral("seed")).toUInt();
}

namespace {

// A groove of one bar of `stepBeats` steps, its default lane's steps from `step` (by its index).
groove::Groove made(double stepBeats, const std::function<groove::Step(int)>& step) {
    groove::Groove g;
    g.stepBeats = stepBeats;
    g.base = stepBeats;
    g = groove::normalized(std::move(g));
    std::vector<groove::Step>& steps = g.lanes.front().steps;
    for (size_t i = 0; i < steps.size(); ++i) steps[i] = step(static_cast<int>(i));
    return groove::normalized(std::move(g));
}

// A lane for `keys` whose steps come from `step`.
void addLane(groove::Groove& g, std::vector<int> keys, const std::function<groove::Step(int)>& step) {
    groove::Lane lane{std::move(keys), std::vector<groove::Step>(static_cast<size_t>(groove::stepsPerCycle(g)))};
    for (size_t i = 0; i < lane.steps.size(); ++i) lane.steps[i] = step(static_cast<int>(i));
    g.lanes.push_back(std::move(lane));
    g = groove::normalized(std::move(g));
}

// MPC swing at `percent`: an offbeat step's offset.
double swung(double percent) { return 2.0 * percent / 100.0 - 1.0; }

const std::vector<int> kKicks{35, 36};
const std::vector<int> kSnares{37, 38, 39, 40};
const std::vector<int> kHats{42, 44, 46};

std::vector<LibraryGroove> makeBuiltins() {
    std::vector<LibraryGroove> all;
    const auto add = [&all](const QString& id, const QString& name, groove::Groove groove) {
        all.push_back({QStringLiteral("builtin:") + id, name, std::move(groove)});
    };
    for (const auto& [beats, label] : {std::pair{0.25, QStringLiteral("16")}, std::pair{0.5, QStringLiteral("8")}}) {
        for (const int percent : {50, 54, 58, 62, 66, 71, 75}) {
            add(QStringLiteral("mpc-%1-%2").arg(label).arg(percent), QStringLiteral("MPC %1 Swing %2").arg(label).arg(percent),
                groove::swing(percent, beats));
        }
    }
    // Triplet shuffles: the offbeat on the triplet, and a lazier one past it.
    add(QStringLiteral("shuffle-8t"), QStringLiteral("Triplet Shuffle 8"), groove::swing(groove::kTripletSwing, 0.5));
    add(QStringLiteral("shuffle-16t"), QStringLiteral("Triplet Shuffle 16"), groove::swing(groove::kTripletSwing, 0.25));
    add(QStringLiteral("shuffle-lazy-8"), QStringLiteral("Lazy Shuffle 8"), groove::swing(70.0, 0.5));
    // Played drum grooves.
    {  // A backbeat laid back: the snare 12 ms late, the hats a little swung, accents on the beat.
        groove::Groove g = made(0.25, [](int i) {
            groove::Step s;
            s.timing = i % 2 == 1 ? swung(54.0) : 0.0;
            s.velocity = i % 4 == 0 ? 0.08 : i % 2 == 1 ? -0.12 : -0.04;
            return s;
        });
        addLane(g, kSnares, [](int i) {
            groove::Step s;
            s.timingMs = 12.0;
            s.velocity = i % 8 == 4 ? 0.1 : -0.2;  // (ghost notes softer)
            return s;
        });
        add(QStringLiteral("laid-back"), QStringLiteral("Laid Back Backbeat"), std::move(g));
    }
    {  // Boom bap: hats at 58 %, the snare late, the kick on time and loud.
        groove::Groove g = made(0.25, [](int i) {
            groove::Step s;
            s.timing = i % 2 == 1 ? swung(58.0) : 0.0;
            s.velocity = i % 2 == 1 ? -0.15 : 0.0;
            return s;
        });
        addLane(g, kSnares, [](int) {
            groove::Step s;
            s.timingMs = 9.0;
            return s;
        });
        addLane(g, kKicks, [](int i) {
            groove::Step s;
            s.velocity = i % 2 == 0 ? 0.1 : -0.1;
            return s;
        });
        add(QStringLiteral("boom-bap"), QStringLiteral("Boom Bap 16"), std::move(g));
    }
    {  // Funk: straight-ish 16ths, the e and a of each beat ghosted.
        add(QStringLiteral("funk-ghosts"), QStringLiteral("Funk 16 Ghosts"), made(0.25, [](int i) {
                groove::Step s;
                s.timing = i % 2 == 1 ? swung(52.0) : 0.0;
                s.velocity = i % 2 == 1 ? -0.3 : i % 4 == 0 ? 0.1 : 0.0;
                s.length = i % 2 == 1 ? 0.6 : 1.0;
                return s;
            }));
    }
    {  // Drunk: hats swung 62 % and each a little off, the kick pushed on the "and" of 3, the snare dragging.
        static constexpr double kNudge[16] = {0.0, 0.03, -0.02, 0.05, 0.01, -0.03, 0.02, 0.06,
                                              -0.01, 0.02, -0.04, 0.04, 0.0, -0.02, 0.03, 0.07};
        groove::Groove g = made(0.25, [](int i) {
            groove::Step s;
            s.timing = (i % 2 == 1 ? swung(62.0) : 0.0) + kNudge[i];
            s.velocity = i % 2 == 1 ? -0.1 : 0.0;
            return s;
        });
        addLane(g, kKicks, [](int i) {
            groove::Step s;
            s.timingMs = i == 10 ? -8.0 : 0.0;
            return s;
        });
        addLane(g, kSnares, [](int) {
            groove::Step s;
            s.timingMs = 15.0;
            return s;
        });
        add(QStringLiteral("drunk"), QStringLiteral("Drunk 16"), std::move(g));
    }
    {  // Pushed: the hats' offbeats a little early, for urgency.
        groove::Groove g = made(0.25, [](int i) {
            groove::Step s;
            s.velocity = i % 2 == 1 ? -0.08 : 0.0;
            return s;
        });
        addLane(g, kHats, [](int i) {
            groove::Step s;
            s.timingMs = i % 2 == 1 ? -6.0 : 0.0;
            s.velocity = i % 2 == 1 ? -0.12 : 0.04;
            return s;
        });
        add(QStringLiteral("pushed-hats"), QStringLiteral("Pushed Hats 16"), std::move(g));
    }
    return all;
}

}  // namespace

const std::vector<LibraryGroove>& builtins() {
    static const std::vector<LibraryGroove> all = makeBuiltins();
    return all;
}

const LibraryGroove* builtin(const QString& id) {
    for (const LibraryGroove& entry : builtins()) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

}  // namespace sub::app::grooves
