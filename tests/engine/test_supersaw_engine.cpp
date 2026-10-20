// The built-in Supersaw: its listing and latency; each saw where its phase says
// (to a hundredth of a sample, as late as it reports) and free of aliasing (more
// than 130 dB down at any pitch); the band-limited step; a stack's layout (the
// design's, as the editor draws it: places, pans, gains) and what plays of it
// (each saw at its pitch and level, Width, Blend, unit power whatever the
// count); Transpose, Fine, the sub; the envelopes, the filter and its envelope
// and key tracking; Phase and Random Phase, Drift; glide in Mono and Legato,
// bends, the wheel, the pedal, stealing; silence and sleeping; the same output
// however blocks are cut, at any rate; its cost. Played on its own (as the
// renderer plays it, in blocks with notes in them) and through the engine.

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Engine.h"
#include "Processor.h"
#include "builtin/BuiltinRegistry.h"
#include "builtin/DspBlocks.h"
#include "builtin/SupersawDesign.h"
#include "harness/Fixtures.h"
#include "harness/Signal.h"
#include "harness/Test.h"
#include "harness/ThreadTime.h"

using namespace subtest;

namespace {

using Values = std::vector<std::pair<std::string, float>>;

// What plays at a frame.
struct Timed {
    int64_t frame;
    sub::ProcessEvent event;
};
Timed noteOn(int64_t frame, int key, int velocity = 127, int32_t id = -1) {
    Timed t{frame, sub::ProcessEvent::noteOn(0, static_cast<uint8_t>(key), static_cast<uint8_t>(velocity))};
    t.event.noteId = id;
    return t;
}
Timed noteOff(int64_t frame, int key, int32_t id = -1) {
    Timed t{frame, sub::ProcessEvent::noteOff(0, static_cast<uint8_t>(key))};
    t.event.noteId = id;
    return t;
}
Timed bend(int64_t frame, int key, int32_t id, float semitones) {
    return {frame, sub::ProcessEvent::noteBend(0, static_cast<uint8_t>(key), id, semitones)};
}
Timed midi(int64_t frame, uint8_t status, uint8_t a, uint8_t b) {
    Timed t{frame, {}};
    t.event.type = sub::ProcessEvent::Type::Midi;
    t.event.data[0] = status;
    t.event.data[1] = a;
    t.event.data[2] = b;
    return t;
}

// A parameter's change at a frame, handed over as automation.
struct Change {
    int64_t frame;
    std::string id;
    float value;
};

// Over the defaults, so a test hears only what it sets: one stack, no drift, velocity off, no filter, a quick
// envelope at full sustain.
const Values kPlain = {{"drift", 0.f}, {"velocity", 0.f}, {"filter_type", 0.f}, {"attack", 1.f},
                       {"sustain", 100.f}, {"release", 5.f}};

// A Supersaw on its own at `rate`, played as the renderer plays it: blocks, the notes in them at their offsets,
// automation handed over before each.
class Player {
public:
    explicit Player(const Values& values = {}, double rate = 48000.0, bool plain = true)
        : device_(sub::BuiltinRegistry::instance().create("supersaw")), rate_(rate) {
        if (plain) set(kPlain);
        set(values);
        device_->prepare(rate, 1024);
    }
    sub::Processor& device() { return *device_; }
    double rate() const { return rate_; }
    int index(const std::string& id) const {
        const auto& params = device_->params();
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == id) return static_cast<int>(i);
        INFO(id);
        REQUIRE(false);
        return -1;
    }
    void set(const std::string& id, float value) { device_->setParam(index(id), value); }
    void set(const Values& values) {
        for (const auto& [id, value] : values) set(id, value);
    }

    // `frames` of its output, left and right: the events at their frames, in blocks `nextBlock()` long.
    std::pair<Samples, Samples> play(int64_t frames, std::vector<Timed> events, const std::function<int()>& nextBlock,
                                     std::vector<Change> changes = {}) {
        std::stable_sort(events.begin(), events.end(), [](const Timed& a, const Timed& b) { return a.frame < b.frame; });
        std::stable_sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return a.frame < b.frame; });
        Samples left(static_cast<size_t>(frames)), right(static_cast<size_t>(frames));
        std::vector<sub::ProcessEvent> block;
        size_t next = 0, nextChange = 0;
        sub::ProcessContext ctx;
        ctx.sampleRate = rate_;
        ctx.offline = true;
        for (int64_t start = 0; start < frames;) {
            const int n = static_cast<int>(std::min<int64_t>(std::clamp(nextBlock(), 1, 1024), frames - start));
            block.clear();
            while (next < events.size() && events[next].frame < start + n) {
                sub::ProcessEvent event = events[next++].event;
                event.sampleOffset = static_cast<int32_t>(std::max<int64_t>(0, events[next - 1].frame - start));
                block.push_back(event);
            }
            while (nextChange < changes.size() && changes[nextChange].frame < start + n) {
                const Change& change = changes[nextChange++];
                const int i = index(change.id);
                device_->automate(i, device_->params()[static_cast<size_t>(i)].toNormalized(change.value),
                                  static_cast<int32_t>(std::max<int64_t>(0, change.frame - start)));
            }
            ctx.inEvents = {block.data(), block.size()};
            ctx.samplePos = start;
            float* channels[2] = {left.data() + start, right.data() + start};
            device_->process(ctx, channels, 2, n);
            device_->clearAutomation();
            start += n;
        }
        return {std::move(left), std::move(right)};
    }
    std::pair<Samples, Samples> play(int64_t frames, const std::vector<Timed>& events, int block = 256,
                                     const std::vector<Change>& changes = {}) {
        return play(frames, events, [block] { return block; }, changes);
    }
    // Only the left channel.
    Samples left(int64_t frames, const std::vector<Timed>& events, int block = 256) {
        return play(frames, events, block).first;
    }

    std::vector<float> display(const std::string& id) {
        const std::vector<sub::DisplayInfo> displays = device_->displays();
        for (size_t d = 0; d < displays.size(); ++d) {
            if (displays[d].id != id) continue;
            std::vector<float> values;
            positions_[d] = device_->readDisplay(static_cast<int>(d), positions_[d], values);
            return values;
        }
        INFO(id);
        REQUIRE(false);
        return {};
    }

private:
    std::shared_ptr<sub::Processor> device_;
    double rate_;
    std::map<size_t, uint64_t> positions_;
};

int64_t seconds(double s, double rate = 48000.0) { return static_cast<int64_t>(std::lround(s * rate)); }

double keyHz(double key) { return 440.0 * std::exp2((key - 69.0) / 12.0); }

// x[from, from + length) through a Kaiser window (β 24: its sidelobes 200 dB down, under anything measured),
// transformed (length a power of two): the magnitudes of bins 0..length/2.
std::vector<double> kaiserSpectrum(const Samples& x, size_t from, size_t length) {
    std::vector<Complex> a(length);
    const double i0 = sub::dsp::besselI0(24.0);
    for (size_t i = 0; i < length; ++i) {
        const double r = 2.0 * static_cast<double>(i) / static_cast<double>(length - 1) - 1.0;
        a[i] = static_cast<double>(x[from + i]) * sub::dsp::besselI0(24.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0;
    }
    fftPow2(a, false);
    std::vector<double> magnitude(length / 2 + 1);
    for (size_t i = 0; i < magnitude.size(); ++i) magnitude[i] = std::abs(a[i]);
    return magnitude;
}

// The strongest component below `top` Hz that isn't a harmonic of f0 (none within 12 bins of one: the window's
// main lobe is 8 either side), in dB under the fundamental: a saw's worst alias. Over the last 2^16 samples of x.
double worstAliasDb(const Samples& x, double rate, double f0, double top = 20000.0) {
    const size_t length = 1 << 16;
    REQUIRE(x.size() >= length);
    const std::vector<double> magnitude = kaiserSpectrum(x, x.size() - length, length);
    const double binHz = rate / static_cast<double>(length);
    double fundamental = 0.0, worst = 0.0;
    for (size_t i = 1; i < magnitude.size(); ++i) {
        const double f = static_cast<double>(i) * binHz;
        if (std::abs(f - f0) < 12 * binHz) fundamental = std::max(fundamental, magnitude[i]);
        if (f > top || i < 12) continue;
        const double harmonic = std::round(f / f0);
        if (harmonic >= 1 && std::abs(f - harmonic * f0) < 12 * binHz) continue;
        worst = std::max(worst, magnitude[i]);
    }
    return db(worst / fundamental);
}

// Where x falls through zero between `from` and `to`, to a fraction of a sample (linear between the two
// samples either side): the saw's jumps.
std::vector<double> fallingZeros(const Samples& x, int64_t from, int64_t to) {
    std::vector<double> zeros;
    for (int64_t i = std::max<int64_t>(from, 1); i < to && i < static_cast<int64_t>(x.size()); ++i) {
        const double a = x[static_cast<size_t>(i - 1)], b = x[static_cast<size_t>(i)];
        if (a > 0.0 && b <= 0.0 && a - b > 0.05) zeros.push_back(static_cast<double>(i - 1) + a / (a - b));
    }
    return zeros;
}

// The amplitude of the component at `freq` over x[from, from + length) (a Hann window).
double amplitudeAt(const Samples& x, double freq, int64_t from, int64_t length, double rate = 48000.0) {
    return windowedAmplitude(x, freq, from, length, rate);
}

constexpr double kVoiceGain = 0.25;           // a stack at full velocity (the device's)
constexpr double kSawFundamental = 2.0 / kPi;  // a saw of ±1's fundamental

}  // namespace

TEST_CASE("supersaw: an instrument, listed with its parameters") {
    const sub::BuiltinInfo info = builtinInfo("supersaw");
    CHECK_EQ(info.name, std::string("Supersaw"));
    CHECK(info.isInstrument());
    std::vector<std::string> ids;
    for (int s = 1; s <= 3; ++s)
        for (const char* p : {"on", "saws", "transpose", "fine", "detune", "curve", "blend", "width", "level"})
            ids.push_back("s" + std::to_string(s) + "_" + p);
    for (const char* p : {"phase", "random", "drift", "sub", "sub_octave", "filter_type", "cutoff", "resonance",
                          "key_track", "filter_env", "filter_attack", "filter_decay", "filter_sustain",
                          "filter_release", "attack", "decay", "sustain", "release", "velocity", "mode", "glide",
                          "bend_range", "volume"})
        ids.emplace_back(p);
    CHECK(paramIds(info.params) == ids);
    const auto param = [&](const std::string& id) {
        for (const sub::ParamInfo& p : info.params)
            if (p.id == id) return p;
        REQUIRE(false);
        return sub::ParamInfo{};
    };
    // At its defaults: one stack of seven, the others ready an octave down and up.
    CHECK_EQ(param("s1_on").defaultValue, 1.f);
    CHECK_EQ(param("s2_on").defaultValue, 0.f);
    CHECK_EQ(param("s3_on").defaultValue, 0.f);
    CHECK_EQ(param("s1_saws").defaultValue, 7.f);
    CHECK_EQ(param("s1_saws").maxValue, 64.f);
    CHECK_EQ(param("s1_saws").stepCount(), 63);
    CHECK_EQ(param("s2_transpose").defaultValue, -12.f);
    CHECK_EQ(param("s3_transpose").defaultValue, 12.f);
    CHECK(param("cutoff").isLog());
    CHECK(param("filter_type").valueLabels ==
          (std::vector<std::string>{"Off", "Low 12", "Low 24", "High 12", "High 24", "Band"}));
    CHECK(param("mode").valueLabels == (std::vector<std::string>{"Poly", "Mono", "Legato"}));
    CHECK(param("sub_octave").valueLabels == (std::vector<std::string>{"0", "-1", "-2"}));

    // Its latency: the step's 9 oscillator samples and the decimator's delay (at twice the rate below 80 kHz).
    for (const auto& [rate, latency] : std::vector<std::pair<double, int>>{
             {44100.0, 47}, {48000.0, 28}, {88200.0, 9}, {96000.0, 9}, {192000.0, 9}}) {
        INFO(std::to_string(rate));
        Player p({}, rate);
        CHECK_EQ(p.device().latencySamples(), latency);
        CHECK(p.device().acceptsMidi());
    }
}

TEST_CASE("supersaw: a saw jumps where its phase says, as late as the device reports") {
    // One saw from the middle of its rise (Phase 180°): it jumps at the note's frame, plus the time its phase takes
    // to wrap, plus the latency, to a thousandth of a sample (timed by its fundamental's phase, which the band-limited
    // step and the decimator leave alone); its falls through zero say so too, to within what band-limiting does to
    // the samples either side; and nothing comes out before the note.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        INFO(std::to_string(rate));
        Player p({{"s1_saws", 1.f}, {"phase", 180.f}, {"random", 0.f}}, rate);
        const int64_t start = 1000;
        const Samples out = p.left(start + seconds(0.5, rate), {noteOn(start, 69)});
        CHECK(allEqual(slice(out, 0, start), 0.0));
        const double factor = rate < 80000.0 ? 2.0 : 1.0;
        const double increment = std::round(440.0 / (rate * factor) * 4294967296.0);
        const double period = 4294967296.0 / increment / factor;  // in samples at the rate
        const double hz = rate / period;
        const double first = static_cast<double>(start + p.device().latencySamples()) + period / 2.0;
        // The fundamental of a saw rising from -1 and falling at t is (2/π) sin(ω (n - t) + π): its transform at ω
        // (a Kaiser window, β 20, over the held part: what leaks from the harmonics far under what is measured) has
        // the phase π/2 - ω t.
        const int64_t from = start + seconds(0.1, rate), length = seconds(0.3, rate);
        Complex sum = 0.0;
        const double omega = 2.0 * kPi * hz / rate;
        for (int64_t n = 0; n < length; ++n) {
            const double r = 2.0 * static_cast<double>(n) / static_cast<double>(length - 1) - 1.0;
            const double w = sub::dsp::besselI0(20.0 * std::sqrt(std::max(0.0, 1.0 - r * r)));
            const double m = static_cast<double>(from + n);
            sum += w * static_cast<double>(out[static_cast<size_t>(from + n)]) * std::polar(1.0, -omega * m);
        }
        double jump = (kPi / 2.0 - std::arg(sum)) / omega;
        jump += period * std::round((first - jump) / period);  // the jump nearest the first expected
        CHECK_NEAR(jump, first, 1e-3);
        const std::vector<double> zeros = fallingZeros(out, start, static_cast<int64_t>(out.size()));
        REQUIRE(zeros.size() > 20);
        for (size_t k = 0; k < 20; ++k) {
            INFO("jump " + std::to_string(k));
            CHECK_NEAR(zeros[k], first + static_cast<double>(k) * period, 0.1);
        }
    }
}

TEST_CASE("supersaw: its saws are free of aliasing") {
    // One saw at pitches up to 10.5 kHz, at 44.1 and 48 kHz (twice the rate inside) and 96 kHz (the rate itself):
    // whatever isn't a harmonic, below 20 kHz, more than 130 dB under the fundamental. (A plain saw's aliases are
    // 17 to 55 dB down.)
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int key : {45, 69, 93, 105, 112, 117, 124}) {
            INFO(std::to_string(rate) + " Hz, key " + std::to_string(key));
            Player p({{"s1_saws", 1.f}}, rate);
            const Samples out = p.left(seconds(0.1, rate) + (1 << 16), {noteOn(0, key)});
            const double alias = worstAliasDb(out, rate, keyHz(key));
            INFO("worst alias " + std::to_string(alias) + " dB");
            CHECK(alias < -130.0);
        }
    }
}

TEST_CASE("supersaw: the band-limited step") {
    const sub::supersaw::StepTable& table = sub::supersaw::stepTable();
    float a[sub::supersaw::kStepTaps], b[sub::supersaw::kStepTaps];
    for (const float fraction : {0.f, 0.1f, 0.25f, 0.5f, 0.77f, 0.999f}) {
        INFO(std::to_string(fraction));
        table.correction(fraction, a);
        // It reaches nothing beyond its taps: the ends are (all but) zero.
        CHECK(std::abs(a[0]) < 1e-5);
        CHECK(std::abs(a[sub::supersaw::kStepTaps - 1]) < 1e-5);
        // Half way at the jump: the step and its mirror image (fraction 1 - f, the taps the other way) add to
        // nothing, as S(-t) = 1 - S(t).
        table.correction(1.f - fraction, b);
        for (int k = 0; k < sub::supersaw::kStepTaps; ++k) CHECK_NEAR(a[k], -b[sub::supersaw::kStepTaps - 1 - k], 2e-6);
    }
    // At the jump itself the band-limited step is half way: -0.5 under the plain step there.
    table.correction(0.f, a);
    CHECK_NEAR(a[sub::supersaw::kStepHalf], -0.5f, 1e-6);
}

TEST_CASE("supersaw: a stack's layout") {
    using sub::supersaw::Saw;
    using sub::supersaw::Shape;
    std::vector<Saw> saws(sub::supersaw::kMaxSaws);
    // Seven, evenly spread with Curve at 0: the outermost Detune² × 100 cents out, the middle one in the middle.
    Shape shape{7, 50.0, 0.0, 75.0, 100.0};
    REQUIRE(sub::supersaw::layout(shape, saws.data()) == 7);
    for (int i = 0; i < 7; ++i) CHECK_NEAR(saws[size_t(i)].cents, 25.0 * (-1.0 + i / 3.0), 1e-9);
    // Each mirrored pair on opposite sides, the outer ones widest, the flat one left, then right, then left.
    CHECK_NEAR(saws[0].pan, -1.0, 1e-12);
    CHECK_NEAR(saws[6].pan, 1.0, 1e-12);
    CHECK_NEAR(saws[1].pan, 2.0 / 3.0, 1e-12);
    CHECK_NEAR(saws[5].pan, -2.0 / 3.0, 1e-12);
    CHECK_NEAR(saws[2].pan, -1.0 / 3.0, 1e-12);
    CHECK_NEAR(saws[3].pan, 0.0, 1e-12);
    // Blend 75 %: all alike; their power 1.
    double power = 0.0;
    for (int i = 0; i < 7; ++i) {
        CHECK_NEAR(saws[size_t(i)].gain, 1.0 / std::sqrt(7.0), 1e-12);
        power += saws[size_t(i)].gain * saws[size_t(i)].gain;
    }
    CHECK_NEAR(power, 1.0, 1e-12);
    // Blend 0: the middle alone; 100: the others at 1.5 times it.
    shape.blend = 0.0;
    sub::supersaw::layout(shape, saws.data());
    CHECK_NEAR(saws[3].gain, 1.0, 1e-12);
    CHECK_EQ(saws[0].gain, 0.0);
    shape.blend = 100.0;
    sub::supersaw::layout(shape, saws.data());
    CHECK_NEAR(saws[0].gain / saws[3].gain, 1.5, 1e-12);
    // An even stack's middle pair shares the middle's weight.
    shape = {8, 50.0, 0.0, 0.0, 100.0};
    sub::supersaw::layout(shape, saws.data());
    CHECK_NEAR(saws[3].gain, std::sqrt(0.5), 1e-12);
    CHECK_NEAR(saws[4].gain, std::sqrt(0.5), 1e-12);
    CHECK_EQ(saws[0].gain, 0.0);
    CHECK_NEAR(saws[3].pan, 1.0 / 7.0, 1e-12);  // (the innermost pair is the fourth: its flat saw to the right)
    CHECK_NEAR(saws[4].pan, -1.0 / 7.0, 1e-12);
    // Curve: above 0 the inner saws gather towards the middle, below towards the edges; the ends stay put.
    for (const double curve : {-100.0, -35.0, 35.0, 100.0}) {
        INFO(std::to_string(curve));
        shape = {9, 100.0, curve, 75.0, 100.0};
        sub::supersaw::layout(shape, saws.data());
        CHECK_NEAR(saws[0].cents, -100.0, 1e-9);
        CHECK_NEAR(saws[8].cents, 100.0, 1e-9);
        const double inner = saws[5].cents - saws[4].cents, outer = saws[8].cents - saws[7].cents;
        CHECK(curve > 0 ? inner < outer : inner > outer);
        for (int i = 0; i < 9; ++i) CHECK_NEAR(saws[size_t(i)].cents, -saws[size_t(8 - i)].cents, 1e-9);
    }
    // Width 0: all in the middle. One saw: in the middle, at unit gain.
    shape = {16, 50.0, 35.0, 75.0, 0.0};
    sub::supersaw::layout(shape, saws.data());
    for (int i = 0; i < 16; ++i) CHECK_EQ(saws[size_t(i)].pan, 0.0);
    shape.saws = 1;
    REQUIRE(sub::supersaw::layout(shape, saws.data()) == 1);
    CHECK_EQ(saws[0].cents, 0.0);
    CHECK_NEAR(saws[0].gain, 1.0, 1e-12);
    // The pan law: equal power, 1 each in the middle.
    for (const double pan : {-1.0, -0.4, 0.0, 0.7, 1.0}) {
        double l = 0.0, r = 0.0;
        sub::supersaw::panGains(pan, l, r);
        CHECK_NEAR(l * l + r * r, 2.0, 1e-12);
    }
    double l = 0.0, r = 0.0;
    sub::supersaw::panGains(0.0, l, r);
    CHECK_NEAR(l, 1.0, 1e-12);
    CHECK_NEAR(r, 1.0, 1e-12);
    // Detune's curve: squared, a semitone at 100 %.
    CHECK_NEAR(sub::supersaw::detuneCents(50.0), 25.0, 1e-12);
    CHECK_NEAR(sub::supersaw::detuneCents(100.0), 100.0, 1e-12);
}

TEST_CASE("supersaw: each saw plays where the layout puts it, as loud as it says") {
    // Seven saws a semitone wide, mono: the spectrum around 440 Hz has a peak for each, at its pitch and
    // amplitude (its gain of a saw's fundamental), and nothing between them.
    Player p({{"s1_detune", 100.f}, {"s1_width", 0.f}, {"s1_blend", 50.f}});
    const Samples out = p.left(seconds(4.2), {noteOn(0, 69)});
    std::vector<sub::supersaw::Saw> saws(7);
    sub::supersaw::layout({7, 100.0, 35.0, 50.0, 0.0}, saws.data());
    for (const sub::supersaw::Saw& saw : saws) {
        const double f = 440.0 * std::exp2(saw.cents / 1200.0);
        INFO(std::to_string(f) + " Hz");
        CHECK_APPROX_REL(amplitudeAt(out, f, seconds(0.2), seconds(4.0)), kVoiceGain * saw.gain * kSawFundamental, 0.01);
        CHECK(amplitudeAt(out, f + 1.5, seconds(0.2), seconds(4.0)) < 1e-3 * kVoiceGain * saw.gain);
    }
}

TEST_CASE("supersaw: a stack is as loud whatever its count") {
    // Unit power: the level of one saw (a third of a saw's square, times the voice's gain) whatever the count,
    // mono, over two seconds of beating.
    const double one = kVoiceGain / std::sqrt(3.0);
    for (const float saws : {1.f, 2.f, 3.f, 7.f, 16.f, 64.f}) {
        INFO(std::to_string(saws) + " saws");
        Player p({{"s1_saws", saws}, {"s1_detune", 100.f}, {"s1_width", 0.f}});
        const Samples out = p.left(seconds(2.3), {noteOn(0, 69)});
        CHECK(std::abs(db(rms(slice(out, seconds(0.3))) / one)) < 0.75);
    }
}

TEST_CASE("supersaw: Width spreads the saws; at 0 both sides are the same") {
    Player mono({{"s1_saws", 16.f}, {"s1_width", 0.f}});
    const auto [l0, r0] = mono.play(seconds(2.0), {noteOn(0, 57)});
    CHECK_ARRAY_EQUAL(l0, r0);
    // Wider, less alike: saws spread evenly over the field at equal power share 2/π of the two sides (0.64).
    const auto spread = [](float width) {
        Player wide({{"s1_saws", 16.f}, {"s1_width", width}});
        const auto [l1, r1] = wide.play(seconds(2.0), {noteOn(0, 57)});
        const Samples l = slice(l1, seconds(0.2)), r = slice(r1, seconds(0.2));
        return std::make_tuple(correlation(std::vector<double>(l.begin(), l.end()), std::vector<double>(r.begin(), r.end())),
                               rms(l), rms(r));
    };
    const auto [half, halfL, halfR] = spread(50.f);
    const auto [full, left, right] = spread(100.f);
    INFO("correlation " + std::to_string(half) + " at 50 %, " + std::to_string(full) + " at 100 %");
    CHECK(half < 0.95);
    CHECK(full < half);
    CHECK(full < 0.7);
    CHECK(std::abs(db(left / right)) < 0.5);                        // as loud on both sides,
    CHECK(std::abs(db(left / rms(slice(l0, seconds(0.2))))) < 0.5);  // and as loud as mono (equal power)
}

TEST_CASE("supersaw: Transpose, Fine, and the stacks together") {
    // Stack 2 alone, one saw: an octave down, then a semitone up from there with Fine.
    Player p({{"s1_on", 0.f}, {"s2_on", 1.f}, {"s2_saws", 1.f}});
    CHECK_APPROX_REL(dominantFreq(slice(p.left(seconds(1.0), {noteOn(0, 69)}), seconds(0.2))), 220.0, 1e-4);
    p.set("s2_fine", 100.f);
    CHECK_APPROX_REL(dominantFreq(slice(p.left(seconds(1.0), {noteOn(0, 69)}), seconds(0.2))), keyHz(58), 1e-4);
    // All three, one saw each: the fundamental and both octaves, each at its level.
    Player all({{"s1_saws", 1.f}, {"s2_on", 1.f}, {"s2_saws", 1.f}, {"s3_on", 1.f}, {"s3_saws", 1.f},
                {"s2_level", -6.f}, {"s3_level", -12.f}});
    const Samples out = all.left(seconds(1.2), {noteOn(0, 69)});
    // (At 440 Hz stack 1's fundamental and stack 2's second harmonic add; 220 and 880 tell the levels.)
    const double a220 = amplitudeAt(out, 220.0, seconds(0.2), seconds(1.0));
    CHECK_APPROX_REL(a220, kVoiceGain * kSawFundamental * std::pow(10.0, -6.0 / 20.0), 0.01);
    const double a1760 = amplitudeAt(out, 1760.0, seconds(0.2), seconds(1.0));
    CHECK(a1760 > 0.0);
}

TEST_CASE("supersaw: the sub is a sine, an octave or two down") {
    for (const auto& [octave, hz] : std::vector<std::pair<float, double>>{{0.f, 440.0}, {1.f, 220.0}, {2.f, 110.0}}) {
        INFO(std::to_string(hz));
        Player p({{"s1_on", 0.f}, {"sub", 100.f}, {"sub_octave", octave}});
        const Samples out = p.left(seconds(1.2), {noteOn(0, 69)});
        CHECK_APPROX_REL(amplitudeAt(out, hz, seconds(0.2), seconds(1.0)), kVoiceGain * std::sqrt(2.0 / 3.0), 1e-3);
        // Pure: no harmonics.
        for (int h = 2; h <= 5; ++h) CHECK(amplitudeAt(out, h * hz, seconds(0.2), seconds(1.0)) < 1e-6);
    }
}

TEST_CASE("supersaw: the amplitude envelope") {
    Player p({{"s1_saws", 1.f}, {"attack", 100.f}, {"decay", 100.f}, {"sustain", 50.f}, {"release", 100.f}});
    const Samples out = p.left(seconds(1.0), {noteOn(0, 69), noteOff(seconds(0.5), 69)});
    const std::vector<double> level = envelope(out, 480);
    const auto at = [&](double s) { return level[static_cast<size_t>(seconds(s))]; };
    CHECK(at(0.02) < at(0.05));  // rising,
    CHECK(at(0.05) < at(0.09));
    CHECK(at(0.3) < 0.6 * at(0.1));  // to the sustain level,
    CHECK_APPROX_REL(rms(slice(out, seconds(0.35), seconds(0.5))), 0.5 * kVoiceGain / std::sqrt(3.0), 0.01);
    CHECK(at(0.6) < 0.2 * at(0.45));  // and released: -60 dB in 100 ms,
    CHECK(maxAbs(slice(out, seconds(0.75))) < 1e-4);
}

TEST_CASE("supersaw: silence comes out as zeros, and it sleeps until a note") {
    Player p;
    const Samples out = p.left(seconds(1.5), {noteOn(0, 60), noteOff(seconds(0.2), 60)});
    CHECK(anyNonzero(slice(out, 0, seconds(0.2))));
    CHECK(allEqual(slice(out, seconds(0.4)), 0.0));
    CHECK(p.display("voices").back() == 0.f);
    // Asleep it publishes nothing; a note wakes it.
    p.display("output");
    p.left(seconds(0.5), {});
    CHECK(p.display("output").empty());
    const Samples again = p.left(seconds(0.5), {noteOn(100, 60)});
    CHECK(allEqual(slice(again, 0, 100), 0.0));
    CHECK(anyNonzero(slice(again, 100)));
}

TEST_CASE("supersaw: the filter") {
    // One saw at 110 Hz into each type, cutoff 1 kHz: its harmonics come out as the type's response says.
    const auto harmonics = [](float type, const Values& more = {}) {
        Values values = {{"s1_saws", 1.f}, {"filter_type", type}, {"cutoff", 1000.f}};
        values.insert(values.end(), more.begin(), more.end());
        Player p(values);
        const Samples out = p.left(seconds(1.2), {noteOn(0, 45)});
        std::vector<double> a;
        for (int h = 1; h <= 40; ++h) a.push_back(amplitudeAt(out, 110.0 * h, seconds(0.2), seconds(1.0)));
        return a;
    };
    const std::vector<double> open = harmonics(0.f);
    const auto gainDb = [&](const std::vector<double>& a, int h) { return db(a[size_t(h - 1)] / open[size_t(h - 1)]); };
    const std::vector<double> low12 = harmonics(1.f), low24 = harmonics(2.f), high12 = harmonics(3.f),
                              high24 = harmonics(4.f), band = harmonics(5.f);
    CHECK(std::abs(gainDb(low12, 1)) < 0.1);  // the passband untouched
    CHECK(std::abs(gainDb(low24, 1)) < 0.1);
    CHECK_NEAR(gainDb(low12, 36), -24.0, 1.5);  // two octaves up (3960 Hz): 12 and 24 dB an octave
    CHECK_NEAR(gainDb(low24, 36), -48.0, 2.0);
    CHECK_NEAR(gainDb(low12, 9), -3.0, 0.5);  // about at the cutoff (990 Hz): Butterworth's -3 dB
    CHECK_NEAR(gainDb(low24, 9), -3.0, 0.5);
    CHECK(std::abs(gainDb(high12, 36)) < 0.2);
    CHECK_NEAR(gainDb(high12, 1), -12.0 * std::log2(1000.0 / 110.0), 1.5);
    CHECK_NEAR(gainDb(high24, 1), -24.0 * std::log2(1000.0 / 110.0), 2.5);
    CHECK(std::abs(gainDb(band, 9)) < 0.5);  // unity at its centre
    CHECK_NEAR(gainDb(band, 36), -9.0, 1.0);  // (a band at Q 0.707: 6 dB an octave either side)
    CHECK(gainDb(band, 1) < -15.0);
    // Resonance lifts the cutoff.
    const std::vector<double> resonant = harmonics(1.f, {{"resonance", 100.f}});
    CHECK(gainDb(resonant, 9) > 15.0);
    // Key Track 100 %: a note an octave up, the cutoff too: the same harmonic as far down (a 10th, where the
    // bilinear transform's cramping towards Nyquist is a fifth of a dB).
    const auto tracked = [&](int key) {
        Player p({{"s1_saws", 1.f}, {"filter_type", 2.f}, {"cutoff", 1000.f}, {"key_track", 100.f}});
        const Samples out = p.left(seconds(1.2), {noteOn(0, key)});
        return amplitudeAt(out, keyHz(key) * 10, seconds(0.2), seconds(1.0)) /
               amplitudeAt(out, keyHz(key), seconds(0.2), seconds(1.0));
    };
    CHECK_NEAR(db(tracked(60)), db(tracked(72)), 0.5);
    // Filter Env +50 % (5 octaves at the peak), sustain 0: bright at first, the cutoff's own later.
    Player env({{"s1_saws", 1.f}, {"filter_type", 2.f}, {"cutoff", 200.f}, {"filter_env", 50.f},
                {"filter_attack", 1.f}, {"filter_decay", 200.f}, {"filter_sustain", 0.f}});
    const Samples out = env.left(seconds(1.5), {noteOn(0, 45)});
    const double early = amplitudeAt(out, 110.0 * 20, seconds(0.0), seconds(0.05));
    const double late = amplitudeAt(out, 110.0 * 20, seconds(1.0), seconds(0.5));
    CHECK(db(early / late) > 40.0);
}

TEST_CASE("supersaw: Phase and Random Phase") {
    // Random 0: every saw starts at Phase, together: the note starts with one great jump. Random 100: spread.
    const auto peak = [](float random) {
        Player p({{"s1_saws", 16.f}, {"s1_width", 0.f}, {"random", random}, {"phase", 0.f}});
        return maxAbs(slice(p.left(seconds(0.05), {noteOn(0, 45)}), 0, seconds(0.012)));
    };
    CHECK(peak(0.f) > 2.5 * peak(100.f));
    // Notes struck twice don't repeat themselves.
    Player p({{"s1_saws", 7.f}});
    const Samples out = p.left(seconds(0.6), {noteOn(0, 60), noteOff(seconds(0.2), 60), noteOn(seconds(0.3), 60)});
    const Samples first = slice(out, 0, seconds(0.1)), second = slice(out, seconds(0.3), seconds(0.4));
    CHECK(!allclose(first, second, 0.0, 1e-3));
}

TEST_CASE("supersaw: Drift wanders each saw a few cents, slowly") {
    // One saw, Drift 100 %: its pitch over 100 ms windows moves (a standard deviation of a few cents) but never
    // far; Drift 0 holds it exactly.
    const auto spread = [](float drift) {
        Player p({{"s1_saws", 1.f}, {"drift", drift}});
        const Samples out = p.left(seconds(8.0), {noteOn(0, 81)});
        std::vector<double> cents;
        for (double t = 0.1; t + 0.2 < 8.0; t += 0.2)
            cents.push_back(1200.0 * std::log2(dominantFreq(slice(out, seconds(t), seconds(t + 0.2))) / keyHz(81)));
        double mean = 0.0, square = 0.0, most = 0.0;
        for (double c : cents) mean += c / cents.size();
        for (double c : cents) {
            square += (c - mean) * (c - mean) / cents.size();
            most = std::max(most, std::abs(c));
        }
        return std::make_pair(std::sqrt(square), most);
    };
    const auto [still, stillMost] = spread(0.f);
    CHECK(stillMost < 0.01);
    const auto [moving, most] = spread(100.f);
    INFO("drift: " + std::to_string(moving) + " cents, at most " + std::to_string(most));
    CHECK(moving > 0.5);
    CHECK(most < 20.0);
}

TEST_CASE("supersaw: glide, in Mono and Legato") {
    // Mono with a 100 ms glide: the second key's pitch reached about 100 ms after it comes, from the first's.
    Player p({{"s1_saws", 1.f}, {"mode", 1.f}, {"glide", 100.f}});
    const Samples out = p.left(seconds(1.0), {noteOn(0, 57), noteOn(seconds(0.4), 69), noteOff(seconds(0.9), 69),
                                              noteOff(seconds(0.9), 57)});
    CHECK_APPROX_REL(dominantFreq(slice(out, seconds(0.1), seconds(0.38))), 220.0, 1e-3);
    const double mid = dominantFreq(slice(out, seconds(0.44), seconds(0.46)));
    CHECK(mid > 250.0);
    CHECK(mid < 400.0);
    CHECK_APPROX_REL(dominantFreq(slice(out, seconds(0.55), seconds(0.85))), 440.0, 1e-3);
    // One voice: the display says one note plays.
    for (const float voices : p.display("voices")) CHECK(voices <= 1.f);
    // Mono strikes the envelope again; Legato doesn't, while a key is held (sustain 30 %, the decay over).
    const auto after = [](float mode) {
        Player q({{"mode", mode}, {"decay", 50.f}, {"sustain", 30.f}, {"attack", 50.f}});
        const Samples o = q.left(seconds(1.0), {noteOn(0, 57), noteOn(seconds(0.5), 64)});
        return rms(slice(o, seconds(0.53), seconds(0.56)));
    };
    CHECK(after(1.f) > 2.0 * after(2.f));
    // Back to the key still held when the newer one ends.
    Player r({{"s1_saws", 1.f}, {"mode", 2.f}});
    const Samples back = r.left(seconds(1.0), {noteOn(0, 57), noteOn(seconds(0.3), 69), noteOff(seconds(0.6), 69)});
    CHECK_APPROX_REL(dominantFreq(slice(back, seconds(0.35), seconds(0.58))), 440.0, 1e-3);
    CHECK_APPROX_REL(dominantFreq(slice(back, seconds(0.65), seconds(0.98))), 220.0, 1e-3);
    // Poly glides each new note from the last one's pitch.
    Player poly({{"s1_saws", 1.f}, {"glide", 200.f}});
    const Samples slid = poly.left(seconds(1.0), {noteOn(0, 57), noteOff(seconds(0.3), 57), noteOn(seconds(0.5), 69)});
    CHECK(dominantFreq(slice(slid, seconds(0.5), seconds(0.55))) < 330.0);
    CHECK_APPROX_REL(dominantFreq(slice(slid, seconds(0.75), seconds(1.0))), 440.0, 1e-3);
}

TEST_CASE("supersaw: a note's bend, and MIDI pitch bend") {
    Player p({{"s1_saws", 1.f}});
    const Samples bent = p.left(seconds(1.0), {noteOn(0, 57, 127, 7), bend(seconds(0.5), 57, 7, 12.f)});
    CHECK_APPROX_REL(dominantFreq(slice(bent, seconds(0.1), seconds(0.45))), 220.0, 1e-3);
    CHECK_APPROX_REL(dominantFreq(slice(bent, seconds(0.55), seconds(0.95))), 440.0, 1e-3);
    // The wheel all the way up: Bend Range semitones, for every note.
    for (const float range : {2.f, 12.f}) {
        Player q({{"s1_saws", 1.f}, {"bend_range", range}});
        const Samples out = q.left(seconds(1.0), {noteOn(0, 57), midi(seconds(0.5), 0xE0, 0x7F, 0x7F)});
        CHECK_APPROX_REL(dominantFreq(slice(out, seconds(0.55), seconds(0.95))), keyHz(57 + range * 8191.0 / 8192.0),
                         1e-3);
    }
}

TEST_CASE("supersaw: the sustain pedal holds released notes") {
    Player p;
    const Samples out = p.left(seconds(1.2), {midi(0, 0xB0, 64, 127), noteOn(0, 60), noteOff(seconds(0.2), 60),
                                              midi(seconds(0.6), 0xB0, 64, 0)});
    CHECK(rms(slice(out, seconds(0.4), seconds(0.55))) > 0.05);  // held past its note-off
    CHECK(maxAbs(slice(out, seconds(0.8))) < 1e-4);              // released with the pedal
}

TEST_CASE("supersaw: a 17th note steals one, which fades out") {
    Player p;
    std::vector<Timed> notes;
    for (int k = 0; k < 17; ++k) notes.push_back(noteOn(seconds(0.01 * k), 40 + 2 * k));
    const Samples out = p.left(seconds(0.5), notes);
    CHECK(allFinite(out));
    float most = 0.f;
    for (const float voices : p.display("voices")) most = std::max(most, voices);
    CHECK_EQ(most, 16.f);
    // All notes off: everything released.
    const Samples off = p.left(seconds(0.5), {midi(0, 0xB0, 123, 0)});
    CHECK(maxAbs(slice(off, seconds(0.1))) < 1e-4);
}

TEST_CASE("supersaw: changing Saws fades saws in and out") {
    // From 3 saws to 32 mid-note: against the same note left at 3, the difference starts from nothing and
    // grows as the gains glide (no step), and nothing changes before.
    const auto render = [](bool change) {
        Player p({{"s1_saws", 3.f}, {"s1_width", 0.f}, {"s1_detune", 100.f}});
        std::vector<Change> changes;
        if (change) changes = {{seconds(0.5), "s1_saws", 32.f}, {seconds(1.0), "s1_saws", 3.f}};
        return std::make_pair(p.play(seconds(1.5), {noteOn(0, 57)}, 256, changes).first,
                              p.device().latencySamples());
    };
    const auto [changed, latency] = render(true);
    const Samples still = render(false).first;
    CHECK(allFinite(changed));
    Samples difference(changed.size());
    for (size_t i = 0; i < changed.size(); ++i) difference[i] = changed[i] - still[i];
    const int64_t at = seconds(0.5);
    CHECK(allEqual(slice(difference, 0, at), 0.0));
    const double onset = maxAbs(slice(difference, at, at + latency + 12));
    const double later = rms(slice(difference, at + seconds(0.015), at + seconds(0.025)));
    INFO("onset " + std::to_string(onset) + ", later " + std::to_string(later));
    CHECK(later > 0.01);
    CHECK(onset < 0.1 * later);
}

TEST_CASE("supersaw: the output doesn't depend on how blocks are cut") {
    // Chords, a bend, the wheel, automation of Detune, Width and the cutoff, the filter's envelope: the same to
    // the bit in blocks of 1024, 64, 37 and of random lengths. Then Mono with glide.
    const std::vector<Timed> notes = {noteOn(100, 48, 100, 1),  noteOn(100, 55, 90, 2),  noteOn(5000, 60, 80, 3),
                                      bend(9000, 55, 2, 2.5f),  midi(12000, 0xE0, 0, 0x50), noteOff(20000, 48, 1),
                                      noteOn(21111, 67, 127, 4), noteOff(30000, 55, 2),   noteOff(30001, 60, 3)};
    const std::vector<Change> changes = {{7000, "s1_detune", 80.f}, {15000, "s1_width", 20.f}, {18000, "cutoff", 900.f}};
    const Values values = {{"s1_saws", 9.f},       {"s2_on", 1.f},        {"s2_saws", 5.f},   {"drift", 40.f},
                           {"filter_type", 2.f},   {"cutoff", 3000.f},    {"resonance", 30.f}, {"filter_env", 30.f},
                           {"key_track", 50.f},    {"sub", 30.f},         {"velocity", 60.f}};
    const auto render = [&](const std::function<int()>& blocks, const Values& more) {
        Values all = values;
        all.insert(all.end(), more.begin(), more.end());
        Player p(all);
        return p.play(seconds(0.8), notes, blocks, changes);
    };
    for (const Values& more : {Values{}, Values{{"mode", 1.f}, {"glide", 60.f}}}) {
        const auto reference = render([] { return 1024; }, more);
        CHECK(rms(reference.first) > 0.01);
        uint32_t seed = 1;
        for (const auto& blocks : std::vector<std::function<int()>>{
                 [] { return 64; }, [] { return 37; },
                 [&seed] {
                     seed = seed * 1664525u + 1013904223u;
                     return 1 + static_cast<int>(seed >> 22);
                 }}) {
            const auto out = render(blocks, more);
            CHECK_ARRAY_EQUAL(out.first, reference.first);
            CHECK_ARRAY_EQUAL(out.second, reference.second);
        }
    }
}

TEST_CASE("supersaw: through the engine, the same twice, in stereo") {
    const auto render = [] {
        sub::Engine engine;
        const uint32_t track = engine.addTrack();
        engine.addBuiltinProcessor(engine.trackChain(track), "supersaw", -1);
        engine.setTrackNotes(track, {{0.5, 1.0, 60}, {0.5, 1.0, 64}, {0.5, 1.0, 67}});
        return engine.renderOffline(0.0, 2 * kSampleRate);
    };
    const Samples a = render(), b = render();
    CHECK_ARRAY_EQUAL(a, b);
    const Samples left = channel(a, 0), right = channel(a, 1);
    // Its latency compensated, the notes land on their beat (the linear-phase filters' faint pre-echo just before).
    CHECK(allEqual(slice(left, 0, kBeat / 2 - 28), 0.0));
    CHECK(maxAbs(slice(left, 0, kBeat / 2)) < 1e-3);
    CHECK(maxAbs(slice(left, kBeat / 2, kBeat / 2 + 96)) > 0.01);
    CHECK(rms(slice(left, kBeat, 2 * kBeat)) > 0.05);
    CHECK(!allclose(left, right, 0.0, 1e-4));  // Width 100 %
    CHECK(allFinite(a));
}

TEST_CASE("supersaw: at every rate") {
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        INFO(std::to_string(rate));
        Player p({{"s1_saws", 1.f}}, rate);
        const Samples out = p.left(seconds(0.6, rate), {noteOn(0, 69)});
        CHECK(allFinite(out));
        CHECK_APPROX_REL(dominantFreq(slice(out, seconds(0.1, rate)), rate), 440.0, 1e-4);
        CHECK_NEAR(db(rms(slice(out, seconds(0.1, rate))) / (kVoiceGain / std::sqrt(3.0))), 0.0, 0.1);
    }
}

TEST_CASE("supersaw: what it costs") {
#if SUBTEST_TIMED
    // Its most: 16 notes of three stacks of 64 saws (3072 saws, at twice the rate) with the filter and drift on,
    // 2 s at 48 kHz in this thread's CPU time, the best of two. A bound for pathologies only.
    double best = 1e9;
    for (int run = 0; run < 2; ++run) {
        Player p({{"s1_saws", 64.f}, {"s2_on", 1.f}, {"s2_saws", 64.f}, {"s3_on", 1.f}, {"s3_saws", 64.f},
                  {"drift", 50.f}, {"filter_type", 2.f}, {"cutoff", 5000.f}});
        std::vector<Timed> notes;
        for (int k = 0; k < 16; ++k) notes.push_back(noteOn(0, 40 + 3 * k));
        const double start = threadSeconds();
        const auto out = p.play(seconds(2.0), notes, 256);
        best = std::min(best, threadSeconds() - start);
        CHECK(allFinite(out.first));
    }
    INFO("2 s in " + std::to_string(best) + " s");
    CHECK(best < 1.5);
#endif
}

TEST_CASE("supersaw: a new Mode takes the next note, asleep or not") {
    // Asleep (or not yet woken) when Mode changes: the next note plays in it, and goes on playing.
    Player p({{"s1_saws", 1.f}});
    // Played, then silent long enough to sleep, ending off the control grid (so it wakes inside a period).
    p.left(seconds(0.5) + 5, {noteOn(0, 57), noteOff(seconds(0.05), 57)});
    p.set("mode", 1.f);
    const Samples woken = p.left(seconds(0.3), {noteOn(3, 57), noteOn(seconds(0.15), 69)});  // (in the period it wakes in)
    CHECK(rms(slice(woken, seconds(0.05), seconds(0.14))) > 0.05);  // still sounding long after a steal's fade
    CHECK_APPROX_REL(dominantFreq(slice(woken, seconds(0.17), seconds(0.29))), 440.0, 1e-3);  // one voice moved to it
    for (const float voices : p.display("voices")) CHECK(voices <= 1.f);
    // Awake: Mode changes where a note starts (both at once): what played fades out, the new note plays on in it.
    Player q({{"s1_saws", 1.f}});
    const auto [out, right] = q.play(seconds(0.5), {noteOn(0, 57), noteOn(seconds(0.2) + 7, 69)}, 256,
                                     {{seconds(0.2) + 7, "mode", 1.f}});
    CHECK(rms(slice(out, seconds(0.3), seconds(0.5))) > 0.05);
    CHECK_APPROX_REL(dominantFreq(slice(out, seconds(0.3), seconds(0.5))), 440.0, 1e-3);
}
