#include "devices/VintageHallLoop.h"

#include "input/GestureKey.h"
#include "model/ParamSpec.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

using Ring = sub::app::VintageHallRing;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRecentSeconds = 0.1;   // a tick's levels are of this much at most (after a backlog)
constexpr double kTapFallDb = 36.0;      // dB/s: how fast the glow falls once the sound does
constexpr double kOnsetDb = 6.0;         // the input this far over its envelope sets a comet off,
constexpr double kOnsetFloorDb = -54.0;  // when louder than this,
constexpr double kStreamDb = -36.0;      // and a held sound this loud one every kStreamSeconds
constexpr double kStreamSeconds = 0.3;
constexpr double kEnvelopeFallDb = 40.0;  // dB/s
constexpr double kEaseSeconds = 0.12;     // Size, Attack, the diffusions
constexpr double kModEaseSeconds = 0.05;  // the moving delays, between display values
constexpr double kWobblePixels = 4.0;     // at full Mod Depth
constexpr double kDecayPixelsPerOctave = 50.0;  // dragged up this far, Decay doubles
constexpr double kSizePerPixel = 0.8;           // % across
constexpr int kBandSteps = 240;                 // the ring's glow, round the loop

// The taps L1..L4, R1..R4 (the displays' order) as points of the loop.
constexpr Ring::Point kTapPoints[VintageHallLoop::kTaps] = {Ring::L1, Ring::L2, Ring::L3, Ring::L4,
                                                            Ring::R1, Ring::R2, Ring::R3, Ring::R4};
constexpr const char* kTapDisplays[VintageHallLoop::kTaps] = {"l1", "l2", "l3", "l4", "r1", "r2", "r3", "r4"};
// Which side hears each tap (0 left, 1 right) and with which of its weights.
constexpr int kTapSide[VintageHallLoop::kTaps] = {0, 0, 0, 0, 1, 1, 1, 1};
constexpr int kTapWeight[VintageHallLoop::kTaps] = {0, 1, 2, 3, 3, 0, 1, 2};
constexpr Ring::Point kAllpasses[4] = {Ring::A, Ring::B, Ring::C, Ring::D};

// The point after an all-pass along the loop: where it ends.
Ring::Point endOf(Ring::Point allpass) {
    switch (allpass) {
        case Ring::A: return Ring::L2;
        case Ring::B: return Ring::L3;
        case Ring::C: return Ring::L4;
        case Ring::D: return Ring::R4;
        default: return allpass;
    }
}

double wrap(double fraction) { return fraction - std::floor(fraction); }

// A level's brightness on the ring: 0 at the floor, 1 at full scale, eased in so quiet tails still show.
double brightness(double db) {
    const double t = std::clamp((db - VintageHallLoop::kFloorDb) / -VintageHallLoop::kFloorDb, 0.0, 1.0);
    return std::pow(t, 0.8);
}

}  // namespace

VintageHallLoop::VintageHallLoop(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::SizeAllCursor);
    const double floor = sub::app::vintageHallDisplayFloorDb();
    for (MeterBallistics& tap : taps_)
        tap.reset(floor);
    input_.reset(floor);
    output_.reset(floor);
    comets_.reserve(kMaxComets);
    ring_ = sub::app::vintageHallRing(size_);
}

// --- Geometry ------------------------------------------------------------------------------

QPointF VintageHallLoop::center() const { return QPointF(width() / 2, height() / 2); }

double VintageHallLoop::maximumRadius() const { return std::max(8.0, std::min(width(), height()) / 2 - 14); }

double VintageHallLoop::targetRadius() const { return maximumRadius() * (0.62 + 0.38 * size_ / 100.0); }

QPointF VintageHallLoop::pointAt(double fraction, double r) const {
    const double angle = -kPi / 2 + 2 * kPi * fraction;  // clockwise from the top
    const QPointF c = center();
    return QPointF(c.x() + r * std::cos(angle), c.y() + r * std::sin(angle));
}

double VintageHallLoop::tapLevelDb(int tap) const {
    return tap >= 0 && tap < kTaps ? taps_[std::size_t(tap)].level : sub::app::vintageHallDisplayFloorDb();
}

double VintageHallLoop::tapWeight(int tap) const {
    return tap >= 0 && tap < kTaps ? weights_[std::size_t(kTapWeight[tap])].value : 0.0;
}

QColor VintageHallLoop::sideColor(int side) const { return side == 0 ? Theme::accent() : Theme::soloOn(); }

QColor VintageHallLoop::colorAt(double fraction) const {
    // Amber from the left's input to the right's, blue from there round; blended over a short way past each.
    const double f = wrap(fraction), half = shown_[Ring::RightIn].value, blend = 0.06;
    const double intoRight = std::clamp((f - half) / blend, 0.0, 1.0);
    const double intoLeft = std::clamp(f / blend, 0.0, 1.0);
    const double right = f >= half ? intoRight : 1.0 - intoLeft;
    return mixColor(sideColor(0), sideColor(1), smoothstep(right));
}

double VintageHallLoop::energyAt(double fraction) const {
    // Straight lines between the taps, round the loop.
    struct Point {
        double at, b;
    };
    std::array<Point, kTaps> points{};
    for (int k = 0; k < kTaps; ++k)
        points[std::size_t(k)] = {shown_[std::size_t(kTapPoints[k])].value, brightness(taps_[std::size_t(k)].level)};
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.at < b.at; });
    const double f = wrap(fraction);
    for (int k = 0; k < kTaps; ++k) {
        const Point& a = points[std::size_t(k)];
        const Point& b = points[std::size_t((k + 1) % kTaps)];
        const double span = wrap(b.at - a.at);
        const double into = wrap(f - a.at);
        if (into <= span && span > 0.0)
            return a.b + (b.b - a.b) * into / span;
    }
    return points[0].b;
}

double VintageHallLoop::wobbleAt(double fraction) const {
    const double depth = modDepth_ / 100.0;
    if (depth <= 0.0)
        return 0.0;
    const double f = wrap(fraction);
    double offset = 0.0;
    // B and D bulge with their moving delays.
    for (const auto& [point, mod] : {std::pair{Ring::B, 0}, std::pair{Ring::D, 1}}) {
        const double from = shown_[std::size_t(point)].value, to = shown_[std::size_t(endOf(point))].value;
        const double span = wrap(to - from), into = wrap(f - from);
        if (span > 0.0 && into <= span)
            offset += std::sin(kPi * into / span) * (2.0 * mod_[std::size_t(mod)].value - 1.0) * kWobblePixels * depth;
    }
    // And a ripple runs round with the sound, at the modulation's pace.
    offset += 0.9 * depth * energyAt(f) * std::sin(6.0 * 2 * kPi * f - shimmer_);
    return offset;
}

void VintageHallLoop::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    DeviceCanvas::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        radius_.snap(targetRadius());
        update();
    }
}

// --- Parameters and animation ------------------------------------------------------------------

void VintageHallLoop::sync() {
    size_ = value(QStringLiteral("size"));
    decayMs_ = value(QStringLiteral("decay"));
    predelayMs_ = value(QStringLiteral("predelay"));
    attack_ = value(QStringLiteral("attack"));
    early_ = value(QStringLiteral("early_diffusion"));
    late_ = value(QStringLiteral("late_diffusion"));
    modRate_ = value(QStringLiteral("mod_rate"));
    modDepth_ = value(QStringLiteral("mod_depth"));
    mix_ = value(QStringLiteral("mix"));
    const double seconds = ring_.seconds;
    ring_ = sub::app::vintageHallRing(size_);
    radius_.target = targetRadius();
    for (int k = 0; k < Ring::kPoints; ++k)
        shown_[std::size_t(k)].target = ring_.at[std::size_t(k)];
    const std::array<float, 4> weights = sub::app::vintageHallTapWeights(attack_);
    for (std::size_t k = 0; k < 4; ++k)
        weights_[k].target = weights[k];
    earlyShown_.target = early_ / 100.0;
    lateShown_.target = late_ / 100.0;
    if (!started_) {  // (the first time: where they are, not easing in from nothing)
        started_ = true;
        radius_.snap(radius_.target);
        for (Eased& e : shown_)
            e.snap(e.target);
        for (Eased& w : weights_)
            w.snap(w.target);
        earlyShown_.snap(earlyShown_.target);
        lateShown_.snap(lateShown_.target);
        for (Eased& m : mod_)
            m.snap(0.5);
    }
    if (seconds != ring_.seconds)
        Q_EMIT loopChanged();
    update();
}

void VintageHallLoop::launch(double levelDb) {
    if (int(comets_.size()) + 2 > kMaxComets) {  // the faintest make way
        std::sort(comets_.begin(), comets_.end(), [this](const Comet& a, const Comet& b) {
            const double fall = 60.0 / std::max(0.2, decayMs_ / 1000.0);
            return a.levelDb - fall * std::max(0.0, a.age) > b.levelDb - fall * std::max(0.0, b.age);
        });
        comets_.resize(std::size_t(kMaxComets - 2));
    }
    const double start = -predelayMs_ / 1000.0;  // (it sets off once the predelay is over)
    comets_.push_back({0, start, levelDb});
    comets_.push_back({1, start, levelDb});
    sinceLaunch_ = 0.0;
}

void VintageHallLoop::refreshDisplays() {
    const double dt = tickSeconds();
    const double floor = sub::app::vintageHallDisplayFloorDb();
    const double valueSeconds = sub::app::vintageHallDisplaySamples() / std::max(1.0, sampleRate());

    // The input: onsets set comets off.
    const std::vector<float> in = readRecent(QStringLiteral("input"), kRecentSeconds);
    double inPeak = floor;
    sinceLaunch_ += dt;
    for (const float v : in) {
        inPeak = std::max(inPeak, double(v));
        if (v > kOnsetFloorDb && v > inputEnvelope_ + kOnsetDb)
            launch(v);
        else if (v > kStreamDb && sinceLaunch_ > kStreamSeconds)
            launch(v - 6.0);  // (a held sound: fainter ones, now and then)
        inputEnvelope_ = std::max(double(v), inputEnvelope_ - kEnvelopeFallDb * valueSeconds);
    }
    if (in.empty())
        inputEnvelope_ = std::max(floor, inputEnvelope_ - kEnvelopeFallDb * dt);

    // The levels: what came, else they fall.
    const auto newest = [&](const QString& id, double& peak) {
        const std::vector<float> values = readRecent(id, kRecentSeconds);
        if (values.empty())
            return false;
        peak = *std::max_element(values.begin(), values.end());
        return true;
    };
    std::array<double, kTaps> tapPeaks{};
    tapPeaks.fill(floor);
    for (int k = 0; k < kTaps; ++k)
        newest(QString::fromLatin1(kTapDisplays[k]), tapPeaks[std::size_t(k)]);
    double outPeak = floor;
    newest(QStringLiteral("output"), outPeak);
    for (int m = 0; m < 2; ++m) {
        const std::vector<float> mod = readRecent(m == 0 ? QStringLiteral("mod_b") : QStringLiteral("mod_d"), kRecentSeconds);
        if (!mod.empty())
            mod_[std::size_t(m)].target = std::clamp(double(mod.back()), 0.0, 1.0);
    }
    for (int k = 0; k < kTaps; ++k)
        taps_[std::size_t(k)].update(tapPeaks[std::size_t(k)], dt, kTapFallDb, 0.0, floor);
    input_.update(inPeak, dt, kTapFallDb * 2, 0.0, floor);
    output_.update(outPeak, dt, kTapFallDb, 0.0, floor);
    // Under the glow's floor nothing shows: there they rest at the displays' own.
    for (MeterBallistics* meter : {&input_, &output_})
        if (meter->level < kFloorDb)
            meter->reset(floor);
    for (MeterBallistics& tap : taps_)
        if (tap.level < kFloorDb)
            tap.reset(floor);

    const bool moving = advance(dt);
    if (moving)
        update();
    if (moving != animating_) {
        animating_ = moving;
        Q_EMIT animatingChanged();
    } else if (moving) {
        Q_EMIT animatingChanged();  // (the comets' count)
    }
}

bool VintageHallLoop::advance(double dt) {
    const double ease = easeFraction(dt, kEaseSeconds);
    bool moving = radius_.step(ease, 1e-3);
    for (Eased& e : shown_)
        moving = e.step(ease, 1e-5) || moving;
    for (Eased& w : weights_)
        moving = w.step(ease, 1e-4) || moving;
    moving = earlyShown_.step(ease) || moving;
    moving = lateShown_.step(ease) || moving;

    // The glow: lit while any tap is over the floor (it falls by itself).
    double lit = brightness(output_.level);
    for (const MeterBallistics& tap : taps_)
        lit = std::max(lit, brightness(tap.level));
    lit = std::max(lit, brightness(input_.level));
    if (lit > 0.0) {
        moving = true;
        for (Eased& m : mod_)
            m.step(easeFraction(dt, kModEaseSeconds), 1e-4);
        shimmer_ = std::fmod(shimmer_ + 2 * kPi * std::max(0.2, modRate_) * 0.5 * dt, 2 * kPi * 64);
    }

    // The comets run on, fading as Decay says.
    if (!comets_.empty()) {
        moving = true;
        const double fall = 60.0 / std::max(0.2, decayMs_ / 1000.0);
        for (Comet& c : comets_)
            c.age += dt;
        comets_.erase(std::remove_if(comets_.begin(), comets_.end(),
                                     [&](const Comet& c) { return c.levelDb - fall * std::max(0.0, c.age) < kFloorDb; }),
                      comets_.end());
    }
    return moving;
}

// --- Dragging ------------------------------------------------------------------------------

void VintageHallLoop::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || device() == nullptr) {
        event->ignore();
        return;
    }
    pressed_ = true;
    if (secondPressOfDoubleClick(event))
        return;  // (the double-click follows)
    gesture_ = newGestureKey();
    lastPos_ = event->position();
    dragDecay_ = decayMs_;
    dragSize_ = size_;
    touch(QStringLiteral("decay"));
    Q_EMIT draggingChanged();
}

void VintageHallLoop::mouseMoveEvent(QMouseEvent* event) {
    if (gesture_.isEmpty())
        return;
    const QPointF pos = event->position(), delta = pos - lastPos_;
    lastPos_ = pos;
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.15 : 1.0;
    dragDecay_ = std::clamp(dragDecay_ * std::exp2(-delta.y() / kDecayPixelsPerOctave * fine), 100.0, 140000.0);
    dragSize_ = std::clamp(dragSize_ + delta.x() * kSizePerPixel * fine, -20.0, 120.0);  // (past the ends a little)
    setParams({{QStringLiteral("decay"), std::clamp(dragDecay_, 200.0, 70000.0)},
               {QStringLiteral("size"), std::clamp(dragSize_, 0.0, 100.0)}},
              gesture_, QStringLiteral("Change Decay and Size"));
}

void VintageHallLoop::endDrag() {
    pressed_ = false;
    if (gesture_.isEmpty())
        return;
    gesture_.clear();
    Q_EMIT draggingChanged();
}

void VintageHallLoop::mouseReleaseEvent(QMouseEvent*) { endDrag(); }

void VintageHallLoop::mouseUngrabEvent() { endDrag(); }

void VintageHallLoop::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || device() == nullptr) {
        event->ignore();
        return;
    }
    endDrag();
    setParams({{QStringLiteral("decay"), defaultValue(QStringLiteral("decay"))},
               {QStringLiteral("size"), defaultValue(QStringLiteral("size"))}},
              QString(), QStringLiteral("Reset Decay and Size"));
}

// --- Painting ------------------------------------------------------------------------------

void VintageHallLoop::paint(SgPainter& p) {
    const QRectF all(0, 0, width(), height());
    p.fillRoundedRect(all, 3, 3, Theme::meterBg());
    p.setAntialiasing(true);
    const QPointF c = center();
    const double r = radius_.value;
    const double out = brightness(output_.level);

    // The bloom: as loud as the reverb's output, in both halves' colours.
    if (out > 0.0) {
        const QColor bloom = mixColor(sideColor(0), sideColor(1), 0.35);
        p.fillRadialGlow(c, r * 0.95, withAlpha(bloom, int(90 * out)), 6);
    }

    // The loop at rest: a faint ring, the halves' colours.
    std::array<QPointF, kBandSteps + 1> line{};
    std::array<QPointF, kBandSteps + 1> inner{}, outer{}, mid{};
    std::array<QColor, kBandSteps + 1> clear{}, core{};
    for (int i = 0; i <= kBandSteps; ++i) {
        const double f = double(i) / kBandSteps;
        const double w = wobbleAt(f);
        line[std::size_t(i)] = pointAt(f, r + w);
    }
    p.drawPolyline(line.data(), int(line.size()), withAlpha(Theme::textDim(), 70), 1.0);

    // Its energy: a glowing band between the taps, wider where it is louder.
    for (int i = 0; i <= kBandSteps; ++i) {
        const double f = double(i) / kBandSteps;
        const double e = energyAt(f), w = wobbleAt(f);
        const double half = 1.0 + 4.5 * e;
        inner[std::size_t(i)] = pointAt(f, r + w - half);
        mid[std::size_t(i)] = pointAt(f, r + w);
        outer[std::size_t(i)] = pointAt(f, r + w + half);
        const QColor color = colorAt(f);
        clear[std::size_t(i)] = withAlpha(color, 0);
        core[std::size_t(i)] = withAlpha(mixColor(color, Theme::text(), 0.25 * e), int(230 * e));
    }
    p.fillGradientStrip(inner.data(), mid.data(), clear.data(), core.data(), int(inner.size()));
    p.fillGradientStrip(mid.data(), outer.data(), core.data(), clear.data(), int(inner.size()));

    // The all-passes inside it: nested arcs, as strong as the diffusions; B and D waver with the modulation.
    const double late = lateShown_.value, early = earlyShown_.value;
    const auto arc = [&](double from, double to, double radius, const QColor& color, double width) {
        const double span = wrap(to - from);
        if (span <= 0.0)
            return;
        const int steps = std::max(4, int(span * 120));
        std::vector<QPointF> points;
        points.reserve(std::size_t(steps + 1));
        for (int s = 0; s <= steps; ++s)
            points.push_back(pointAt(from + span * s / steps, radius));
        p.drawPolyline(points.data(), int(points.size()), color, width, Qt::RoundCap);
    };
    for (int a = 0; a < 4; ++a) {
        const Ring::Point point = kAllpasses[a];
        const double from = shown_[std::size_t(point)].value, to = shown_[std::size_t(endOf(point))].value;
        const double span = wrap(to - from);
        const bool moved = a == 1 || a == 3;
        const double depth = modDepth_ / 100.0;
        for (int level = 0; level < 3; ++level) {
            const double share = ring_.levels[std::size_t(a)][std::size_t(level)];
            if (share <= 0.0)
                break;
            double radius = r - 8.0 - 3.5 * level;
            if (moved && level == 2)  // (the innermost moves)
                radius += (2.0 * mod_[a == 1 ? 0 : 1].value - 1.0) * 2.0 * depth;
            const double used = span * 0.92 * share, start = from + (span - used) / 2;
            const QColor color = moved && level == 2 ? mixColor(Theme::textDim(), Theme::text(), 0.6)
                                                     : Theme::textDim();
            arc(start, start + used, radius, withAlpha(color, int(30 + 150 * late)), 1.0 + 0.6 * late);
        }
    }
    // The input's all-passes: from where each side comes in to its first tap.
    for (const auto& [from, to] : {std::pair{Ring::LeftIn, Ring::L1}, std::pair{Ring::RightIn, Ring::R2}}) {
        const double a = shown_[std::size_t(from)].value, b = shown_[std::size_t(to)].value;
        arc(a + 0.004, b - 0.004, r - 8.0, withAlpha(Theme::textDim(), int(30 + 150 * early)), 1.0 + 0.6 * early);
    }

    // The comets: a head and a trail behind it, fading as Decay says, smeared lap by lap.
    const double fall = 60.0 / std::max(0.2, decayMs_ / 1000.0);
    const double lap = std::max(0.05, ring_.seconds);
    constexpr int kTrail = 24;
    std::array<QPointF, kTrail + 1> tIn{}, tOut{}, tMid{};
    std::array<QColor, kTrail + 1> tClear{}, tCore{};
    for (const Comet& comet : comets_) {
        if (comet.age < 0.0)
            continue;
        const double b = brightness(comet.levelDb - fall * comet.age);
        if (b <= 0.0)
            continue;
        const double laps = comet.age / lap;
        const double head = shown_[comet.side == 0 ? Ring::LeftIn : Ring::RightIn].value + laps;
        const double length = std::min(0.45, 0.03 + (0.04 + 0.22 * late) * std::min(laps, 2.0));
        const QColor color = sideColor(comet.side);
        for (int s = 0; s <= kTrail; ++s) {
            const double t = double(s) / kTrail, f = head - length * t;
            const double fade = std::pow(1.0 - t, 1.6) * b;
            const double w = wobbleAt(f), half = 0.8 + 3.2 * fade;
            tIn[std::size_t(s)] = pointAt(f, r + w - half);
            tMid[std::size_t(s)] = pointAt(f, r + w);
            tOut[std::size_t(s)] = pointAt(f, r + w + half);
            tClear[std::size_t(s)] = withAlpha(color, 0);
            tCore[std::size_t(s)] = withAlpha(mixColor(color, Theme::text(), 0.5 * (1.0 - t)), int(255 * fade));
        }
        p.fillGradientStrip(tIn.data(), tMid.data(), tClear.data(), tCore.data(), kTrail + 1);
        p.fillGradientStrip(tMid.data(), tOut.data(), tCore.data(), tClear.data(), kTrail + 1);
        p.fillRadialGlow(tMid[0], 4.0 + 8.0 * b, withAlpha(mixColor(color, Theme::text(), 0.5), int(235 * b)), 4);
    }

    // The taps: outside the ring, as big as their weights (hollow: subtracted), lit as they are heard.
    for (int k = 0; k < kTaps; ++k) {
        const double f = shown_[std::size_t(kTapPoints[k])].value;
        const double weight = weights_[std::size_t(kTapWeight[k])].value;
        const double size = std::abs(weight);
        const QColor color = sideColor(kTapSide[k]);
        const QPointF at = pointAt(f, r + 8.0);
        const double heard = brightness(taps_[std::size_t(k)].level) * std::min(1.0, 1.5 * size);
        p.drawLine(pointAt(f, r + 2.5), pointAt(f, r + 5.0), withAlpha(color, int(60 + 120 * size)), 1.0);
        if (heard > 0.0)
            p.fillRadialGlow(at, 3.0 + 7.0 * heard, withAlpha(color, int(170 * heard)), 4);
        const double dot = 0.9 + 2.4 * size;
        if (weight < -0.02)
            p.drawEllipse(QRectF(at.x() - dot, at.y() - dot, 2 * dot, 2 * dot), color, 1.0);
        else
            p.fillEllipse(at, dot, dot, withAlpha(color, size < 0.02 ? 70 : 255));
    }

    // Where each side comes in: a mark pointing into the ring, flashing with the input.
    const QFont small = uiFont(7);
    const double flash = brightness(input_.level);
    for (int side = 0; side < 2; ++side) {
        const double f = shown_[side == 0 ? Ring::LeftIn : Ring::RightIn].value;
        const QColor color = sideColor(side);
        const QPointF tip = pointAt(f, r + 3.0), back = pointAt(f, r + 10.0);
        const QPointF across = pointAt(f + 0.25, 1.0) - center();  // (a unit vector along the ring)
        const QPointF a = back + across * 3.5, b = back - across * 3.5;
        const QPointF triangle[3] = {tip, a, b};
        if (flash > 0.0)
            p.fillRadialGlow(tip, 4.0 + 8.0 * flash, withAlpha(color, int(150 * flash)), 4);
        p.fillPolygon(triangle, 3, color);
        const QPointF label = pointAt(f, r - 21.0);  // (inside the all-passes' arcs, clear of the taps)
        p.drawText(QRectF(label.x() - 8, label.y() - 6, 16, 12), Qt::AlignCenter | Qt::TextDontClip,
                   side == 0 ? QStringLiteral("L") : QStringLiteral("R"), withAlpha(color, 220), small);
    }

    // In the middle: Decay, and the loop's length.
    const double textWidth = std::max(20.0, 2 * r - 22);
    const QRectF decayBox(c.x() - textWidth / 2, c.y() - 9, textWidth, 14);
    p.drawText(decayBox, Qt::AlignCenter, sub::app::formatValue(decayMs_, QStringLiteral("ms")), Theme::text(),
               uiFont(9, true));
    p.drawText(QRectF(decayBox.left(), c.y() + 5, textWidth, 11), Qt::AlignCenter,
               QStringLiteral("%1 ms loop").arg(std::lround(ring_.seconds * 1000.0)), Theme::textDim(), small);
    if (dragging())
        p.drawRoundedRect(all.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3, withAlpha(Theme::accent(), 120), 1.0);
}

}  // namespace sub::ui
