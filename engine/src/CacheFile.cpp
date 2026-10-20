#include "CacheFile.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <system_error>

#include "AudioSource.h"
#include "platform/Bytes.h"
#include "platform/Paths.h"
#include "platform/StreamFile.h"

namespace sub {
namespace {

constexpr uint64_t kAlign = platform::StreamFile::kAlignment;
constexpr uint64_t kHeaderBytes = 4096;  // a cache file's header: page 0 starts on a block of its own
constexpr char kCacheMagic[8] = {'S', 'U', 'B', 'C', 'A', 'C', 'H', 'E'};
constexpr uint32_t kCacheVersion = 1;
constexpr char kPeakMagic[8] = {'S', 'U', 'B', 'P', 'E', 'A', 'K', 'S'};
constexpr uint32_t kPeakVersion = 1;

constexpr float kInt16Scale = 0.000030517578125f;           // 1 / 32768: miniaudio's own
constexpr float kInt24Scale = 0.00000011920928955078125f;  // 1 / 8388608

uint64_t alignDown(uint64_t v) { return v / kAlign * kAlign; }
uint64_t alignUp(uint64_t v) { return (v + kAlign - 1) / kAlign * kAlign; }

uint16_t le16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t le64(const unsigned char* p) { return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32); }
uint16_t be16(const unsigned char* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// One sample as miniaudio decodes it: integers scaled by a power of two (exact either way), floats as they are.
template <SampleEncoding E, bool Big>
inline float sampleAt(const unsigned char* p) noexcept {
    if constexpr (E == SampleEncoding::Int16) {
        const auto v = static_cast<int16_t>(Big ? be16(p) : le16(p));
        return static_cast<float>(v) * kInt16Scale;
    } else if constexpr (E == SampleEncoding::Int24) {
        const uint32_t bits = Big ? (static_cast<uint32_t>(p[2]) << 8) | (static_cast<uint32_t>(p[1]) << 16) |
                                        (static_cast<uint32_t>(p[0]) << 24)
                                  : (static_cast<uint32_t>(p[0]) << 8) | (static_cast<uint32_t>(p[1]) << 16) |
                                        (static_cast<uint32_t>(p[2]) << 24);
        return static_cast<float>(static_cast<int32_t>(bits) >> 8) * kInt24Scale;
    } else {
        return std::bit_cast<float>(Big ? be32(p) : le32(p));
    }
}

template <SampleEncoding E, bool Big>
bool convertPages(const StreamLayout& layout, const unsigned char* bytes, uint64_t offset, uint64_t got, int64_t first,
                  int64_t count, float* const* pages) noexcept {
    constexpr uint64_t bps = E == SampleEncoding::Int16 ? 2 : E == SampleEncoding::Int24 ? 3 : 4;
    const uint32_t channels = layout.channels;
    bool complete = true;
    for (int64_t i = 0; i < count; ++i) {
        const int64_t k = first + i;
        float* out = pages[i];
        const int64_t start = k * kStreamPageFrames;
        const int64_t inFile = std::clamp<int64_t>(layout.frames - start, 0, kStreamPageFrames);
        int64_t have = 0;  // of them, read
        if (layout.paged) {
            const uint64_t at = layout.dataOffset + static_cast<uint64_t>(k) * layout.pageBytes() - offset;
            for (uint32_t c = 0; c < channels; ++c) {
                const uint64_t from = at + c * static_cast<uint64_t>(kStreamPageFrames) * bps;
                const int64_t readable = from >= got ? 0 : static_cast<int64_t>((got - from) / bps);
                have = std::min(inFile, readable);
                float* dst = out + c * kStreamPageFrames;
                for (int64_t f = 0; f < have; ++f) dst[f] = sampleAt<E, Big>(bytes + from + static_cast<uint64_t>(f) * bps);
                std::fill(dst + have, dst + kStreamPageFrames, 0.f);
            }
        } else {
            const uint64_t frameBytes = channels * bps;
            const uint64_t at = layout.dataOffset + static_cast<uint64_t>(start) * frameBytes - offset;
            const int64_t readable = at >= got ? 0 : static_cast<int64_t>((got - at) / frameBytes);
            have = std::min(inFile, readable);
            if (channels == 1) {
                for (int64_t f = 0; f < have; ++f) out[f] = sampleAt<E, Big>(bytes + at + static_cast<uint64_t>(f) * bps);
            } else {
                float* right = out + kStreamPageFrames;
                for (int64_t f = 0; f < have; ++f) {
                    const unsigned char* frame = bytes + at + static_cast<uint64_t>(f) * frameBytes;
                    out[f] = sampleAt<E, Big>(frame);
                    right[f] = sampleAt<E, Big>(frame + bps);
                }
            }
            for (uint32_t c = 0; c < channels; ++c)
                std::fill(out + c * kStreamPageFrames + have, out + (c + 1) * kStreamPageFrames, 0.f);
        }
        if (have < inFile) complete = false;
    }
    return complete;
}

// Reads a file's bytes anywhere (headers, chunks) through StreamFile: whole aligned blocks, kept a few at a time.
class ByteSource {
public:
    explicit ByteSource(const std::string& path) : buffer_(kWindow) { ok_ = file_.open(path); }
    bool ok() const { return ok_; }
    uint64_t size() const { return file_.size(); }
    // `n` bytes from `at` into `out`; false if the file ends first.
    bool bytes(uint64_t at, void* out, size_t n) {
        auto* dst = static_cast<unsigned char*>(out);
        while (n > 0) {
            if (!(at >= start_ && at < start_ + got_)) {
                start_ = alignDown(at);
                const int64_t got = file_.read(start_, buffer_.data(), buffer_.size());
                got_ = got > 0 ? static_cast<uint64_t>(got) : 0;
                if (at >= start_ + got_) return false;
            }
            const uint64_t from = at - start_;
            const size_t take = static_cast<size_t>(std::min<uint64_t>(n, got_ - from));
            std::memcpy(dst, static_cast<const unsigned char*>(buffer_.data()) + from, take);
            dst += take;
            at += take;
            n -= take;
        }
        return true;
    }

private:
    static constexpr size_t kWindow = 64 * 1024;
    platform::StreamFile file_;
    platform::AlignedBuffer buffer_;
    uint64_t start_ = 0, got_ = 0;
    bool ok_ = false;
};

// An AIFF's sample rate: an 80-bit IEEE extended float (big-endian).
double extendedToDouble(const unsigned char* p) {
    const int exponent = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | p[2 + i];
    if (exponent == 0 && mantissa == 0) return 0.0;
    const double value = std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
    return (p[0] & 0x80) ? -value : value;
}

bool encodingOf(int bits, bool isFloat, SampleEncoding& encoding) {
    if (isFloat) {
        if (bits != 32) return false;
        encoding = SampleEncoding::Float32;
        return true;
    }
    if (bits == 16) encoding = SampleEncoding::Int16;
    else if (bits == 24) encoding = SampleEncoding::Int24;
    else return false;
    return true;
}

std::optional<StreamLayout> wavLayout(ByteSource& in, const unsigned char* head, uint32_t& sampleRate) {
    const bool rf64 = std::memcmp(head, "RF64", 4) == 0;
    uint64_t dataSize64 = 0;  // RF64's ds64
    bool haveFormat = false;
    int bits = 0;
    bool isFloat = false;
    uint32_t channels = 0, rate = 0, blockAlign = 0;
    uint64_t at = 12;
    const uint64_t size = in.size();
    while (at + 8 <= size) {
        unsigned char chunk[8];
        if (!in.bytes(at, chunk, 8)) return std::nullopt;
        const uint32_t length = le32(chunk + 4);
        const uint64_t body = at + 8;
        if (std::memcmp(chunk, "ds64", 4) == 0) {
            unsigned char ds[24];
            if (length < 24 || !in.bytes(body, ds, 24)) return std::nullopt;
            dataSize64 = le64(ds + 8);
        } else if (std::memcmp(chunk, "fmt ", 4) == 0) {
            unsigned char fmt[40] = {};
            if (length < 16 || !in.bytes(body, fmt, std::min<uint32_t>(length, 40))) return std::nullopt;
            uint16_t tag = le16(fmt);
            channels = le16(fmt + 2);
            rate = le32(fmt + 4);
            blockAlign = le16(fmt + 12);
            bits = le16(fmt + 14);
            if (tag == 0xFFFE) {  // WAVE_FORMAT_EXTENSIBLE: its sub-format, and all its bits valid
                if (length < 40 || le16(fmt + 18) != bits) return std::nullopt;
                tag = le16(fmt + 24);
            }
            if (tag != 1 && tag != 3) return std::nullopt;
            isFloat = tag == 3;
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!haveFormat) return std::nullopt;
            StreamLayout layout;
            if (!encodingOf(bits, isFloat, layout.encoding)) return std::nullopt;
            if (channels < 1 || channels > 2 || blockAlign != channels * static_cast<uint32_t>(bits / 8)) return std::nullopt;
            uint64_t bytes = (rf64 && length == 0xFFFFFFFFu) ? dataSize64 : length;
            bytes = std::min(bytes, size - body);  // (a header that says more than the file holds)
            layout.channels = channels;
            layout.dataOffset = body;
            layout.frames = static_cast<int64_t>(bytes / blockAlign);
            sampleRate = rate;
            return layout;
        }
        at = body + length + (length & 1);  // chunks are padded to even sizes
    }
    return std::nullopt;
}

std::optional<StreamLayout> aiffLayout(ByteSource& in, const unsigned char* head, uint32_t& sampleRate) {
    const bool aifc = std::memcmp(head + 8, "AIFC", 4) == 0;
    bool haveFormat = false;
    StreamLayout layout;
    layout.bigEndian = true;
    int bits = 0;
    uint32_t channels = 0, frames = 0;
    bool isFloat = false;
    double rate = 0.0;
    uint64_t at = 12;
    const uint64_t size = in.size();
    while (at + 8 <= size) {
        unsigned char chunk[8];
        if (!in.bytes(at, chunk, 8)) return std::nullopt;
        const uint32_t length = be32(chunk + 4);
        const uint64_t body = at + 8;
        if (std::memcmp(chunk, "COMM", 4) == 0) {
            unsigned char comm[22] = {};
            if (length < 18 || !in.bytes(body, comm, std::min<uint32_t>(length, 22))) return std::nullopt;
            channels = be16(comm);
            frames = be32(comm + 2);
            bits = static_cast<int16_t>(be16(comm + 6));
            rate = extendedToDouble(comm + 8);
            if (aifc) {
                if (length < 22) return std::nullopt;
                if (std::memcmp(comm + 18, "NONE", 4) == 0 || std::memcmp(comm + 18, "twos", 4) == 0) {
                } else if (std::memcmp(comm + 18, "sowt", 4) == 0) {
                    layout.bigEndian = false;
                } else if (std::memcmp(comm + 18, "fl32", 4) == 0 || std::memcmp(comm + 18, "FL32", 4) == 0) {
                    isFloat = true;
                } else {
                    return std::nullopt;
                }
            }
            haveFormat = true;
        } else if (std::memcmp(chunk, "SSND", 4) == 0) {
            if (!haveFormat) return std::nullopt;
            unsigned char ssnd[8];
            if (length < 8 || !in.bytes(body, ssnd, 8)) return std::nullopt;
            if (!encodingOf(bits, isFloat, layout.encoding)) return std::nullopt;
            if (channels < 1 || channels > 2 || rate <= 0.0 || rate != std::floor(rate)) return std::nullopt;
            layout.channels = channels;
            layout.dataOffset = body + 8 + be32(ssnd);
            const uint64_t frameBytes = channels * static_cast<uint64_t>(layout.bytesPerSample());
            const uint64_t available = layout.dataOffset < size ? (size - layout.dataOffset) / frameBytes : 0;
            layout.frames = static_cast<int64_t>(std::min<uint64_t>(frames, available));
            sampleRate = static_cast<uint32_t>(rate);
            return layout;
        }
        at = body + length + (length & 1);
    }
    return std::nullopt;
}

std::string hex(uint64_t v) {
    char text[17];
    std::snprintf(text, sizeof text, "%llx", static_cast<unsigned long long>(v));
    return text;
}

// A name beside `path` no other writer uses.
std::string temporaryBeside(const std::string& path) {
    static std::atomic<uint64_t> counter{0};
    std::random_device random;
    return path + ".tmp" + hex((static_cast<uint64_t>(random()) << 20) ^ counter.fetch_add(1));
}

struct FileCloser {
    void operator()(std::FILE* f) const {
        if (f) std::fclose(f);
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// StreamLayout

int StreamLayout::bytesPerSample() const noexcept {
    switch (encoding) {
        case SampleEncoding::Int16: return 2;
        case SampleEncoding::Int24: return 3;
        default: return 4;
    }
}

uint64_t StreamLayout::pageBytes() const noexcept {
    return static_cast<uint64_t>(channels) * kStreamPageFrames * static_cast<uint64_t>(bytesPerSample());
}

void StreamLayout::span(int64_t first, int64_t count, uint64_t& offset, uint64_t& bytes) const noexcept {
    if (paged) {
        offset = dataOffset + static_cast<uint64_t>(first) * pageBytes();
        bytes = static_cast<uint64_t>(count) * pageBytes();
        return;
    }
    const uint64_t frameBytes = channels * static_cast<uint64_t>(bytesPerSample());
    const int64_t from = first * kStreamPageFrames;
    const int64_t to = std::max(from, std::min((first + count) * kStreamPageFrames, frames));
    offset = alignDown(dataOffset + static_cast<uint64_t>(from) * frameBytes);
    bytes = alignUp(dataOffset + static_cast<uint64_t>(to) * frameBytes) - offset;
}

bool StreamLayout::convert(const unsigned char* bytes, uint64_t offset, uint64_t got, int64_t first, int64_t count,
                           float* const* pages) const noexcept {
    switch (encoding) {
        case SampleEncoding::Int16:
            return bigEndian ? convertPages<SampleEncoding::Int16, true>(*this, bytes, offset, got, first, count, pages)
                             : convertPages<SampleEncoding::Int16, false>(*this, bytes, offset, got, first, count, pages);
        case SampleEncoding::Int24:
            return bigEndian ? convertPages<SampleEncoding::Int24, true>(*this, bytes, offset, got, first, count, pages)
                             : convertPages<SampleEncoding::Int24, false>(*this, bytes, offset, got, first, count, pages);
        default:
            return bigEndian ? convertPages<SampleEncoding::Float32, true>(*this, bytes, offset, got, first, count, pages)
                             : convertPages<SampleEncoding::Float32, false>(*this, bytes, offset, got, first, count, pages);
    }
}

std::optional<StreamLayout> asIsLayout(const std::string& path, uint32_t& sampleRate) {
    ByteSource in(path);
    if (!in.ok()) return std::nullopt;
    unsigned char head[12];
    if (!in.bytes(0, head, 12)) return std::nullopt;
    std::optional<StreamLayout> layout;
    if ((std::memcmp(head, "RIFF", 4) == 0 || std::memcmp(head, "RF64", 4) == 0) && std::memcmp(head + 8, "WAVE", 4) == 0) {
        layout = wavLayout(in, head, sampleRate);
    } else if (std::memcmp(head, "FORM", 4) == 0 &&
               (std::memcmp(head + 8, "AIFF", 4) == 0 || std::memcmp(head + 8, "AIFC", 4) == 0)) {
        layout = aiffLayout(in, head, sampleRate);
    }
    if (layout) layout->path = path;
    return layout;
}

std::string cacheFileName(const std::string& sourcePath, const platform::FileStamp& stamp, uint32_t sampleRate) {
    return hex(platform::fnv1a(platform::fileKey(sourcePath))) + "-" + hex(stamp.size) + "-" + hex(stamp.modified) +
           "-" + std::to_string(sampleRate);
}

std::optional<StreamLayout> readCacheFile(const std::string& path, uint32_t sampleRate,
                                          const platform::FileStamp& stamp, uint32_t* fileRate) {
    const std::unique_ptr<std::FILE, FileCloser> file(platform::openFile(path, false));
    if (!file) return std::nullopt;
    unsigned char header[64];
    if (std::fread(header, 1, sizeof header, file.get()) != sizeof header) return std::nullopt;
    if (std::memcmp(header, kCacheMagic, 8) != 0 || le32(header + 8) != kCacheVersion) return std::nullopt;
    if (le64(header + 56) != platform::fnv1a(header, 56, platform::kFnvOffsetBasis)) return std::nullopt;
    StreamLayout layout;
    layout.path = path;
    layout.paged = true;
    const uint32_t encoding = le32(header + 12);
    if (encoding > static_cast<uint32_t>(SampleEncoding::Float32)) return std::nullopt;
    layout.encoding = static_cast<SampleEncoding>(encoding);
    layout.channels = le32(header + 16);
    layout.frames = static_cast<int64_t>(le64(header + 24));
    layout.dataOffset = kHeaderBytes;
    if (le32(header + 20) != sampleRate || le32(header + 32) != kStreamPageFrames) return std::nullopt;
    if (layout.channels < 1 || layout.channels > 2 || layout.frames <= 0) return std::nullopt;
    if (stamp != platform::FileStamp{} && (le64(header + 40) != stamp.size || le64(header + 48) != stamp.modified))
        return std::nullopt;
    const auto onDisk = platform::stamp(path);
    if (!onDisk || onDisk->size != kHeaderBytes + static_cast<uint64_t>(layout.pages()) * layout.pageBytes())
        return std::nullopt;
    if (fileRate) *fileRate = le32(header + 36);
    return layout;
}

// ---------------------------------------------------------------------------
// CacheFileWriter

CacheFileWriter::CacheFileWriter(std::string path, uint32_t channels, uint32_t sampleRate, SampleEncoding encoding,
                                 uint32_t fileRate, const platform::FileStamp& stamp)
    : path_(std::move(path)), channels_(channels), sampleRate_(sampleRate), fileRate_(fileRate ? fileRate : sampleRate),
      stamp_(stamp), encoding_(encoding) {
    if (channels_ < 1 || channels_ > 2) throw std::invalid_argument("one or two channels");
    std::error_code ignored;
    std::filesystem::create_directories(platform::toPath(path_).parent_path(), ignored);
    temporary_ = temporaryBeside(path_);
    file_ = platform::openFile(temporary_, true);
    if (!file_) throw std::runtime_error("Could not write the cache file " + temporary_);
    const std::vector<unsigned char> header(kHeaderBytes, 0);  // (its fields once the frames are known)
    if (std::fwrite(header.data(), 1, header.size(), file_) != header.size()) {
        std::fclose(file_);
        file_ = nullptr;
        std::filesystem::remove(platform::toPath(temporary_), ignored);
        throw std::runtime_error("Could not write the cache file " + temporary_);
    }
    page_.assign(static_cast<size_t>(channels_) * kStreamPageFrames, 0.f);
    StreamLayout layout;
    layout.encoding = encoding_;
    layout.channels = channels_;
    encoded_.resize(static_cast<size_t>(layout.pageBytes()));
}

CacheFileWriter::~CacheFileWriter() {
    if (file_) std::fclose(file_);
    if (!finished_) {
        std::error_code ignored;
        std::filesystem::remove(platform::toPath(temporary_), ignored);
    }
}

bool CacheFileWriter::write(const float* interleaved, int64_t frames) {
    for (int64_t i = 0; i < frames;) {
        const int64_t n = std::min(frames - i, kStreamPageFrames - inPage_);
        for (uint32_t c = 0; c < channels_; ++c) {
            float* dst = page_.data() + c * kStreamPageFrames + inPage_;
            for (int64_t f = 0; f < n; ++f) dst[f] = interleaved[(i + f) * channels_ + c];
        }
        inPage_ += n;
        i += n;
        if (inPage_ == kStreamPageFrames && !flushPage()) return false;
    }
    return true;
}

bool CacheFileWriter::writePlanar(const float* const* channels, int64_t frames) {
    for (int64_t i = 0; i < frames;) {
        const int64_t n = std::min(frames - i, kStreamPageFrames - inPage_);
        for (uint32_t c = 0; c < channels_; ++c)
            std::copy_n(channels[c] + i, n, page_.data() + c * kStreamPageFrames + inPage_);
        inPage_ += n;
        i += n;
        if (inPage_ == kStreamPageFrames && !flushPage()) return false;
    }
    return true;
}

bool CacheFileWriter::flushPage() {
    if (inPage_ == 0 || !file_) return file_ != nullptr;
    unsigned char* out = encoded_.data();
    for (uint32_t c = 0; c < channels_; ++c) {
        const float* in = page_.data() + c * kStreamPageFrames;
        for (int64_t f = 0; f < kStreamPageFrames; ++f) {
            const float v = f < inPage_ ? in[f] : 0.f;
            if (encoding_ == SampleEncoding::Float32) {
                const auto bits = std::bit_cast<uint32_t>(v);
                for (int b = 0; b < 4; ++b) *out++ = static_cast<unsigned char>(bits >> (8 * b));
                continue;
            }
            // Integers only where they give back these floats exactly (and never a -0).
            const bool wide = encoding_ == SampleEncoding::Int24;
            const float scaled = v * (wide ? 8388608.f : 32768.f);
            const float lowest = wide ? -8388608.f : -32768.f;
            const float highest = wide ? 8388607.f : 32767.f;
            if (!(scaled >= lowest && scaled <= highest) || (v == 0.f && std::signbit(v))) return false;
            const auto x = static_cast<int32_t>(scaled);
            if (static_cast<float>(x) != scaled) return false;
            const auto bits = static_cast<uint32_t>(x);
            *out++ = static_cast<unsigned char>(bits);
            *out++ = static_cast<unsigned char>(bits >> 8);
            if (wide) *out++ = static_cast<unsigned char>(bits >> 16);
        }
    }
    if (std::fwrite(encoded_.data(), 1, encoded_.size(), file_) != encoded_.size()) {
        throw std::runtime_error("Could not write the cache file " + temporary_ + " (is the disk full?)");
    }
    frames_ += inPage_;
    inPage_ = 0;
    return true;
}

StreamLayout CacheFileWriter::finish() {
    if (!flushPage()) throw std::runtime_error("Could not write the cache file " + temporary_);
    if (frames_ == 0) throw std::runtime_error("No audio for the cache file " + path_);
    platform::ByteWriter header;
    header.raw(kCacheMagic, 8);
    header.u32(kCacheVersion);
    header.u32(static_cast<uint32_t>(encoding_));
    header.u32(channels_);
    header.u32(sampleRate_);
    header.u64(static_cast<uint64_t>(frames_));
    header.u32(static_cast<uint32_t>(kStreamPageFrames));
    header.u32(fileRate_);
    header.u64(stamp_.size);
    header.u64(stamp_.modified);
    header.u64(platform::fnv1a(header.bytes.data(), header.bytes.size(), platform::kFnvOffsetBasis));
    std::rewind(file_);
    const bool written = std::fwrite(header.bytes.data(), 1, header.bytes.size(), file_) == header.bytes.size() &&
                         std::fflush(file_) == 0;
    const bool closed = std::fclose(file_) == 0;
    file_ = nullptr;
    if (!written || !closed) throw std::runtime_error("Could not write the cache file " + temporary_);
    StreamLayout layout;
    layout.path = path_;
    layout.encoding = encoding_;
    layout.paged = true;
    layout.channels = channels_;
    layout.frames = frames_;
    layout.dataOffset = kHeaderBytes;
    if (!platform::replaceFile(temporary_, path_)) {
        // Another writer put the same file in place, and someone reads it (Windows won't replace it then): keep that one.
        if (!readCacheFile(path_, sampleRate_, stamp_)) throw std::runtime_error("Could not write the cache file " + path_);
        std::error_code ignored;
        std::filesystem::remove(platform::toPath(temporary_), ignored);
    }
    finished_ = true;
    return layout;
}

MadeCacheFile makeCacheFile(const std::string& source, uint32_t sampleRate, const std::string& path) {
    const platform::FileStamp stamp = platform::stamp(source).value_or(platform::FileStamp{});
    struct Abandoned {};
    // As the decoder's own integers if it has them, else floats; and floats if the integers turn out not to
    // hold every sample exactly after all.
    for (const bool asFloats : {false, true}) {
        std::optional<CacheFileWriter> writer;
        std::optional<PeakBuilder> peaks;
        MadeCacheFile made;
        try {
            AudioSource::decode(
                source, sampleRate,
                [&](const AudioSource::DecodeFormat& format) {
                    const SampleEncoding encoding = asFloats || format.integerBits == 0 ? SampleEncoding::Float32
                                                    : format.integerBits == 16          ? SampleEncoding::Int16
                                                                                        : SampleEncoding::Int24;
                    writer.emplace(path, format.channels, sampleRate, encoding, format.fileRate, stamp);
                    peaks.emplace(format.channels);
                    made.fileRate = format.fileRate;
                },
                [&](const float* interleaved, int64_t frames) {
                    if (!writer->write(interleaved, frames)) throw Abandoned{};
                    peaks->add(interleaved, frames);
                });
        } catch (const Abandoned&) {
            continue;
        }
        if (!writer) throw std::runtime_error("Audio file '" + source + "' contains no audio");
        made.layout = writer->finish();
        made.peaks = peaks->finish();
        return made;
    }
    throw std::runtime_error("Could not write the cache file " + path);
}

// ---------------------------------------------------------------------------
// Peak files

bool writePeakFile(const std::string& path, uint32_t channels, int64_t frames,
                   const std::vector<std::vector<float>>& peaks) {
    platform::ByteWriter out;
    out.raw(kPeakMagic, 8);
    out.u32(kPeakVersion);
    out.u32(channels);
    out.u64(static_cast<uint64_t>(frames));
    out.u32(static_cast<uint32_t>(peaks.size()));
    for (const auto& level : peaks) out.u64(level.size());
    out.u64(platform::fnv1a(out.bytes.data(), out.bytes.size(), platform::kFnvOffsetBasis));
    for (const auto& level : peaks) out.raw(level.data(), level.size() * sizeof(float));  // (little-endian, as every CPU we run on)
    return platform::writeFileAtomically(path, out.bytes);
}

std::optional<std::vector<std::vector<float>>> readPeakFile(const std::string& path, uint32_t channels,
                                                            int64_t frames) {
    const auto bytes = platform::readFile<std::vector<char>>(path);
    if (!bytes) return std::nullopt;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes->data());
    const size_t size = bytes->size();
    constexpr size_t kFixed = 8 + 4 + 4 + 8 + 4;
    if (size < kFixed || std::memcmp(p, kPeakMagic, 8) != 0 || le32(p + 8) != kPeakVersion) return std::nullopt;
    if (le32(p + 12) != channels || static_cast<int64_t>(le64(p + 16)) != frames) return std::nullopt;
    const uint32_t levels = le32(p + 24);
    if (levels != AudioSource::kNumPeakLevels) return std::nullopt;
    const size_t headerSize = kFixed + levels * 8 + 8;
    if (size < headerSize || le64(p + headerSize - 8) != platform::fnv1a(p, headerSize - 8, platform::kFnvOffsetBasis))
        return std::nullopt;
    std::vector<std::vector<float>> peaks(levels);
    size_t at = headerSize;
    for (uint32_t level = 0; level < levels; ++level) {
        const uint64_t count = le64(p + kFixed + level * 8);
        if (count > (size - at) / sizeof(float)) return std::nullopt;
        peaks[level].resize(static_cast<size_t>(count));
        std::memcpy(peaks[level].data(), p + at, static_cast<size_t>(count) * sizeof(float));
        at += static_cast<size_t>(count) * sizeof(float);
    }
    // Level 0: a (min, max) pair per channel every kBaseSamplesPerPeak frames.
    const int64_t spp = AudioSource::kBaseSamplesPerPeak;
    if (peaks[0].size() != static_cast<size_t>((frames + spp - 1) / spp) * channels * 2) return std::nullopt;
    return peaks;
}

std::vector<std::vector<float>> scanPeaks(const StreamLayout& layout) {
    platform::StreamFile file;
    if (!file.open(layout.path)) throw std::runtime_error("Could not open audio file '" + layout.path + "'");
    constexpr int64_t kPagesARead = 8;
    std::vector<float> pages(static_cast<size_t>(kPagesARead) * 2 * kStreamPageFrames);
    float* pointers[kPagesARead];
    for (int64_t i = 0; i < kPagesARead; ++i) pointers[i] = pages.data() + i * 2 * kStreamPageFrames;
    platform::AlignedBuffer buffer;
    PeakBuilder peaks(layout.channels);
    for (int64_t first = 0; first < layout.pages(); first += kPagesARead) {
        const int64_t count = std::min(kPagesARead, layout.pages() - first);
        uint64_t offset = 0, bytes = 0;
        layout.span(first, count, offset, bytes);
        if (buffer.size() < bytes) buffer = platform::AlignedBuffer(static_cast<size_t>(bytes));
        const int64_t got = file.read(offset, buffer.data(), static_cast<size_t>(bytes));
        if (got < 0 || !layout.convert(static_cast<const unsigned char*>(buffer.data()), offset, static_cast<uint64_t>(got),
                                       first, count, pointers))
            throw std::runtime_error("Could not read audio file '" + layout.path + "'");
        for (int64_t i = 0; i < count; ++i) {
            const int64_t frames = std::min(kStreamPageFrames, layout.frames - (first + i) * kStreamPageFrames);
            const float* channels[2] = {pointers[i], pointers[i] + kStreamPageFrames};
            peaks.addPlanar(channels, frames);
        }
    }
    return peaks.finish();
}

}  // namespace sub
