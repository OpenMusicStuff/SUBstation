#include "devices/VocoderGraph.h"

#include "audio/VocoderResponse.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

constexpr double kRiseSeconds = 0.015, kFallSeconds = 0.18;  // the bars
constexpr double kComeSeconds = 0.05, kGoSeconds = 0.4;      // all of them, as sound comes and goes
constexpr double kSilentSeconds = 0.25;                      // nothing came for this long: none
constexpr double kRecentSeconds = 0.05;

const QString kNames[12] = {QStringLiteral("C"),  QStringLiteral("C#"), QStringLiteral("D"),  QStringLiteral("D#"),
                            QStringLiteral("E"),  QStringLiteral("F"),  QStringLiteral("F#"), QStringLiteral("G"),
                            QStringLiteral("G#"), QStringLiteral("A"),  QStringLiteral("A#"), QStringLiteral("B")};

QString hzText(double hz) {
    return hz >= 1000.0 ? QStringLiteral("%1 kHz").arg(hz / 1000.0, 0, 'f', hz >= 10000.0 ? 1 : 2)
                        : QStringLiteral("%1 Hz").arg(std::lround(hz));
}

}  // namespace

VocoderGraph::VocoderGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::NoButton);  // (clicks go on to the frame)
    for (Eased& level : levels_) level.snap(kFloorDb);
    curveFrequencies_.resize(kCurvePoints);
    for (int i = 0; i < kCurvePoints; ++i)
        curveFrequencies_[size_t(i)] = kLowHz * std::pow(kHighHz / kLowHz, double(i) / (kCurvePoints - 1));
}

double VocoderGraph::bandLevel(int k) const {
    return k >= 0 && k < kMaxBands ? levels_[size_t(k)].value : kFloorDb;
}

double VocoderGraph::bandCentre(int k) const { return k >= 0 && k < bandCount() ? centres_[size_t(k)] : 0.0; }

void VocoderGraph::sync() {
    const int choice = int(std::lround(value(QStringLiteral("bands"))));
    low_ = value(QStringLiteral("low"));
    high_ = value(QStringLiteral("high"));
    const double bandwidth = value(QStringLiteral("bandwidth")) / 100.0;
    const bool retro = std::lround(value(QStringLiteral("filter"))) == 1;
    formant_ = value(QStringLiteral("formant"));
    const double rate = sampleRate() > 0.0 ? sampleRate() : 48000.0;
    const sub::app::VocoderBands bands = sub::app::vocoderBands(choice, low_, high_, bandwidth, retro);
    const int before = bandCount();
    centres_.assign(bands.centres.begin(), bands.centres.end());
    carrierCentres_.clear();
    for (const double c : centres_) {  // (the engine leaves out a band moved past 0.45 of the rate)
        const double shifted = c * std::exp2(formant_ / 12.0);
        if (shifted <= 0.45 * rate) carrierCentres_.push_back(shifted);
    }
    const QList<double> frequencies(curveFrequencies_.begin(), curveFrequencies_.end());
    curves_.clear();
    for (int k = 0; k < bandCount(); ++k) {
        const QList<double> db =
            sub::app::vocoderBandResponseDb(choice, low_, high_, bandwidth, retro, k, 0.0, rate, frequencies);
        curves_.emplace_back(db.begin(), db.end());
    }
    if (bandCount() != before) Q_EMIT layoutChanged();
    update();
}

void VocoderGraph::refreshDisplays() {
    const double dt = tickSeconds();
    const std::vector<float> values = readDisplay(QStringLiteral("bands"));
    std::vector<float> frame;
    if (values.size() >= size_t(kDisplayBands)) {
        const size_t whole = values.size() - values.size() % kDisplayBands;  // (frames stay whole)
        frame.assign(values.begin() + std::ptrdiff_t(whole - kDisplayBands), values.begin() + std::ptrdiff_t(whole));
    }
    bool moved = advance(frame.empty() ? nullptr : &frame, dt);
    const std::vector<float> pitch = readRecent(QStringLiteral("pitch"), kRecentSeconds);
    if (!pitch.empty()) {
        const double now = std::isfinite(pitch.back()) ? std::max(0.f, pitch.back()) : 0.0;
        if (now != pitch_) {
            pitch_ = now;
            Q_EMIT pitchChanged();
            moved = true;
        }
    }
    if (moved) update();
}

bool VocoderGraph::advance(const std::vector<float>* frame, double dt) {
    silent_ = frame ? 0.0 : silent_ + dt;
    bool moved = false;
    if (frame) {
        for (int k = 0; k < kMaxBands && k < int(frame->size()); ++k) {
            const float v = (*frame)[size_t(k)];
            levels_[size_t(k)].target = std::isfinite(v) ? std::clamp(double(v), kFloorDb, 0.0) : kFloorDb;
        }
    } else if (silent_ >= kSilentSeconds) {
        for (Eased& level : levels_) level.target = kFloorDb;
    }
    presence_.target = silent_ < kSilentSeconds ? 1.0 : 0.0;
    for (Eased& level : levels_) {
        const bool rising = level.target > level.value;
        moved = level.step(easeFraction(dt, rising ? kRiseSeconds : kFallSeconds), 0.05) || moved;
    }
    const bool coming = presence_.target > presence_.value;
    moved = presence_.step(easeFraction(dt, coming ? kComeSeconds : kGoSeconds), 0.004) || moved;
    return moved;
}

void VocoderGraph::paint(SgPainter& p) {
    const double w = width(), h = height();
    if (w < 40.0 || h < 40.0) return;
    p.setAntialiasing(true);
    const QRectF outer = QRectF(0, 0, w, h).adjusted(0.5, 0.5, -0.5, -0.5);
    p.fillRoundedRect(outer, 3, 3, Theme::meterBg());
    p.drawRoundedRect(outer, 3, 3, Theme::border());

    const double top = 18.0, bottom = h - 12.0;
    const QRectF plot(4.0, top, w - 8.0, bottom - top);
    const LogAxis axis{kLowHz, kHighHz, plot.left(), plot.width()};
    drawDecadeGrid(p, plot, axis);
    const auto yOf = [&](double db) { return dbToY(db, plot, kFloorDb, 0.0); };

    // The bars: each band from halfway to the one below to halfway to the one above.
    const int count = bandCount();
    const double presence = presence_.value;
    for (int k = 0; k < count; ++k) {
        const double c = centres_[size_t(k)];
        const double below = k > 0 ? std::sqrt(c * centres_[size_t(k - 1)]) : c / std::sqrt(count > 1 ? centres_[1] / c : 2.0);
        const double above = k + 1 < count ? std::sqrt(c * centres_[size_t(k + 1)]) : c * std::sqrt(count > 1 ? c / centres_[size_t(k - 1)] : 2.0);
        const double x0 = axis.position(std::clamp(below, kLowHz, kHighHz)) + 0.75;
        const double x1 = axis.position(std::clamp(above, kLowHz, kHighHz)) - 0.75;
        if (x1 <= x0) continue;
        const QRectF well(x0, plot.top(), x1 - x0, plot.height());
        p.fillRect(well, withAlpha(Theme::text(), 10));
        const double y = yOf(levels_[size_t(k)].value);
        if (y < plot.bottom() - 0.5) {
            const QRectF bar(x0, y, x1 - x0, plot.bottom() - y);
            const double loud = (levels_[size_t(k)].value - kFloorDb) / -kFloorDb;
            p.fillRect(bar, withAlpha(Theme::accent(), int(std::lround((70.0 + 150.0 * loud) * presence))));
            p.drawLine(QPointF(x0, y), QPointF(x1, y), withAlpha(Theme::accent(), int(std::lround(255.0 * presence))), 1.0);
        }
    }

    // The filters, faintly.
    std::vector<QPointF> line;
    line.reserve(kCurvePoints);
    for (const std::vector<double>& curve : curves_) {
        line.clear();
        for (int i = 0; i < kCurvePoints; ++i) {
            const double db = curve[size_t(i)];
            if (db < kFloorDb) {
                if (line.size() >= 2) p.drawPolyline(line.data(), int(line.size()), withAlpha(Theme::text(), 70), 1.0);
                line.clear();
                continue;
            }
            line.emplace_back(axis.position(curveFrequencies_[size_t(i)]), yOf(db));
        }
        if (line.size() >= 2) p.drawPolyline(line.data(), int(line.size()), withAlpha(Theme::text(), 70), 1.0);
    }

    // Where Formant moves the carrier's bands, along the bottom.
    for (const double c : carrierCentres_) {
        if (c < kLowHz || c > kHighHz) continue;
        const double x = axis.position(c);
        p.drawLine(QPointF(x, bottom + 2.0), QPointF(x, h - 3.0),
                   formant_ != 0.0 ? Theme::soloOn() : withAlpha(Theme::textDim(), 160), 1.0);
    }

    // Along the top: the bands and their range; the pitch followed.
    const QFont font = uiFont(8);
    p.drawText(QRectF(6, 2, w * 0.6, 14), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 bands · %2 – %3").arg(count).arg(hzText(std::min(low_, high_)), hzText(std::max(low_, high_))),
               Theme::textDim(), font);
    if (pitch_ > 0.0) {
        const double midi = 69.0 + 12.0 * std::log2(pitch_ / 440.0);
        const int note = int(std::lround(midi));
        const QString text = note >= 0 && note < 128
                                 ? QStringLiteral("%1 · %2%3").arg(hzText(pitch_), kNames[note % 12]).arg(note / 12 - 1)
                                 : hzText(pitch_);
        p.drawText(QRectF(w * 0.5, 2, w * 0.5 - 6, 14), Qt::AlignRight | Qt::AlignVCenter, text, Theme::accent(), font);
    }
}

}  // namespace sub::ui
