#include "devices/HueWheel.h"

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

using sub::app::analysis::Chroma;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRecentSeconds = 0.2;      // a tick reads this much of the sound at most (after a backlog)
constexpr double kAnalysisSeconds = 1.0 / 30;  // the chromagram is measured this often
constexpr double kEaseSeconds = 0.09;       // notes switching on and off, moving
constexpr double kLevelEaseSeconds = 0.05;  // the chromagram between measurements
constexpr double kPulseSeconds = 0.45;      // a note switched on: its pop and ring
constexpr double kHalfSpan = 0.44;          // a note's segment either side of its centre (in pitch classes)
constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr const char* kParams[12] = {"note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f",
                                     "note_fs", "note_g", "note_gs", "note_a", "note_as", "note_b"};

// A chromagram level's brightness: 0 at the floor, 1 at full scale, eased in so quiet sounds still show.
double brightness(double db) {
    const double t = std::clamp((db - HueWheel::kFloorDb) / -HueWheel::kFloorDb, 0.0, 1.0);
    return std::pow(t, 1.3);
}

}  // namespace

HueWheel::HueWheel(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumSize, kMinimumSize);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    colorShown_.snap(1.0);
}

// --- Notes ---------------------------------------------------------------------------------

QColor HueWheel::noteColor(int pitchClass) {
    const int c = ((pitchClass % 12) + 12) % 12;
    return QColor::fromHsvF(float(c) / 12.0f, 0.58f, 0.97f);
}

QString HueWheel::noteName(int pitchClass) {
    return QString::fromLatin1(kNames[((pitchClass % 12) + 12) % 12]);
}

QString HueWheel::noteParam(int pitchClass) {
    return QString::fromLatin1(kParams[((pitchClass % 12) + 12) % 12]);
}

int HueWheel::moveOf(int notes, int c) {
    c = ((c % 12) + 12) % 12;
    if (notes >> c & 1)
        return 0;
    if (notes >> ((c + 11) % 12) & 1)
        return -1;
    if (notes >> ((c + 1) % 12) & 1)
        return 1;
    return 0;
}

void HueWheel::setNotes(int notes, const QString& text) {
    if (device() == nullptr)
        return;
    sub::app::OrderedMap<QString, double> values;
    for (int c = 0; c < kClasses; ++c)
        values.insert(noteParam(c), (notes >> c & 1) ? 1.0 : 0.0);
    values.insert(QStringLiteral("auto"), 0.0);
    setParams(values, QString(), text);
}

void HueWheel::setAuto(bool on) {
    if (device() == nullptr)
        return;
    setParams({{QStringLiteral("auto"), on ? 1.0 : 0.0}}, QString(),
              on ? QStringLiteral("Follow the Song's Key") : QStringLiteral("Choose the Key"));
}

int HueWheel::sourceNotes() const {
    if (midi_)
        return held_;
    return auto_ && autoNotes_ >= 0 ? autoNotes_ : buttons_;
}

void HueWheel::setAutoNotes(int notes) {
    if (notes == autoNotes_)
        return;
    autoNotes_ = notes;
    showNotes(sourceNotes());
    Q_EMIT notesChanged();
    update();
}

void HueWheel::toggleNote(int pitchClass) {
    if (device() == nullptr || midi_ || pitchClass < 0 || pitchClass >= kClasses)
        return;
    const int from = sourceNotes();
    const bool on = from >> pitchClass & 1;
    const QString text = on ? QStringLiteral("Take Out %1").arg(noteName(pitchClass))
                            : QStringLiteral("Add %1").arg(noteName(pitchClass));
    touch(noteParam(pitchClass));
    if (auto_) {  // (the key's notes become the buttons, that one switched)
        setNotes(from ^ (1 << pitchClass), text);
        return;
    }
    setParams({{noteParam(pitchClass), on ? 0.0 : 1.0}}, QString(), text);
}

void HueWheel::setCaption(const QString& caption) {
    if (caption == caption_)
        return;
    caption_ = caption;
    Q_EMIT captionChanged();
    update();
}

double HueWheel::levelDb(int pitchClass) const { return chroma_.levelDb(Chroma::Output, pitchClass); }

double HueWheel::inputLevelDb(int pitchClass) const { return chroma_.levelDb(Chroma::Input, pitchClass); }

// --- Geometry ------------------------------------------------------------------------------

QPointF HueWheel::center() const { return QPointF(width() / 2, height() / 2); }

double HueWheel::radius() const { return std::max(20.0, std::min(width(), height()) / 2 - 5); }

QPointF HueWheel::pointAt(double angle, double r) const {
    const double a = -kPi / 2 + 2 * kPi * angle / kClasses;
    const QPointF c = center();
    return QPointF(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
}

int HueWheel::noteAt(QPointF point) const {
    const QPointF d = point - center();
    const double r = std::hypot(d.x(), d.y()), outer = radius();
    if (r < outer * 0.3 || r > outer + 4)
        return -1;
    double angle = std::atan2(d.y(), d.x()) + kPi / 2;  // from the top, clockwise
    if (angle < 0)
        angle += 2 * kPi;
    return int(std::lround(angle / (2 * kPi) * kClasses)) % kClasses;
}

// --- Parameters and animation --------------------------------------------------------------

void HueWheel::sync() {
    int buttons = 0;
    for (int c = 0; c < kClasses; ++c)
        if (value(noteParam(c)) >= 0.5)
            buttons |= 1 << c;
    buttons_ = buttons;
    auto_ = value(QStringLiteral("auto")) >= 0.5;
    highQuality_ = value(QStringLiteral("quality")) >= 0.5;
    color_ = value(QStringLiteral("color"));
    start_ = value(QStringLiteral("start"));
    end_ = value(QStringLiteral("end"));
    quality_.target = highQuality_ ? 1.0 : 0.0;
    colorShown_.target = std::clamp(color_ / 100.0, 0.0, 2.0);
    showNotes(sourceNotes());
    if (!started_) {  // (the first time: as they are, not easing in from nothing)
        started_ = true;
        for (int c = 0; c < kClasses; ++c) {
            on_[std::size_t(c)].snap(on_[std::size_t(c)].target);
            moved_[std::size_t(c)].snap(moved_[std::size_t(c)].target);
            pulse_[std::size_t(c)] = 0.0;
        }
        quality_.snap(quality_.target);
        colorShown_.snap(colorShown_.target);
    }
    Q_EMIT notesChanged();
    update();
}

void HueWheel::showNotes(int notes) {
    const int before = notes_;
    notes_ = notes;
    for (int c = 0; c < kClasses; ++c) {
        const bool on = notes >> c & 1;
        if (on && !(before >> c & 1))
            pulse_[std::size_t(c)] = 1.0;
        on_[std::size_t(c)].target = on ? 1.0 : 0.0;
        const int move = moveOf(notes, c);
        moved_[std::size_t(c)].target = move != 0 ? 1.0 : 0.0;
        if (move != 0)
            direction_[std::size_t(c)] = move;
    }
}

void HueWheel::refreshDisplays() {
    const double dt = tickSeconds();
    const double rate = sampleRate();

    // The notes in use, and whether they are MIDI's (the engine's display "notes").
    const std::vector<float> held = readRecent(QStringLiteral("notes"), kRecentSeconds);
    if (!held.empty()) {
        const int shown = int(held.back());
        const bool midi = (shown & kFromMidi) != 0;
        held_ = shown & 0xfff;
        if (midi != midi_ || (midi && held_ != notes_)) {
            midi_ = midi;
            setCursor(hovered_ >= 0 && !midi_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
            showNotes(sourceNotes());
            Q_EMIT notesChanged();
        }
    }

    // The chromagram.
    const std::vector<float> in = readRecent(QStringLiteral("input"), kRecentSeconds);
    const std::vector<float> out = readRecent(QStringLiteral("output"), kRecentSeconds);
    chroma_.feed(Chroma::Input, in.data(), in.size(), rate);
    chroma_.feed(Chroma::Output, out.data(), out.size(), rate);
    sinceAnalysis_ += dt;
    if (sinceAnalysis_ >= kAnalysisSeconds) {
        // (the band Hue works on, as it clamps Start and End)
        const double low = std::max(start_, 160.0), high = std::max(low * 1.01, std::min(end_, 4000.0));
        chroma_.update(low, high, sinceAnalysis_);
        sinceAnalysis_ = 0.0;
    }

    const bool moving = advance(dt);
    if (moving)
        update();
    if (moving != animating_) {
        animating_ = moving;
        Q_EMIT animatingChanged();
    }
}

bool HueWheel::advance(double dt) {
    const double ease = easeFraction(dt, kEaseSeconds), levelEase = easeFraction(dt, kLevelEaseSeconds);
    bool moving = false;
    time_ = std::fmod(time_ + dt, 3600.0);
    for (int c = 0; c < kClasses; ++c) {
        const std::size_t i = std::size_t(c);
        moving = on_[i].step(ease) || moving;
        moving = moved_[i].step(ease) || moving;
        if (pulse_[i] > 0.0) {
            pulse_[i] = std::max(0.0, pulse_[i] - dt / kPulseSeconds);
            moving = true;
        }
        out_[i].target = brightness(chroma_.levelDb(Chroma::Output, c));
        in_[i].target = brightness(chroma_.levelDb(Chroma::Input, c));
        moving = out_[i].step(levelEase, 1e-3) || moving;
        moving = in_[i].step(levelEase, 1e-3) || moving;
        // the sparks flow while the moved note sounds
        if (moved_[i].value > 0.0 && in_[i].value > 0.0) {
            flow_[i] = std::fmod(flow_[i] + dt * (0.35 + 1.4 * in_[i].value), 1.0);
            moving = true;
        }
    }
    moving = quality_.step(ease) || moving;
    moving = colorShown_.step(ease) || moving;
    return moving;
}

// --- The mouse -----------------------------------------------------------------------------

void HueWheel::setHovered(int note) {
    if (note == hovered_)
        return;
    hovered_ = note;
    setCursor(note >= 0 && !midi_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
    Q_EMIT hoveredNoteChanged();
    update();
}

void HueWheel::hoverMoveEvent(QHoverEvent* event) { setHovered(noteAt(event->position())); }

void HueWheel::hoverLeaveEvent(QHoverEvent*) { setHovered(-1); }

void HueWheel::mousePressEvent(QMouseEvent* event) {
    const int note = event->button() == Qt::LeftButton ? noteAt(event->position()) : -1;
    if (note < 0 || midi_ || device() == nullptr) {
        event->ignore();
        return;
    }
    toggleNote(note);
}

// --- Painting ------------------------------------------------------------------------------

void HueWheel::paint(SgPainter& p) {
    const QRectF all(0, 0, width(), height());
    p.fillRoundedRect(all, 3, 3, Theme::meterBg());
    p.setAntialiasing(true);
    const QPointF c = center();
    const double ro = radius(), ri = ro * 0.71, r0 = ro * 0.3, reach = ri - 4 - r0;
    const double hq = quality_.value;
    const double vivid = std::clamp(0.55 + 0.3 * colorShown_.value, 0.4, 1.0);  // Color: the lit notes' strength
    const QColor dark = QColor(16, 16, 20);

    // The bloom: the colour the sound is now (its notes' hues, as loud as each).
    double loudest = 0.0, r = 0.0, g = 0.0, b = 0.0, sum = 0.0;
    for (int k = 0; k < kClasses; ++k) {
        const double level = out_[std::size_t(k)].value;
        loudest = std::max(loudest, level);
        const double w = level * level;
        const QColor color = noteColor(k);
        r += w * color.redF();
        g += w * color.greenF();
        b += w * color.blueF();
        sum += w;
    }
    if (loudest > 0.0 && sum > 0.0) {
        const QColor bloom = QColor::fromRgbF(float(r / sum), float(g / sum), float(b / sum));
        p.fillRadialGlow(c, ri * 0.98, withAlpha(bloom, int(120 * loudest)), 6);
    }
    p.drawEllipse(QRectF(c.x() - ri, c.y() - ri, 2 * ri, 2 * ri), withAlpha(Theme::textDim(), 40), 1.0);
    p.drawEllipse(QRectF(c.x() - r0, c.y() - r0, 2 * r0, 2 * r0), withAlpha(Theme::textDim(), 30), 1.0);

    // The output's chromagram: a petal per note, in its hue, as long as it is loud.
    constexpr int kArc = 9;
    std::array<QPointF, kArc> inner{}, outer{};
    std::array<QColor, kArc> innerColors{}, outerColors{};
    for (int k = 0; k < kClasses; ++k) {
        const double level = out_[std::size_t(k)].value;
        if (level <= 0.0)
            continue;
        const QColor color = noteColor(k);
        const double tip = r0 + reach * level;
        for (int s = 0; s < kArc; ++s) {
            const double t = double(s) / (kArc - 1) * 2 - 1;  // -1..1 across the petal
            const double shape = std::sqrt(std::max(0.0, 1 - t * t));
            inner[std::size_t(s)] = pointAt(k + t * 0.18, r0);
            outer[std::size_t(s)] = pointAt(k + t * 0.36, r0 + (tip - r0) * (0.35 + 0.65 * shape));
            innerColors[std::size_t(s)] = withAlpha(color, int(70 * level));
            outerColors[std::size_t(s)] = withAlpha(mixColor(color, Qt::white, 0.25 * level), int(235 * level * shape));
        }
        p.fillGradientStrip(inner.data(), outer.data(), innerColors.data(), outerColors.data(), kArc);
        if (level > 0.5)
            p.fillRadialGlow(pointAt(k, tip), 3 + 7 * level, withAlpha(color, int(200 * (level - 0.5))), 4);
    }
    // The input's, behind: a faint outline, so what Hue pulled in shows against what came in.
    bool anyIn = false;
    for (const Eased& level : in_)
        anyIn = anyIn || level.value > 0.0;
    if (anyIn) {
        constexpr int kSteps = 8;  // between each note and the next
        std::vector<QPointF> outline;
        outline.reserve(kClasses * kSteps + 1);
        for (int i = 0; i <= kClasses * kSteps; ++i) {
            const int k = (i / kSteps) % kClasses;
            const double t = double(i % kSteps) / kSteps, ease = 0.5 - 0.5 * std::cos(kPi * t);
            const double level = in_[std::size_t(k)].value * (1 - ease) + in_[std::size_t((k + 1) % kClasses)].value * ease;
            outline.push_back(pointAt(k + t, r0 + reach * level));
        }
        p.drawPolyline(outline.data(), int(outline.size()), withAlpha(Theme::text(), 110), 1.0);
    }

    // The notes round the ring.
    const QFont label = uiFont(7, true);
    for (int k = 0; k < kClasses; ++k) {
        const std::size_t i = std::size_t(k);
        const QColor color = noteColor(k);
        const double on = on_[i].value, moved = moved_[i].value, pop = pulse_[i];
        const int toward = direction_[i];
        const QColor target = noteColor(k + toward);
        const bool hover = hovered_ == k && !midi_;
        const double top = ro + 2.5 * std::sin(kPi * pop) * (pop > 0.0 ? 1.0 : 0.0);
        const QColor off = withAlpha(Theme::textDim(), hover ? 60 : 32);
        for (int s = 0; s < kArc; ++s) {
            const double t = double(s) / (kArc - 1) * 2 - 1;
            const double angle = k + t * kHalfSpan;
            inner[std::size_t(s)] = pointAt(angle, ri);
            outer[std::size_t(s)] = pointAt(angle, top);
            // lit: its hue; moved: shading into where it goes (on that side); else dark
            const double toTarget = moved * std::clamp(0.5 + 0.5 * t * toward, 0.0, 1.0);
            QColor fill = mixColor(off, withAlpha(color, 90), moved * 0.6);
            fill = mixColor(fill, withAlpha(target, 170), toTarget * 0.8);
            fill = mixColor(fill, withAlpha(color, int(255 * vivid)), on);
            innerColors[std::size_t(s)] = withAlpha(fill, int(fill.alpha() * (0.7 + 0.1 * on)));
            outerColors[std::size_t(s)] = hover ? mixColor(fill, Theme::text(), 0.25) : fill;
        }
        p.fillGradientStrip(inner.data(), outer.data(), innerColors.data(), outerColors.data(), kArc);
        // a lit note sings as it sounds
        const double sound = out_[i].value * on;
        if (sound > 0.0)
            p.fillRadialGlow(pointAt(k, (ri + top) / 2), 5 + 9 * sound, withAlpha(color, int(150 * sound)), 4);
        if (hover)
            p.drawArc(QRectF(c.x() - top - 1.5, c.y() - top - 1.5, 2 * top + 3, 2 * top + 3),
                      90 - (k + kHalfSpan) * 30, 2 * kHalfSpan * 30, withAlpha(Theme::text(), 160), 1.0);
        // its ring, just switched on
        if (pop > 0.0) {
            const double ring = top + 2 + 9 * (1 - pop);
            p.drawArc(QRectF(c.x() - ring, c.y() - ring, 2 * ring, 2 * ring), 90 - (k + kHalfSpan) * 30,
                      2 * kHalfSpan * 30, withAlpha(color, int(220 * pop)), 1.5);
        }
        const QPointF at = pointAt(k, (ri + ro) / 2);
        const QColor text = mixColor(mixColor(Theme::textDim(), Theme::text(), hover ? 0.6 : 0.0), dark, on * 0.9);
        p.drawText(QRectF(at.x() - 10, at.y() - 6, 20, 12), Qt::AlignCenter | Qt::TextDontClip, noteName(k), text,
                   label);
    }

    // The moves: an arrow inside the ring from each moved note onto its neighbour, sparks flowing while it sounds.
    for (int k = 0; k < kClasses; ++k) {
        const std::size_t i = std::size_t(k);
        const double moved = moved_[i].value;
        if (moved <= 0.0)
            continue;
        const int toward = direction_[i];
        const QColor color = noteColor(k + toward);
        const double ar = ri - 6.5, sound = in_[i].value;
        // Low Latency's shifter wavers about the note: its arrow does too
        const auto along = [&](double t) {
            const double wobble = (1 - hq) * 1.3 * std::sin(2 * kPi * (3 * t - 1.7 * time_)) * std::sin(kPi * t);
            return pointAt(k + toward * (0.12 + 0.68 * t), ar + wobble - 3.0 * std::sin(kPi * t));
        };
        constexpr int kSteps = 16;
        std::array<QPointF, kSteps + 1> line{};
        const double drawn = moved;  // (it grows out as the move comes in)
        for (int s = 0; s <= kSteps; ++s)
            line[std::size_t(s)] = along(drawn * s / kSteps);
        p.drawPolyline(line.data(), kSteps + 1, withAlpha(color, int(200 * moved)), 1.3, Qt::RoundCap);
        // the head
        const QPointF tip = along(drawn), back = along(std::max(0.0, drawn - 0.18));
        QPointF dir = tip - back;
        const double length = std::hypot(dir.x(), dir.y());
        if (length > 1e-6) {
            dir /= length;
            const QPointF across(-dir.y(), dir.x());
            const QPointF head[3] = {tip + dir * 2.5, tip - dir * 3.0 + across * 3.0, tip - dir * 3.0 - across * 3.0};
            p.fillPolygon(head, 3, withAlpha(color, int(230 * moved)));
        }
        if (sound > 0.0) {
            for (int d = 0; d < 3; ++d) {
                const double t = std::fmod(flow_[i] + d / 3.0, 1.0);
                const double fade = std::sin(kPi * t) * moved * std::max(0.35, sound);
                p.fillRadialGlow(along(t * drawn), 2.5 + 2.5 * sound, withAlpha(mixColor(color, Qt::white, 0.4),
                                                                                  int(230 * fade)), 3);
            }
        }
    }

    // In the middle: the key, and the quality.
    const double textWidth = std::max(24.0, 2 * r0 + 16);
    const QRectF keyBox(c.x() - textWidth / 2, c.y() - 9, textWidth, 13);
    p.drawText(keyBox, Qt::AlignCenter, caption_, Theme::text(), uiFont(8, true));
    p.drawText(QRectF(keyBox.left(), c.y() + 3, textWidth, 11), Qt::AlignCenter,
               highQuality_ ? QStringLiteral("HQ") : QStringLiteral("LL"),
               mixColor(Theme::textDim(), Theme::accent(), hq), uiFont(7, true));
}

}  // namespace sub::ui
