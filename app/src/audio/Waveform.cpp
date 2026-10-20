#include "audio/Waveform.h"

#include "AudioSource.h"

#include <algorithm>
#include <utility>

namespace sub::app {

Waveform::Waveform(std::shared_ptr<const sub::AudioSource> source) : source_(std::move(source)) {}

QString Waveform::path() const { return source_ ? QString::fromStdString(source_->path()) : QString(); }

qint64 Waveform::frames() const { return source_ ? source_->frames() : 0; }

int Waveform::channels() const { return source_ ? static_cast<int>(source_->channels()) : 0; }

int Waveform::sampleRate() const { return source_ ? static_cast<int>(source_->sampleRate()) : 0; }

double Waveform::duration() const { return source_ ? source_->duration() : 0.0; }

bool Waveform::resident() const { return !source_ || source_->resident(); }

const float* Waveform::channelData(int channel) const {
    if (!source_ || channel < 0 || channel >= channels()) return nullptr;
    return source_->channelData(static_cast<uint32_t>(channel));
}

bool Waveform::read(int channel, qint64 start, qint64 count, float* out) const {
    if (count <= 0) return true;
    if (!source_ || channel < 0 || channel >= channels()) {
        std::fill_n(out, count, 0.f);
        return false;
    }
    return source_->read(static_cast<uint32_t>(channel), start, count, out);
}

std::vector<float> Waveform::samples(int channel, bool* ok) const {
    std::vector<float> out(static_cast<size_t>(std::max<qint64>(0, frames())));
    const bool read = this->read(channel, 0, frames(), out.data());
    if (ok) *ok = read;
    return out;
}

int Waveform::peakLevels() const { return source_ ? source_->numPeakLevels() : 0; }

int Waveform::samplesPerPeak(int level) { return sub::AudioSource::samplesPerPeak(level); }

qint64 Waveform::peakCount(int level) const {
    if (!source_ || level < 0 || level >= source_->numPeakLevels()) return 0;
    return source_->numPeaks(level);
}

const float* Waveform::peaks(int level) const {
    if (!source_ || level < 0 || level >= source_->numPeakLevels()) return nullptr;
    return source_->peaks(level);
}

}  // namespace sub::app
