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
constexpr double kGlowSeconds = 0.12;   // a side's glow, the gauge's
constexpr double kFollowSeconds = 0.12; // the sides, following a new Low or High Cut
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

QRectF HueSpectrum::plot() const { return QRectF(0, 0, std::max(10.0, width()), std::max(10.0, height())); }

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
    auto_ = value(QStringLiteral("auto")) >= 0.5;
    // The sides follow a new Low or High Cut (refreshDisplays()); the first time they are there at once, so nothing
    // slides in as the device is added.
    startShown_.target = std::log2(std::max(1.0, start_));
    endShown_.target = std::log2(std::max(1.0, end_));
    colorShown_.snap(color_);
    if (!started_ && device() != nullptr) {
        started_ = true;
        startShown_.snap(startShown_.target);
        endShown_.snap(endShown_.target);
    }
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
    const double follow = easeFraction(dt, kFollowSeconds), fade = easeFraction(dt, kGlowSeconds);
    moving = startShown_.step(follow, 1e-4) || moving;
    moving = endShown_.step(follow, 1e-4) || moving;
    // the side under the mouse (or dragged) glows; the gauge lights while Color is
    const auto lit = [&](const char* part) {
        return hovered_ == QLatin1String(part) || part_ == QLatin1String(part) ? 1.0 : 0.0;
    };
    sideGlow_[0].target = lit("start");
    sideGlow_[1].target = lit("end");
    moving = sideGlow_[0].step(fade, 1e-3) || moving;
    moving = sideGlow_[1].step(fade, 1e-3) || moving;
    const double glowTarget = lit("color");
    if (glow_ != glowTarget) {
        glow_ += (glowTarget - glow_) * fade;
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
    setCursor(part.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    Q_EMIT hoveredChanged();
    update();
}

void HueSpectrum::hoverMoveEvent(QHoverEvent* event) {
    if (dragging())
        return;
    // (over the editor's buttons laid on it: nothing of its own is hovered)
    const QPointF at = event->position();
    QQuickItem* over = childAt(at.x(), at.y());
    setHovered(over != nullptr && over->isVisible() ? QString() : partAt(at));
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
    p.fillRect(all, Theme::meterBg());  // (edge to edge of the body)
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
    const int notes = midi_ && shownNotes_ >= 0 ? (shownNotes_ & 0xfff)
                      : auto_ && autoNotes_ >= 0     ? autoNotes_
                                                     : buttons_;
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

    // The band's sides: the crossovers' slopes (Linkwitz-Riley, 24 dB/octave), only where they fall, fading in from
    // the corner and out towards the bottom; the side moved (hovered or dragged) glows in the colour of its note.
    {
        const double flatY = area.top() + 30, dbSpan = 48.0;  // 0 dB under the buttons, -48 dB at the bottom
        const double reach = area.bottom() - flatY;
        for (const bool isEnd : {false, true}) {
            const double cut = isEnd ? end : start, xCut = isEnd ? xe : xs;
            const double lift = sideGlow_[isEnd ? 1 : 0].value;
            const QColor glowColor = mixColor(pitchColor(cut), Qt::white, 0.45);
            // the glow: over the side, strongest by the slope's corner, fading out towards the edge and down
            if (lift > 0.0) {
                std::vector<QPointF> tops, bottoms;
                std::vector<QColor> topColors, bottomColors;
                for (int i = 0; i < n; ++i) {
                    const std::size_t k = std::size_t(i);
                    const double x = columnX[k];
                    const double into = isEnd ? (x - xCut) : (xCut - x);  // px out from the corner (< 0: in the band)
                    if (into < -10.0)
                        continue;
                    const double span = isEnd ? area.right() - xCut : xCut - area.left();
                    const double t = into < 0.0 ? smoothstep(1.0 + into / 10.0)
                                                : 1.0 - 0.75 * smoothstep(into / std::max(20.0, span));
                    tops.push_back(QPointF(x, area.top()));
                    bottoms.push_back(QPointF(x, area.bottom()));
                    topColors.push_back(withAlpha(glowColor, int(46 * lift * t)));
                    bottomColors.push_back(withAlpha(glowColor, int(10 * lift * t)));
                }
                if (tops.size() >= 2)
                    p.fillGradientStrip(tops.data(), bottoms.data(), topColors.data(), bottomColors.data(),
                                        int(tops.size()));
            }
            // the slope: a thin line with a soft glow, fading in from the corner and out as it falls. Sampled finely
            // across and kept a point every pixel or so along it (it is steep low down), each side offset at right
            // angles to it and feathered to nothing, so it is smooth however steep.
            struct Point {
                QPointF at;
                double alpha;
            };
            std::vector<Point> line;
            const QColor lineColor = mixColor(Theme::text(), glowColor, lift);
            for (double x = area.left(); x <= area.right(); x += 0.25) {
                const double f = ax.valueAt(x);
                const double r = std::pow(isEnd ? f / cut : cut / f, 4.0);  // past the corner: rising
                const double db = -20.0 * std::log10(1.0 + r);
                const double down = -db / dbSpan;
                if (down > 1.05 || db > -0.05)
                    continue;
                const QPointF at(x, flatY + down * reach);
                if (!line.empty()) {
                    const QPointF d = at - line.back().at;
                    if (d.x() * d.x() + d.y() * d.y() < 1.0)
                        continue;
                }
                const double a = smoothstep(-db / 4.0) * (1.0 - smoothstep(down / 1.05));
                line.push_back({at, a * (0.45 + 0.55 * lift)});
            }
            const int count = int(line.size());
            if (count >= 2) {
                // each point's normal: at right angles to the line through its neighbours
                std::vector<QPointF> normal(line.size());
                for (int i = 0; i < count; ++i) {
                    const QPointF d = line[std::size_t(std::min(i + 1, count - 1))].at - line[std::size_t(std::max(i - 1, 0))].at;
                    const double length = std::max(1e-9, std::hypot(d.x(), d.y()));
                    normal[std::size_t(i)] = QPointF(-d.y() / length, d.x() / length);
                }
                // a band either side of the line, from `inner` to `outer` px out, its colour fading from `from` to `to`
                const auto band = [&](double inner, double outer, double fromAlpha, double toAlpha) {
                    for (const double side : {-1.0, 1.0}) {
                        std::vector<QPointF> in(line.size()), out(line.size());
                        std::vector<QColor> inColors(line.size()), outColors(line.size());
                        for (std::size_t k = 0; k < line.size(); ++k) {
                            in[k] = line[k].at + normal[k] * (side * inner);
                            out[k] = line[k].at + normal[k] * (side * outer);
                            inColors[k] = withAlpha(lineColor, int(255 * fromAlpha * line[k].alpha));
                            outColors[k] = withAlpha(lineColor, int(255 * toAlpha * line[k].alpha));
                        }
                        p.fillGradientStrip(in.data(), out.data(), inColors.data(), outColors.data(), count);
                    }
                };
                band(0.0, 3.5, 0.16, 0.0);                       // the glow
                const double core = 0.55 + 0.35 * lift;
                band(0.0, core, 0.85, 0.85);                     // the line
                band(core, core + 0.9, 0.85, 0.0);               // its edge, feathered (antialiased)
            }
            // its frequency by the corner, while it glows
            if (lift > 0.0) {
                const QFont small = uiFont(7, true);
                const QString text = hzText(cut);
                const double w = SgPainter::textWidth(text, small) + 4;
                const double y = flatY + 8.0 / dbSpan * reach;
                const double tx = std::clamp(isEnd ? xCut + 6 : xCut - 6 - w, area.left() + 2, area.right() - w - 10);
                p.drawText(QRectF(tx, y, w, 12), (isEnd ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, text,
                           withAlpha(Theme::text(), int(255 * lift)), small);
            }
        }
    }

    // Color: a gauge at the right edge, its value while hovered or dragged.
    const bool colorHot = hovered_ == QLatin1String("color") || part_ == QLatin1String("color");
    const double gaugeTop = area.top() + 30, gaugeBottom = area.bottom() - 4;
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
        const QRectF box(area.right() - 10 - w, std::clamp(level - 8, area.top() + 30, area.bottom() - 24), w, 16);
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
