#include "builtin/SnarlTables.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <numbers>

#include "builtin/Dsp.h"
#include "builtin/SnarlTablesData.h"

#include <kfr/dft.hpp>

namespace sub::snarl {
namespace {

constexpr double kPi = std::numbers::pi;

// A cycle as its harmonics: x(t) = sum over h of Re(c[h] e^(2 pi i h t)), t the phase (0..1); c[0] (DC) unused.
using Spectrum = std::vector<std::complex<double>>;

// Wave's shapes as they were drawn (0 sine, 1 triangle, 2 saw, 3 square, 4 a pulse of 20 %, its DC taken out
// and its peak at 1), from their Fourier series.
Spectrum basicShape(int shape) {
    constexpr double width = 0.2;
    Spectrum c(Wavetables::kTopHarmonics + 1);
    for (int h = 1; h <= Wavetables::kTopHarmonics; ++h) {
        const bool odd = h % 2 == 1;
        std::complex<double>& v = c[static_cast<size_t>(h)];
        switch (shape) {
        case 0: v = h == 1 ? std::complex<double>(0.0, -1.0) : 0.0; break;            // sin 2 pi t
        case 1: v = odd ? -8.0 / (kPi * kPi * h * h) : 0.0; break;                       // 1 - 4 |t - 1/2|
        case 2: v = std::complex<double>(0.0, 2.0 / (kPi * h)); break;                  // 2 t - 1
        case 3: v = odd ? std::complex<double>(0.0, -4.0 / (kPi * h)) : 0.0; break;    // +1, then -1
        default: {                                                                       // +1 for 20 %, then -1
            const double a = 2.0 / (kPi * h) * std::sin(2.0 * kPi * h * width);
            const double b = 2.0 / (kPi * h) * (1.0 - std::cos(2.0 * kPi * h * width));
            v = std::complex<double>(a, -b) / (2.0 - 2.0 * width);
        }
        }
    }
    return c;
}

// A real FFT of an AKWF cycle's length (600 samples: KFR's DFT, any even size), unscaled both ways.
class CycleFft {
public:
    static constexpr int kSize = akwf::kLength;

    CycleFft() : plan_(kSize, kfr::dft_pack_format::CCs), temp_(plan_.temp_size), bins_(kSize / 2 + 1), time_(kSize) {}

    // An AKWF cycle's harmonics (its DFT; the Nyquist bin left out).
    Spectrum harmonics(const int16_t* samples) {
        for (int i = 0; i < kSize; ++i) time_[static_cast<size_t>(i)] = samples[i] / 32768.0;
        plan_.execute(reinterpret_cast<kfr::complex<double>*>(bins_.data()), time_.data(), temp_.data());
        Spectrum c(Wavetables::kTopHarmonics + 1);
        for (int h = 1; h < kSize / 2; ++h) c[static_cast<size_t>(h)] = 2.0 / kSize * bins_[static_cast<size_t>(h)];
        return c;
    }

    // How well `c` correlates with `previous` turned by each of kSize shifts of a sample: the inverse transform
    // of their cross-spectrum. The shift that correlates best.
    int bestShift(const Spectrum& c, const Spectrum& previous) {
        std::fill(bins_.begin(), bins_.end(), std::complex<double>());
        for (int h = 1; h < kSize / 2; ++h)
            bins_[static_cast<size_t>(h)] = c[static_cast<size_t>(h)] * std::conj(previous[static_cast<size_t>(h)]);
        plan_.execute(time_.data(), reinterpret_cast<const kfr::complex<double>*>(bins_.data()), temp_.data(),
                      kfr::cinvert_t{});
        return static_cast<int>(std::max_element(time_.begin(), time_.end()) - time_.begin());
    }

private:
    kfr::dft_plan_real<double> plan_;
    std::vector<kfr::u8> temp_;
    std::vector<std::complex<double>> bins_;
    std::vector<double> time_;
};

double rms(const Spectrum& c) {
    double sum = 0.0;
    for (const auto& v : c) sum += std::norm(v) / 2.0;
    return std::sqrt(sum);
}

// `c` shifted in time (its cycle turned) by the shift that correlates best with `previous` (one of its own
// samples' worth at a time).
void align(Spectrum& c, const Spectrum& previous, CycleFft& fft) {
    const int best = fft.bestShift(c, previous);
    for (size_t h = 1; h < c.size(); ++h)
        c[h] *= std::polar(1.0, 2.0 * kPi * static_cast<double>(h) * best / CycleFft::kSize);
}

// `alpha` of the way from `a` to `b`: each harmonic's level linearly, its phase the short way round.
Spectrum blend(const Spectrum& a, const Spectrum& b, double alpha) {
    Spectrum c(a.size());
    for (size_t h = 1; h < a.size(); ++h) {
        const double level = (1.0 - alpha) * std::abs(a[h]) + alpha * std::abs(b[h]);
        const double from = std::arg(a[h]);
        const double turn = std::remainder(std::arg(b[h]) - from, 2.0 * kPi);
        c[h] = std::polar(level, from + alpha * turn);
    }
    return c;
}

std::vector<Spectrum> framesOf(const std::vector<Spectrum>& keys) {
    std::vector<Spectrum> frames;
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        for (int s = 0; s < Wavetables::kStepsBetween; ++s)
            frames.push_back(blend(keys[k], keys[k + 1], static_cast<double>(s) / Wavetables::kStepsBetween));
    }
    frames.push_back(keys.back());
    return frames;
}

std::vector<Spectrum> keysOf(int table) {
    std::vector<Spectrum> keys;
    if (table == static_cast<int>(Table::Basic)) {
        for (int shape = 0; shape < 5; ++shape) keys.push_back(basicShape(shape));
        return keys;
    }
    const akwf::Table& source = akwf::kTables[table - 1];
    CycleFft fft;
    for (int w = 0; w < akwf::kWaves; ++w) {
        Spectrum c = fft.harmonics(source.samples[w]);
        const double level = rms(c);
        if (level > 0.0) {
            for (auto& v : c) v *= Wavetables::kLoudness / level;
        }
        if (!keys.empty()) align(c, keys.back(), fft);
        keys.push_back(std::move(c));
    }
    return keys;
}

}  // namespace

const std::vector<std::string>& tableLabels() {
    static const std::vector<std::string> kLabels = [] {
        std::vector<std::string> labels = {"Basic"};
        for (const akwf::Table& table : akwf::kTables) labels.emplace_back(table.name);
        return labels;
    }();
    return kLabels;
}

int Wavetables::levelLength(int level) noexcept { return std::max(64, 4 * levelHarmonics(level)); }

const Wavetables& Wavetables::instance() {
    static const Wavetables tables;
    return tables;
}

Wavetables::Wavetables() {
    static_assert(std::size(akwf::kTables) == kTables - 1, "a table for each of the AKWF folders, and Basic");
    for (int table = 0; table < kTables; ++table) {
        const std::vector<Spectrum> frames = framesOf(keysOf(table));
        Data& data = tables_[static_cast<size_t>(table)];
        data.frames = static_cast<int>(frames.size());
        for (int level = 0; level < kLevels; ++level) {
            const int length = levelLength(level);
            const int harmonics = std::min(levelHarmonics(level), length / 2 - 1);
            kfr::dft_plan_real<float> plan(static_cast<size_t>(length), kfr::dft_pack_format::CCs);
            std::vector<kfr::u8> temp(plan.temp_size);
            std::vector<std::complex<float>> bins(static_cast<size_t>(length / 2 + 1));
            std::vector<float> cycle(static_cast<size_t>(length));
            std::vector<float>& out = data.levels[static_cast<size_t>(level)];
            out.assign(frames.size() * static_cast<size_t>(length + 4), 0.f);
            for (size_t f = 0; f < frames.size(); ++f) {
                // The unscaled inverse sums each bin and its mirror: half of c[h] in bin h is c[h] e^(2 pi i h t)'s
                // real part.
                std::fill(bins.begin(), bins.end(), std::complex<float>());
                for (int h = 1; h <= harmonics; ++h)
                    bins[static_cast<size_t>(h)] = std::complex<float>(frames[f][static_cast<size_t>(h)] / 2.0);
                plan.execute(cycle.data(), reinterpret_cast<const kfr::complex<float>*>(bins.data()), temp.data(),
                             kfr::cinvert_t{});
                float* at = out.data() + f * static_cast<size_t>(length + 4);
                at[0] = cycle[static_cast<size_t>(length - 1)];
                std::copy(cycle.begin(), cycle.end(), at + 1);
                for (int i = 0; i < 3; ++i) at[1 + length + i] = cycle[static_cast<size_t>(i)];
            }
        }
    }
}

float Wavetables::sample(const Data& data, int level, int frame, double t) const noexcept {
    const int length = levelLength(level);
    const float* cycle = data.levels[static_cast<size_t>(level)].data() + static_cast<size_t>(frame) * (length + 4) + 1;
    const double x = t * length;
    const int i = std::min(static_cast<int>(x), length - 1);
    const auto fraction = static_cast<float>(x - i);
    return dsp::hermite(cycle[i - 1], cycle[i], cycle[i + 1], cycle[i + 2], fraction);
}

float Wavetables::read(int table, double position, double t, double harmonicLimit) const noexcept {
    const Data& data = tables_[static_cast<size_t>(std::clamp(table, 0, kTables - 1))];
    const double place = std::clamp(position, 0.0, 1.0) * (data.frames - 1);
    const int frame = std::min(static_cast<int>(place), std::max(0, data.frames - 2));
    const double between = std::min(place - frame, 1.0);
    // The levels either side of the note's limit: the fuller one has its harmonics up to twice it at most. How far
    // it lies from one to the other, linearly in frequency (the octave's exponent and mantissa: no logarithm).
    const auto over = static_cast<float>(kTopHarmonics / std::max(harmonicLimit, 1e-6));
    int level = 0;
    float toNext = 0.f;
    if (over >= static_cast<float>(1 << (kLevels - 1))) {
        level = kLevels - 1;
    } else if (over > 1.f) {
        const auto bits = std::bit_cast<uint32_t>(over);
        level = static_cast<int>(bits >> 23) - 127;
        toNext = std::bit_cast<float>((bits & 0x007fffffu) | 0x3f800000u) - 1.f;
    }
    const auto at = [&](int l) {
        const float a = sample(data, l, frame, t);
        return between > 0.0 ? a + static_cast<float>(between) * (sample(data, l, frame + 1, t) - a) : a;
    };
    const float fuller = at(level);
    return toNext > 0.f ? fuller + toNext * (at(level + 1) - fuller) : fuller;
}

std::vector<float> Wavetables::cycle(int table, double position, int points) const {
    std::vector<float> values(static_cast<size_t>(std::max(0, points)));
    for (int p = 0; p < points; ++p)
        values[static_cast<size_t>(p)] = read(table, position, static_cast<double>(p) / points, kTopHarmonics);
    return values;
}

}  // namespace sub::snarl
