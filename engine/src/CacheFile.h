#pragma once
// Where a streamed source's frames are on disk (docs/engine/streaming.md): a WAV
// or AIFF file at the engine's rate read as it is, or a cache file decoded from
// any other once (FLAC, MP3, another rate, more than two channels), and the peak
// file beside it. Both live in the stream cache's folder (StreamCache.h), named
// by the source's fileKey(), its stamp (size and last write) and the engine's
// rate, so a file that changed, or another rate, gets files of its own.
//
// A cache file is a 4 KiB header, then pages: page k holds frames [k * P,
// (k + 1) * P) of every channel, one channel after the other, P =
// kStreamPageFrames, each page a whole number of 4 KiB blocks: a page is one
// aligned read whatever the system. Its samples are what AudioSource::load()
// decodes, kept as 16-bit or 24-bit integers where the decoder's floats are
// those integers exactly (the file's own, neither resampled nor mixed down),
// else as floats: a streamed source plays the very frames a resident one does.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "platform/Files.h"

namespace sub {

inline constexpr int kStreamPageShift = 15;
inline constexpr int64_t kStreamPageFrames = int64_t{1} << kStreamPageShift;  // 32768: about 0.7 s at 48 kHz

enum class SampleEncoding : uint8_t { Int16, Int24, Float32 };

// How a streamed file holds its frames, and how to read pages of them.
struct StreamLayout {
    std::string path;  // the file read: the source itself, or its cache file
    SampleEncoding encoding = SampleEncoding::Float32;
    bool bigEndian = false;  // (an AIFF's)
    // A cache file: page by page, each channel's frames together. Else the
    // frames one after the other, each frame's channels together (a WAV, an AIFF).
    bool paged = false;
    uint32_t channels = 1;  // 1 or 2: in the file, and as played
    int64_t frames = 0;
    uint64_t dataOffset = 0;  // where frame 0 (a cache file: page 0) is

    int bytesPerSample() const noexcept;
    int64_t pages() const noexcept { return (frames + kStreamPageFrames - 1) / kStreamPageFrames; }
    // A cache file's page: every channel's kStreamPageFrames frames (whole 4 KiB blocks).
    uint64_t pageBytes() const noexcept;
    // What to read for pages [first, first + count): from `offset`, `bytes` long, aligned on
    // platform::StreamFile::kAlignment (the end of the file may come first).
    void span(int64_t first, int64_t count, uint64_t& offset, uint64_t& bytes) const noexcept;
    // Pages [first, first + count) from what span() said to read, `got` bytes of it (fewer than asked at the
    // file's end), into each page's samples (`pages[i]`: channel c at c * kStreamPageFrames). Frames past the
    // file's end, or past what was got, are 0. Returns false if the file ended before the frames did (it changed).
    bool convert(const unsigned char* bytes, uint64_t offset, uint64_t got, int64_t first, int64_t count,
                 float* const* pages) const noexcept;
};

// A WAV (RIFF or RF64) or AIFF (AIFF, AIFF-C) file that can be streamed as it is:
// one or two channels of 16-bit or 24-bit integers, or 32-bit floats. Its layout
// and rate; none for anything else (another format or encoding, a file it can't
// read), which needs a cache file.
std::optional<StreamLayout> asIsLayout(const std::string& path, uint32_t& sampleRate);

// The name (without its extension) of a source's cache file and peak file.
std::string cacheFileName(const std::string& sourcePath, const platform::FileStamp& stamp, uint32_t sampleRate);
inline constexpr const char* kCacheFileExtension = ".subcache";
inline constexpr const char* kPeakFileExtension = ".subpeaks";

// A cache file's layout, from its header, if it is one at `sampleRate` of a
// source with `stamp` (renders: no stamp); none otherwise (or incomplete).
std::optional<StreamLayout> readCacheFile(const std::string& path, uint32_t sampleRate,
                                          const platform::FileStamp& stamp, uint32_t* fileRate = nullptr);

// Writes a cache file block by block: into a temporary file beside `path`,
// which finish() puts in place (a crash never leaves half of one under its name).
class CacheFileWriter {
public:
    // Throws std::runtime_error if the file can't be made.
    CacheFileWriter(std::string path, uint32_t channels, uint32_t sampleRate, SampleEncoding encoding,
                    uint32_t fileRate = 0, const platform::FileStamp& stamp = {});
    ~CacheFileWriter();  // abandons the file unless finished
    CacheFileWriter(const CacheFileWriter&) = delete;
    CacheFileWriter& operator=(const CacheFileWriter&) = delete;

    // `frames` more frames, interleaved. False if the integers it writes can't
    // hold them exactly (it is abandoned then: write them again as floats).
    bool write(const float* interleaved, int64_t frames);
    // The same from planar channels (`channels` pointers).
    bool writePlanar(const float* const* channels, int64_t frames);
    // Puts the file in place (or keeps one put there meanwhile: the same
    // samples) and gives its layout. Throws std::runtime_error on failure.
    StreamLayout finish();

private:
    bool flushPage();

    std::string path_, temporary_;
    std::FILE* file_ = nullptr;
    uint32_t channels_;
    uint32_t sampleRate_;
    uint32_t fileRate_;
    platform::FileStamp stamp_;
    SampleEncoding encoding_;
    std::vector<float> page_;  // planar: channel c at c * kStreamPageFrames
    std::vector<unsigned char> encoded_;
    int64_t inPage_ = 0;
    int64_t frames_ = 0;
    bool finished_ = false;
};

// Decodes `source` at `sampleRate` (AudioSource::decode(): what load() holds)
// into a new cache file at `path`, and gives its layout, the file's own rate and
// the peaks (PeakBuilder's). Throws std::runtime_error on failure.
struct MadeCacheFile {
    StreamLayout layout;
    uint32_t fileRate = 0;
    std::vector<std::vector<float>> peaks;
};
MadeCacheFile makeCacheFile(const std::string& source, uint32_t sampleRate, const std::string& path);

// Peak files: AudioSource's levels for a source of `channels` and `frames`.
bool writePeakFile(const std::string& path, uint32_t channels, int64_t frames,
                   const std::vector<std::vector<float>>& peaks);
std::optional<std::vector<std::vector<float>>> readPeakFile(const std::string& path, uint32_t channels,
                                                            int64_t frames);
// A file streamed as it is, read through once for its peaks. Throws std::runtime_error if it can't be.
std::vector<std::vector<float>> scanPeaks(const StreamLayout& layout);

}  // namespace sub
