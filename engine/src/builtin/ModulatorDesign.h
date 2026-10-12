#pragma once
// What the modulators share (the LFO and the Envelope: builtin/devices/Lfo.cpp
// and Envelope.cpp; Modulation.h says what a modulator is): their drawn curve,
// its parameters, their rates and lengths in time or synced to the tempo, the
// smoothing of their value, and their displays.
//
// The curve is up to kPoints breakpoints (x: 0..1 of a cycle or of the
// envelope's length, y: the value, 0..1), each bending the segment after it as
// automation bends (automationShape: a positive bend bulges the segment upward,
// whichever way it goes). Two points at one x make a step. Before the first
// point the curve has its value, after the last the last's. Its points are
// hidden parameters that can't be automated ("p1_used", "p1_x", "p1_y",
// "p1_curve", ..), which the editor writes whole, as the Sidechain's are.
//
// Displays, one value per kDisplaySamples samples, pushed together so they stay
// in step: "value" (the modulator's output, 0..1), "phase" (where on its curve it
// is, 0..1; -1: an envelope at rest) and "key" (the Envelope's sidechain level,
// its peak over the stretch; the LFO's: 1 where a note restarted it, else 0).

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "Automation.h"
#include "Processor.h"
#include "builtin/DspBlocks.h"
#include "rt/FastMath.h"
#include "rt/RtUtils.h"

namespace sub::modulator {

constexpr int kPoints = 32;
constexpr int kDisplaySamples = 64;
enum PointParam { PointUsed = 0, PointX, PointY, PointCurve, kPointParams };

struct Point {
    float x = 0.f, y = 0.f, curve = 0.f;
};

// The curve shapes the editor offers (and the devices start with).
// A sine from the middle, up first (one of the LFO's shapes): nine points, each bend fitted to the sine's
// eighth (within 0.6 % of the range).
inline const std::vector<Point>& sinePoints() {
    static const std::vector<Point> kPoints = {
        {0.f, 0.5f, 0.055f},           {0.125f, 0.853553f, 0.329f}, {0.25f, 1.f, 0.329f},
        {0.375f, 0.853553f, 0.055f},   {0.5f, 0.5f, -0.055f},       {0.625f, 0.146447f, -0.329f},
        {0.75f, 0.f, -0.329f},         {0.875f, 0.146447f, -0.055f}, {1.f, 0.5f, 0.f},
    };
    return kPoints;
}
// The LFO's default: up from the bottom to the top at half the cycle, and down again.
inline const std::vector<Point>& trianglePoints() {
    static const std::vector<Point> kPoints = {{0.f, 0.f, 0.f}, {0.5f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
    return kPoints;
}
// The Envelope's: up at once, then falling away (a pluck).
inline const std::vector<Point>& pluckPoints() {
    static const std::vector<Point> kPoints = {{0.f, 0.f, 0.f}, {0.02f, 1.f, -0.55f}, {1.f, 0.f, 0.f}};
    return kPoints;
}

// A device's curve parameters from `first` on, kPoints of them, starting as `defaults`.
inline void appendPointParams(std::vector<ParamInfo>& list, const std::vector<Point>& defaults) {
    static const std::vector<std::string> kOffOn = {"Off", "On"};
    for (int p = 0; p < kPoints; ++p) {
        const std::string id = "p" + std::to_string(p + 1) + "_";
        const std::string name = "Point " + std::to_string(p + 1) + " ";
        const bool isDefault = p < static_cast<int>(defaults.size());
        const Point d = isDefault ? defaults[static_cast<size_t>(p)] : Point{1.f, 0.f, 0.f};
        ParamInfo used{id + "used", name + "Used", "", 0.f, 1.f, isDefault ? 1.f : 0.f, false, kOffOn};
        ParamInfo x{id + "x", name + "Time", "", 0.f, 1.f, d.x};
        ParamInfo y{id + "y", name + "Level", "", 0.f, 1.f, d.y};
        ParamInfo curve{id + "curve", name + "Curve", "", -1.f, 1.f, d.curve};
        for (ParamInfo* info : {&used, &x, &y, &curve}) {
            info->automatable = false;
            info->hidden = true;
            list.push_back(*info);
        }
    }
}

// The breakpoints in use, sorted by x, read from a device's parameters
// (`param(index)`, from the first point's `first`). Its value at x is
// automation's (within float rounding: its shapes are worked out with
// fastmath::exp, per sample on the audio thread).
class Curve {
public:
    template <typename Param>
    void load(const Param& param, int first) noexcept {
        count_ = 0;
        for (int p = 0; p < kPoints; ++p) {
            const int at = first + p * kPointParams;
            if (!automationSwitchOn(param(at + PointUsed))) continue;
            points_[static_cast<size_t>(count_++)] = {std::clamp(param(at + PointX), 0.f, 1.f),
                                                      std::clamp(param(at + PointY), 0.f, 1.f),
                                                      std::clamp(param(at + PointCurve), -1.f, 1.f)};
        }
        std::stable_sort(points_.begin(), points_.begin() + count_, [](const Point& a, const Point& b) { return a.x < b.x; });
        // Each segment's shape (automationShape's), worked out once: exp(a x) - 1 over exp(a) - 1.
        for (int p = 0; p + 1 < count_; ++p) {
            const Point& from = points_[static_cast<size_t>(p)];
            const Point& to = points_[static_cast<size_t>(p + 1)];
            const float bend = to.y >= from.y ? from.curve : -from.curve;
            Segment& s = segments_[static_cast<size_t>(p)];
            s.a = -bend * kAutomationCurvature;
            s.straight = std::abs(s.a) < 1e-4f;
            s.scale = s.straight ? 1.f : 1.f / std::expm1(s.a);
            s.span = to.x - from.x;
        }
    }

    int size() const noexcept { return count_; }
    // Where it rests: its last point's value (0 without points).
    float last() const noexcept { return count_ > 0 ? points_[static_cast<size_t>(count_ - 1)].y : 0.f; }

    // The curve at x (0..1): the first point's value before it, the last's after.
    float at(double x) const noexcept {
        if (count_ == 0) return 0.f;
        const auto fx = static_cast<float>(x);
        if (fx <= points_[0].x) return points_[0].y;
        for (int p = 1; p < count_; ++p) {
            const Point& to = points_[static_cast<size_t>(p)];
            if (fx >= to.x) continue;
            const Point& from = points_[static_cast<size_t>(p - 1)];
            const Segment& s = segments_[static_cast<size_t>(p - 1)];
            if (s.span <= 0.f) return to.y;
            const float t = (fx - from.x) / s.span;
            const float shaped = s.straight ? t : (fastmath::exp(s.a * t) - 1.f) * s.scale;
            return from.y + (to.y - from.y) * shaped;
        }
        return last();
    }

private:
    struct Segment {
        float a = 0.f, scale = 1.f, span = 0.f;
        bool straight = true;
    };
    std::array<Point, kPoints> points_{};
    std::array<Segment, kPoints> segments_{};
    int count_ = 0;
};

// The note values a rate or a length can be synced to (DspBlocks.h's).
inline const std::vector<std::string>& syncedLabels() { return dsp::syncedDivisionLabels(); }
inline double syncedBeats(int index) noexcept { return dsp::syncedCycleBeats(index); }

// The value smoothed over `ms` (a one-pole; none from 0): its coefficient at a rate.
inline float smoothing(float ms, double sampleRate) noexcept {
    return ms <= 0.01f ? 0.f : static_cast<float>(onePoleCoefficient(ms * 0.001, sampleRate));
}

}  // namespace sub::modulator
