#include "music/Groove.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>

namespace sub::groove {
namespace {

constexpr double kEps = 1e-9;
// A step held after the one before it lands at least this much of a step's time after it.
constexpr double kMinGap = 1e-3;
// How far back a step may be held by those before it (more can't be: offsets are bounded).
constexpr int kMaxHold = 512;

double clampFinite(double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

int64_t floorDiv(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A triangular jitter in (-1, 1), more often small than large, from the seed, the cycle and the step.
double jitter(uint32_t seed, int64_t cycle, int step) {
    const auto c = static_cast<uint64_t>(cycle);
    uint32_t h = mix(seed ^ 0x9e3779b9U);
    h = mix(h ^ static_cast<uint32_t>(c) ^ mix(static_cast<uint32_t>(c >> 32) + 0x632be5abU));
    h = mix(h ^ (static_cast<uint32_t>(step) * 0x85ebca6bU));
    const uint32_t g = mix(h + 0x27d4eb2fU);
    constexpr double kScale = 1.0 / 4294967296.0;
    return static_cast<double>(h) * kScale + static_cast<double>(g) * kScale - 1.0;
}

Step heldStep(const Step& step) {
    return {clampFinite(step.timing, -kMaxTiming, kMaxTiming, 0.0),
            clampFinite(step.timingMs, -kMaxTimingMs, kMaxTimingMs, 0.0),
            clampFinite(step.velocity, -1.0, 1.0, 0.0), clampFinite(step.length, kMinLength, kMaxLength, 1.0)};
}

// Where a lane's warp puts the steps, in seconds.
struct Warp {
    const Lane& lane;
    int steps;            // in a cycle
    double stepBeats;
    const TempoMap& tempo;
    double timing;        // amounts, held to their ranges
    double random;
    uint32_t seed;

    // Global step k (cycle floor(k / steps)) moved by its offsets.
    double landing(int64_t k) const {
        const int64_t cycle = floorDiv(k, steps);
        const int i = static_cast<int>(k - cycle * steps);
        const Step& step = lane.steps[static_cast<size_t>(i)];
        double offset = clampFinite(step.timing, -kMaxTiming, kMaxTiming, 0.0) * timing;
        if (random > 0.0) offset += random * kRandomSteps * jitter(seed, cycle, i);
        const double ms = clampFinite(step.timingMs, -kMaxTimingMs, kMaxTimingMs, 0.0) * timing;
        return tempo.secondsAt((static_cast<double>(k) + offset) * stepBeats) + ms / 1000.0;
    }

    // Step k's landing, held after every step before it: the latest of each
    // earlier step's landing plus kMinGap of the time between them and k (an
    // earlier step that can't be later than k's own landing ends the search).
    double held(int64_t k) const {
        const double at = tempo.secondsAt(static_cast<double>(k) * stepBeats);
        double best = landing(k);
        const double reach = kMaxTiming * timing + random * kRandomSteps;  // steps, either way
        const double reachMs = kMaxTimingMs * timing / 1000.0;
        for (int64_t j = k - 1; k - j <= kMaxHold; --j) {
            const double from = tempo.secondsAt(static_cast<double>(j) * stepBeats);
            const double gap = kMinGap * (at - from);
            const double latest = tempo.secondsAt((static_cast<double>(j) + reach) * stepBeats) + reachMs + gap;
            if (latest <= best) break;
            best = std::max(best, landing(j) + gap);
        }
        return best;
    }
};

// Interpolates the lane's steps at `at` steps from beat 0 (fractions between two steps).
Step stepAt(const Lane& lane, int steps, double at, int& nearest) {
    const double floor = std::floor(at);
    const double frac = at - floor;
    const int64_t k = static_cast<int64_t>(floor);
    const auto index = [steps](int64_t s) { return static_cast<size_t>(s - floorDiv(s, steps) * steps); };
    const Step a = heldStep(lane.steps[index(k)]);
    nearest = static_cast<int>(index(frac < 0.5 ? k : k + 1));
    if (frac < kEps) return a;
    const Step b = heldStep(lane.steps[index(k + 1)]);
    const auto lerp = [frac](double x, double y) { return x + (y - x) * frac; };
    return {lerp(a.timing, b.timing), lerp(a.timingMs, b.timingMs), lerp(a.velocity, b.velocity),
            lerp(a.length, b.length)};
}

// Fills the steps no note fell on from their nearest filled neighbours either way round the cycle.
void fillEmpty(std::vector<Step>& steps, const std::vector<int>& counts) {
    const int n = static_cast<int>(steps.size());
    std::vector<int> filled;
    for (int i = 0; i < n; ++i) {
        if (counts[static_cast<size_t>(i)] > 0) filled.push_back(i);
    }
    if (filled.empty()) return;
    for (int i = 0; i < n; ++i) {
        if (counts[static_cast<size_t>(i)] > 0) continue;
        // The filled step before it and after it, round the cycle.
        const auto after = std::upper_bound(filled.begin(), filled.end(), i);
        const int next = after == filled.end() ? filled.front() : *after;
        const int prev = after == filled.begin() ? filled.back() : *(after - 1);
        const int toPrev = (i - prev + n) % n;
        const int toNext = (next - i + n) % n;
        const double w = toPrev + toNext > 0 ? static_cast<double>(toPrev) / (toPrev + toNext) : 0.0;
        const Step& a = steps[static_cast<size_t>(prev)];
        const Step& b = steps[static_cast<size_t>(next)];
        const auto lerp = [w](double x, double y) { return x + (y - x) * w; };
        steps[static_cast<size_t>(i)] = {lerp(a.timing, b.timing), lerp(a.timingMs, b.timingMs),
                                         lerp(a.velocity, b.velocity), lerp(a.length, b.length)};
    }
}

Lane laneFromNotes(const std::vector<NoteTime>& notes, const Groove& shape, std::vector<int> keys) {
    const int n = stepsPerCycle(shape);
    Lane lane{std::move(keys), std::vector<Step>(static_cast<size_t>(n))};
    if (notes.empty()) return lane;
    double velocitySum = 0.0, lengthSum = 0.0;
    for (const NoteTime& note : notes) {
        velocitySum += note.velocity;
        lengthSum += note.lengthBeats;
    }
    const double averageVelocity = velocitySum / static_cast<double>(notes.size());
    const double averageLength = lengthSum / static_cast<double>(notes.size());
    std::vector<int> counts(static_cast<size_t>(n), 0);
    std::vector<Step> sums(static_cast<size_t>(n), Step{0.0, 0.0, 0.0, 0.0});
    for (const NoteTime& note : notes) {
        const double at = note.beat / shape.stepBeats;
        const double nearest = std::floor(at + 0.5);
        const auto i = static_cast<size_t>(static_cast<int64_t>(nearest) - floorDiv(static_cast<int64_t>(nearest), n) * n);
        ++counts[i];
        sums[i].timing += at - nearest;
        sums[i].velocity += (note.velocity - averageVelocity) / 127.0;
        sums[i].length += averageLength > 0.0 ? note.lengthBeats / averageLength : 1.0;
    }
    for (size_t i = 0; i < lane.steps.size(); ++i) {
        if (counts[i] == 0) continue;
        const double count = counts[i];
        lane.steps[i] = heldStep({sums[i].timing / count, 0.0, sums[i].velocity / count, sums[i].length / count});
    }
    fillEmpty(lane.steps, counts);
    return lane;
}

void appendNumber(std::string& out, double value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
}

bool parseNumber(std::string_view text, double& value) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && std::isfinite(value);
}

// The words of a line, split at spaces.
std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start < line.size()) {
        const size_t space = line.find(' ', start);
        const size_t end = space == std::string_view::npos ? line.size() : space;
        if (end > start) out.push_back(line.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

}  // namespace

int stepIndex(double beats) {
    int best = kDefaultStep;
    double distance = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < kSteps.size(); ++i) {
        const double d = std::abs(std::log(std::max(beats, 1e-6) / kSteps[i].beats));
        if (d < distance) {
            distance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int stepsPerCycle(const Groove& groove) {
    if (!(groove.stepBeats > 0.0) || !std::isfinite(groove.stepBeats)) return 1;
    const double steps = std::round(std::max(1, groove.bars) * kBarBeats / groove.stepBeats);
    return static_cast<int>(std::clamp(steps, 1.0, static_cast<double>(kMaxSteps)));
}

double cycleBeats(const Groove& groove) { return stepsPerCycle(groove) * groove.stepBeats; }

Groove normalized(Groove groove) {
    // A step dividing a beat into whole steps (up to 24), else the nearest choice.
    const double perBeat = groove.stepBeats > 0.0 && std::isfinite(groove.stepBeats) ? 1.0 / groove.stepBeats : 0.0;
    const double whole = std::round(perBeat);
    if (whole >= 1.0 && whole <= 24.0 && std::abs(perBeat - whole) < 1e-6) {
        groove.stepBeats = 1.0 / whole;
    } else {
        groove.stepBeats = kSteps[static_cast<size_t>(stepIndex(groove.stepBeats))].beats;
    }
    groove.bars = groove.bars <= 1 ? 1 : groove.bars == 2 ? 2 : 4;
    while (groove.bars > 1 && std::round(groove.bars * kBarBeats / groove.stepBeats) > kMaxSteps) groove.bars /= 2;
    groove.base = groove.base > 0.0 && std::isfinite(groove.base) ? std::clamp(groove.base, 1.0 / 64, kBarBeats)
                                                                  : groove.stepBeats;
    const auto n = static_cast<size_t>(stepsPerCycle(groove));
    std::vector<Lane> lanes;
    bool hasDefault = false;
    std::vector<char> taken(128, 0);
    for (Lane& lane : groove.lanes) {
        const bool isDefault = lane.keys.empty();
        if (isDefault && hasDefault) continue;
        std::vector<int> keys;
        for (const int key : lane.keys) {
            if (key < 0 || key > 127 || taken[static_cast<size_t>(key)]) continue;
            taken[static_cast<size_t>(key)] = 1;
            keys.push_back(key);
        }
        if (!isDefault && keys.empty()) continue;  // every key it named is another's
        std::sort(keys.begin(), keys.end());
        lane.keys = std::move(keys);
        lane.steps.resize(n);
        for (Step& step : lane.steps) step = heldStep(step);
        if (isDefault) {
            hasDefault = true;
            lanes.insert(lanes.begin(), std::move(lane));
        } else {
            lanes.push_back(std::move(lane));
        }
    }
    if (!hasDefault) lanes.insert(lanes.begin(), Lane{{}, std::vector<Step>(n)});
    groove.lanes = std::move(lanes);
    return groove;
}

const Lane* laneFor(const Groove& groove, int key) {
    const Lane* fallback = nullptr;
    for (const Lane& lane : groove.lanes) {
        if (lane.keys.empty()) {
            if (!fallback) fallback = &lane;
        } else if (std::find(lane.keys.begin(), lane.keys.end(), key) != lane.keys.end()) {
            return &lane;
        }
    }
    return fallback;
}

Placed place(const Groove& groove, double beat, int key, double velocity, double lengthBeats, const TempoMap& tempo,
             const Amounts& amounts) {
    Placed out{beat, std::clamp(std::isfinite(velocity) ? velocity : 100.0, 1.0, 127.0),
               std::isfinite(lengthBeats) ? std::max(0.0, lengthBeats) : 0.0, 0};
    const Lane* lane = laneFor(groove, key);
    const int n = stepsPerCycle(groove);
    if (!lane || static_cast<int>(lane->steps.size()) != n || !(groove.stepBeats > 0.0) || !std::isfinite(beat)) {
        return out;
    }
    const double step = groove.stepBeats;
    const double base = groove.base > 0.0 ? groove.base : step;
    // Quantize: toward the base grid first.
    const double grid = std::floor(beat / base + 0.5) * base;
    const double at = beat + (grid - beat) * clampFinite(amounts.quantize, 0.0, 1.0, 0.0);

    // Timing: the warp at `at`, between the steps either side of it (in proportion of their time).
    const Warp warp{*lane, n, step, tempo, clampFinite(amounts.timing, 0.0, kMaxTimingAmount, 0.0),
                    clampFinite(amounts.random, 0.0, 1.0, 0.0), amounts.seed};
    const double x = at / step;
    const double floor = std::floor(x);
    const auto k = static_cast<int64_t>(floor);
    const double from = tempo.secondsAt(floor * step);
    const double to = tempo.secondsAt((floor + 1.0) * step);
    const double t = to > from ? (tempo.secondsAt(at) - from) / (to - from) : x - floor;
    const double here = warp.held(k);
    const double next = warp.held(k + 1);
    out.beat = tempo.beatAt(here + std::clamp(t, 0.0, 1.0) * (next - here));

    // Velocity and length: the step the note falls on (the base grid's point), interpolated between two.
    const Step values = stepAt(*lane, n, grid / step, out.step);
    out.velocity = std::clamp(out.velocity + values.velocity * 127.0 * clampFinite(amounts.velocity, -1.0, 1.0, 0.0),
                              1.0, 127.0);
    out.lengthBeats *= 1.0 + (values.length - 1.0) * clampFinite(amounts.length, 0.0, 1.0, 0.0);
    return out;
}

Groove swing(double percent, double stepBeats) {
    Groove groove;
    groove.stepBeats = stepBeats;
    groove.base = stepBeats;
    groove = normalized(std::move(groove));
    groove.base = groove.stepBeats;
    const double late = 2.0 * clampFinite(percent, kMinSwing, kMaxSwing, kMinSwing) / 100.0 - 1.0;
    std::vector<Step>& steps = groove.lanes.front().steps;
    for (size_t i = 1; i < steps.size(); i += 2) steps[i].timing = late;
    return groove;
}

std::optional<double> swingOf(const Groove& groove) {
    if (groove.lanes.size() != 1 || !groove.lanes.front().keys.empty()) return std::nullopt;
    if (std::abs(groove.base - groove.stepBeats) > kEps) return std::nullopt;
    const std::vector<Step>& steps = groove.lanes.front().steps;
    if (steps.empty() || steps.size() % 2 != 0) return std::nullopt;
    const double late = steps.size() > 1 ? steps[1].timing : 0.0;
    if (late < -kEps || late > kMaxTiming + kEps) return std::nullopt;
    for (size_t i = 0; i < steps.size(); ++i) {
        const Step& s = steps[i];
        if (std::abs(s.timing - (i % 2 != 0 ? late : 0.0)) > 1e-9 || s.timingMs != 0.0 || s.velocity != 0.0 ||
            s.length != 1.0) {
            return std::nullopt;
        }
    }
    return (late + 1.0) * 50.0;
}

Groove fromNotes(std::span<const NoteTime> notes, double stepBeats, int bars, bool perKeyLanes) {
    Groove shape;
    shape.stepBeats = stepBeats;
    shape.bars = bars;
    shape = normalized(std::move(shape));
    shape.base = shape.stepBeats;
    std::vector<NoteTime> all;
    std::map<int, std::vector<NoteTime>> byKey;
    for (const NoteTime& note : notes) {
        if (!std::isfinite(note.beat) || !std::isfinite(note.velocity) || !std::isfinite(note.lengthBeats)) continue;
        all.push_back(note);
        byKey[note.key].push_back(note);
    }
    Groove groove = shape;
    groove.lanes = {laneFromNotes(all, shape, {})};
    if (perKeyLanes && byKey.size() > 1) {
        for (const auto& [key, keyNotes] : byKey) {
            if (key >= 0 && key <= 127) groove.lanes.push_back(laneFromNotes(keyNotes, shape, {key}));
        }
    }
    return normalized(std::move(groove));
}

Groove fromOnsets(std::span<const double> onsetBeats, std::span<const float> levels, double stepBeats, int bars) {
    float loudest = 0.f;
    for (size_t i = 0; i < onsetBeats.size() && i < levels.size(); ++i) {
        if (std::isfinite(levels[i])) loudest = std::max(loudest, std::abs(levels[i]));
    }
    std::vector<NoteTime> notes;
    for (size_t i = 0; i < onsetBeats.size(); ++i) {
        const float level = i < levels.size() && std::isfinite(levels[i]) ? std::abs(levels[i]) : loudest;
        const double share = loudest > 0.f ? static_cast<double>(level / loudest) : 1.0;
        notes.push_back({onsetBeats[i], 60, 1.0 + 126.0 * share, stepBeats});
    }
    return fromNotes(notes, stepBeats, bars, false);
}

std::string toText(const Groove& groove) {
    std::string out = "groove 1\nstep ";
    appendNumber(out, groove.stepBeats);
    out += "\nbars ";
    out += std::to_string(groove.bars);
    out += "\nbase ";
    appendNumber(out, groove.base);
    out += '\n';
    for (const Lane& lane : groove.lanes) {
        out += "lane";
        for (const int key : lane.keys) {
            out += ' ';
            out += std::to_string(key);
        }
        out += '\n';
        for (const Step& step : lane.steps) {
            out += "s ";
            appendNumber(out, step.timing);
            out += ' ';
            appendNumber(out, step.timingMs);
            out += ' ';
            appendNumber(out, step.velocity);
            out += ' ';
            appendNumber(out, step.length);
            out += '\n';
        }
    }
    return out;
}

std::optional<Groove> fromText(std::string_view text) {
    Groove groove;
    groove.lanes.clear();
    bool header = false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        start = end + 1;
        const std::vector<std::string_view> parts = words(line);
        if (parts.empty()) continue;
        const std::string_view word = parts.front();
        if (!header) {
            if (word != "groove" || parts.size() < 2 || parts[1] != "1") return std::nullopt;
            header = true;
            continue;
        }
        double value = 0.0;
        if (word == "step" && parts.size() == 2 && parseNumber(parts[1], value)) {
            groove.stepBeats = value;
        } else if (word == "bars" && parts.size() == 2 && parseNumber(parts[1], value)) {
            groove.bars = static_cast<int>(std::clamp(value, 1.0, 64.0));
        } else if (word == "base" && parts.size() == 2 && parseNumber(parts[1], value)) {
            groove.base = value;
        } else if (word == "lane") {
            Lane lane;
            for (size_t i = 1; i < parts.size(); ++i) {
                if (!parseNumber(parts[i], value)) return std::nullopt;
                lane.keys.push_back(static_cast<int>(value));
            }
            groove.lanes.push_back(std::move(lane));
        } else if (word == "s" && parts.size() == 5 && !groove.lanes.empty()) {
            Step step;
            if (!parseNumber(parts[1], step.timing) || !parseNumber(parts[2], step.timingMs) ||
                !parseNumber(parts[3], step.velocity) || !parseNumber(parts[4], step.length)) {
                return std::nullopt;
            }
            groove.lanes.back().steps.push_back(step);
        } else {
            return std::nullopt;
        }
    }
    if (!header) return std::nullopt;
    return normalized(std::move(groove));
}

}  // namespace sub::groove
