#include "music/TempoMap.h"

#include <algorithm>
#include <cmath>

namespace sub {

namespace {

constexpr double kBeatEpsilon = 1e-9;  // beats this close are the same (bar lines)
constexpr double kStraight = 1e-4;     // a curve's exponent this small: a straight line

// The seconds over x (0..1) of a curved segment, BPM = p + k e^(a x), `length` beats long:
// 60 length * integral of 1 / (p + k e^(a u)) from 0 to x.
double curvedSeconds(double length, double a, double p, double k, double x) {
    const double t0 = p + k;
    if (std::abs(p) > 1e-6 * std::abs(t0)) {
        // (1 / p) [x - ln((p + k e^(a x)) / t0) / a]
        const double ratio = std::log1p(k * std::expm1(a * x) / t0);
        return 60.0 * length / p * (x - ratio / a);
    }
    // Nearly k e^(a x): 1 / (k e^(au)) * sum of (-q e^(-au))^n, q = p / k, integrated term by term.
    const double q = p / k;
    double sum = 0.0;
    double power = 1.0;
    for (int n = 0; n < 4; ++n) {
        const double m = (n + 1) * a;
        sum += power * -std::expm1(-m * x) / m;
        power *= -q;
    }
    return 60.0 * length / k * sum;
}


}  // namespace

// ---------------------------------------------------------------------------
// Tempo

TempoMap::TempoMap(double bpm) : TempoMap(std::vector<Point>{{0.0, bpm}}) {}

TempoMap::TempoMap(std::vector<Point> points) {
    for (Point& point : points) {
        point.beat = std::isfinite(point.beat) ? std::max(0.0, point.beat) : 0.0;
        point.bpm = std::isfinite(point.bpm) ? std::clamp(point.bpm, kMinBpm, kMaxBpm) : 120.0;
        point.curve = std::isfinite(point.curve) ? std::clamp(point.curve, -1.0, 1.0) : 0.0;
    }
    std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.beat < b.beat; });
    if (points.empty()) points.push_back({0.0, 120.0});
    if (points.front().beat > 0.0) points.insert(points.begin(), {0.0, points.front().bpm});
    points_ = std::move(points);
    constant_ = std::all_of(points_.begin(), points_.end(), [&](const Point& p) { return p.bpm == points_.front().bpm; });
    seconds_.assign(points_.size(), 0.0);
    for (size_t i = 1; i < points_.size(); ++i) seconds_[i] = segmentSeconds(i - 1, points_[i].beat);
}

size_t TempoMap::segmentAtBeat(double beat) const {
    // The last point at or before it (of two at one beat, the second: the jump has happened).
    const auto after = std::upper_bound(points_.begin(), points_.end(), beat,
                                        [](double b, const Point& p) { return b < p.beat; });
    return after == points_.begin() ? 0 : static_cast<size_t>(after - points_.begin()) - 1;
}

size_t TempoMap::segmentAtSeconds(double seconds) const {
    const auto after = std::upper_bound(seconds_.begin(), seconds_.end(), seconds);
    return after == seconds_.begin() ? 0 : static_cast<size_t>(after - seconds_.begin()) - 1;
}

TempoMap::Shape TempoMap::shapeOf(size_t i) const {
    const Point& from = points_[i];
    const Point& to = points_[i + 1];
    Shape shape;
    shape.length = to.beat - from.beat;
    const double rise = to.bpm - from.bpm;
    // The same bend as an automation segment's: its curve, turned round when it goes down.
    const double bend = rise >= 0.0 ? from.curve : -from.curve;
    const double a = -bend * kCurvature;
    if (std::abs(a) < kStraight || rise == 0.0) return shape;
    shape.a = a;
    shape.k = rise / std::expm1(a);
    shape.p = from.bpm - shape.k;
    return shape;
}

double TempoMap::segmentSeconds(size_t i, double beat) const {
    const Point& from = points_[i];
    const double t0 = from.bpm;
    const double offset = beat - from.beat;
    if (i + 1 >= points_.size() || beat < from.beat) return seconds_[i] + 60.0 * offset / t0;  // steady after the last
    const Point& to = points_[i + 1];
    const Shape shape = shapeOf(i);
    if (shape.a != 0.0 && shape.length > 0.0) {
        return seconds_[i] + curvedSeconds(shape.length, shape.a, shape.p, shape.k, offset / shape.length);
    }
    const double slope = shape.length > 0.0 ? (to.bpm - t0) / shape.length : 0.0;  // BPM a beat
    if (slope == 0.0) return seconds_[i] + 60.0 * offset / t0;
    // The integral of 60 / (t0 + slope * x) from 0 to offset.
    return seconds_[i] + 60.0 / slope * std::log1p(slope * offset / t0);
}

double TempoMap::segmentBeat(size_t i, double seconds) const {
    const Point& from = points_[i];
    const double t0 = from.bpm;
    const double elapsed = seconds - seconds_[i];
    if (i + 1 >= points_.size() || elapsed < 0.0) return from.beat + elapsed * t0 / 60.0;
    const Point& to = points_[i + 1];
    const Shape shape = shapeOf(i);
    if (shape.a != 0.0 && shape.length > 0.0) {
        // Newton's method on x along it, kept within what brackets it (the seconds only grow).
        const double total = seconds_[i + 1] - seconds_[i];
        if (elapsed >= total) return to.beat;
        double low = 0.0, high = 1.0;
        double x = elapsed / total;
        for (int step = 0; step < 60; ++step) {
            const double error = curvedSeconds(shape.length, shape.a, shape.p, shape.k, x) - elapsed;
            (error > 0.0 ? high : low) = x;
            const double bpm = shape.p + shape.k * std::exp(shape.a * x);
            double next = x - error * bpm / (60.0 * shape.length);
            if (!(next > low && next < high)) next = 0.5 * (low + high);
            if (std::abs(next - x) <= 1e-16) break;
            x = next;
        }
        return from.beat + x * shape.length;
    }
    const double slope = shape.length > 0.0 ? (to.bpm - t0) / shape.length : 0.0;
    if (slope == 0.0) return from.beat + elapsed * t0 / 60.0;
    return from.beat + t0 / slope * std::expm1(slope * elapsed / 60.0);
}

double TempoMap::bpmAt(double beat) const {
    const size_t i = segmentAtBeat(beat);
    const Point& from = points_[i];
    if (i + 1 >= points_.size() || beat <= from.beat) return from.bpm;
    const Point& to = points_[i + 1];
    const Shape shape = shapeOf(i);
    if (shape.length <= 0.0) return to.bpm;
    const double x = (beat - from.beat) / shape.length;
    if (shape.a != 0.0) return shape.p + shape.k * std::exp(shape.a * x);
    return from.bpm + (to.bpm - from.bpm) * x;
}

double TempoMap::secondsAt(double beat) const {
    if (beat <= 0.0) return 60.0 * beat / points_.front().bpm;
    return segmentSeconds(segmentAtBeat(beat), beat);
}

double TempoMap::beatAt(double seconds) const {
    if (seconds <= 0.0) return seconds * points_.front().bpm / 60.0;
    return segmentBeat(segmentAtSeconds(seconds), seconds);
}

int64_t TempoMap::sampleAt(double beat, double rate) const { return std::llround(secondsAt(beat) * rate); }

double TempoMap::beatAtSample(double sample, double rate) const { return beatAt(sample / rate); }

// ---------------------------------------------------------------------------
// Time signature

bool MeterMap::valid(int numerator, int denominator) {
    const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
    return numerator >= 1 && numerator <= 32 && powerOfTwo && denominator <= 32;
}

MeterMap::MeterMap(int numerator, int denominator) : MeterMap(std::vector<Change>{{0.0, numerator, denominator}}) {}

MeterMap::MeterMap(std::vector<Change> changes) {
    std::erase_if(changes, [](const Change& c) { return !valid(c.numerator, c.denominator) || !std::isfinite(c.beat); });
    for (Change& change : changes) change.beat = std::max(0.0, change.beat);
    std::stable_sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return a.beat < b.beat; });
    for (const Change& change : changes) {  // (of two at one beat, the last)
        if (!changes_.empty() && std::abs(changes_.back().beat - change.beat) < kBeatEpsilon) {
            changes_.back() = change;
        } else {
            changes_.push_back(change);
        }
    }
    if (changes_.empty()) changes_.push_back({0.0, 4, 4});
    if (changes_.front().beat > 0.0) {
        Change first = changes_.front();
        first.beat = 0.0;
        changes_.insert(changes_.begin(), first);
    }
    bars_.assign(changes_.size(), 0);
    for (size_t i = 1; i < changes_.size(); ++i) {
        const double span = (changes_[i].beat - changes_[i - 1].beat) / changes_[i - 1].beatsPerBar();
        bars_[i] = bars_[i - 1] + static_cast<int64_t>(std::ceil(span - kBeatEpsilon));  // (a bar cut short counts)
    }
}

const MeterMap::Change& MeterMap::at(double beat) const {
    const auto after = std::upper_bound(changes_.begin(), changes_.end(), beat + kBeatEpsilon,
                                        [](double b, const Change& c) { return b < c.beat; });
    return after == changes_.begin() ? changes_.front() : *(after - 1);
}

MeterMap::Bar MeterMap::barAt(double beat) const {
    const Change& change = at(beat);
    const size_t i = static_cast<size_t>(&change - changes_.data());
    const double length = change.beatsPerBar();
    const auto k = static_cast<int64_t>(std::floor((beat - change.beat) / length + kBeatEpsilon));
    Bar bar;
    bar.index = bars_[i] + k;
    bar.start = change.beat + static_cast<double>(k) * length;
    bar.length = length;
    if (i + 1 < changes_.size()) bar.length = std::min(length, changes_[i + 1].beat - bar.start);
    bar.numerator = change.numerator;
    bar.denominator = change.denominator;
    return bar;
}

double MeterMap::barStart(int64_t bar) const {
    const auto after = std::upper_bound(bars_.begin(), bars_.end(), bar);
    const size_t i = after == bars_.begin() ? 0 : static_cast<size_t>(after - bars_.begin()) - 1;
    return changes_[i].beat + static_cast<double>(bar - bars_[i]) * changes_[i].beatsPerBar();
}

}  // namespace sub
