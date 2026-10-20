#include "audio/SampleSlices.h"

#include "builtin/SampleSlicing.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <vector>

namespace sub::app::sampleSlices {
namespace {

// The waveform's first two channels (one twice for a mono file), as the Sampler reads them, from
// frame `start` on (`length` of them): in place, or (a long file streamed from disk) read into
// memory for the while.
struct Channels {
    std::array<const float*, 2> data{};
    std::array<std::vector<float>, 2> read;
    int count = 0;

    explicit Channels(const Waveform& waveform) : Channels(waveform, 0, waveform.frames()) {}

    Channels(const Waveform& waveform, qint64 start, qint64 length) {
        count = std::min(2, waveform.channels());
        for (int c = 0; c < count; ++c) {
            data[c] = waveform.channelData(c);
            if (data[c]) {
                data[c] += start;
            } else {
                read[c].resize(static_cast<size_t>(length));
                waveform.read(c, start, length, read[c].data());
                data[c] = read[c].data();
            }
        }
        if (count < 2) data[1] = data[0];
    }
};

}  // namespace

std::vector<Transient> transients(const Waveform& waveform, bool reversed) {
    if (waveform.isNull() || waveform.frames() <= 0)
        return {};
    const Channels channels(waveform);
    const std::vector<sub::slicing::Onset> onsets = sub::slicing::detectOnsets(
        channels.data.data(), channels.count, waveform.frames(), waveform.sampleRate(), reversed);
    std::vector<Transient> result;
    result.reserve(onsets.size());
    for (const sub::slicing::Onset& onset : onsets)
        result.push_back({onset.frame, onset.strength});
    return result;
}

std::vector<qint64> sliceStarts(const Settings& settings, const std::vector<Transient>& transients, qint64 start,
                                qint64 end, double sampleRate) {
    std::vector<sub::slicing::Onset> onsets;
    onsets.reserve(transients.size());
    for (const Transient& transient : transients)
        onsets.push_back({transient.frame, transient.strength});
    sub::slicing::SliceSettings engine;
    engine.by = static_cast<sub::slicing::SliceBy>(settings.by);
    engine.sensitivity = static_cast<float>(settings.sensitivity);
    engine.regionBeats = settings.regionBeats;
    engine.divisionBeats = settings.divisionBeats;
    engine.regions = settings.regions;
    std::array<int64_t, sub::slicing::kMaxSlices> starts{};
    const int count =
        sub::slicing::sliceStarts(engine, onsets.data(), onsets.size(), start, end, sampleRate, starts.data());
    return {starts.begin(), starts.begin() + count};
}

double divisionBeats(int index) {
    constexpr auto count = static_cast<int>(std::size(sub::slicing::kSliceDivisionBeats));
    return sub::slicing::kSliceDivisionBeats[std::clamp(index, 0, count - 1)];
}

int firstSliceKey() { return sub::slicing::kFirstSliceKey; }

int maxSlices() { return sub::slicing::kMaxSlices; }

qint64 nearestZeroCrossing(const Waveform& waveform, bool reversed, qint64 frame) {
    const qint64 frames = waveform.frames();
    if (waveform.isNull() || frame <= 0 || frame >= frames)
        return frame;
    // Only the frames the search looks at (the one before each, too), as it plays: a file streamed
    // from disk isn't read whole on each drag.
    const auto reach = static_cast<int64_t>(sub::slicing::kSnapSeconds * waveform.sampleRate());
    const qint64 start = std::max<qint64>(0, frame - reach - 1), end = std::min<qint64>(frames, frame + reach + 1);
    const Channels channels(waveform, reversed ? frames - end : start, end - start);
    return start + sub::slicing::nearestZeroCrossing(channels.data.data(), channels.count, end - start, reversed,
                                                     frame - start, reach);
}

}  // namespace sub::app::sampleSlices
