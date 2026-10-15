#include "audio/AudioAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

#include <kiss_fftr.h>

#include "harmony/ChordInference.h"

namespace sub::intelligence::audio {

namespace {

using harmony::pitchClass;
using Profile = std::array<float, 12>;

// Decimating: to about kAnalysisRate, by a whole factor, through a windowed-sinc
// low-pass (Blackman) of kTapsPerFactor taps per unit of the factor, cutting at
// kCutoff of the new Nyquist frequency.
constexpr double kAnalysisRate = 11025.0;
constexpr double kMinSampleRate = 8000.0;
constexpr int kTapsPerFactor = 10;
constexpr double kCutoff = 0.84;
constexpr double kSilence = 1e-4;  // a peak under this (-80 dBFS) is silence

// The onsets: 512-point frames 64 samples apart (172 a second at 11 025 Hz), the
// spectrum's energy in bands a quarter-octave or more wide from 43 Hz up, its
// rise from frame to frame (log-compressed, kOnsetFloor of the mean band energy
// counting as nothing) summed over the bands; less its mean over kOnsetMean
// seconds about it, what is left of it above zero.
constexpr int kOnsetWindow = 512;
constexpr int kOnsetHop = 64;
constexpr double kOnsetFloor = 1e-3;
constexpr double kOnsetMean = 0.5;

// The tempo: the comb's autocorrelation lags go up to kCombSpan of the onsets
// (less for the coarse search: kCoarseSeconds), a candidate needs two of them.
// The coarse search steps kCoarseStep (relative), the fine one kFineStep over
// kFineSpan either side. The prior: a normal over log2 of the tempo, centred on
// kPriorBpm, kPriorOctaves wide. A candidate whose off-beats are kOffBeatRatio
// as strong as its beats (or more) loses, by up to kOffBeatPenalty.
constexpr double kMinTempoSeconds = 1.0;
constexpr double kCombSpan = 0.6;
constexpr double kCoarseSeconds = 6.0;
constexpr double kCoarseStep = 0.001;
constexpr double kFineStep = 0.00002;
constexpr double kFineSpan = 0.002;
constexpr double kPriorBpm = 135.0;
constexpr double kPriorOctaves = 0.5;
constexpr int kTempoCandidates = 5;
constexpr double kOffBeatRatio = 0.6;
constexpr double kOffBeatPenalty = 0.6;
// The confidence: how far the best comb stands above the median one, in
// kConfidenceScale's.
constexpr double kConfidenceScale = 0.3;
// Rounding: to a whole BPM within kWholeBpm of it; to the tempo a short file's
// length makes, if it is kBarBeats × 2^n beats long (up to kMaxLoopBeats) within
// kLoopTolerance (relative) and kLoopBeatTolerance beats.
constexpr double kWholeBpm = 0.15;
constexpr double kBarBeats = 4.0;
constexpr double kMaxLoopBeats = 64.0;
constexpr double kLoopTolerance = 0.02;
constexpr double kLoopBeatTolerance = 0.25;
// A file that is whole bars long at a tempo is likely a loop at that tempo:
// such a candidate's score counts this much more.
constexpr double kLoopBonus = 1.5;
// Where the onsets put a hit, against where it is: 13 ms late (its frames see
// it once it is well into them), measured on drum loops.
constexpr double kOnsetLead = -0.013;

// The chroma: 4096-point frames 2048 samples apart (186 ms at 11 025 Hz). A peak
// stands kPeakProminence times over the spectrum's mean kPeakNeighbourhood bins
// either side, and within kPeakRange of the frame's loudest. Pitch classes from
// kLowHz to kHighHz, the bass from kBassLowHz to kBassHighHz; a peak further
// than kInTune semitones from the tuning's notes is no note. A peak's
// harmonics 3, 5, 6 and 7 (those of other pitch classes), kHarmonicShare of 1/h
// of it, are taken from the peaks they fall on (within kHarmonicMatch,
// relative), down to kOwnFloor of them. Each pitch class's sum is a share of
// the frame's loudest, to kChromaPower (none under kChromaFloor).
constexpr int kChromaWindow = 4096;
constexpr int kChromaHop = 2048;
constexpr double kPeakProminence = 3.0;
constexpr int kPeakNeighbourhood = 16;
constexpr double kPeakRange = 1e-3;
constexpr double kLowHz = 55.0;
constexpr double kHighHz = 2000.0;
constexpr double kBassLowHz = 30.0;
constexpr double kBassHighHz = 250.0;
constexpr double kInTune = 0.4;
constexpr double kHarmonicMatch = 0.015;
constexpr std::array<int, 4> kHarmonics{3, 5, 6, 7};
constexpr double kHarmonicShare = 0.8;  // (an instrument's harmonics are seldom all 1/h)
constexpr double kOwnFloor = 0.1;       // (a note played on another's harmonic may beat with it and all but vanish)
constexpr double kChromaPower = 0.5;    // (a loud, doubled bass doesn't drown the other notes: about a note a note)
constexpr double kChromaFloor = 0.2;
constexpr double kBassShare = 0.25;  // the bass: the lowest peak this strong (of the bass range's loudest)
// A frame counts by its tonality, the share of its power in its peaks between
// kLowHz and kHighHz (none at kTonalFrom, fully from kTonalFull), and by its
// level against the file's loud frames (its kLoudPercentile): none kQuietDb
// under, fully kFullDb under.
constexpr double kTonalFrom = 0.15;
constexpr double kTonalFull = 0.5;
constexpr double kLoudPercentile = 0.95;
constexpr double kQuietDb = -40.0;
constexpr double kFullDb = -20.0;
constexpr double kMainLobe = 1.5;  // a Hann window's sinusoid: its power over its peak bin's
// A step with less than this of a note sounding (note-beats a beat) has none.
constexpr double kMinStepNotes = 0.25;
// The grid without a tempo: as if at 120 BPM, a step every quarter second.
constexpr double kFreeBeatsPerSecond = 2.0;
constexpr double kStep = 0.5;  // beats: an eighth note

// KISS FFT's real transforms, freed when done.
struct RealFft {
    explicit RealFft(int size, bool inverse = false) : cfg(kiss_fftr_alloc(size, inverse ? 1 : 0, nullptr, nullptr)) {}
    ~RealFft() { kiss_fftr_free(cfg); }
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;
    kiss_fftr_cfg cfg;
};

std::vector<float> hann(int size) {
    std::vector<float> w(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i)
        w[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / size));
    return w;
}

// The channels mixed to mono and decimated to about kAnalysisRate. A block of
// the mix at a time (kDecimateBlock outputs' worth), the filter's taps padded
// to a multiple of kLanes and summed in kLanes running sums (which compilers
// vectorize).
constexpr size_t kDecimateBlock = 4096;
constexpr int kLanes = 8;
std::vector<float> decimate(const float* const* channels, int channelCount, size_t frames, double sampleRate,
                            double& rate) {
    const int factor = std::max(1, static_cast<int>(std::lround(sampleRate / kAnalysisRate)));
    rate = sampleRate / factor;
    const auto d = static_cast<size_t>(factor);
    const size_t out = (frames + d - 1) / d;
    std::vector<float> y(out, 0.f);
    const float gain = 1.f / static_cast<float>(channelCount);
    if (factor == 1) {
        for (int c = 0; c < channelCount; ++c) {
            for (size_t i = 0; i < frames; ++i) y[i] += gain * channels[c][i];
        }
        return y;
    }
    const int half = kTapsPerFactor * factor / 2;
    const int taps = 2 * half + 1;
    const auto padded = static_cast<size_t>((taps + kLanes - 1) / kLanes * kLanes);
    std::vector<float> h(padded, 0.f);
    const double cutoff = kCutoff * 0.5 / factor;  // cycles a sample
    double sum = 0.0;
    for (int j = 0; j < taps; ++j) {
        const double x = j - half;
        const double sinc =
            x == 0 ? 2.0 * cutoff : std::sin(2.0 * std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
        const double a = 2.0 * std::numbers::pi * j / (taps - 1);
        const double blackman = 0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2.0 * a);
        h[static_cast<size_t>(j)] = static_cast<float>(sinc * blackman);
        sum += sinc * blackman;
    }
    for (float& t : h) t = static_cast<float>(t / sum * gain);

    std::vector<float> mono(kDecimateBlock * d + padded);
    for (size_t n0 = 0; n0 < out; n0 += kDecimateBlock) {
        const size_t n1 = std::min(out, n0 + kDecimateBlock);
        // The mix from the first output's first tap to past the last's.
        const auto from = static_cast<std::ptrdiff_t>(n0 * d) - half;
        const size_t length = (n1 - 1 - n0) * d + padded;
        std::fill(mono.begin(), mono.begin() + static_cast<std::ptrdiff_t>(length), 0.f);
        const std::ptrdiff_t lo = std::max<std::ptrdiff_t>(0, from);
        const std::ptrdiff_t hi =
            std::min(static_cast<std::ptrdiff_t>(frames), from + static_cast<std::ptrdiff_t>(length));
        for (int c = 0; c < channelCount; ++c) {
            for (std::ptrdiff_t i = lo; i < hi; ++i) mono[static_cast<size_t>(i - from)] += channels[c][i];
        }
        for (size_t n = n0; n < n1; ++n) {
            const float* x = mono.data() + (n - n0) * d;
            std::array<float, kLanes> acc{};
            for (size_t j = 0; j < padded; j += kLanes) {
                for (size_t l = 0; l < kLanes; ++l) acc[l] += h[j + l] * x[j + l];
            }
            float total = 0.f;
            for (const float v : acc) total += v;
            y[n] = total;
        }
    }
    return y;
}

float peakOf(const std::vector<float>& x) {
    float peak = 0.f;
    for (const float v : x) peak = std::max(peak, std::abs(v));
    return peak;
}

// The `window`'s length of `x` (its first `count` samples) from `from` on
// (zeros outside them), windowed.
void frameAt(const std::vector<float>& x, size_t count, std::ptrdiff_t from, const std::vector<float>& window,
             std::vector<float>& out) {
    const auto size = static_cast<std::ptrdiff_t>(window.size());
    for (std::ptrdiff_t i = 0; i < size; ++i) {
        const std::ptrdiff_t at = from + i;
        const bool inside = at >= 0 && at < static_cast<std::ptrdiff_t>(count);
        out[static_cast<size_t>(i)] = inside ? x[static_cast<size_t>(at)] * window[static_cast<size_t>(i)] : 0.f;
    }
}

// --- The tempo ---------------------------------------------------------------

// The onset envelope of the first `count` samples, `rate` a second.
std::vector<double> onsets(const std::vector<float>& x, size_t count, double rate) {
    const size_t frames = count / kOnsetHop + 1;  // each ending where its time is: an onset at 0 is seen coming in
    // Bands: from bin 2 (43 Hz) up, each a quarter-octave or a bin wider than the last.
    std::vector<int> edges{2};
    while (edges.back() < kOnsetWindow / 2) {
        const int wider = std::max(edges.back() + 1, static_cast<int>(std::lround(edges.back() * 1.19)));
        edges.push_back(std::min(kOnsetWindow / 2 + 1, wider));
    }
    const size_t bands = edges.size() - 1;

    const std::vector<float> window = hann(kOnsetWindow);
    const RealFft fft(kOnsetWindow);
    std::vector<float> frame(kOnsetWindow);
    std::vector<kiss_fft_cpx> spectrum(kOnsetWindow / 2 + 1);
    std::vector<float> energy(frames * bands);
    double mean = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        frameAt(x, count, static_cast<std::ptrdiff_t>(i) * kOnsetHop - kOnsetWindow, window, frame);
        kiss_fftr(fft.cfg, frame.data(), spectrum.data());
        for (size_t b = 0; b < bands; ++b) {
            double e = 0.0;
            for (int k = edges[b]; k < edges[b + 1]; ++k) {
                const kiss_fft_cpx& c = spectrum[static_cast<size_t>(k)];
                e += double(c.r) * c.r + double(c.i) * c.i;
            }
            energy[i * bands + b] = static_cast<float>(e);
            mean += e;
        }
    }
    mean /= static_cast<double>(frames * bands);
    std::vector<double> flux(frames, 0.0);
    if (mean <= 0.0) return flux;
    const double floor = kOnsetFloor * mean;
    for (size_t i = 1; i < frames; ++i) {
        double rise = 0.0;
        for (size_t b = 0; b < bands; ++b) {
            const double now = std::log1p(energy[i * bands + b] / floor);
            const double before = std::log1p(energy[(i - 1) * bands + b] / floor);
            rise += std::max(0.0, now - before);
        }
        flux[i] = rise;
    }
    // Less the local mean, above zero.
    const auto reach = static_cast<size_t>(kOnsetMean * rate / kOnsetHop / 2);
    std::vector<double> sums(frames + 1, 0.0);
    for (size_t i = 0; i < frames; ++i) sums[i + 1] = sums[i] + flux[i];
    std::vector<double> envelope(frames);
    for (size_t i = 0; i < frames; ++i) {
        const size_t a = i > reach ? i - reach : 0, b = std::min(frames, i + reach + 1);
        envelope[i] = std::max(0.0, flux[i] - (sums[b] - sums[a]) / static_cast<double>(b - a));
    }
    return envelope;
}

// The autocorrelation of x less its mean, each lag divided by how many products
// it sums (unbiased), and by lag 0's.
std::vector<double> autocorrelation(const std::vector<double>& x) {
    const size_t n = x.size();
    double mean = 0.0;
    for (const double v : x) mean += v;
    mean /= static_cast<double>(n);
    size_t size = 2;
    while (size < 2 * n) size *= 2;
    std::vector<float> padded(size, 0.f);
    for (size_t i = 0; i < n; ++i) padded[i] = static_cast<float>(x[i] - mean);
    std::vector<kiss_fft_cpx> spectrum(size / 2 + 1);
    {
        const RealFft forward(static_cast<int>(size));
        kiss_fftr(forward.cfg, padded.data(), spectrum.data());
    }
    for (kiss_fft_cpx& c : spectrum) {
        c.r = c.r * c.r + c.i * c.i;
        c.i = 0.f;
    }
    const RealFft inverse(static_cast<int>(size), true);
    kiss_fftri(inverse.cfg, spectrum.data(), padded.data());
    std::vector<double> ac(n, 0.0);
    if (padded[0] <= 0.f) return ac;
    for (size_t lag = 0; lag < n; ++lag) ac[lag] = padded[lag] / static_cast<double>(n - lag);
    const double zero = ac[0];
    for (double& v : ac) v /= zero;
    return ac;
}

double at(const std::vector<double>& x, double position) {
    if (position < 0.0) return 0.0;
    const auto i = static_cast<size_t>(position);
    if (i + 1 >= x.size()) return i < x.size() ? x[i] : 0.0;
    const double f = position - static_cast<double>(i);
    return x[i] * (1.0 - f) + x[i + 1] * f;
}

// The mean of the autocorrelation at the multiples of `period` up to `maxLag`;
// none (NaN) under two of them.
double comb(const std::vector<double>& ac, double period, double maxLag) {
    const int count = static_cast<int>(maxLag / period);
    if (count < 2) return std::nan("");
    double sum = 0.0;
    for (int k = 1; k <= count; ++k) sum += at(ac, k * period);
    return sum / count;
}

// The comb of onsets that are a loop `length` frames long (wrapping round),
// `beats` beats: the mean of their autocorrelation at every beat but the first.
double loopComb(const std::vector<double>& envelope, double length, int beats) {
    const auto n = std::min(envelope.size(), static_cast<size_t>(std::lround(length)));
    if (n < 2 || beats < 2) return 0.0;
    double mean = 0.0;
    for (size_t i = 0; i < n; ++i) mean += envelope[i];
    mean /= static_cast<double>(n);
    const auto lagged = [&](size_t lag) {
        double sum = 0.0;
        for (size_t i = 0; i < n; ++i) sum += (envelope[i] - mean) * (envelope[(i + lag) % n] - mean);
        return sum;
    };
    const double zero = lagged(0);
    if (zero <= 0.0) return 0.0;
    double sum = 0.0;
    for (int k = 1; k < beats; ++k) {
        const double lag = static_cast<double>(n) * k / beats;
        const auto below = static_cast<size_t>(lag);
        const double f = lag - static_cast<double>(below);
        sum += (1.0 - f) * lagged(below % n) + f * lagged((below + 1) % n);
    }
    return sum / (beats - 1) / zero;
}

double prior(double bpm) {
    const double octaves = std::log2(bpm / kPriorBpm) / kPriorOctaves;
    return std::exp(-0.5 * octaves * octaves);
}

// The phase (in frames, 0 <= phase < period) where a pulse train of `period`
// meets the most onsets, and how much it meets there (the mean); and the most
// a pulse train half a period later meets, give or take kOffBeatReach of it.
struct Pulse {
    double phase = 0.0;
    double on = 0.0;
    double off = 0.0;
};
constexpr double kOffBeatReach = 0.125;
Pulse pulseOf(const std::vector<double>& envelope, double period, double resolution) {
    const auto mean = [&](double phase) {
        double sum = 0.0;
        int count = 0;
        for (double t = phase; t < static_cast<double>(envelope.size() - 1); t += period, ++count)
            sum += at(envelope, t);
        return count ? sum / count : 0.0;
    };
    Pulse best;
    for (double phase = 0.0; phase < period; phase += resolution) {
        const double on = mean(phase);
        if (on > best.on) {
            best.on = on;
            best.phase = phase;
        }
    }
    for (double d = -kOffBeatReach * period; d <= kOffBeatReach * period; d += 1.0)
        best.off = std::max(best.off, mean(std::fmod(best.phase + period / 2 + d + period, period)));
    return best;
}

// The tempo of the first `count` decimated samples (`rate` a second) of a file
// `seconds` long.
std::optional<Tempo> tempoOf(const std::vector<float>& x, size_t count, double rate, double seconds) {
    if (static_cast<double>(count) < kMinTempoSeconds * rate) return std::nullopt;
    const std::vector<double> envelope = onsets(x, count, rate);
    const double fps = rate / kOnsetHop;
    const std::vector<double> ac = autocorrelation(envelope);
    if (ac.empty() || ac[0] <= 0.0) return std::nullopt;
    const double maxLag = kCombSpan * static_cast<double>(envelope.size());
    const double coarseLag = std::min(maxLag, kCoarseSeconds * fps);
    const auto periodOf = [&](double bpm) { return 60.0 * fps / bpm; };
    // For the pulse trains, the onsets a little smoothed (a triangle 5 frames
    // wide): hits a few milliseconds apart (a human's) meet as one.
    std::vector<double> smooth(envelope.size(), 0.0);
    for (size_t i = 0; i < envelope.size(); ++i) {
        for (int d = -2; d <= 2; ++d) {
            const auto j = static_cast<std::ptrdiff_t>(i) + d;
            if (j >= 0 && j < static_cast<std::ptrdiff_t>(envelope.size()))
                smooth[i] += (3 - std::abs(d)) / 9.0 * envelope[static_cast<size_t>(j)];
        }
    }

    // Coarse: every tempo, a step of kCoarseStep.
    std::vector<double> bpms, combs, scores;
    for (double bpm = kMinBpm; bpm <= kMaxBpm; bpm *= 1.0 + kCoarseStep) {
        const double c = comb(ac, periodOf(bpm), coarseLag);
        if (std::isnan(c)) continue;
        bpms.push_back(bpm);
        combs.push_back(c);
        scores.push_back(std::max(0.0, c) * prior(bpm));
    }
    if (bpms.size() < 3) return std::nullopt;
    std::vector<double> sorted = combs;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const double median = sorted[sorted.size() / 2];

    // The best few peaks; and if the onsets are the whole file, the tempos its
    // length makes whole bars of (4, 8, 16... beats), their comb over the
    // onsets as a loop (wrapping round). Of them, the best whose off-beats
    // aren't as strong as its beats.
    struct Candidate {
        double bpm;
        double comb;
        bool loop;
    };
    std::vector<size_t> peaks;
    for (size_t i = 1; i + 1 < scores.size(); ++i) {
        if (scores[i] > 0.0 && scores[i] >= scores[i - 1] && scores[i] > scores[i + 1]) peaks.push_back(i);
    }
    std::sort(peaks.begin(), peaks.end(), [&](size_t a, size_t b) { return scores[a] > scores[b]; });
    if (peaks.size() > static_cast<size_t>(kTempoCandidates)) peaks.resize(kTempoCandidates);
    std::vector<Candidate> candidates;
    for (const size_t i : peaks) candidates.push_back({bpms[i], combs[i], false});
    const double loopFrames = seconds * fps;
    if (std::abs(static_cast<double>(count) / rate - seconds) * fps < 1.0) {
        for (double beats = kBarBeats; beats <= kMaxLoopBeats; beats *= 2) {
            const double bpm = 60.0 * beats / seconds;
            if (bpm < kMinBpm || bpm > kMaxBpm) continue;
            const double linear = comb(ac, periodOf(bpm), coarseLag);
            const double wrapped = loopComb(envelope, loopFrames, static_cast<int>(beats));
            candidates.push_back({bpm, std::isnan(linear) ? wrapped : std::max(linear, wrapped), true});
        }
    }
    if (candidates.empty()) return std::nullopt;
    Candidate chosen = candidates.front();
    double chosenScore = -1.0;
    for (const Candidate& c : candidates) {
        const Pulse pulse = pulseOf(smooth, periodOf(c.bpm), 1.0);
        const double ratio = pulse.on > 0.0 ? pulse.off / pulse.on : 1.0;
        const double penalty = std::clamp((ratio - kOffBeatRatio) / (1.0 - kOffBeatRatio), 0.0, 1.0) * kOffBeatPenalty;
        const double score =
            std::max(0.0, c.comb) * prior(c.bpm) * (1.0 - penalty) * (c.loop ? kLoopBonus : 1.0);
        if (score > chosenScore) {
            chosenScore = score;
            chosen = c;
        }
    }
    const double confidence = std::clamp((chosen.comb - median) / kConfidenceScale, 0.0, 1.0);
    if (confidence < kMinTempoConfidence) return std::nullopt;

    // Fine: about it, every multiple of the period the onsets allow (a loop's
    // tempo is exact already).
    double bpm = chosen.bpm;
    if (!chosen.loop) {
        double best = -1e9;
        for (double b = chosen.bpm * (1.0 - kFineSpan); b <= chosen.bpm * (1.0 + kFineSpan); b *= 1.0 + kFineStep) {
            const double c = comb(ac, periodOf(b), maxLag);
            if (!std::isnan(c) && c > best) {
                best = c;
                bpm = b;
            }
        }
        // A trimmed loop after all (its comb lost to another's): the tempo its length makes.
        for (double beats = kBarBeats; beats <= kMaxLoopBeats; beats *= 2) {
            const double here = seconds * bpm / 60.0;
            if (std::abs(here - beats) <= std::min(kLoopTolerance * beats, kLoopBeatTolerance)) {
                bpm = 60.0 * beats / seconds;
                break;
            }
        }
    }
    if (std::abs(bpm - std::round(bpm)) <= kWholeBpm) bpm = std::round(bpm);

    const double period = periodOf(bpm);
    const Pulse pulse = pulseOf(smooth, period, 0.25);
    double first = std::fmod(pulse.phase / fps + kOnsetLead, 60.0 / bpm);
    if (first < 0.0) first += 60.0 / bpm;
    if (first >= 60.0 / bpm - 1e-9) first = 0.0;
    return Tempo{bpm, confidence, first};
}

// --- The chroma --------------------------------------------------------------

struct Peak {
    float hz;
    float amplitude;
};

struct ChromaFrame {
    Profile chroma{};  // shares of the loudest pitch class, by the frame's weight
    int bass = -1;     // the bass's MIDI pitch, if any
    float weight = 0.f;
};

// The chroma of each frame (kChromaHop apart, centred on index × hop).
std::vector<ChromaFrame> chromaOf(const std::vector<float>& x, double rate) {
    const size_t frames = x.size() / kChromaHop + 1;
    const double binHz = rate / kChromaWindow;
    const int lowBin = std::max(1, static_cast<int>(kBassLowHz / binHz) - 1);
    const int highBin = std::min(kChromaWindow / 2 - 2, static_cast<int>(std::ceil(kHighHz / binHz)) + 1);
    const int lowTonal = static_cast<int>(kLowHz / binHz);

    const std::vector<float> window = hann(kChromaWindow);
    const RealFft fft(kChromaWindow);
    std::vector<float> frame(kChromaWindow);
    std::vector<kiss_fft_cpx> spectrum(kChromaWindow / 2 + 1);
    std::vector<float> magnitude(kChromaWindow / 2 + 1);
    std::vector<double> sums(kChromaWindow / 2 + 2);

    // The peaks of every frame, its power and its tonal power.
    std::vector<Peak> peaks;
    std::vector<size_t> firstPeak(frames + 1, 0);
    std::vector<double> power(frames), tonal(frames);
    for (size_t f = 0; f < frames; ++f) {
        firstPeak[f] = peaks.size();
        frameAt(x, x.size(), static_cast<std::ptrdiff_t>(f) * kChromaHop - kChromaWindow / 2, window, frame);
        kiss_fftr(fft.cfg, frame.data(), spectrum.data());
        double total = 0.0, loudest = 0.0;
        for (size_t k = 0; k < spectrum.size(); ++k) {
            const double p = double(spectrum[k].r) * spectrum[k].r + double(spectrum[k].i) * spectrum[k].i;
            magnitude[k] = static_cast<float>(std::sqrt(p));
            total += p;
            const auto bin = static_cast<int>(k);
            if (bin >= lowBin && bin <= highBin) loudest = std::max(loudest, double(magnitude[k]));
        }
        power[f] = total;
        if (loudest <= 0.0) continue;
        for (size_t k = 0; k < magnitude.size(); ++k) sums[k + 1] = sums[k] + magnitude[k];
        double inPeaks = 0.0;
        for (int k = std::max(2, lowBin); k <= highBin; ++k) {
            const auto u = static_cast<size_t>(k);
            const float m = magnitude[u];
            if (m < kPeakRange * loudest || m <= magnitude[u - 1] || m < magnitude[u + 1] || m < magnitude[u - 2] ||
                m < magnitude[u + 2])
                continue;
            const size_t a = static_cast<size_t>(std::max(0, k - kPeakNeighbourhood));
            const size_t b = std::min(magnitude.size(), u + kPeakNeighbourhood + 1);
            const double localMean = (sums[b] - sums[a]) / static_cast<double>(b - a);
            if (m < kPeakProminence * localMean) continue;
            // Where it really is, and how loud: a parabola through the log magnitudes.
            const double l = std::log(std::max(1e-12f, magnitude[u - 1])), c = std::log(double(m)),
                         r = std::log(std::max(1e-12f, magnitude[u + 1]));
            const double denominator = l - 2.0 * c + r;
            const double offset = denominator < 0.0 ? std::clamp(0.5 * (l - r) / denominator, -0.5, 0.5) : 0.0;
            const double amplitude = std::exp(c - 0.25 * (l - r) * offset);
            const double hz = (k + offset) * binHz;
            peaks.push_back({static_cast<float>(hz), static_cast<float>(amplitude)});
            if (k >= lowTonal) inPeaks += kMainLobe * amplitude * amplitude;
        }
        tonal[f] = inPeaks;
    }
    firstPeak[frames] = peaks.size();

    // The recording's tuning: how far its peaks lie from equal temperament on
    // A = 440 Hz, a circular mean (louder peaks counting more).
    std::complex<double> deviation{};
    for (const Peak& p : peaks) {
        if (p.hz < 100.0 || p.hz > kHighHz) continue;
        const double semitones = 12.0 * std::log2(p.hz / 440.0);
        deviation += std::polar(double(p.amplitude), 2.0 * std::numbers::pi * semitones);
    }
    const double tuning = std::abs(deviation) > 0.0 ? std::arg(deviation) / (2.0 * std::numbers::pi) : 0.0;

    // The level the frames' are measured against: the loud ones'.
    std::vector<double> levels;
    levels.reserve(frames);
    for (const double p : power) {
        if (p > 0.0) levels.push_back(p);
    }
    std::vector<ChromaFrame> result(frames);
    if (levels.empty()) return result;
    const auto loudAt = static_cast<std::ptrdiff_t>(kLoudPercentile * static_cast<double>(levels.size() - 1));
    std::nth_element(levels.begin(), levels.begin() + loudAt, levels.end());
    const double loud = levels[static_cast<size_t>(loudAt)];

    for (size_t f = 0; f < frames; ++f) {
        if (power[f] <= 0.0) continue;
        const double db = 10.0 * std::log10(power[f] / loud);
        const double level = std::clamp((db - kQuietDb) / (kFullDb - kQuietDb), 0.0, 1.0);
        const double tonality = std::clamp((tonal[f] / power[f] - kTonalFrom) / (kTonalFull - kTonalFrom), 0.0, 1.0);
        const double weight = level * tonality;
        if (weight <= 0.0) continue;
        const Peak* begin = peaks.data() + firstPeak[f];
        const Peak* end = peaks.data() + firstPeak[f + 1];
        // A peak's amplitude, if there is one at hz (within kHarmonicMatch).
        const auto amplitudeAt = [&](double hz) {
            const Peak* p = std::lower_bound(begin, end, hz * (1.0 - kHarmonicMatch),
                                             [](const Peak& peak, double h) { return peak.hz < h; });
            double found = 0.0;
            for (; p != end && p->hz <= hz * (1.0 + kHarmonicMatch); ++p)
                found = std::max(found, double(p->amplitude));
            return found;
        };
        std::array<double, 12> chroma{};
        double bassLoudest = 0.0;
        for (const Peak* p = begin; p != end; ++p) {
            if (p->hz <= kBassHighHz) bassLoudest = std::max(bassLoudest, double(p->amplitude));
        }
        ChromaFrame& out = result[f];
        for (const Peak* p = begin; p != end; ++p) {
            const double pitch = 69.0 + 12.0 * std::log2(p->hz / 440.0) - tuning;
            const double nearest = std::round(pitch);
            if (std::abs(pitch - nearest) > kInTune) continue;
            const int midi = static_cast<int>(nearest);
            const bool bass = p->hz >= kBassLowHz && p->hz <= kBassHighHz && p->amplitude >= kBassShare * bassLoudest;
            if (out.bass < 0 && bass) out.bass = midi;  // (the peaks go up in frequency: the first is the lowest)
            if (p->hz < kLowHz) continue;
            double harmonics = 0.0;
            for (const int h : kHarmonics) harmonics += amplitudeAt(p->hz / h) / h;
            const double own = std::max(kOwnFloor * p->amplitude, p->amplitude - kHarmonicShare * harmonics);
            chroma[static_cast<size_t>(pitchClass(midi))] += own;
        }
        const double top = *std::max_element(chroma.begin(), chroma.end());
        if (top <= 0.0) {
            out.bass = -1;
            continue;
        }
        for (size_t pc = 0; pc < 12; ++pc) {
            const double share = std::pow(chroma[pc] / top, kChromaPower);
            out.chroma[pc] = share >= kChromaFloor ? static_cast<float>(share) : 0.f;
        }
        out.weight = static_cast<float>(weight);
    }
    return result;
}

// Adds `weight` a beat over [a, b) to the steps it overlaps.
void spread(std::vector<Profile>& steps, double origin, double step, double a, double b, const Profile& weights,
            double weight) {
    if (b <= a) return;
    const auto from = static_cast<size_t>(std::max(0.0, std::floor((a - origin) / step)));
    for (size_t t = from; t < steps.size(); ++t) {
        const double s = origin + static_cast<double>(t) * step;
        if (s >= b) break;
        const double overlap = std::min(b, s + step) - std::max(a, s);
        if (overlap <= 0.0) continue;
        for (size_t pc = 0; pc < 12; ++pc) steps[t][pc] += static_cast<float>(overlap * weight * weights[pc]);
    }
}

// Which beat of four the bars start on: where the harmony changes most (the
// pitch classes of the two beats before against the two after).
int barOffset(const harmony::StepProfiles& profiles) {
    const auto perBeat = static_cast<size_t>(std::lround(1.0 / profiles.step));
    if (perBeat == 0 || std::abs(perBeat * profiles.step - 1.0) > 1e-9) return 0;
    const size_t steps = profiles.sounding.size();
    std::array<double, 4> change{};
    const auto sum = [&](size_t from, size_t to) {
        std::array<double, 12> total{};
        for (size_t t = from; t < to; ++t) {
            for (size_t pc = 0; pc < 12; ++pc) total[pc] += profiles.sounding[t][pc];
        }
        double all = 0.0;
        for (const double v : total) all += v;
        if (all > 0.0) {
            for (double& v : total) v /= all;
        }
        return std::make_pair(total, all);
    };
    for (size_t t = 2 * perBeat; t + 2 * perBeat <= steps; ++t) {
        const double beat = profiles.origin + static_cast<double>(t) * profiles.step;
        if (std::abs(beat - std::round(beat)) > 1e-6) continue;
        const auto [before, a] = sum(t - 2 * perBeat, t);
        const auto [after, b] = sum(t, t + 2 * perBeat);
        if (a <= 0.0 || b <= 0.0) continue;
        double distance = 0.0;
        for (size_t pc = 0; pc < 12; ++pc) distance += std::abs(before[pc] - after[pc]);
        const auto which = static_cast<int>(((static_cast<long long>(std::round(beat)) % 4) + 4) % 4);
        change[static_cast<size_t>(which)] += distance * std::min(a, b);
    }
    return static_cast<int>(std::max_element(change.begin(), change.end()) - change.begin());
}

}  // namespace

std::optional<Tempo> estimateTempo(const float* mono, std::size_t frames, double sampleRate) {
    if (!mono || frames == 0 || !(sampleRate >= kMinSampleRate)) return std::nullopt;
    double rate = 0.0;
    const std::vector<float> x = decimate(&mono, 1, frames, sampleRate, rate);
    if (peakOf(x) < kSilence) return std::nullopt;
    return tempoOf(x, x.size(), rate, static_cast<double>(frames) / sampleRate);
}

Analysis analyze(const float* const* channels, int channelCount, std::size_t frames, double sampleRate,
                 const Options& options) {
    Analysis result;
    if (!channels || channelCount <= 0 || frames == 0 || !(sampleRate >= kMinSampleRate)) return result;
    double rate = 0.0;
    std::vector<float> x = decimate(channels, channelCount, frames, sampleRate, rate);
    const float peak = peakOf(x);
    if (peak < kSilence) return result;
    for (float& v : x) v /= peak;
    const double seconds = static_cast<double>(frames) / sampleRate;
    const auto tempoSamples =
        std::min(x.size(), static_cast<size_t>(std::max(0.0, options.maxTempoSeconds) * rate));
    result.tempo = tempoOf(x, tempoSamples, rate, seconds);
    if (!options.chords) return result;

    // The beat grid: seconds to beats.
    const double perSecond = result.tempo ? result.tempo->bpm / 60.0 : kFreeBeatsPerSecond;
    const double zero = result.tempo ? result.tempo->firstBeatSec : 0.0;
    const auto beatOf = [&](double t) { return (t - zero) * perSecond; };

    const std::vector<ChromaFrame> chroma = chromaOf(x, rate);

    const double hop = kChromaHop / rate;
    double first = -1.0, last = -1.0;  // seconds: where pitched sound starts and ends
    for (size_t f = 0; f < chroma.size(); ++f) {
        if (chroma[f].weight <= 0.f) continue;
        const double t = static_cast<double>(f) * hop;
        if (first < 0.0) first = std::max(0.0, t - hop / 2);
        last = std::min(seconds, t + hop / 2);
    }
    if (first < 0.0 || last <= first) return result;

    harmony::StepProfiles profiles;
    profiles.step = kStep;
    const auto stepsFor = [&] {
        return static_cast<size_t>(std::ceil((beatOf(seconds) - profiles.origin) / profiles.step - 1e-9));
    };
    profiles.origin = std::floor(beatOf(0.0) / profiles.step) * profiles.step;
    while (stepsFor() > static_cast<size_t>(harmony::kMaxSteps)) {
        profiles.step *= 2;
        profiles.origin = std::floor(beatOf(0.0) / profiles.step) * profiles.step;
    }
    const size_t steps = std::max<size_t>(1, stepsFor());
    profiles.sounding.assign(steps, Profile{});
    profiles.bass.assign(steps, Profile{});
    profiles.first = beatOf(first);
    profiles.last = beatOf(last);
    for (size_t f = 0; f < chroma.size(); ++f) {
        const ChromaFrame& frame = chroma[f];
        if (frame.weight <= 0.f) continue;
        const double t = static_cast<double>(f) * hop;
        const double a = beatOf(std::max(0.0, t - hop / 2)), b = beatOf(std::min(seconds, t + hop / 2));
        spread(profiles.sounding, profiles.origin, profiles.step, a, b, frame.chroma, frame.weight);
        if (frame.bass >= 0) {
            Profile bass{};
            bass[static_cast<size_t>(pitchClass(frame.bass))] = 1.f;
            const double weight = frame.weight * harmony::bassTrust(frame.bass);
            spread(profiles.bass, profiles.origin, profiles.step, a, b, bass, weight);
        }
    }
    // Steps with next to nothing pitched in them have nothing.
    std::array<double, 12> whole{};
    double soundingBeats = 0.0;
    for (size_t t = 0; t < steps; ++t) {
        double energy = 0.0;
        for (const float w : profiles.sounding[t]) energy += w;
        if (energy < kMinStepNotes * profiles.step) {
            profiles.sounding[t].fill(0.f);
            profiles.bass[t].fill(0.f);
            continue;
        }
        soundingBeats += energy;
        for (size_t pc = 0; pc < 12; ++pc) whole[pc] += profiles.sounding[t][pc] + profiles.bass[t][pc];
    }
    result.key = harmony::estimateKey(whole, soundingBeats);

    // Bars where the harmony changes most (beat 0 is one of the grid's beats).
    const int offset = result.tempo ? barOffset(profiles) : 0;
    profiles.origin -= offset;
    profiles.first -= offset;
    profiles.last -= offset;
    for (const harmony::ChordSpan& span : harmony::inferChords(profiles, result.key, kBarBeats)) {
        const double start = zero + (span.start + offset) / perSecond;
        const double end = zero + (span.end + offset) / perSecond;
        result.chords.push_back({std::max(0.0, start), std::min(seconds, end), span.chord});
    }
    return result;
}

}  // namespace sub::intelligence::audio
