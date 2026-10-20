#pragma once
// Grooves in the project: its library (Project::grooves()), what plays one (a
// clip's groove, the Groove device, the project's default), and what a groove
// does to a clip's notes and audio. The maths is music/'s (music/Groove.h:
// groove::place(), which the engine's Groove device plays too); this is where
// the project's grooves meet it. Pure functions, tested on their own.
//
// A groove is played from a source: Swing (MPC swing at a percent, on a step),
// the project's default groove, a groove of the library (by id: edit it, and
// everything playing it follows), or a groove of its own (what a library groove
// leaves its users when it is removed while they play it, so nothing changes
// sound). A clip's groove is a source and the amounts it is played at.
//
// A MIDI clip's groove moves its notes where the engine bridge flattens them
// (groovedNotes(): the clip's own notes stay as they are); an audio clip's moves
// its audio through its warp markers (groovedMarkers(): the transients its audio
// has go where the groove puts them, the audio between them stretched; its own
// markers stay as they are). Apply Groove writes either into the clip
// (applied()).

#include "model/Clip.h"
#include "model/GrooveTypes.h"
#include "model/Timebase.h"

#include "music/Groove.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace sub::app {

namespace grooves {

// The sources' names, as saved and as the UI and agents give them: "swing", "project", "library", "own".
QString kindName(GrooveSource::Kind kind);
std::optional<GrooveSource::Kind> kindFromName(const QString& name);

// A swing source, and a library groove's.
GrooveSource swingSource(double percent, double stepBeats = 0.25);
GrooveSource librarySource(const QString& libraryId);

// The groove a source plays, given the project's library and default groove (none: it plays none,
// straight: Project without a default, or a library groove that isn't there). groove::normalized().
std::optional<groove::Groove> resolve(const GrooveSource& source, const std::vector<LibraryGroove>& library,
                                      const std::optional<GrooveSource>& projectDefault);
// How it is shown: "Swing 66.7 % (1/16)", "Project", the library groove's name, or "(name)" for its own.
QString describe(const GrooveSource& source, const std::vector<LibraryGroove>& library);

// The library groove a source plays turned into its own copy (a library groove removed while it is
// used): `id`'s users keep playing it. Other sources stay as they are.
GrooveSource detached(const GrooveSource& source, const QString& id, const groove::Groove& groove);

// --- MIDI clips ------------------------------------------------------------------------------------

// The notes a MIDI clip is heard playing (Clip::heardNotes()), grooved: each placed by
// groove::place() on the timeline (where the song's tempo puts it), its length and velocity as the
// groove has them, and a note running into the next on its key ending where that one starts.
std::vector<PlayedNote> groovedNotes(const Clip& clip, const groove::Groove& groove, const groove::Amounts& amounts,
                                     const Tempo& tempo);

// --- Audio clips -------------------------------------------------------------------------------------

// A warped audio clip's warp markers as its groove moves its audio: a point at each of its own
// markers, its window's ends and each transient (`transientSeconds`, from the file's start) in its
// window, each where groove::place() puts its beat (Quantize first pulling it to the base grid). The
// clip's start stays where it is: what it plays is grooved from there. A point that would make a
// stretch too fast or slow (its tempo outside kMinSegmentBpm..kMaxSegmentBpm) is left out. Empty
// for a clip that isn't warped.
std::vector<WarpMarker> groovedMarkers(const Clip& clip, const std::vector<double>& transientSeconds,
                                       const groove::Groove& groove, const groove::Amounts& amounts,
                                       const Tempo& tempo);
// Whether a clip can take a groove: an audio clip warped, or not yet (a groove warps it), but not
// one whose tempo is a frozen track's map; or a MIDI clip.
bool takesGroove(const Clip& clip);
// An audio clip warped so a groove can move it (as warping it by hand does: at `tempo`, the
// project's at its start, if it had no segment BPM yet); others as they are.
Clip warpedForGroove(const Clip& clip, double tempo);

// --- Apply Groove ----------------------------------------------------------------------------------

// The clip as its groove plays it, written into it, and its groove cleared: a MIDI clip's notes
// moved (in its content: groovedNotes()), an audio clip's warp markers (groovedMarkers()). Without a
// groove (or none resolved: `groove` none), only cleared.
Clip applied(const Clip& clip, const std::optional<groove::Groove>& groove, const std::vector<double>& transientSeconds,
             const Tempo& tempo);

// --- Making grooves ----------------------------------------------------------------------------------

// Extract Groove from a MIDI clip: its heard notes' offsets from the grid (in timeline beats, on the
// song's cycle, as a groove plays), their velocities from the clip's average; a lane a key with
// `perKeyLanes`.
groove::Groove fromMidiClip(const Clip& clip, double stepBeats, int bars, bool perKeyLanes,
                            std::vector<groove::NoteTime>* notes = nullptr);
// Extract Groove from an audio clip: its transients in its window (seconds from the file's start,
// and how strong each is), where they play on the timeline (warped: through its warp; unwarped: at
// `tempo` from its start).
groove::Groove fromAudioClip(const Clip& clip, const std::vector<std::pair<double, float>>& transients, double tempo,
                             double stepBeats, int bars, std::vector<groove::NoteTime>* notes = nullptr);
// What a groove was made from (into `notes`, if any): the notes, folded into its cycle's beats.
// (fromMidiClip and fromAudioClip hand theirs over so.)
std::vector<groove::NoteTime> folded(std::vector<groove::NoteTime> notes, const groove::Groove& groove);

// --- The Groove device ------------------------------------------------------------------------------

// The Groove device's kind (the engine's builtin:groove).
inline const QString kDeviceKind = QStringLiteral("groove");
// Its source, as its state keeps it (deviceState's values: "source" swing, project or groove;
// "groove_id" the library groove it plays; "groove" that groove's text, groove::toText(), which the
// device plays; "seed" Random's). Swing's percent and step are its parameters, not its state.
GrooveSource deviceSource(const std::optional<QString>& state);
// Its state playing `source` (a Swing source: its Swing; Library: the library's groove, copied in;
// Own: its own), keeping the rest of `state`; its seed, if it has none yet, from `seedFrom`.
std::optional<QString> deviceState(const std::optional<QString>& state, const GrooveSource& source,
                                   const std::vector<LibraryGroove>& library, const QString& seedFrom = {});
// Its seed (0: none yet).
uint32_t deviceSeed(const std::optional<QString>& state);

// The built-in grooves: MPC swing at 50 to 75 % on 16ths and 8ths, triplet shuffles, and a few
// played drum grooves. Their ids start "builtin:".
const std::vector<LibraryGroove>& builtins();
const LibraryGroove* builtin(const QString& id);

}  // namespace grooves

}  // namespace sub::app
