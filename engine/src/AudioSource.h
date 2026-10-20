#pragma once
// An audio file at the engine sample rate, plus min/max peak mipmaps for
// waveform drawing. Immutable once loaded, so it can be shared freely between
// the UI, snapshots and the audio thread.
//
// Its frames are kept one of two ways (docs/engine/streaming.md):
//  * Resident: decoded into memory (load(), fromSamples()): channelData().
//  * Streamed: read from disk as they play, a page at a time, into the stream
//    cache's pool (StreamCache.h): stream(). The audio thread reads either
//    through a SourceCursor (SourceCursor.h); other threads through read().

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sub {

class StreamedAudio;  // StreamCache.h

struct AudioFileInfo {
    int64_t frames = 0;        // at the file's own sample rate
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    double duration = 0.0;     // seconds
};

// Min/max peaks at kNumPeakLevels levels (AudioSource's), made from frames as
// they come: a decode, a scan of a file, samples in memory.
class PeakBuilder {
public:
    explicit PeakBuilder(uint32_t channels);
    // `frames` more frames, interleaved (`channels` of them each), or planar (a pointer a channel).
    void add(const float* interleaved, int64_t frames);
    void addPlanar(const float* const* channels, int64_t frames);
    // The levels: [level][peak][channel][2] as (min, max) pairs.
    std::vector<std::vector<float>> finish();

private:
    uint32_t channels_;
    int64_t inPeak_ = 0;  // frames of the peak under way
    std::vector<float> current_;  // its (min, max) per channel
    std::vector<float> level0_;
};

class AudioSource : public std::enable_shared_from_this<AudioSource> {
public:
    static constexpr int kNumPeakLevels = 6;
    static constexpr int kBaseSamplesPerPeak = 32;   // level n holds 32 * 4^n frames per peak

    // Reads only what is needed to report length and format. Throws on failure.
    static AudioFileInfo probe(const std::string& utf8Path);

    // What decode() hands over first: the decoded stream's format.
    struct DecodeFormat {
        uint32_t channels = 0;      // 1 or 2: mono stays mono, more are mixed down to stereo
        uint32_t fileChannels = 0;  // the file's own
        uint32_t fileRate = 0;      // the file's own rate
        // The decoder's own sample format, when the file's samples come as they
        // are (not resampled, not mixed down): 16 or 24 bits as integers; 0 otherwise.
        int integerBits = 0;
        int64_t expectedFrames = 0;  // what the decoder expects (0: unknown)
    };
    // Decodes the file at `sampleRate` as load() does, handing `begin` its format,
    // then `take` its frames a chunk at a time (interleaved). Throws on failure.
    // load() and the stream cache's cache files (CacheFile.h) both decode
    // through it, so they hold the same samples, to the bit.
    static void decode(const std::string& utf8Path, uint32_t sampleRate,
                       const std::function<void(const DecodeFormat&)>& begin,
                       const std::function<void(const float* interleaved, int64_t frames)>& take);

    // Decodes the whole file to float, resampled to `sampleRate`. Mono files stay
    // mono; anything with more than two channels is downmixed to stereo.
    static std::shared_ptr<AudioSource> load(const std::string& utf8Path, uint32_t sampleRate);
    // Samples already in memory (planar: channel c at [c * frames, (c + 1) *
    // frames), one or two channels), at `sampleRate`: what load() decodes from
    // the 32-bit float file of them at that rate (a track's render, freezing it:
    // RenderJob::sources()), without reading it. Throws if there are none.
    // Without `peaks`, it has none (a stretched clip's render: WarpCache.h).
    static std::shared_ptr<AudioSource> fromSamples(const std::string& utf8Path, uint32_t sampleRate,
                                                    uint32_t channels, std::vector<float> planar, bool peaks = true);
    // A file read from disk as it plays: `stream`'s frames, with its peaks
    // (PeakBuilder's), at `sampleRate` (the file's own: `fileRate`).
    static std::shared_ptr<AudioSource> streamed(const std::string& utf8Path, uint32_t sampleRate, uint32_t fileRate,
                                                 std::shared_ptr<StreamedAudio> stream,
                                                 std::vector<std::vector<float>> peaks);

    const std::string& path() const { return path_; }
    uint32_t channels() const { return channels_; }
    int64_t frames() const { return frames_; }
    uint32_t sampleRate() const { return sampleRate_; }
    uint32_t fileSampleRate() const { return fileSampleRate_; }
    double duration() const { return sampleRate_ ? static_cast<double>(frames_) / sampleRate_ : 0.0; }

    // Whether its frames are in memory (else streamed from disk: stream()).
    bool resident() const noexcept { return !stream_; }
    const StreamedAudio* stream() const noexcept { return stream_.get(); }
    const std::shared_ptr<StreamedAudio>& sharedStream() const noexcept { return stream_; }

    // Planar storage of a resident source: channel c occupies [c * frames, (c + 1) * frames).
    // Null for a streamed one: read it through read() (or a SourceCursor).
    const float* data() const { return stream_ ? nullptr : data_.data(); }
    const float* channelData(uint32_t channel) const {
        return stream_ ? nullptr : data_.data() + static_cast<size_t>(channel) * frames_;
    }
    // Channel `side` of the source played as stereo (0 left, 1 right): a mono
    // file's one channel on both sides.
    const float* stereoChannel(uint32_t side) const { return channelData(channels_ > 1 ? side : 0u); }

    // Frames [start, start + count) of channel `channel` into `out`, from memory,
    // or from disk through the stream cache, waiting for its reads: any thread
    // but the audio thread's (the waveform drawn at sample level, analysis,
    // reversing). Frames outside the file are 0. False if a streamed file
    // couldn't be read (its frames are 0 there).
    bool read(uint32_t channel, int64_t start, int64_t count, float* out) const;

    int numPeakLevels() const { return kNumPeakLevels; }
    static int samplesPerPeak(int level);
    int64_t numPeaks(int level) const;
    // Layout: [numPeaks][channels][2] with (min, max) pairs.
    const float* peaks(int level) const;
    // All of them (what a peak file keeps: CacheFile.h).
    const std::vector<std::vector<float>>& peakLevels() const { return peaks_; }

private:
    AudioSource() = default;
    void buildPeaks();

    std::string path_;
    uint32_t channels_ = 0;
    uint32_t sampleRate_ = 0;
    uint32_t fileSampleRate_ = 0;
    int64_t frames_ = 0;
    std::vector<float> data_;
    std::shared_ptr<StreamedAudio> stream_;
    std::vector<std::vector<float>> peaks_;
};

}  // namespace sub
