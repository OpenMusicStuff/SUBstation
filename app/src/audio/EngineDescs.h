#pragma once
// The engine's view of the model's clips and notes (model -> engine
// descriptions). An application-layer header that includes the engine's: for
// the bridge and its tests, never the UI.

#include "model/Clip.h"
#include "model/GrooveTypes.h"
#include "model/Timebase.h"
#include "model/Track.h"

#include "Engine.h"

#include <functional>
#include <optional>
#include <vector>

namespace sub::app {

// What the clips' grooves (Clip::groove) play by: the project's library and default groove, its
// tempo, and the files' transients (an audio clip's; empty: not decoded yet, played as it is).
struct GrooveContext {
    const std::vector<LibraryGroove>* library = nullptr;
    std::optional<GrooveSource> defaultGroove;
    Tempo tempo;
    std::function<std::vector<double>(const QString& path)> transients;
};

// The engine's view of a clip. Positions stay in beats and seconds; the engine
// converts them to samples at the current tempo and sample rate.
sub::ClipDesc clipDesc(const Clip& clip);
// The engine's view of the audio clips that play: all but the deactivated ones; with `grooves`, a
// clip with a groove played through its grooved warp markers (grooves::groovedMarkers()).
std::vector<sub::ClipDesc> clipDescs(const std::vector<Clip>& clips, const GrooveContext* grooves = nullptr);
// The notes a MIDI track plays, from all of its clips (but deactivated ones), in timeline beats.
std::vector<sub::NoteDesc> noteDescs(const Track& track, const GrooveContext* grooves = nullptr);
// A note a clip plays, bend and all (its times the note's own: beats from its start).
sub::NoteDesc noteDesc(const PlayedNote& played);
// The notes these MIDI clips play (a deactivated one none), in timeline beats; with `grooves`, a
// clip with a groove its notes grooved (grooves::groovedNotes()).
std::vector<sub::NoteDesc> clipNoteDescs(const std::vector<Clip>& clips, const GrooveContext* grooves = nullptr);
// A clip as its groove plays it: an audio clip's warp markers grooved (none: as it is, its file not
// decoded yet or its groove resolving to none). A MIDI clip as it is (its notes are grooved apart).
Clip groovedClip(const Clip& clip, const GrooveContext& grooves);

}  // namespace sub::app
