#include "AudioSource.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "MiniaudioFiles.h"
#include "StreamCache.h"

namespace sub {
namespace {

// RAII wrapper so every error path uninitialises the decoder.
class Decoder {
public:
    Decoder(const std::string& utf8Path, const ma_decoder_config& config) {
        const ma_result result = initDecoderFile(utf8Path, config, &decoder_);
        if (result != MA_SUCCESS) {
            throw std::runtime_error("Could not open audio file '" + utf8Path + "': " + ma_result_description(result));
        }
    }
    ~Decoder() { ma_decoder_uninit(&decoder_); }
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    ma_decoder* get() { return &decoder_; }

private:
    ma_decoder decoder_{};
};

}  // namespace

AudioFileInfo AudioSource::probe(const std::string& utf8Path) {
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    Decoder decoder(utf8Path, config);
    AudioFileInfo info;
    info.channels = decoder.get()->outputChannels;
    info.sampleRate = decoder.get()->outputSampleRate;
    ma_uint64 length = 0;
    if (ma_decoder_get_length_in_pcm_frames(decoder.get(), &length) != MA_SUCCESS || length == 0) {
        // Some streams do not report a length; count the frames instead.
        std::vector<float> scratch(4096 * std::max<uint32_t>(info.channels, 1));
        length = 0;
        for (;;) {
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(decoder.get(), scratch.data(), 4096, &read);
            if (read == 0) break;
            length += read;
        }
    }
    info.frames = static_cast<int64_t>(length);
    info.duration = info.sampleRate ? static_cast<double>(length) / info.sampleRate : 0.0;
    return info;
}

void AudioSource::decode(const std::string& utf8Path, uint32_t sampleRate,
                         const std::function<void(const DecodeFormat&)>& begin,
                         const std::function<void(const float*, int64_t)>& take) {
    if (sampleRate == 0) throw std::invalid_argument("sample rate must be positive");

    DecodeFormat format;
    ma_format nativeFormat = ma_format_unknown;
    {
        const ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0);
        Decoder probe(utf8Path, config);
        format.fileChannels = probe.get()->outputChannels;
        format.fileRate = probe.get()->outputSampleRate;
        nativeFormat = probe.get()->outputFormat;
    }
    format.channels = format.fileChannels >= 2 ? 2 : 1;
    // Neither resampled nor mixed down: the samples are the file's integers, scaled.
    if (format.fileRate == sampleRate && format.fileChannels <= 2) {
        if (nativeFormat == ma_format_s16) format.integerBits = 16;
        if (nativeFormat == ma_format_s24 || nativeFormat == ma_format_s32) format.integerBits = 24;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, format.channels, sampleRate);
    config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
    Decoder decoder(utf8Path, config);

    ma_uint64 expected = 0;
    ma_decoder_get_length_in_pcm_frames(decoder.get(), &expected);
    format.expectedFrames = static_cast<int64_t>(expected);
    begin(format);

    constexpr ma_uint64 kChunk = 16384;
    std::vector<float> interleaved(kChunk * format.channels);
    for (;;) {
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(decoder.get(), interleaved.data(), kChunk, &read);
        if (read > 0) take(interleaved.data(), static_cast<int64_t>(read));
        if (read < kChunk || result != MA_SUCCESS) break;
    }
}

std::shared_ptr<AudioSource> AudioSource::load(const std::string& utf8Path, uint32_t sampleRate) {
    auto source = std::shared_ptr<AudioSource>(new AudioSource());
    std::vector<float>& data = source->data_;
    // Decoded in place: each channel straight into its part of the planar
    // buffer, made as long as the decoder expects (longer if it was wrong).
    uint32_t channels = 1;
    int64_t capacity = 0;  // frames each channel has room for
    int64_t frames = 0;
    const auto grow = [&](int64_t needed) {
        const int64_t room = std::max<int64_t>(needed, capacity > 0 ? capacity * 2 : int64_t{1} << 18);
        std::vector<float> larger(static_cast<size_t>(room) * channels);
        for (uint32_t c = 0; c < channels; ++c) {
            const auto from = data.begin() + static_cast<ptrdiff_t>(c * static_cast<size_t>(capacity));
            std::copy(from, from + frames, larger.begin() + static_cast<ptrdiff_t>(c * static_cast<size_t>(room)));
        }
        data = std::move(larger);
        capacity = room;
    };
    decode(
        utf8Path, sampleRate,
        [&](const DecodeFormat& format) {
            channels = format.channels;
            source->fileSampleRate_ = format.fileRate;
            if (format.expectedFrames > 0) grow(format.expectedFrames);
        },
        [&](const float* interleaved, int64_t read) {
            if (frames + read > capacity) grow(frames + read);
            for (uint32_t c = 0; c < channels; ++c) {
                float* out = data.data() + static_cast<size_t>(c) * static_cast<size_t>(capacity) + frames;
                for (int64_t i = 0; i < read; ++i) out[i] = interleaved[i * channels + c];
            }
            frames += read;
        });
    if (frames == 0) throw std::runtime_error("Audio file '" + utf8Path + "' contains no audio");
    if (channels > 1 && capacity != frames) {  // the second channel down to where it belongs (no overlap: frames < capacity)
        std::copy(data.begin() + capacity, data.begin() + capacity + frames, data.begin() + frames);
    }
    data.resize(static_cast<size_t>(frames) * channels);

    source->path_ = utf8Path;
    source->channels_ = channels;
    source->sampleRate_ = sampleRate;
    source->frames_ = frames;
    source->buildPeaks();
    return source;
}

std::shared_ptr<AudioSource> AudioSource::fromSamples(const std::string& utf8Path, uint32_t sampleRate,
                                                      uint32_t channels, std::vector<float> planar, bool peaks) {
    if (sampleRate == 0) throw std::invalid_argument("sample rate must be positive");
    if (channels < 1 || channels > 2) throw std::invalid_argument("one or two channels");
    auto source = std::shared_ptr<AudioSource>(new AudioSource());
    source->path_ = utf8Path;
    source->channels_ = channels;
    source->sampleRate_ = sampleRate;
    source->fileSampleRate_ = sampleRate;
    source->frames_ = static_cast<int64_t>(planar.size() / channels);
    if (source->frames_ == 0) throw std::runtime_error("Audio file '" + utf8Path + "' contains no audio");
    planar.resize(static_cast<size_t>(source->frames_) * channels);
    source->data_ = std::move(planar);
    if (peaks) {
        source->buildPeaks();
    } else {
        source->peaks_.assign(kNumPeakLevels, {});
    }
    return source;
}

std::shared_ptr<AudioSource> AudioSource::streamed(const std::string& utf8Path, uint32_t sampleRate, uint32_t fileRate,
                                                   std::shared_ptr<StreamedAudio> stream,
                                                   std::vector<std::vector<float>> peaks) {
    if (!stream) throw std::invalid_argument("no stream");
    auto source = std::shared_ptr<AudioSource>(new AudioSource());
    source->path_ = utf8Path;
    source->channels_ = stream->channels();
    source->sampleRate_ = sampleRate;
    source->fileSampleRate_ = fileRate;
    source->frames_ = stream->frames();
    if (source->frames_ == 0) throw std::runtime_error("Audio file '" + utf8Path + "' contains no audio");
    source->stream_ = std::move(stream);
    peaks.resize(kNumPeakLevels);
    source->peaks_ = std::move(peaks);
    return source;
}

bool AudioSource::read(uint32_t channel, int64_t start, int64_t count, float* out) const {
    if (count <= 0) return true;
    if (channel >= channels_) {
        std::fill_n(out, count, 0.f);
        return true;
    }
    if (stream_) return stream_->read(channel, start, count, out);
    const float* samples = channelData(channel);
    for (int64_t i = 0; i < count; ++i) {
        const int64_t k = start + i;
        out[i] = k >= 0 && k < frames_ ? samples[k] : 0.f;
    }
    return true;
}

int AudioSource::samplesPerPeak(int level) { return kBaseSamplesPerPeak << (2 * level); }

int64_t AudioSource::numPeaks(int level) const {
    if (level < 0 || level >= kNumPeakLevels) return 0;
    return static_cast<int64_t>(peaks_[level].size() / (static_cast<size_t>(channels_) * 2));
}

const float* AudioSource::peaks(int level) const {
    if (level < 0 || level >= kNumPeakLevels) return nullptr;
    return peaks_[level].data();
}

namespace {

// Each coarser level combines four peaks of the one below.
void coarserLevels(std::vector<std::vector<float>>& peaks, uint32_t channels) {
    for (int level = 1; level < AudioSource::kNumPeakLevels; ++level) {
        const auto& prev = peaks[level - 1];
        const int64_t prevCount = static_cast<int64_t>(prev.size() / (static_cast<size_t>(channels) * 2));
        const int64_t count = (prevCount + 3) / 4;
        auto& cur = peaks[level];
        cur.resize(static_cast<size_t>(count) * channels * 2);
        for (int64_t p = 0; p < count; ++p) {
            for (uint32_t c = 0; c < channels; ++c) {
                float lo = std::numeric_limits<float>::max();
                float hi = std::numeric_limits<float>::lowest();
                for (int64_t q = p * 4; q < std::min(prevCount, p * 4 + 4); ++q) {
                    const size_t at = (static_cast<size_t>(q) * channels + c) * 2;
                    lo = std::min(lo, prev[at]);
                    hi = std::max(hi, prev[at + 1]);
                }
                const size_t at = (static_cast<size_t>(p) * channels + c) * 2;
                cur[at] = lo;
                cur[at + 1] = hi;
            }
        }
    }
}

}  // namespace

void AudioSource::buildPeaks() {
    peaks_.assign(kNumPeakLevels, {});
    const int64_t spp = kBaseSamplesPerPeak;
    const int64_t count0 = (frames_ + spp - 1) / spp;
    auto& level0 = peaks_[0];
    level0.resize(static_cast<size_t>(count0) * channels_ * 2);
    for (uint32_t c = 0; c < channels_; ++c) {
        const float* samples = channelData(c);
        for (int64_t p = 0; p < count0; ++p) {
            const int64_t begin = p * spp;
            const int64_t end = std::min(frames_, begin + spp);
            float lo = std::numeric_limits<float>::max();
            float hi = std::numeric_limits<float>::lowest();
            for (int64_t i = begin; i < end; ++i) {
                lo = std::min(lo, samples[i]);
                hi = std::max(hi, samples[i]);
            }
            const size_t at = (static_cast<size_t>(p) * channels_ + c) * 2;
            level0[at] = lo;
            level0[at + 1] = hi;
        }
    }
    coarserLevels(peaks_, channels_);
}

// ---------------------------------------------------------------------------
// PeakBuilder

PeakBuilder::PeakBuilder(uint32_t channels) : channels_(std::max<uint32_t>(channels, 1)), current_(channels_ * 2) {}

void PeakBuilder::add(const float* interleaved, int64_t frames) {
    constexpr int64_t spp = AudioSource::kBaseSamplesPerPeak;
    for (int64_t i = 0; i < frames; ++i) {
        if (inPeak_ == 0) {
            for (uint32_t c = 0; c < channels_; ++c) {
                current_[c * 2] = std::numeric_limits<float>::max();
                current_[c * 2 + 1] = std::numeric_limits<float>::lowest();
            }
        }
        for (uint32_t c = 0; c < channels_; ++c) {
            const float v = interleaved[i * channels_ + c];
            current_[c * 2] = std::min(current_[c * 2], v);
            current_[c * 2 + 1] = std::max(current_[c * 2 + 1], v);
        }
        if (++inPeak_ == spp) {
            level0_.insert(level0_.end(), current_.begin(), current_.end());
            inPeak_ = 0;
        }
    }
}

void PeakBuilder::addPlanar(const float* const* channels, int64_t frames) {
    constexpr int64_t kChunk = 1024;
    float interleaved[kChunk * 2];
    for (int64_t at = 0; at < frames; at += kChunk) {
        const int64_t n = std::min(kChunk, frames - at);
        for (int64_t i = 0; i < n; ++i)
            for (uint32_t c = 0; c < channels_; ++c) interleaved[i * channels_ + c] = channels[c][at + i];
        add(interleaved, n);
    }
}

std::vector<std::vector<float>> PeakBuilder::finish() {
    if (inPeak_ > 0) {
        level0_.insert(level0_.end(), current_.begin(), current_.end());
        inPeak_ = 0;
    }
    std::vector<std::vector<float>> peaks(AudioSource::kNumPeakLevels);
    peaks[0] = std::move(level0_);
    level0_.clear();
    coarserLevels(peaks, channels_);
    return peaks;
}

}  // namespace sub
