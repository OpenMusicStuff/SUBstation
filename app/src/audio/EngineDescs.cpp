#include "audio/EngineDescs.h"

#include "model/Grooves.h"
#include "model/Timebase.h"

namespace sub::app {

sub::ClipDesc clipDesc(const Clip& clip) {
    sub::ClipDesc desc;
    desc.path = clip.path.toStdString();
    desc.startBeat = clip.startBeat;
    desc.durationSec = clip.durationSec;
    desc.offsetSec = clip.offsetSec;
    desc.gain = static_cast<float>(dbToGain(clip.gainDb));
    desc.pan = static_cast<float>(clip.pan);
    desc.warp = clip.isWarped();
    desc.segmentBpm = clip.segmentBpm;
    if (clip.sourceMap && !clip.sourceMap->constant()) desc.sourceMap = clip.sourceMap->shared();
    // The engine's modes are in kWarpModes' order; an unknown one plays as Standard.
    const qsizetype mode = kWarpModes.indexOf(clip.warpMode);
    desc.warpMode = mode >= 0 ? static_cast<sub::WarpMode>(mode) : sub::WarpMode::Standard;
    desc.transpose = clip.transpose + clip.detune / 100.0;
    desc.id = clip.id.toStdString();
    desc.fadeInSec = clip.fadeInSec;
    desc.fadeOutSec = clip.fadeOutSec;
    desc.fadeInCurve = static_cast<float>(clip.fadeInCurve);
    desc.fadeOutCurve = static_cast<float>(clip.fadeOutCurve);
    return desc;
}

Clip groovedClip(const Clip& clip, const GrooveContext& grooves) {
    if (!clip.groove || !clip.isWarped() || !grooves.library) return clip;
    const std::optional<groove::Groove> plays = grooves::resolve(clip.groove->source, *grooves.library, grooves.defaultGroove);
    if (!plays) return clip;
    const std::vector<double> transients = grooves.transients ? grooves.transients(clip.path) : std::vector<double>{};
    if (transients.empty()) return clip;
    const std::vector<WarpMarker> markers =
        grooves::groovedMarkers(clip, transients, *plays, clip.groove->amounts, grooves.tempo);
    if (markers.size() < 2) return clip;
    Clip grooved = clip;
    grooved.setWarpMarkers(markers);
    return grooved;
}

std::vector<sub::ClipDesc> clipDescs(const std::vector<Clip>& clips, const GrooveContext* grooves) {
    std::vector<sub::ClipDesc> descs;
    for (const Clip& clip : clips) {
        if (!clip.plays()) continue;
        descs.push_back(clipDesc(grooves && clip.groove ? groovedClip(clip, *grooves) : clip));
    }
    return descs;
}

std::vector<sub::NoteDesc> noteDescs(const Track& track, const GrooveContext* grooves) {
    return clipNoteDescs(track.clips, grooves);
}

sub::NoteDesc noteDesc(const PlayedNote& played) {
    sub::NoteDesc desc{played.start, played.end - played.start, played.note.pitch, played.note.velocity, {}, {}};
    for (const BendPoint& point : played.note.bend) desc.bend.push_back({point.time, point.semitones, point.curve});
    for (const Vibrato& v : played.note.vibrato) desc.vibrato.push_back({v.start, v.length, v.depth, v.rate, v.fade});
    return desc;
}

std::vector<sub::NoteDesc> clipNoteDescs(const std::vector<Clip>& clips, const GrooveContext* grooves) {
    std::vector<sub::NoteDesc> notes;
    for (const Clip& clip : clips) {
        std::optional<groove::Groove> plays;
        if (grooves && grooves->library && clip.groove && clip.plays()) {
            plays = grooves::resolve(clip.groove->source, *grooves->library, grooves->defaultGroove);
        }
        if (plays) {
            for (const PlayedNote& played : grooves::groovedNotes(clip, *plays, clip.groove->amounts, grooves->tempo)) {
                notes.push_back(noteDesc(played));
            }
            continue;
        }
        for (const PlayedNote& played : clip.heardNotes()) notes.push_back(noteDesc(played));
    }
    return notes;
}

}  // namespace sub::app
