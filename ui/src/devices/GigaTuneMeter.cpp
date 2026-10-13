#include "devices/GigaTuneMeter.h"

#include "audio/GigaTuneScale.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QFontMetricsF>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

// Time constants (seconds) of the animation.
constexpr double kNeedleSeconds = 0.045;
constexpr double kDotSeconds = 0.03;
constexpr double kComeSeconds = 0.06, kGoSeconds = 0.22;    // the voice coming in, going
constexpr double kSlideSeconds = 0.07;                      // one note's name into the next
constexpr double kInTuneSeconds = 0.12;
constexpr double kLightSeconds = 0.03, kFadeSeconds = 0.25;  // the notes along the bottom

const QString kNames[12] = {QStringLiteral("C"),  QStringLiteral("C#"), QStringLiteral("D"),  QStringLiteral("D#"),
                            QStringLiteral("E"),  QStringLiteral("F"),  QStringLiteral("F#"), QStringLiteral("G"),
                            QStringLiteral("G#"), QStringLiteral("A"),  QStringLiteral("A#"), QStringLiteral("B")};
constexpr bool kBlack[12] = {false, true, false, true, false, false, true, false, true, false, true, false};

// "+12 ¢", "−3 ¢", "0 ¢"
QString centsText(double cents) {
    const long rounded = std::lround(cents);
    if (rounded == 0)
        return QStringLiteral("0 ¢");
    return (rounded > 0 ? QStringLiteral("+%1 ¢") : QStringLiteral("−%1 ¢")).arg(std::labs(rounded));
}

}  // namespace

GigaTuneMeter::GigaTuneMeter(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::NoButton);  // (clicks go on to the frame)
    slide_.snap(1.0);
}

void GigaTuneMeter::setSongTonic(int tonic) {
    if (tonic == songTonic_)
        return;
    songTonic_ = tonic;
    Q_EMIT songKeyChanged();
    readNotes();
}

void GigaTuneMeter::setSongMinor(bool minor) {
    if (minor == songMinor_)
        return;
    songMinor_ = minor;
    Q_EMIT songKeyChanged();
    readNotes();
}

void GigaTuneMeter::sync() {
    key_ = int(std::lround(value(QStringLiteral("key"))));
    scale_ = int(std::lround(value(QStringLiteral("scale"))));
    pitchShift_ = int(std::lround(value(QStringLiteral("pitch"))));
    readNotes();
    update();
}

void GigaTuneMeter::readNotes() {
    const int notes = sub::app::gigatuneScaleNotes(key_, scale_, songTonic_, songMinor_);
    if (notes == notes_)
        return;
    notes_ = notes;
    Q_EMIT notesChanged();
    update();
}

void GigaTuneMeter::refreshDisplays() {
    const double dt = tickSeconds();
    const std::vector<float> pitch = readRecent(QStringLiteral("pitch"), kRecentSeconds);
    const std::vector<float> corrected = readRecent(QStringLiteral("corrected"), kRecentSeconds);
    const std::vector<float> note = readRecent(QStringLiteral("note"), kRecentSeconds);
    const bool came = !pitch.empty() && !corrected.empty() && !note.empty();
    if (advance(came, came ? pitch.back() : 0.f, came ? corrected.back() : 0.f, came ? note.back() : -1.f, dt))
        update();
}

bool GigaTuneMeter::advance(bool came, float pitch, float corrected, float note, double dt) {
    silent_ = came ? 0.0 : silent_ + dt;
    const bool voiced = came ? pitch > 0.f && note >= 0.f : voiced_ && silent_ < kSilentSeconds;
    bool moved = false;
    if (came && voiced) {
        const int n = int(std::lround(note));
        bool snap = !voiced_ || presence_.value < 0.05;  // (the voice coming in: no sweep from where it was)
        if (n != note_) {
            if (note_ >= 0 && presence_.value >= 0.05) {  // the name slides from the last
                previousNote_ = note_;
                slide_.value = 0.0;
                slide_.target = 1.0;
            } else {
                previousNote_ = -1;
                slide_.snap(1.0);
            }
            note_ = n;
            snap = true;  // (the cents are another note's)
            Q_EMIT heardChanged();
            moved = true;
        }
        heardCents_ = (double(pitch) - n) * 100.0;
        correctedCents_ = (double(corrected) - n) * 100.0;
        needle_.target = std::clamp(heardCents_, -kRangeCents, kRangeCents);
        dot_.target = std::clamp(correctedCents_, -kRangeCents, kRangeCents);
        if (snap) {
            needle_.snap(needle_.target);
            dot_.snap(dot_.target);
        }
        moved = true;  // (the figures)
    }
    if (voiced != voiced_) {
        voiced_ = voiced;
        Q_EMIT heardChanged();
    }
    presence_.target = voiced_ ? 1.0 : 0.0;
    inTune_.target = voiced_ && std::abs(correctedCents_) <= kInTuneCents ? 1.0 : 0.0;
    for (int c = 0; c < 12; ++c)
        glow_[size_t(c)].target = voiced_ && note_ >= 0 && note_ % 12 == c ? 1.0 : 0.0;

    moved = needle_.step(easeFraction(dt, kNeedleSeconds), 0.05) || moved;
    moved = dot_.step(easeFraction(dt, kDotSeconds), 0.05) || moved;
    const bool coming = presence_.target > presence_.value;
    moved = presence_.step(easeFraction(dt, coming ? kComeSeconds : kGoSeconds), 0.004) || moved;
    moved = inTune_.step(easeFraction(dt, kInTuneSeconds), 0.004) || moved;
    moved = slide_.step(easeFraction(dt, kSlideSeconds), 0.004) || moved;
    for (Eased& glow : glow_) {
        const bool lighting = glow.target > glow.value;
        moved = glow.step(easeFraction(dt, lighting ? kLightSeconds : kFadeSeconds), 0.004) || moved;
    }
    return moved;
}

void GigaTuneMeter::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const double w = width(), h = height();
    if (w < 40.0 || h < 40.0)
        return;
    const QRectF outer = QRectF(0, 0, w, h).adjusted(0.5, 0.5, -0.5, -0.5);
    p.fillRoundedRect(outer, 5, 5, Theme::meterBg());
    p.drawRoundedRect(outer, 5, 5, withAlpha(Theme::gridBeat(), 150));

    const double pad = 9.0;
    const double presence = presence_.value;
    const auto alpha = [](double a) { return int(std::lround(std::clamp(a, 0.0, 255.0))); };

    // --- The note: its name, large, the octave beside it; sliding up into the next ------------
    const QFont big = uiFont(19, true), octaveFont = uiFont(9), small = uiFont(7.5);
    const QFontMetricsF bigMetrics(big);
    const double baseline = 5.0 + bigMetrics.ascent();
    const QColor nameColor = mixColor(Theme::textDim(), Theme::text(), presence);
    const auto drawNote = [&](int note, double opacity, double dy) {
        if (note < 0 || opacity <= 0.0)
            return;
        const QString& name = kNames[note % 12];
        const QColor color = withAlpha(nameColor, alpha(255.0 * opacity));
        p.drawText(QPointF(pad, baseline + dy), name, color, big);
        p.drawText(QPointF(pad + bigMetrics.horizontalAdvance(name) + 1.5, baseline + dy),
                   QString::number(note / 12 - 1), withAlpha(Theme::textDim(), alpha(255.0 * opacity)), octaveFont);
    };
    p.save();
    p.setClipRect(QRectF(1, 1, w * 0.62, baseline + bigMetrics.descent() + 1));
    if (note_ < 0) {
        p.drawText(QPointF(pad, baseline), QStringLiteral("–"), Theme::textDim(), big);
    } else {
        const double slide = smoothstep(slide_.value);
        drawNote(previousNote_, 1.0 - slide, -6.0 * slide);
        drawNote(note_, slide, 6.0 * (1.0 - slide));
    }
    p.restore();
    // Under it, the note that comes out while Pitch moves it.
    double headerBottom = baseline + bigMetrics.descent();
    if (pitchShift_ != 0 && note_ >= 0) {
        const int out = std::clamp(note_ + pitchShift_, 0, 127);
        const QString text = QStringLiteral("out %1%2").arg(kNames[out % 12]).arg(out / 12 - 1);
        p.drawText(QRectF(pad, headerBottom - 1, w * 0.5, 12), Qt::AlignLeft | Qt::AlignVCenter, text,
                   withAlpha(Theme::textDim(), alpha(140 + 115 * presence)), small);
        headerBottom += 11.0;
    }

    // --- The figures: the pitch heard, where it is taken (cents of the note) ------------------
    const QFont figures = uiFont(8);
    const int right = Qt::AlignRight | Qt::AlignVCenter;
    if (note_ >= 0) {
        p.drawText(QRectF(w * 0.5, 7, w * 0.5 - pad, 13), right, centsText(heardCents_),
                   withAlpha(Theme::textDim(), alpha(255.0 * presence)), figures);
        p.drawText(QRectF(w * 0.5, 20, w * 0.5 - pad, 13), right, centsText(correctedCents_),
                   withAlpha(Theme::accent(), alpha(255.0 * presence)), figures);
    }

    // --- The cents scale ----------------------------------------------------------------------
    const double stripHeight = 11.0;
    const double stripTop = h - pad - stripHeight;
    const double track = std::round(0.5 * (std::max(headerBottom, 33.0) + stripTop)) + 0.5;
    const double x0 = pad + 6.0, x1 = w - pad - 6.0;
    const auto xOf = [&](double cents) { return 0.5 * (x0 + x1) + cents / kRangeCents * 0.5 * (x1 - x0); };

    // The band round 0, glowing while what comes out is in tune.
    const double tune = inTune_.value * presence;
    const QRectF band(xOf(-kInTuneCents), track - 6.0, xOf(kInTuneCents) - xOf(-kInTuneCents), 12.0);
    if (tune > 0.0)
        p.fillRadialGlow(QPointF(xOf(0.0), track), 12.0, withAlpha(Theme::meterLow(), alpha(30.0 * tune)), 4);
    p.fillRoundedRect(band, 3, 3, withAlpha(Theme::meterLow(), alpha(12.0 + 34.0 * tune)));

    p.drawLine(QPointF(x0, track), QPointF(x1, track), withAlpha(Theme::gridBeat(), 150));
    for (int cents = -50; cents <= 50; cents += 5) {
        const double tick = cents == 0 ? 8.0 : cents % 25 == 0 ? 5.0 : cents % 10 == 0 ? 3.0 : 0.0;
        if (tick > 0.0)
            p.drawLine(QPointF(xOf(cents), track - tick), QPointF(xOf(cents), track + tick),
                       withAlpha(Theme::gridBeat(), cents == 0 ? 220 : 150));
    }
    p.drawText(QRectF(x0 - 8, track + 6, 24, 11), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("−50"),
               Theme::textDim(), small);
    p.drawText(QRectF(x1 - 16, track + 6, 24, 11), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("+50"),
               Theme::textDim(), small);

    if (presence > 0.0 && note_ >= 0) {
        const double needleX = xOf(needle_.value), dotX = xOf(dot_.value);
        // The correction being made: from the pitch heard to where it is taken.
        if (std::abs(needleX - dotX) > 0.5)
            p.fillRect(QRectF(std::min(needleX, dotX), track - 1.5, std::abs(needleX - dotX), 3.0),
                       withAlpha(Theme::accent(), alpha(70.0 * presence)));
        // The pitch heard: a needle (a notch past the end while it is further than the scale goes).
        const QColor needle = withAlpha(Theme::text(), alpha(210.0 * presence));
        p.drawLine(QPointF(needleX, track - 11.0), QPointF(needleX, track + 11.0), needle, 1.5, Qt::RoundCap);
        if (std::abs(heardCents_) > kRangeCents) {
            const double side = heardCents_ > 0 ? 1.0 : -1.0;
            const QPointF notch[3] = {{needleX + side * 3.0, track - 14.0}, {needleX + side * 7.0, track - 11.0},
                                      {needleX + side * 3.0, track - 8.0}};
            p.fillPolygon(notch, 3, needle);
        }
        // Where it is taken: a lit dot.
        const QPointF dot(dotX, track);
        p.fillRadialGlow(dot, 10.0, withAlpha(Theme::accent(), alpha(90.0 * presence)), 4);
        p.fillEllipse(dot, 3.5, 3.5, withAlpha(Theme::accent(), alpha(255.0 * presence)));
    }

    // --- The twelve notes: the scale's lit, the one corrected to glowing ----------------------
    const double gap = 2.0;
    const double cell = (w - 2.0 * pad - 11.0 * gap) / 12.0;
    for (int c = 0; c < 12; ++c) {
        const bool inScale = (notes_ >> c) & 1;
        const double top = stripTop + (kBlack[c] ? 3.0 : 0.0);  // (the black keys a little shorter)
        const QRectF rect(pad + c * (cell + gap), top, cell, stripTop + stripHeight - top);
        p.fillRoundedRect(rect, 2, 2, withAlpha(Theme::text(), inScale ? 50 : 12));
        const double glow = glow_[size_t(c)].value;
        if (glow > 0.0) {
            p.fillRadialGlow(rect.center(), 9.0, withAlpha(Theme::accent(), alpha(70.0 * glow)), 3);
            p.fillRoundedRect(rect, 2, 2, withAlpha(Theme::accent(), alpha(235.0 * glow)));
        }
    }
}

}  // namespace sub::ui
