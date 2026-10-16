#include "devices/VitalFilterGraph.h"

#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QLinearGradient>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <complex>

namespace sub::ui {

namespace {
using Complex = std::complex<double>;
constexpr double kFineRatio = 0.2;  // Shift
constexpr double kGrab = 10.0;
constexpr double kPi = 3.14159265358979323846;

// Resonance (0..1) as a state-variable filter's Q: 0.5 (none) to about 22.
double qOf(double resonance) { return 0.5 * std::pow(2.0, std::clamp(resonance, 0.0, 1.0) * 5.5); }

struct Svf {
    Complex lp, bp, hp;
};
// A state-variable filter's outputs at `hz` (cutoff `fc`, `q`); the band-pass 1 at its peak.
Svf svf(double hz, double fc, double q) {
    const Complex s(0.0, hz / std::max(fc, 1.0));
    const Complex d = s * s + s / q + 1.0;
    return {1.0 / d, (s / q) / d, (s * s) / d};
}

// The classic models' (Analog, Dirty, Ladder, Digital) and their styles (12dB, 24dB, Notch Blend, Notch Spread,
// B/P/N): blend 0..2 morphs low-pass, band-pass, high-pass.
Complex classic(int style, double blend, double hz, double fc, double q) {
    const double wl = std::clamp(1.0 - blend, 0.0, 1.0), wh = std::clamp(blend - 1.0, 0.0, 1.0);
    const double wb = std::max(0.0, 1.0 - std::abs(1.0 - blend));
    switch (style) {
    case 1: {  // 24 dB: two in series, the resonance shared between them
        const Svf a = svf(hz, fc, std::sqrt(q * 0.5) + 0.2);
        const Complex stage = wl * a.lp + wb * a.bp + wh * a.hp;
        return stage * stage;
    }
    case 2: {  // Notch Blend: low-pass and high-pass mixed, a notch between them
        const Svf a = svf(hz, fc, q);
        return (1.0 - blend / 2) * a.lp + (blend / 2) * a.hp;
    }
    case 3: {  // Notch Spread: two notches, the blend spreading them apart
        const double spread = std::pow(2.0, blend * 1.5);
        const Svf a = svf(hz, fc / spread, std::max(q, 0.7)), b = svf(hz, fc * spread, std::max(q, 0.7));
        return (1.0 - a.bp) * (1.0 - b.bp);
    }
    case 4: {  // B/P/N: band-pass, peak, notch
        const Svf a = svf(hz, fc, q);
        const Complex band = a.bp, peak = 1.0 + a.bp, notch = 1.0 - a.bp;
        return blend <= 1.0 ? band + (peak - band) * blend : peak + (notch - peak) * (blend - 1.0);
    }
    default: {
        const Svf a = svf(hz, fc, q);
        return wl * a.lp + wb * a.bp + wh * a.hp;
    }
    }
}

double db(Complex h) { return 20.0 * std::log10(std::max(std::abs(h), 1e-6)); }
}  // namespace

VitalFilterGraph::VitalFilterGraph(QQuickItem* parent) : VitalGraph(parent) {
    setImplicitSize(240, 100);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::SizeAllCursor);
}

double VitalFilterGraph::response(const Settings& s, double hz) {
    const double fc = s.cutoffHz;
    const double q = qOf(s.resonance);
    switch (s.model) {
    case Diode: {
        // A 24 dB low-pass, and below the cutoff a low shelf (style 0) or a low cut (style 1).
        const Svf a = svf(hz, fc, std::sqrt(q * 0.5) + 0.2);
        const Complex low(0.0, hz / std::max(fc * 0.12, 1.0));
        const Complex cut = low / (low + 1.0);
        return db(a.lp * a.lp * (s.style == 1 ? cut : 0.4 + 0.6 * cut));
    }
    case Formant: {
        // Three peaks, a vowel's, moved by X and Y, shifted and spread.
        const double shift = std::pow(2.0, s.formantShift / 12.0), spread = std::pow(2.0, s.formantSpread * 0.6);
        const double f1 = (250 + 600 * s.formantY) * shift / spread;
        const double f2 = (650 + 1700 * s.formantX) * shift;
        const double f3 = (2300 + 900 * s.formantX * s.formantY) * shift * spread;
        const double fq = 1.5 + 16.0 * std::clamp((s.formantResonance - 0.3) / 0.7, 0.0, 1.0);
        const Complex h = svf(hz, f1, fq).bp + 0.7 * svf(hz, f2, fq).bp + 0.4 * svf(hz, f3, fq).bp;
        return db(h);
    }
    case Comb: {
        // Teeth at the cutoff's harmonics: a feedback comb, or a flanger's (feedforward, + or -).
        const double g = 0.35 + 0.6 * std::clamp(s.resonance, 0.0, 1.0);
        const Complex delay = std::polar(1.0, -2.0 * kPi * hz / std::max(fc, 1.0));
        const int kind = s.style % 3;
        Complex h = kind == 0 ? (1.0 - g) / (1.0 - g * delay) : kind == 1 ? (1.0 + g * delay) / (1.0 + g)
                                                                           : (1.0 - g * delay) / (1.0 + g);
        if (s.style >= 3) {  // Band Spread: around the cutoff, the offset its width
            const double width = std::pow(2.0, s.combOffset / 24.0);
            h *= 1.0 + svf(hz, fc * width, 0.7).bp - svf(hz, fc / width, 0.7).lp;
        }
        return db(h);
    }
    case Phaser: {
        // Four all-passes about the cutoff (spread by the blend) mixed with the sound: notches.
        const double spread = 0.25 + 0.5 * std::clamp(s.blend, 0.0, 2.0);
        Complex all(1.0, 0.0);
        for (int k = 0; k < 4; ++k) {
            const Complex x(0.0, hz / (fc * std::pow(2.0, (k - 1.5) * spread)));
            all *= (1.0 - x) / (1.0 + x);
        }
        const double g = 0.5 + 0.5 * std::clamp(s.resonance, 0.0, 1.0);
        return db((1.0 + g * all) / (1.0 + g));
    }
    default: return db(classic(s.style, s.blend, hz, fc, q));
    }
}

QRectF VitalFilterGraph::area() const { return plot().adjusted(0, 4, 0, -2); }

double VitalFilterGraph::xOf(double hz) const {
    const QRectF a = area();
    return LogAxis{kLow, kHigh, a.left(), a.width()}.position(hz);
}

double VitalFilterGraph::yOf(double dbValue) const { return dbToY(dbValue, area(), kFloorDb, kCeilingDb); }

QPointF VitalFilterGraph::handlePosition() const {
    const QRectF a = area().adjusted(6, 6, -6, -6);
    const double x = std::clamp(xOf(s_.cutoffHz), a.left(), a.right());
    return {x, a.bottom() - std::clamp(s_.resonance, 0.0, 1.0) * a.height()};
}

void VitalFilterGraph::readSection() {
    s_.model = int(std::lround(param(QStringLiteral("model"))));
    s_.style = int(std::lround(param(QStringLiteral("style"))));
    cutoffNote_ = param(QStringLiteral("cutoff"));
    s_.cutoffHz = hzOfNote(cutoffNote_);
    s_.resonance = param(QStringLiteral("resonance"));
    s_.blend = param(QStringLiteral("blend"));
    s_.formantX = param(QStringLiteral("formant_x"));
    s_.formantY = param(QStringLiteral("formant_y"));
    s_.formantShift = param(QStringLiteral("formant_transpose"));
    s_.formantResonance = param(QStringLiteral("formant_resonance"));
    s_.formantSpread = param(QStringLiteral("formant_spread"));
    s_.combOffset = param(QStringLiteral("blend_transpose"));
    updateCurve();
}

void VitalFilterGraph::updateCurve() {
    const QRectF a = area();
    const int columns = std::max(2, int(a.width()));
    curve_.assign(std::size_t(columns) + 1, 0.0);
    const LogAxis axis{kLow, kHigh, a.left(), a.width()};
    for (int i = 0; i <= columns; ++i) {
        // The loudest of a few points across the column: a comb's teeth narrower than a pixel still show.
        double loudest = -1e9;
        const int subs = s_.model == Comb ? 4 : 1;
        for (int k = 0; k < subs; ++k) {
            const double hz = axis.valueAt(a.left() + i + double(k) / subs);
            loudest = std::max(loudest, response(s_, hz));
        }
        curve_[std::size_t(i)] = loudest;
    }
}

void VitalFilterGraph::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    VitalGraph::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateCurve();
        columns_ = spectrum_.columns(std::max(2, int(area().width())), kLow, kHigh);
    }
}

void VitalFilterGraph::refreshDisplays() {
    const double seconds = tickSeconds();
    const std::vector<float> samples = readDisplay(QStringLiteral("output"));
    bool moved = advanceDim(seconds);
    if (spectrum_.add(samples.data(), samples.size(), sampleRate(), seconds)) {
        columns_ = spectrum_.columns(std::max(2, int(area().width())), kLow, kHigh);
        moved = true;
    }
    if (moved)
        update();
}

void VitalFilterGraph::paint(SgPainter& p) {
    paintBackground(p);
    const QRectF a = area();
    drawDecadeGrid(p, a, LogAxis{kLow, kHigh, a.left(), a.width()});
    for (double level = -36; level <= 24; level += 12)
        p.drawLine(QPointF(a.left(), yOf(level)), QPointF(a.right(), yOf(level)), level == 0 ? gridMajor() : gridMinor());

    // The output's spectrum, faint behind.
    if (!columns_.empty()) {
        std::vector<QPointF> spectrum;
        spectrum.reserve(columns_.size());
        const double step = a.width() / double(std::max<std::size_t>(1, columns_.size() - 1));
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            const double level = std::clamp((columns_[i] + 90.0) / 90.0, 0.0, 1.0);  // -90..0 dBFS over the height
            spectrum.emplace_back(a.left() + double(i) * step, a.bottom() - level * a.height());
        }
        p.fillToBaseline(spectrum.data(), int(spectrum.size()), a.bottom(), QColor(255, 255, 255, 16));
    }

    p.setAntialiasing(true);
    p.save();
    p.setOpacity(1.0 - 0.6 * dim());
    const QColor color = Theme::accent();
    std::vector<QPointF> line;
    line.reserve(curve_.size());
    for (std::size_t i = 0; i < curve_.size(); ++i)
        line.emplace_back(a.left() + double(i), yOf(std::clamp(curve_[i], kFloorDb - 6, kCeilingDb + 6)));
    QLinearGradient fill(a.topLeft(), a.bottomLeft());
    fill.setColorAt(0, withAlpha(color, 80));
    fill.setColorAt(1, withAlpha(color, 4));
    p.setClipRect(a);
    p.fillToBaseline(line.data(), int(line.size()), a.bottom(), fill);
    drawGlowPolyline(p, line, color, 1.7);
    p.restore();

    const QPointF handle = handlePosition();
    p.fillRadialGlow(handle, 10, withAlpha(color, dragging() ? 110 : 60), 3);
    p.fillEllipse(handle, 4.0, 4.0, mixColor(color, Qt::white, 0.5));
}

void VitalFilterGraph::mousePressEvent(QMouseEvent* event) {
    if (secondPressOfDoubleClick(event))
        return;
    anchor_ = event->position();
    // Off the handle, the press puts the cutoff (and the resonance) where it is, then drags from there.
    const QPointF d = handlePosition() - anchor_;
    beginGesture(QStringLiteral("cutoff"));
    if (std::hypot(d.x(), d.y()) > kGrab) {
        const QRectF a = area().adjusted(6, 6, -6, -6);
        const double hz = LogAxis{kLow, kHigh, area().left(), area().width()}.valueAt(anchor_.x());
        const double note = std::clamp(69.0 + 12.0 * std::log2(hz / 440.0), kMinCutoff, kMaxCutoff);
        const double resonance = std::clamp((a.bottom() - anchor_.y()) / std::max(1.0, a.height()), 0.0, 1.0);
        setDragged({{prefix() + QStringLiteral("cutoff"), note}, {prefix() + QStringLiteral("resonance"), resonance}});
        startNote_ = note;
        startResonance_ = resonance;
    } else {
        startNote_ = cutoffNote_;
        startResonance_ = s_.resonance;
    }
    update();
    event->accept();
}

void VitalFilterGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging())
        return;
    const double fine = event->modifiers() & Qt::ShiftModifier ? kFineRatio : 1.0;
    const QPointF moved = (event->position() - anchor_) * fine;
    const QRectF a = area();
    // Across: the axis' notes per pixel (its 10 octaves); up and down: the resonance.
    const double notesPerPixel = 12.0 * std::log2(kHigh / kLow) / std::max(1.0, a.width());
    setDragged({{prefix() + QStringLiteral("cutoff"), std::clamp(startNote_ + moved.x() * notesPerPixel, kMinCutoff, kMaxCutoff)},
                {prefix() + QStringLiteral("resonance"),
                 std::clamp(startResonance_ - moved.y() / std::max(1.0, a.height() - 12), 0.0, 1.0)}});
}

void VitalFilterGraph::mouseReleaseEvent(QMouseEvent*) {
    endGesture();
    update();
}

void VitalFilterGraph::mouseUngrabEvent() { endGesture(); }

void VitalFilterGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    const QString cutoff = prefix() + QStringLiteral("cutoff"), resonance = prefix() + QStringLiteral("resonance");
    setParams({{cutoff, defaultValue(cutoff)}, {resonance, defaultValue(resonance)}}, QString(),
              QStringLiteral("Reset Filter"));
    event->accept();
}

}  // namespace sub::ui
