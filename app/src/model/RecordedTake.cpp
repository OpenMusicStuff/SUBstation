#include "model/RecordedTake.h"

#include <algorithm>
#include <limits>

namespace sub::app {

std::vector<RecordedTake> loopPasses(const RecordedTake& take) {
    if (take.wraps.empty() && !take.punchInSec && !take.punchOutSec) return {take};
    // Where each pass begins in recording time, and how far behind it the timeline is there.
    const double begin = take.startSec, end = take.startSec + take.durationSec;
    std::vector<std::pair<double, double>> cuts{{begin, 0.0}};
    for (const RecordedTake::Wrap& wrap : take.wraps) {
        if (wrap.atSec >= end) break;
        if (wrap.atSec <= begin) {
            cuts.front().second += wrap.backSec;
        } else {
            cuts.emplace_back(wrap.atSec, cuts.back().second + wrap.backSec);
        }
    }
    // Of each, only what is inside the punch region, in recording time.
    const double punchIn = take.punchInSec.value_or(-std::numeric_limits<double>::infinity());
    const double punchOut = take.punchOutSec.value_or(std::numeric_limits<double>::infinity());
    std::vector<RecordedTake> passes;
    for (size_t i = 0; i < cuts.size(); ++i) {
        const double back = cuts[i].second;
        const double from = std::max(cuts[i].first, punchIn + back);
        const double to = std::min(i + 1 < cuts.size() ? cuts[i + 1].first : end, punchOut + back);
        if (to <= from || (to - from < kMinPassSec && cuts.size() > 1)) continue;
        RecordedTake pass;
        pass.trackId = take.trackId;
        pass.path = take.path;
        pass.midi = take.midi;
        pass.startSec = from - back;
        pass.durationSec = to - from;
        pass.offsetSec = take.offsetSec + (from - begin);
        pass.fileSec = take.fileSec > 0 ? take.fileSec : take.offsetSec + take.durationSec;
        const bool wrapped = i + 1 < cuts.size() && to >= cuts[i + 1].first;  // (it ended where the loop wrapped)
        if (wrapped || to - back >= punchOut) pass.cutSec = to - back;
        for (const RecordedTakeNote& note : take.notes) {
            if (note.start < from || note.start >= to) continue;
            RecordedTakeNote played = note;
            played.start -= back;
            played.end = std::min(note.end, to) - back;
            pass.notes.push_back(std::move(played));
        }
        passes.push_back(std::move(pass));
    }
    return passes;
}

}  // namespace sub::app
