#pragma once
// The song's tempo and time signature along the timeline, and the conversions
// between its beats (quarter notes), seconds and bars that follow from them.
// No Qt: the engine, the application layer and the UI share it.
//
// Tempo: breakpoints (beat, BPM). Between two, the tempo goes in a straight line
// in beats from one to the other (a ramp; the same BPM twice: steady); two at the
// same beat are a jump. Before the first and after the last it stays as they
// are. Seconds are the tempo's integral, worked out exactly: over a ramp from
// T0 to T1 BPM across L beats, t(b) = 60 L / (T1 - T0) * ln(T(b) / T0), and back
// again by its inverse, so beats -> seconds -> beats returns the same beat.
//
// Time signature: changes (beat, numerator, denominator), each starting a bar.
// A bar is numerator * 4 / denominator beats long; a change that doesn't fall on
// a bar line of the one before starts its bar there all the same (the bar before
// is cut short), as Live does. Bars are counted from 0 at beat 0.

#include <cstdint>
#include <vector>

namespace sub {

class TempoMap {
public:
    struct Point {
        double beat = 0.0;
        double bpm = 120.0;
        friend bool operator==(const Point&, const Point&) = default;
    };
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 999.0;

    TempoMap() : TempoMap(120.0) {}
    explicit TempoMap(double bpm);
    // Points in any order (sorted by beat, stably: two at one beat are a jump in
    // that order); BPMs clamped to kMinBpm..kMaxBpm; beats below 0 count as 0.
    // None: 120 BPM.
    explicit TempoMap(std::vector<Point> points);

    const std::vector<Point>& points() const { return points_; }
    bool constant() const { return constant_; }  // one tempo throughout
    double bpmAt(double beat) const;
    double secondsAt(double beat) const;          // from beat 0 (negative before it)
    double beatAt(double seconds) const;          // the inverse
    // The same in samples at `rate` (rounded to the nearest sample).
    int64_t sampleAt(double beat, double rate) const;
    double beatAtSample(double sample, double rate) const;
    // Seconds between two beats (negative if `to` is before `from`).
    double secondsBetween(double from, double to) const { return secondsAt(to) - secondsAt(from); }

    friend bool operator==(const TempoMap& a, const TempoMap& b) { return a.points_ == b.points_; }

private:
    // Over segment `i` (from points_[i] to points_[i + 1], or after the last).
    double segmentSeconds(size_t i, double beat) const;
    double segmentBeat(size_t i, double seconds) const;
    size_t segmentAtBeat(double beat) const;
    size_t segmentAtSeconds(double seconds) const;

    std::vector<Point> points_;    // at least one
    std::vector<double> seconds_;  // at each point
    bool constant_ = true;
};

class MeterMap {
public:
    struct Change {
        double beat = 0.0;
        int numerator = 4;
        int denominator = 4;
        double beatsPerBar() const { return numerator * 4.0 / denominator; }
        friend bool operator==(const Change&, const Change&) = default;
    };
    // Where a beat is: its bar (0-based), the beat that bar starts at, its length,
    // and its signature.
    struct Bar {
        int64_t index = 0;
        double start = 0.0;
        double length = 4.0;
        int numerator = 4;
        int denominator = 4;
    };

    MeterMap() : MeterMap(4, 4) {}
    MeterMap(int numerator, int denominator);
    // Changes in any order (sorted by beat; the last of two at one beat wins).
    // One at beat 0 is the song's own; without one there, the first change's
    // signature holds from beat 0. None: 4/4. Signatures that aren't valid (a
    // numerator outside 1..32, a denominator not a power of two up to 32) are
    // left out.
    explicit MeterMap(std::vector<Change> changes);

    const std::vector<Change>& changes() const { return changes_; }
    bool constant() const { return changes_.size() == 1; }
    static bool valid(int numerator, int denominator);

    Bar barAt(double beat) const;          // (beats below 0: bars of the first signature, backwards)
    double barStart(int64_t bar) const;    // the beat bar `bar` starts at
    const Change& at(double beat) const;   // the signature in force

    friend bool operator==(const MeterMap& a, const MeterMap& b) { return a.changes_ == b.changes_; }

private:
    std::vector<Change> changes_;  // at least one, the first at beat 0
    std::vector<int64_t> bars_;    // the bar each change starts
};

}  // namespace sub
