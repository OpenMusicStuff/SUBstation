#include "devices/HueSpectrum.h"

#include "devices/HueWheel.h"
#include "input/GestureKey.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QHoverEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sub::ui {

namespace {

using sub::app::analysis::EqAnalyzer;

constexpr double kRecentSeconds = 0.2;  // a tick reads this much of the sound at most (after a backlog)
constexpr double kGlowSeconds = 0.1;
constexpr double kColumnPixels = 2.0;
// The parameters' ranges (Start, End, Color), and how far apart Low and High Cut stay.
constexpr double kStartMin = 80.0, kStartMax = 1800.0, kEndMin = 400.0, kEndMax = 8000.0, kGap = 1.25;
constexpr double kColorMax = 200.0;
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
    const double x = point.x(), ds = std::abs(x - xs), de = std::abs(x - xe);
    if (x < xs || (ds <= kGrabPixels && ds <= de))
        return QStringLiteral("start");
    if (x > xe || de <= kGrabPixels)
        return QStringLiteral("end");
    return QStringLiteral("color");
}

// --- Parameters and animation --------------------------------------------------------------

void HueSpectrum::sync() {
    start_ = value(QStringLiteral("start"));
    end_ = value(QStringLiteral("end"));
    color_ = value(QStringLiteral("color"));
    int bits = 0;
    for (int c = 0; c < 12; ++c)
        if (value(HueWheel::noteParam(c)) >= 0.5)
            bits |= 1 << c;
    buttons_ = bits;
    // (where they are at once: nothing slides in as the device is added, or as automation moves them)
    startShown_.snap(std::log2(std::max(1.0, start_)));
    endShown_.snap(std::log2(std::max(1.0, end_)));
    colorShown_.snap(color_);
    update();
}

void HueSpectrum::refreshDisplays() {
    const double dt = tickSeconds();
    const double rate = sampleRate();
    bool moving = false;
    const std::vector<float> notes = readRecent(QStringLiteral("notes"), kRecentSeconds);
    if (!notes.empty() && int(notes.back()) != shownNotes_) {
        shownNotes_ = int(notes.back());
        moving = true;
        if (((shownNotes_ & kFromMidi) != 0) != midi_) {
            midi_ = !midi_;
            Q_EMIT midiChanged();
        }
    }
    const std::vector<float> in = readRecent(QStringLiteral("input"), kRecentSeconds);
    const std::vector<float> out = readRecent(QStringLiteral("output"), kRecentSeconds);
    analyzer_.feed(EqAnalyzer::Input, in.data(), in.size(), rate);
    analyzer_.feed(EqAnalyzer::Output, out.data(), out.size(), rate);

    moving = analyzer_.live(EqAnalyzer::Input) || analyzer_.live(EqAnalyzer::Output) || moving;
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
    setCursor(part.isEmpty() ? Qt::ArrowCursor : part == QLatin1String("color") ? Qt::SizeVerCursor : Qt::SizeHorCursor);
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
    lastX_ = event->position().x();
    lastY_ = event->position().y();
    dragValue_ = part == QLatin1String("start") ? start_ : part == QLatin1String("end") ? end_ : color_;
    touch(part);
    Q_EMIT draggingChanged();
    update();
}

void HueSpectrum::mouseMoveEvent(QMouseEvent* event) {
    if (gesture_.isEmpty())
        return;
    const QPointF pos = event->position();
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.2 : 1.0;
    const double dx = pos.x() - lastX_, dy = pos.y() - lastY_;
    lastX_ = pos.x();
    lastY_ = pos.y();
    if (part_ == QLatin1String("color")) {
        dragValue_ = std::clamp(dragValue_ - dy * kColorPerPixel * fine, -20.0, kColorMax + 20.0);  // (past the ends a little)
        setParams({{QStringLiteral("color"), std::clamp(dragValue_, 0.0, kColorMax)}}, gesture_,
                  QStringLiteral("Change Color"));
        return;
    }
    // across the view's width the frequency goes kHighHz / kLowHz times
    dragValue_ *= std::pow(kHighHz / kLowHz, dx * fine / plot().width());
    if (part_ == QLatin1String("start")) {
        dragValue_ = std::clamp(dragValue_, kStartMin * 0.8, kStartMax * 1.25);
        setParams({{QStringLiteral("start"), std::clamp(dragValue_, kStartMin, std::min(kStartMax, end_ / kGap))}},
                  gesture_, QStringLiteral("Change Low Cut"));
    } else {
        dragValue_ = std::clamp(dragValue_, kEndMin * 0.8, kEndMax * 1.25);
        setParams({{QStringLiteral("end"), std::clamp(dragValue_, std::max(kEndMin, start_ * kGap), kEndMax)}},
                  gesture_, QStringLiteral("Change High Cut"));
    }
}

void HueSpectrum::endDrag() {
    if (gesture_.isEmpty())
        return;
    gesture_.clear();
    part_.clear();
    Q_EMIT draggingChanged();
    update();
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
    const QString text = part == QLatin1String("start") ? QStringLiteral("Reset Low Cut")
                         : part == QLatin1String("end") ? QStringLiteral("Reset High Cut")
                                                        : QStringLiteral("Reset Color");
    setParams({{part, defaultValue(part)}}, QString(), text);
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
    const double color = colorShown_.value;
    const double vivid = std::clamp(color / 100.0, 0.0, 1.0);             // grey at 0 %, its hues at 100 %
    const double beyond = std::clamp((color - 100.0) / 100.0, 0.0, 1.0);  // and glowing past it
    p.setAntialiasing(true);

    // The spectra, a column every kColumnPixels.
    const int n = std::max(2, int(area.width() / kColumnPixels) + 1);
    std::vector<double> freqs(static_cast<std::size_t>(n)), columnX(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double x = area.left() + area.width() * i / (n - 1);
        columnX[std::size_t(i)] = x;
        freqs[std::size_t(i)] = ax.valueAt(x);
    }
    const std::vector<double> outDb = analyzer_.columns(EqAnalyzer::Output, freqs);
    const std::vector<double> inDb = analyzer_.columns(EqAnalyzer::Input, freqs);
    const bool outLive = analyzer_.live(EqAnalyzer::Output), inLive = analyzer_.live(EqAnalyzer::Input);

    // The notes in use: a thin line in its hue at every octave within the band, brighter as it sounds.
    const int notes = midi_ && shownNotes_ >= 0 ? (shownNotes_ & 0xfff) : buttons_;
    for (int c = 0; c < 12 && notes != 0; ++c) {
        if (!(notes >> c & 1))
            continue;
        const QColor hue = HueWheel::noteColor(c);
        for (double hz = 440.0 * std::exp2((c - 9 - 48) / 12.0); hz < kHighHz; hz *= 2.0) {
            if (hz < start || hz > end || hz < kLowHz)
                continue;
            const double x = ax.position(hz);
            const double sounds = outLive ? strength(levelDbAt(hz)) : 0.0;
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()),
                       withAlpha(hue, int(40 + 160 * sounds * sounds)), 1.0 + sounds);
        }
    }

    // The input: a faint line behind.
    if (inLive) {
        std::vector<QPointF> line(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
            line[std::size_t(i)] = QPointF(columnX[std::size_t(i)], yOf(inDb[std::size_t(i)]));
        p.drawPolyline(line.data(), n, withAlpha(Theme::textDim(), 130), 1.0);
    }
    // The output: filled in the colours of its notes (as vivid as Color; grey outside the band), glowing on top.
    if (outLive) {
        std::vector<QPointF> base(static_cast<std::size_t>(n)), top(static_cast<std::size_t>(n));
        std::vector<QColor> baseColors(static_cast<std::size_t>(n)), topColors(static_cast<std::size_t>(n));
        const QColor grey = Theme::textDim();
        for (int i = 0; i < n; ++i) {
            const std::size_t k = std::size_t(i);
            const double x = columnX[k];
            base[k] = QPointF(x, area.bottom());
            top[k] = QPointF(x, yOf(outDb[k]));
            const double inside = smoothstep(std::min((x - xs) / 6.0 + 0.5, (xe - x) / 6.0 + 0.5));
            const double s = strength(outDb[k]);
            const QColor tint = mixColor(grey, pitchColor(freqs[k]), inside * vivid);
            const QColor lit = mixColor(tint, Qt::white, (0.15 + 0.35 * beyond) * s * inside);
            baseColors[k] = withAlpha(tint, int(20 + (20 + 40 * beyond) * inside));
            topColors[k] = withAlpha(lit, int(90 + (140 + 25 * beyond) * inside * (0.4 + 0.6 * s)));
        }
        p.fillGradientStrip(base.data(), top.data(), baseColors.data(), topColors.data(), n);
        drawGlowPolyline(p, top, withAlpha(Theme::text(), 200), 1.2);
    }

    // The band's shape: the crossovers' (Linkwitz-Riley, 24 dB/octave) as a soft curve over the spectrum, the sides
    // it leaves alone dimmed as far as they fall, each corner a dot; brighter, with its frequency, under the mouse.
    {
        const double flatY = area.top() + 26, dbSpan = 48.0;  // 0 dB under the buttons, -48 dB at the bottom
        const auto gainAt = [&](double hz) {
            const double l = std::pow(hz / start, 4.0), h = std::pow(hz / end, 4.0);
            return (l / (1.0 + l)) * (1.0 / (1.0 + h));
        };
        std::vector<QPointF> curve(static_cast<std::size_t>(n)), roof(static_cast<std::size_t>(n));
        std::vector<QColor> roofColors(static_cast<std::size_t>(n)), curveColors(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const std::size_t k = std::size_t(i);
            const double g = gainAt(freqs[k]);
            const double db = 20.0 * std::log10(std::max(g, 1e-6));
            const double y = std::min(area.bottom(), flatY - db / dbSpan * (area.bottom() - flatY));
            curve[k] = QPointF(columnX[k], y);
            roof[k] = QPointF(columnX[k], area.top());
            roofColors[k] = withAlpha(QColor(0, 0, 0), int(85 * (1.0 - g)));
            curveColors[k] = withAlpha(QColor(0, 0, 0), int(55 * (1.0 - g)));
        }
        p.fillGradientStrip(roof.data(), curve.data(), roofColors.data(), curveColors.data(), n);
        const bool lowHot = hovered_ == QLatin1String("start") || part_ == QLatin1String("start");
        const bool highHot = hovered_ == QLatin1String("end") || part_ == QLatin1String("end");
        // the curve: faint, each side brighter while it is hovered or dragged
        std::vector<QPointF> side;
        const auto stroke = [&](int from, int to, bool hot) {
            side.assign(curve.begin() + from, curve.begin() + to);
            if (side.size() < 2)
                return;
            const double lift = hot ? glow_ : 0.0;
            drawGlowPolyline(p, side, withAlpha(Theme::text(), int(70 + 110 * lift)), 1.0 + 0.4 * lift);
        };
        int split = 0;  // the curve's middle (in log frequency), where its sides meet
        const double middle = std::sqrt(start * end);
        while (split < n - 1 && freqs[std::size_t(split)] < middle)
            ++split;
        stroke(0, split + 1, lowHot);
        stroke(split, n, highHot);

        const QFont small = uiFont(7, true);
        for (const bool isEnd : {false, true}) {
            const bool hot = isEnd ? highHot : lowHot;
            const double hz = isEnd ? end : start, x = isEnd ? xe : xs;
            const double y = flatY + 6.0 / dbSpan * (area.bottom() - flatY);  // (its corner: 6 dB down)
            const double lift = hot ? glow_ : 0.0;
            if (lift > 0.0)
                p.fillRadialGlow(QPointF(x, y), 9.0, withAlpha(Theme::text(), int(70 * lift)), 4);
            p.fillEllipse(QPointF(x, y), 2.2 + 0.8 * lift, 2.2 + 0.8 * lift, withAlpha(Theme::text(), int(130 + 125 * lift)));
            // its frequency: faint by the corner, clear under the mouse (outside the band, where the side is)
            const QString text = hzText(hz);
            const double w = SgPainter::textWidth(text, small) + 4;
            const double tx = std::clamp(isEnd ? x + 5 : x - 5 - w, area.left() + 2, area.right() - w - 10);
            p.drawText(QRectF(tx, y + 3, w, 12), (isEnd ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, text,
                       withAlpha(Theme::text(), int(90 + 165 * lift)), small);
        }
    }

    // Color: a gauge at the right edge, its value while hovered or dragged.
    const bool colorHot = hovered_ == QLatin1String("color") || part_ == QLatin1String("color");
    const double gaugeTop = area.top() + 24, gaugeBottom = area.bottom() - 4;
    const double level = gaugeBottom - (gaugeBottom - gaugeTop) * std::clamp(color / kColorMax, 0.0, 1.0);
    p.fillRoundedRect(QRectF(area.right() - 5, gaugeTop, 3, gaugeBottom - gaugeTop), 1.5, 1.5, withAlpha(Theme::textDim(), 50));
    p.fillRoundedRect(QRectF(area.right() - 5, level, 3, gaugeBottom - level), 1.5, 1.5,
                      withAlpha(Theme::text(), int(80 + 140 * (colorHot ? glow_ : 0.0))));
    const double mid = gaugeBottom - (gaugeBottom - gaugeTop) * 0.5;  // (100 %)
    p.drawLine(QPointF(area.right() - 7, mid), QPointF(area.right() - 1, mid), withAlpha(Theme::text(), 120), 1.0);
    if (colorHot && glow_ > 0.0) {
        const QString text = QStringLiteral("Color %1 %").arg(std::lround(color));
        const QFont bold = uiFont(8, true);
        const double w = SgPainter::textWidth(text, bold) + 10;
        const QRectF box(area.right() - 10 - w, std::clamp(level - 8, area.top() + 24, area.bottom() - 36), w, 16);
        p.fillRoundedRect(box, 3, 3, withAlpha(Theme::meterBg(), int(220 * glow_)));
        p.drawText(box, Qt::AlignCenter, text, withAlpha(Theme::text(), int(255 * glow_)), bold);
    }

    // Before any sound: a hint.
    if (!outLive && !inLive) {
        p.drawText(QRectF(area.left(), area.center().y() - 6, area.width(), 12), Qt::AlignCenter,
                   QStringLiteral("Drag up and down for Color, the sides for Low and High Cut"),
                   withAlpha(Theme::textDim(), 160), uiFont(7));
    }
}

}  // namespace sub::ui
