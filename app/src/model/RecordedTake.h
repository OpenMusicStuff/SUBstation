#pragma once
// A take a recording made: the engine bridge reports them when a recording ends
// (EngineBridge::takesRecorded), and the editor makes them clips in one undo
// step (ProjectEditor::addRecordings).

#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <utility>
#include <vector>

namespace sub::app {

// A note of a MIDI take, in seconds on the timeline, within the take, and how
// it was bent as it played (MIDI 2.0's per-note pitch bend): (seconds from its
// start, semitones), in time order.
struct RecordedTakeNote {
    double start = 0.0;
    double end = 0.0;
    int pitch = 60;
    int velocity = 100;
    std::vector<std::pair<double, double>> bend;

    bool operator==(const RecordedTakeNote&) const = default;
};

// A recorded WAV file and where it starts: `startSec` from the timeline's start
// (negative: it began before it). A MIDI take has no file but `notes`.
//
// While the loop wrapped as it recorded, a take is one straight stretch of
// *recording time*: the timeline as if the loop had never wrapped. `wraps` says
// where it did: at `atSec` (recording time: the loop's end) the timeline went
// `backSec` back (to the loop's start). Its times (startSec, its notes') are in
// recording time; each pass round the loop is a take of its own
// (ProjectEditor::addRecordings, loopPasses()).
struct RecordedTake {
    struct Wrap {
        double atSec = 0.0;
        double backSec = 0.0;

        bool operator==(const Wrap&) const = default;
    };

    QString trackId;
    QString path;
    double startSec = 0.0;
    double durationSec = 0.0;
    std::vector<RecordedTakeNote> notes;
    bool midi = false;
    std::vector<Wrap> wraps;
    // A pass of a take (loopPasses): where in the file it starts, and how long
    // the file is (0: durationSec, from offsetSec).
    double offsetSec = 0.0;
    double fileSec = 0.0;
    // Punch In and Out as they were (timeline seconds): what was played before
    // `punchInSec` or from `punchOutSec` is left out of its passes (loopPasses).
    std::optional<double> punchInSec;
    std::optional<double> punchOutSec;
    // A pass cut short (at the loop's end, or Punch Out: loopPasses), where
    // (timeline seconds): nothing of it goes past, not even a note's end put on
    // the grid (ProjectEditor::recordedMidiClip). None: it ends where it ended.
    std::optional<double> cutSec;

    bool operator==(const RecordedTake&) const = default;
};

// A pass round the loop this short (seconds: stopping just after the loop
// wrapped) is no take of its own, unless it is all there is.
inline constexpr double kMinPassSec = 0.05;

// A take as the passes it made round the loop, in order: each a take of its own
// on the timeline (no wraps, no punch), the part of the take between two wraps
// (an audio pass a stretch of its file, `offsetSec` into it; a MIDI pass the
// notes that start in it, cut at its end), and of that only what is inside the
// punch region (punchInSec, punchOutSec): a pass outside it is none. Without
// wraps or punching, the take itself.
std::vector<RecordedTake> loopPasses(const RecordedTake& take);

}  // namespace sub::app

// The bridge's takesRecorded signal carries them across queued connections.
Q_DECLARE_METATYPE(std::vector<sub::app::RecordedTake>)
