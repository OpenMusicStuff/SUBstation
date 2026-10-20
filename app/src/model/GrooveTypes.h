#pragma once
// The project's grooves as values (Grooves.h has what works on them): a groove
// of its library, what plays a groove (a source), and a clip's groove. Apart
// from Grooves.h so a Clip can hold its groove (Clip::groove).

#include "music/Groove.h"

#include <QString>

#include <optional>
#include <vector>

namespace sub::app {

// A groove of the project's library: its id, its name, the groove, and the
// notes (or an audio clip's hits: key 60) it was made from, folded into its
// cycle's beats, which the groove editor draws under it (none for one made
// otherwise).
struct LibraryGroove {
    QString id;
    QString name;
    groove::Groove groove;
    std::vector<groove::NoteTime> notes;

    friend bool operator==(const LibraryGroove&, const LibraryGroove&) = default;
};

// What plays a groove (Grooves.h). Swing: `swing` percent (50..75) on steps
// of `stepBeats`; Project: the project's default; Library: the library's
// groove `libraryId`; Own: `own`.
struct GrooveSource {
    enum class Kind { Swing, Project, Library, Own };
    Kind kind = Kind::Swing;
    double swing = groove::kMinSwing;
    double stepBeats = 0.25;
    QString libraryId;
    std::optional<groove::Groove> own;

    friend bool operator==(const GrooveSource&, const GrooveSource&) = default;
};

// A clip's groove (Clip::groove): its source and how much of it plays
// (Timing, Velocity, Random, Quantize; Length stays 0 for clips).
struct ClipGroove {
    GrooveSource source;
    groove::Amounts amounts;

    friend bool operator==(const ClipGroove&, const ClipGroove&) = default;
};

}  // namespace sub::app
