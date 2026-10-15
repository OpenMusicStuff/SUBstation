#include "model/RecordedTake.h"

#include <algorithm>

namespace sub::app {

std::vector<RecordedTake> loopPasses(const RecordedTake& take) {
    if (take.wraps.empty()) return {take};
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
    std::vector<RecordedTake> passes;
    for (size_t i = 0; i < cuts.size(); ++i) {
        const auto [from, back] = cuts[i];
        const double to = i + 1 < cuts.size() ? cuts[i + 1].first : end;
        if (to - from < kMinPassSec && cuts.size() > 1) continue;
        RecordedTake pass;
        pass.trackId = take.trackId;
        pass.path = take.path;
        pass.midi = take.midi;
        pass.startSec = from - back;
        pass.durationSec = to - from;
        pass.offsetSec = take.offsetSec + (from - begin);
        pass.fileSec = take.fileSec > 0 ? take.fileSec : take.offsetSec + take.durationSec;
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
