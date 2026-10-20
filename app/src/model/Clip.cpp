#include "model/Clip.h"

#include "model/Automation.h"
#include "model/Timebase.h"

#include "NoteBend.h"

#include <algorithm>
#include <cmath>

namespace sub::app {

double Note::bendAt(double beat, double tempo) const {
    // The engine's rules, in beats: a vibrato's cycles a second are 60 / tempo cycles a beat.
    return sub::bend::at(bend, vibrato, beat, tempo > 0.0 ? 60.0 / tempo : 0.0);
}

double Note::curveAt(double beat) const { return sub::bend::curveAt(bend, beat); }

QString legacyWarpMode(const QString& name) {
    if (name == u"Beats") return QStringLiteral("Transients");
    if (name == u"Tones") return QStringLiteral("Standard");
    if (name == u"Complex") return QStringLiteral("Standard");
    if (name == u"Texture") return QStringLiteral("Smooth");
    if (name == u"Complex Pro") return QStringLiteral("Formants");
    return name;
}

Clip Clip::audio(const QString& id, const QString& path, const QString& name, double startBeat, double durationSec,
                 double offsetSec, double sourceDurationSec) {
    Clip clip;
    clip.kind = Kind::Audio;
    clip.id = id;
    clip.path = path;
    clip.name = name;
    clip.startBeat = startBeat;
    clip.durationSec = durationSec;
    clip.offsetSec = offsetSec;
    clip.sourceDurationSec = sourceDurationSec;
    return clip;
}

Clip Clip::midi(const QString& id, const QString& name, double startBeat, double durationBeats, double offsetBeats,
                std::vector<Note> notes) {
    Clip clip;
    clip.kind = Kind::Midi;
    clip.id = id;
    clip.name = name;
    clip.startBeat = startBeat;
    clip.durationBeats = durationBeats;
    clip.offsetBeats = offsetBeats;
    clip.notes = std::move(notes);
    return clip;
}

bool Clip::isWarped() const { return isAudio() && warp && segmentBpm > 0; }

void Clip::setWarpMarkers(std::vector<WarpMarker> markers) {
    std::sort(markers.begin(), markers.end(), [](const WarpMarker& a, const WarpMarker& b) { return a.sec < b.sec; });
    std::vector<WarpMarker> kept;
    for (const WarpMarker& m : markers) {
        if (!std::isfinite(m.sec) || !std::isfinite(m.beat)) continue;
        if (kept.empty() || (m.sec > kept.back().sec + 1e-9 && m.beat > kept.back().beat + 1e-9)) kept.push_back(m);
    }
    const bool hadMarkers = !warpMarkers.empty();
    if (kept.size() < 2) {
        warpMarkers.clear();
        if (hadMarkers) sourceMap.reset();  // (a frozen track's map is its own: kept)
        return;
    }
    warpMarkers = std::move(kept);
    const std::vector<double> tempos = warpTempos();
    segmentBpm = tempos.front();
    // As a tempo map from the file's start: its beat 0 at second 0 (the first stretch's tempo going
    // back to it), a jump at each marker to the next stretch's tempo, the last's after the last.
    const double origin = warpMarkers.front().beat - warpMarkers.front().sec * tempos.front() / 60.0;
    std::vector<TempoMap::Point> points;
    for (size_t i = 0; i < tempos.size(); ++i) {
        points.push_back({warpMarkers[i].beat - origin, tempos[i]});
        points.push_back({warpMarkers[i + 1].beat - origin, tempos[i]});
    }
    sourceMap = Tempo(TempoMap(std::move(points)));
}

std::vector<double> Clip::warpTempos() const {
    std::vector<double> tempos;
    for (size_t i = 0; i + 1 < warpMarkers.size(); ++i) {
        const WarpMarker& a = warpMarkers[i];
        const WarpMarker& b = warpMarkers[i + 1];
        tempos.push_back(60.0 * (b.beat - a.beat) / (b.sec - a.sec));
    }
    return tempos;
}

void Clip::scaleWarp(double factor) {
    if (!(factor > 0.0)) return;
    segmentBpm *= factor;
    if (warpMarkers.empty()) return;
    std::vector<WarpMarker> scaled = warpMarkers;
    for (WarpMarker& m : scaled) m.beat *= factor;
    setWarpMarkers(std::move(scaled));
}

double Clip::warpBeatAt(double sec) const {
    if (warpMarkers.empty() && sourceMap) return sourceMap->beatAt(sec);
    if (warpMarkers.empty()) return segmentBpm > 0 ? secondsToBeats(sec, segmentBpm) : 0.0;
    const TempoMap& map = sourceMap->map();
    const double origin = warpMarkers.front().beat - map.beatAt(warpMarkers.front().sec);
    return map.beatAt(sec) + origin;
}

double Clip::warpSecondsAt(double beat) const {
    if (warpMarkers.empty() && sourceMap) return sourceMap->secondsAt(beat);
    if (warpMarkers.empty()) return segmentBpm > 0 ? beatsToSeconds(beat, segmentBpm) : 0.0;
    const TempoMap& map = sourceMap->map();
    const double origin = warpMarkers.front().beat - map.beatAt(warpMarkers.front().sec);
    return map.secondsAt(beat - origin);
}

double Clip::sourceTempo(const Tempo& tempo) const { return isWarped() ? segmentBpm : tempo.bpmAt(startBeat); }

namespace {

// A source map's seconds at a timeline beat of its clip (its offset plays at its start beat).
double mappedSource(const Clip& clip, double beat) {
    const TempoMap& map = clip.sourceMap->map();
    return map.secondsAt(map.beatAt(clip.offsetSec) + beat - clip.startBeat);
}

}  // namespace

double Clip::beatsToSourceFrom(double from, double beats, const Tempo& tempo) const {
    if (isWarped() && sourceMap) return mappedSource(*this, from + beats) - mappedSource(*this, from);
    return isWarped() ? beatsToSeconds(beats, segmentBpm) : tempo.secondsFor(from, beats);
}

double Clip::sourceToBeatsFrom(double from, double seconds, const Tempo& tempo) const {
    if (isWarped() && sourceMap) {
        const double at = mappedSource(*this, from);
        return sourceMap->map().beatAt(at + seconds) - sourceMap->map().beatAt(at);
    }
    return isWarped() ? secondsToBeats(seconds, segmentBpm) : tempo.beatsFor(from, seconds);
}

double Clip::lengthBeats(const Tempo& tempo) const {
    return isMidi() ? durationBeats : sourceToBeats(durationSec, tempo);
}

double Clip::endBeat(const Tempo& tempo) const { return startBeat + lengthBeats(tempo); }

void Clip::fitFades() {
    if (!isAudio()) {
        fadeInSec = fadeOutSec = fadeInCurve = fadeOutCurve = 0.0;
        return;
    }
    const double length = std::max(0.0, durationSec);
    fadeInSec = std::max(0.0, fadeInSec);
    fadeOutSec = std::max(0.0, fadeOutSec);
    const double both = fadeInSec + fadeOutSec;
    if (both > length && both > 0) {
        fadeInSec *= length / both;
        fadeOutSec = length - fadeInSec;
    }
    fadeInCurve = std::clamp(fadeInCurve, -1.0, 1.0);
    fadeOutCurve = std::clamp(fadeOutCurve, -1.0, 1.0);
    if (fadeInSec == 0) fadeInCurve = 0.0;
    if (fadeOutSec == 0) fadeOutCurve = 0.0;
}

double Clip::fadeGain(double x, double curve) { return automation::shape(std::clamp(x, 0.0, 1.0), curve); }

double Clip::windowEnd() const { return offsetBeats + durationBeats; }

double Clip::toTimeline(double contentBeat) const { return startBeat + contentBeat - offsetBeats; }

std::vector<PlayedNote> Clip::playedNotes() const {
    std::vector<PlayedNote> played;
    if (!isMidi()) return played;
    const double end = windowEnd();
    for (const Note& note : notes) {
        if (offsetBeats <= note.start && note.start < end) {
            played.push_back({toTimeline(note.start), toTimeline(std::min(note.end(), end)), note});
        }
    }
    return played;
}

std::vector<PlayedNote> Clip::heardNotes() const {
    std::vector<PlayedNote> heard;
    if (!plays()) return heard;
    for (const PlayedNote& played : playedNotes()) {
        if (!played.note.muted) heard.push_back(played);
    }
    return heard;
}

}  // namespace sub::app
