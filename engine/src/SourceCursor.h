#pragma once
// Reading a source's frames wherever they are: one call in front of every read
// the renderer makes of a source (a clip played as it is, a span for Re-Pitch's
// kernel, the browser's preview; docs/engine/streaming.md). A resident source's
// frames are copied from memory; a streamed one's from the stream cache's pages
// (StreamCache.h), the audio thread never waiting for one: frames whose page
// isn't in memory are silence, faded at the miss's edges, and counted.
// Offline renders wait for the disk instead (fetchBlocking()): they render the
// same frames whatever the disk.

#include <algorithm>
#include <cstdint>

#include "AudioSource.h"
#include "StreamCache.h"

namespace sub {

// How a renderer reads streamed sources: waiting for the disk (offline renders,
// the warp cache's thread) or never (live), and whether misses there are expected
// (the playhead dragged: not reported as a problem).
struct StreamReads {
    bool blocking = false;
    bool missesExpected = false;
};

class SourceCursor {
public:
    explicit SourceCursor(const AudioSource& source) noexcept : source_(source) {}

    // Copies source frames [start, start + n) of both sides (a mono file's one
    // channel on both) into outL/outR. Frames before 0 or past the end are
    // silence. Frames whose page isn't loaded are silence too, faded over
    // StreamedAudio::kMissFade frames at each edge, and counted (`expected`:
    // the playhead was dragged, no problem). Never blocks, never allocates.
    // Returns the frames that were there.
    int64_t fetch(int64_t start, int64_t n, float* outL, float* outR, bool expected = false) const noexcept {
        if (const StreamedAudio* stream = source_.stream()) return stream->fetch(start, n, outL, outR, expected);
        copyResident(start, n, outL, outR);
        return std::clamp<int64_t>(std::min(start + n, source_.frames()) - std::max<int64_t>(start, 0), 0, n);
    }

    // The same, waiting for the pages that aren't in memory (offline renders,
    // the warp cache's thread): every frame there, never missing.
    void fetchBlocking(int64_t start, int64_t n, float* outL, float* outR) const {
        if (const StreamedAudio* stream = source_.stream()) {
            stream->fetchBlocking(start, n, outL, outR);
            return;
        }
        copyResident(start, n, outL, outR);
    }

    // Either, as `reads` says.
    int64_t fetch(int64_t start, int64_t n, float* outL, float* outR, StreamReads reads) const {
        if (reads.blocking) {
            fetchBlocking(start, n, outL, outR);
            return n;
        }
        return fetch(start, n, outL, outR, reads.missesExpected);
    }

private:
    void copyResident(int64_t start, int64_t n, float* outL, float* outR) const noexcept {
        const float* left = source_.stereoChannel(0);
        const float* right = source_.stereoChannel(1);
        const int64_t total = source_.frames();
        const int64_t from = std::clamp<int64_t>(-start, 0, n);  // frames before the file
        const int64_t to = std::clamp<int64_t>(total - start, from, n);  // and from its end on
        std::fill(outL, outL + from, 0.f);
        std::fill(outR, outR + from, 0.f);
        if (to > from) {
            std::copy_n(left + start + from, to - from, outL + from);
            std::copy_n(right + start + from, to - from, outR + from);
        }
        std::fill(outL + to, outL + n, 0.f);
        std::fill(outR + to, outR + n, 0.f);
    }

    const AudioSource& source_;
};

}  // namespace sub
