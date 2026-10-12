#include "devices/HueSpectrum.h"

#include "devices/HueWheel.h"
#include "input/GestureKey.h"
#include "model/ParamSpec.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QHoverEvent>
#include <QMouseEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sub::ui {

namespace {

using sub::app::analysis::EqAnalyzer;

constexpr double kRecentSeconds = 0.2;  // a tick reads this much of the sound at most (after a backlog)
constexpr double kEaseSeconds = 0.08;   // the handles, to a value set elsewhere
constexpr double kGlowSeconds = 0.1;
constexpr double kColumnPixels = 2.0;
// The parameters' ranges (Start, End), and how far apart the handles stay.
constexpr double kStartMin = 80.0, kStartMax = 1800.0, kEndMin = 400.0, kEndMax = 8000.0, kGap = 1.25;
constexpr double kTopDb = EqAnalyzer::kCeilDb, kBottomDb = -84.0;

// The colour of a frequency: the hue of the note it is, between notes blended (the wheel's hues).
QColor pitchColor(double hz) {
    const double pitch = 12.0 * std::log2(hz / 440.0) + 9.0;
    const double pc = pitch - 12.0 * std::floor(pitch / 12.0);
    return QColor::fromHsvF(float(pc / 12.0), 0.58f, 0.97f);
}

// A level's strength (0..1) above the view's floor.
double strength(double db) { return std::clamp((db - kBottomDb) / (kTopDb - kBottomDb), 0.0, 1.0); }

QString hzText(double hz) {
    return hz >= 1000.0 ? QStringLiteral("%1 kHz").arg(pythonFixed(hz / 1000.0, hz >= 10000.0 ? 1 : 2))
                        : QStringLiteral("%1 Hz").arg(std::lround(hz));
}

}  // namespace

HueSpectrum::HueSpectrum(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

// --- Geometry ------------------------------------------------------------------------------

QRectF HueSpectrum::plot() const { return QRectF(1, 1, std::max(10.0, width() - 2), std::max(10.0, height() - 2)); }

LogAxis HueSpectrum::axis() const {
    const QRectF p = plot();
    return LogAxis{kLowHz, kHighHz, p.left(), p.width()};
}

double HueSpectrum::xOf(double hz) const { return axis().position(std::clamp(hz, kLowHz, kHighHz)); }

double HueSpectrum::hzAt(double x) const { return axis().valueAt(x); }

double HueSpectrum::yOf(double db) const { return dbToY(db, plot(), kBottomDb, kTopDb); }

double HueSpectrum::levelDbAt(double hz) const {
    const std::vector<double> at = analyzer_.columns(EqAnalyzer::Output, {hz / 1.01, hz, hz * 1.01});
    return at[1];
}

QString HueSpectrum::partAt(QPointF point) const {
    if (!plot().adjusted(-2, -2, 2, 2).contains(point))
        return {};
    const double xs = xOf(std::exp2(startShown_.value)), xe = xOf(std::exp2(endShown_.value));
    const double ds = std::abs(point.x() - xs), de = std::abs(point.x() - xe);
    if (std::min(ds, de) <= kGrabPixels)
        return ds <= de ? QStringLiteral("start") : QStringLiteral("end");
    if (point.x() > xs && point.x() < xe)
        return QStringLiteral("band");
    return {};
}

// --- Parameters and animation --------------------------------------------------------------

void HueSpectrum::sync() {
    start_ = value(QStringLiteral("start"));
    end_ = value(QStringLiteral("end"));
    midi_ = value(QStringLiteral("midi")) >= 0.5;
    int bits = 0;
    for (int c = 0; c < 12; ++c)
        if (value(HueWheel::noteParam(c)) >= 0.5)
            bits |= 1 << c;
    notes_ = bits;
    startShown_.target = std::log2(std::max(1.0, start_));
    endShown_.target = std::log2(std::max(1.0, end_));
    if (!started_ || dragging()) {  // (where they are: the first time, and under the mouse)
        started_ = true;
        startShown_.snap(startShown_.target);
        endShown_.snap(endShown_.target);
    }
    update();
}

void HueSpectrum::refreshDisplays() {
    const double dt = tickSeconds();
    const double rate = sampleRate();
    const std::vector<float> held = readRecent(QStringLiteral("notes"), kRecentSeconds);
    if (!held.empty())
        held_ = int(held.back()) & 0xfff;
    const std::vector<float> in = readRecent(QStringLiteral("input"), kRecentSeconds);
    const std::vector<float> out = readRecent(QStringLiteral("output"), kRecentSeconds);
    analyzer_.feed(EqAnalyzer::Input, in.data(), in.size(), rate);
    analyzer_.feed(EqAnalyzer::Output, out.data(), out.size(), rate);

    const double ease = easeFraction(dt, kEaseSeconds);
    bool moving = analyzer_.live(EqAnalyzer::Input) || analyzer_.live(EqAnalyzer::Output);
    moving = startShown_.step(ease, 1e-4) || moving;
    moving = endShown_.step(ease, 1e-4) || moving;
    const double glowTarget = !hovered_.isEmpty() || dragging() ? 1.0 : 0.0;
    if (glow_ != glowTarget) {
        glow_ += (glowTarget - glow_) * easeFraction(dt, kGlowSeconds);
        if (std::abs(glow_ - glowTarget) < 1e-3)
            glow_ = glowTarget;
        moving = true;
    }
    if (moving)
        update();
    if (moving != animating_) {
        animating_ = moving;
        Q_EMIT animatingChanged();
    }
}

// --- The mouse -----------------------------------------------------------------------------

void HueSpectrum::setHovered(const QString& part) {
    if (part == hovered_)
        return;
    hovered_ = part;
    setCursor(part.isEmpty() ? Qt::ArrowCursor : part == QLatin1String("band") ? Qt::OpenHandCursor : Qt::SizeHorCursor);
    Q_EMIT hoveredChanged();
    update();
}

void HueSpectrum::hoverMoveEvent(QHoverEvent* event) {
    if (!dragging())
        setHovered(partAt(event->position()));
}

void HueSpectrum::hoverLeaveEvent(QHoverEvent*) {
    if (!dragging())
        setHovered({});
}

void HueSpectrum::mousePressEvent(QMouseEvent* event) {
    const QString part = event->button() == Qt::LeftButton ? partAt(event->position()) : QString();
    if (part.isEmpty() || device() == nullptr) {
        event->ignore();
        return;
    }
    if (secondPressOfDoubleClick(event))
        return;  // (the double-click follows)
    part_ = part;
    gesture_ = newGestureKey();
    pressX_ = event->position().x();
    dragStart_ = start_;
    dragEnd_ = end_;
    touch(part == QLatin1String("end") ? QStringLiteral("end") : QStringLiteral("start"));
    if (part == QLatin1String("band"))
        setCursor(Qt::ClosedHandCursor);
    Q_EMIT draggingChanged();
    update();
}

void HueSpectrum::mouseMoveEvent(QMouseEvent* event) {
    if (gesture_.isEmpty())
        return;
    const double x = event->position().x();
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.2 : 1.0;
    // across the view's width the frequency goes kHighHz / kLowHz times
    const double factor = std::pow(kHighHz / kLowHz, (x - pressX_) * fine / plot().width());
    pressX_ = x;
    if (part_ == QLatin1String("start")) {
        dragStart_ = std::clamp(dragStart_ * factor, kStartMin, std::min(kStartMax, end_ / kGap));
        setParams({{QStringLiteral("start"), dragStart_}}, gesture_, QStringLiteral("Change Low Cut"));
    } else if (part_ == QLatin1String("end")) {
        dragEnd_ = std::clamp(dragEnd_ * factor, std::max(kEndMin, start_ * kGap), kEndMax);
        setParams({{QStringLiteral("end"), dragEnd_}}, gesture_, QStringLiteral("Change High Cut"));
    } else {
        // both, as far as either can go
        const double lowest = std::max(kStartMin / dragStart_, kEndMin / dragEnd_);
        const double highest = std::min(kStartMax / dragStart_, kEndMax / dragEnd_);
        const double f = std::clamp(factor, std::min(1.0, lowest), std::max(1.0, highest));
        dragStart_ *= f;
        dragEnd_ *= f;
        setParams({{QStringLiteral("start"), dragStart_}, {QStringLiteral("end"), dragEnd_}}, gesture_,
                  QStringLiteral("Move Band"));
    }
}

void HueSpectrum::endDrag() {
    if (gesture_.isEmpty())
        return;
    gesture_.clear();
    part_.clear();
    Q_EMIT draggingChanged();
    hovered_ = QStringLiteral("?");  // (worked out again)
    setHovered({});
}

void HueSpectrum::mouseReleaseEvent(QMouseEvent* event) {
    endDrag();
    setHovered(partAt(event->position()));
}

void HueSpectrum::mouseUngrabEvent() { endDrag(); }

void HueSpectrum::mouseDoubleClickEvent(QMouseEvent* event) {
    const QString part = event->button() == Qt::LeftButton ? partAt(event->position()) : QString();
    if (part.isEmpty() || device() == nullptr) {
        event->ignore();
        return;
    }
    endDrag();
    if (part == QLatin1String("start"))
        setParams({{QStringLiteral("start"), defaultValue(QStringLiteral("start"))}}, QString(),
                  QStringLiteral("Reset Low Cut"));
    else if (part == QLatin1String("end"))
        setParams({{QStringLiteral("end"), defaultValue(QStringLiteral("end"))}}, QString(),
                  QStringLiteral("Reset High Cut"));
    else
        setParams({{QStringLiteral("start"), defaultValue(QStringLiteral("start"))},
                   {QStringLiteral("end"), defaultValue(QStringLiteral("end"))}},
                  QString(), QStringLiteral("Reset Band"));
}

// --- Painting ------------------------------------------------------------------------------

void HueSpectrum::paint(SgPainter& p) {
    const QRectF all(0, 0, width(), height());
    const QRectF area = plot();
    p.fillRoundedRect(all, 3, 3, Theme::meterBg());
    p.setClipRect(area);
    const LogAxis ax = axis();
    drawDecadeGrid(p, area, ax);
    const double start = std::exp2(startShown_.value), end = std::exp2(endShown_.value);
    const double xs = ax.position(std::clamp(start, kLowHz, kHighHz)), xe = ax.position(std::clamp(end, kLowHz, kHighHz));
    p.setAntialiasing(true);

    // The spectra, a column every kColumnPixels.
    const int n = std::max(2, int(area.width() / kColumnPixels) + 1);
    std::vector<double> freqs(static_cast<std::size_t>(n));
    std::vector<double> columnX(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double x = area.left() + area.width() * i / (n - 1);
        columnX[std::size_t(i)] = x;
        freqs[std::size_t(i)] = ax.valueAt(x);
    }
    const std::vector<double> outDb = analyzer_.columns(EqAnalyzer::Output, freqs);
    const std::vector<double> inDb = analyzer_.columns(EqAnalyzer::Input, freqs);
    const bool outLive = analyzer_.live(EqAnalyzer::Output), inLive = analyzer_.live(EqAnalyzer::Input);

    // The notes in use: a thin line in its hue at every octave within the band, glowing as it sounds.
    const int notes = midi_ ? held_ : notes_;
    if (notes != 0) {
        for (int c = 0; c < 12; ++c) {
            if (!(notes >> c & 1))
                continue;
            const QColor color = HueWheel::noteColor(c);
            for (double hz = 440.0 * std::exp2((c - 9 - 48) / 12.0); hz < kHighHz; hz *= 2.0) {
                if (hz < start || hz > end || hz < kLowHz)
                    continue;
                const double x = ax.position(hz);
                const double sounds = outLive ? strength(levelDbAt(hz)) : 0.0;
                p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()),
                           withAlpha(color, int(40 + 150 * sounds * sounds)), 1.0 + sounds);
            }
        }
    }

    // The input: a faint line behind.
    if (inLive) {
        std::vector<QPointF> line(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            line[std::size_t(i)] = QPointF(columnX[std::size_t(i)], yOf(inDb[std::size_t(i)]));
        p.drawPolyline(line.data(), n, withAlpha(Theme::textDim(), 130), 1.0);
    }
    // The output: filled in the colours of its notes (grey outside the band), a glowing line along its top.
    if (outLive) {
        std::vector<QPointF> base(static_cast<std::size_t>(n)), top(static_cast<std::size_t>(n));
        std::vector<QColor> baseColors(static_cast<std::size_t>(n)), topColors(static_cast<std::size_t>(n));
        const QColor grey = Theme::textDim();
        for (int i = 0; i < n; ++i) {
            const std::size_t k = std::size_t(i);
            const double x = columnX[k], y = yOf(outDb[k]);
            base[k] = QPointF(x, area.bottom());
            top[k] = QPointF(x, y);
            const double inside = smoothstep(std::min((x - xs) / 6.0 + 0.5, (xe - x) / 6.0 + 0.5));
            const double s = strength(outDb[k]);
            const QColor color = mixColor(grey, pitchColor(freqs[k]), inside);
            baseColors[k] = withAlpha(color, int(20 + 20 * inside));
            topColors[k] = withAlpha(mixColor(color, Qt::white, 0.2 * s * inside), int(90 + 140 * inside * (0.4 + 0.6 * s)));
        }
        p.fillGradientStrip(base.data(), top.data(), baseColors.data(), topColors.data(), n);
        drawGlowPolyline(p, top, withAlpha(Theme::text(), 200), 1.2);
    }

    // Outside the band: shaded.
    const QColor shade = withAlpha(QColor(0, 0, 0), 95);
    if (xs > area.left())
        p.fillRect(QRectF(area.left(), area.top(), xs - area.left(), area.height()), shade);
    if (xe < area.right())
        p.fillRect(QRectF(xe, area.top(), area.right() - xe, area.height()), shade);

    // The handles: a line, a grip, the frequency.
    const QFont small = uiFont(7, true);
    for (const bool isEnd : {false, true}) {
        const QString part = isEnd ? QStringLiteral("end") : QStringLiteral("start");
        const bool hot = hovered_ == part || part_ == part || hovered_ == QLatin1String("band") || part_ == QLatin1String("band");
        const double x = isEnd ? xe : xs, hz = isEnd ? end : start;
        const QColor color = mixColor(Theme::accent(), Theme::text(), hot ? 0.25 * glow_ : 0.0);
        if (hot && glow_ > 0.0)
            p.fillRect(QRectF(x - 3, area.top(), 6, area.height()), withAlpha(Theme::accent(), int(40 * glow_)));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()), withAlpha(color, hot ? 230 : 170), hot ? 1.6 : 1.1);
        const QRectF grip(x - 3.5, area.top() + 3, 7, 14);
        p.fillRoundedRect(grip, 2.5, 2.5, color);
        for (int line = 0; line < 3; ++line) {
            const double y = grip.top() + 4 + 3 * line;
            p.drawLine(QPointF(grip.left() + 2, y), QPointF(grip.right() - 2, y), withAlpha(Theme::meterBg(), 200), 1.0);
        }
        const QString text = hzText(hz);
        const double w = SgPainter::textWidth(text, small) + 4;
        // inside the band (the start's to the right of its line, the end's to the left), or outside where it is narrow
        const double room = xe - xs - 18 - SgPainter::textWidth(hzText(start), small) - SgPainter::textWidth(hzText(end), small);
        const bool inside = room > 0.0;
        const bool leftOfLine = isEnd == inside;
        double tx = leftOfLine ? x - 6 - w : x + 6;
        tx = std::clamp(tx, area.left() + 2, area.right() - w - 2);
        p.drawText(QRectF(tx, area.top() + 4, w, 12), (leftOfLine ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                   text, withAlpha(Theme::text(), hot ? 255 : 200), small);
    }
    // Before any sound: a hint.
    if (!outLive && !inLive) {
        p.drawText(QRectF(area.left(), area.center().y() - 6, area.width(), 12), Qt::AlignCenter,
                   QStringLiteral("Drag the handles for Low and High Cut"), withAlpha(Theme::textDim(), 160), uiFont(7));
    }
}

}  // namespace sub::ui
