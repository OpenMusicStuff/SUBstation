#include "devices/TransientGraph.h"

#include "audio/TransientResponse.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sub::ui {

namespace {

constexpr const char* kStreamIds[TransientGraph::kStreams] = {"input", "output", "attack", "sustain", "detect"};
constexpr double kLagSeconds = 0.03;      // the drawing stays this far behind the newest value,
constexpr double kLagEaseSeconds = 0.16;  // easing back to it at this pace
constexpr double kMaxLagSeconds = 0.1;    // and jumps on when it falls further behind
constexpr double kQuietSeconds = 0.25;    // no values for this long: the engine stopped
constexpr double kRecentSeconds = 0.1;    // a tick's levels are of this much at most (after a backlog)
constexpr double kOnsetDb = 6.0;          // a transient this strong strikes,
constexpr double kRearmDb = 2.0;          // once it has fallen back under this
constexpr double kRingSeconds = 0.35;     // a strike's ring
constexpr double kSparkDrag = 0.22;       // s: how soon a spark slows
constexpr double kMeterFloorDb = -60.0;
constexpr double kMeterWidth = 5.0;
constexpr double kShownPixels = 0.4;  // less change than this (in the outline) isn't coloured
constexpr double kStrongDb = 6.0;     // a change this big glows brighter

QColor attackColor() { return Theme::accent(); }
QColor sustainColor() { return Theme::soloOn(); }

float clearValue(int stream) {
    return stream == TransientGraph::Input || stream == TransientGraph::Output
               ? float(sub::app::transientDisplayFloorDb())
               : 0.f;
}

qint64 floorDiv(qint64 a, qint64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

}  // namespace

TransientGraph::TransientGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kWidth, kMinimumHeight);
    fitRings(sampleRate() / sub::app::transientDisplaySamples());
    for (int s = 0; s < kStreams; ++s)
        last_[std::size_t(s)] = clearValue(s);
    ends_.fill(-1);
    outMeter_.reset(kMeterFloorDb);
    sparks_.reserve(kMaxSparks);
}

// --- Geometry ------------------------------------------------------------------------------

QFont TransientGraph::textFont() { return uiFont(7); }

QRectF TransientGraph::plot() const {
    return QRectF(5, 5, std::max(1.0, width() - 10 - kMeterWidth - 4), std::max(1.0, height() - 10));
}

QRectF TransientGraph::wave() const {
    const QRectF r = plot();
    const double lane = std::max(20.0, std::round(r.height() * 0.3));
    return QRectF(r.left(), r.top(), r.width(), std::max(1.0, r.height() - lane - 3));
}

QRectF TransientGraph::lane() const {
    const QRectF r = plot(), w = wave();
    return QRectF(r.left(), w.bottom() + 3, r.width(), std::max(1.0, r.bottom() - w.bottom() - 3));
}

QRectF TransientGraph::meter() const {
    const QRectF r = plot();
    return QRectF(width() - 5 - kMeterWidth, r.top(), kMeterWidth, r.height());
}

double TransientGraph::xOfIndex(double index) const {
    const QRectF r = plot();
    return r.right() - (scroll_ - index) * r.width() / std::max(1.0, kHistorySeconds * rate_);
}

double TransientGraph::halfHeight(double db) const {
    if (!std::isfinite(db))
        db = db > 0 ? kWaveCeilingDb : kWaveFloorDb;
    const double f = std::clamp((db - kWaveFloorDb) / (kWaveCeilingDb - kWaveFloorDb), 0.0, 1.0);
    return f * wave().height() / 2;
}

double TransientGraph::laneY(double db) const {
    const QRectF l = lane();
    if (!std::isfinite(db))
        db = 0.0;
    return l.center().y() - std::clamp(db / kLaneDb, -1.0, 1.0) * (l.height() / 2 - 1);
}

int TransientGraph::bucketSize() const {
    const double px = plot().width() / std::max(1.0, kHistorySeconds * rate_);
    return px >= 1.0 ? 1 : int(std::ceil(1.0 / px - 1e-9));
}

qint64 TransientGraph::historyBegin() const {
    qint64 latest = newest_;
    for (const qint64 end : ends_)
        latest = std::max(latest, end);
    return std::max(begin_, latest - capacity_);
}

int TransientGraph::historySize() const { return started_ ? int(std::max<qint64>(0, newest_ - historyBegin())) : 0; }

float TransientGraph::historyAt(int stream, qint64 index) const {
    if (stream < 0 || stream >= kStreams)
        return 0.f;
    if (!started_ || index < historyBegin() || index >= ends_[std::size_t(stream)])
        return clearValue(stream);
    return rings_[std::size_t(stream)][std::size_t(index & (capacity_ - 1))];
}

qint64 TransientGraph::ringCapacity(double valuesPerSecond) {
    const double values = 1.1 * (kHistorySeconds + kMaxLagSeconds) * std::max(1.0, valuesPerSecond);
    qint64 capacity = 64;
    while (double(capacity) < values)
        capacity *= 2;
    return capacity;
}

void TransientGraph::fitRings(double valuesPerSecond) {
    rate_ = std::max(1.0, valuesPerSecond);
    const qint64 capacity = ringCapacity(rate_);
    if (capacity == capacity_)
        return;
    capacity_ = capacity;
    for (int s = 0; s < kStreams; ++s)
        rings_[std::size_t(s)].assign(std::size_t(capacity_), clearValue(s));
    started_ = false;  // (the next read starts the history again)
}

// --- The displays --------------------------------------------------------------------------

void TransientGraph::write(int stream, qint64 index, float value) {
    if (!std::isfinite(value))
        value = clearValue(stream);
    rings_[std::size_t(stream)][std::size_t(index & (capacity_ - 1))] = value;
    const float before = last_[std::size_t(stream)];
    const bool level = stream == Input || stream == Output;
    const bool differs = level ? std::abs(std::clamp(value, float(kWaveFloorDb), float(kWaveCeilingDb)) -
                                          std::clamp(before, float(kWaveFloorDb), float(kWaveCeilingDb))) > 0.01f
                               : std::abs(value - before) > 0.01f;
    if (differs)
        lastVaried_ = std::max(lastVaried_, index);
    last_[std::size_t(stream)] = value;
}

void TransientGraph::strike(qint64 index, float detectedDb) {
    const double strength = std::clamp((detectedDb - kOnsetDb) / 18.0, 0.3, 1.0);
    const double edge = halfHeight(historyAt(Output, index));
    strikes_.push_back({double(index), edge, 0.0, strength});
    // Sparks from both edges of the outline, flying outwards and slowing, a little scattered.
    const auto random = [this] {
        seed_ ^= seed_ << 13;
        seed_ ^= seed_ >> 17;
        seed_ ^= seed_ << 5;
        return double(seed_) / 4294967296.0;
    };
    const int count = 6 + int(std::lround(8 * strength));
    for (int i = 0; i < count; ++i) {
        if (int(sparks_.size()) >= kMaxSparks)
            sparks_.erase(sparks_.begin());
        const double side = i % 2 == 0 ? -1.0 : 1.0;  // (up from the top edge, down from the bottom)
        Spark spark;
        spark.index = double(index) + random() * 2.0;
        spark.y = side * edge;
        spark.vx = (random() - 0.35) * 90.0;
        spark.vy = side * (25.0 + random() * 75.0 * (0.5 + strength));
        spark.life = 0.35 + random() * 0.45;
        spark.size = 1.2 + random() * 1.5;
        spark.strength = strength;
        sparks_.push_back(spark);
    }
}

bool TransientGraph::readStreams(bool& restarted) {
    std::array<std::pair<qint64, std::vector<float>>, kStreams> reads;
    bool any = false;
    for (int s = 0; s < kStreams; ++s) {
        reads[std::size_t(s)] = readDisplayAt(QString::fromLatin1(kStreamIds[s]));
        any = any || !reads[std::size_t(s)].second.empty();
    }
    if (!any)
        return false;
    // A stream that doesn't go on from where it was (the processor made again, or values dropped):
    // the history starts afresh.
    restarted = !started_;
    for (int s = 0; s < kStreams; ++s) {
        const auto& [first, values] = reads[std::size_t(s)];
        if (!values.empty() && ends_[std::size_t(s)] >= 0 && first != ends_[std::size_t(s)])
            restarted = true;
    }
    if (restarted) {
        for (int s = 0; s < kStreams; ++s) {
            std::fill(rings_[std::size_t(s)].begin(), rings_[std::size_t(s)].end(), clearValue(s));
            last_[std::size_t(s)] = clearValue(s);
        }
        ends_.fill(-1);
        begin_ = std::numeric_limits<qint64>::min();
        lastVaried_ = std::numeric_limits<qint64>::min();
        sparks_.clear();
        strikes_.clear();
        started_ = true;
    }
    const qint64 recent = std::max<qint64>(1, qint64(std::ceil(kRecentSeconds * rate_)));
    double gainNow = 0.0, attackNow = 0.0;
    bool gainCame = false;
    for (int s = 0; s < kStreams; ++s) {
        const auto& [first, values] = reads[std::size_t(s)];
        if (values.empty())
            continue;
        const qint64 end = first + qint64(values.size());
        if (ends_[std::size_t(s)] < 0)
            begin_ = std::max(begin_, first);
        for (qint64 i = std::max(first, end - capacity_); i < end; ++i)
            write(s, i, values[std::size_t(i - first)]);
        ends_[std::size_t(s)] = end;
        const qint64 from = std::max(first, end - recent);

        if (s == Output) {
            double loudest = kMeterFloorDb;
            for (qint64 i = from; i < end; ++i)
                loudest = std::max(loudest, double(values[std::size_t(i - first)]));
            levelOut_ = loudest;
        } else if (s == Sustain) {
            // The gain now: the one furthest from 0 dB of what came lately (a short transient isn't missed).
            for (qint64 i = from; i < end; ++i) {
                const double attack = historyAt(Attack, i), sustain = values[std::size_t(i - first)];
                if (std::abs(attack + sustain) >= std::abs(gainNow)) {
                    gainNow = attack + sustain;
                    attackNow = attack;
                }
            }
            gainCame = true;
            attackLeads_ = std::abs(attackNow) >= std::abs(gainNow - attackNow);
        } else if (s == Detect) {
            // A transient's rise strikes (only lately: a backlog's are long gone).
            for (qint64 i = first; i < end; ++i) {
                const float detected = values[std::size_t(i - first)];
                if (armed_ && detected >= kOnsetDb) {
                    armed_ = false;
                    if (i >= from)
                        strike(i, detected);
                } else if (detected < kRearmDb) {
                    armed_ = true;
                }
            }
        }
    }
    if (gainCame)
        gainNow_ = gainNow;
    newest_ = std::numeric_limits<qint64>::max();
    for (const qint64 end : ends_)
        if (end >= 0)
            newest_ = std::min(newest_, end);
    newest_ = std::max(newest_, begin_);
    lastVaried_ = std::max(lastVaried_, begin_);
    return true;
}

void TransientGraph::refreshDisplays() {
    const double dt = tickSeconds();
    fitRings(sampleRate() / sub::app::transientDisplaySamples());

    bool restarted = false;
    const bool came = readStreams(restarted);
    const bool wasIdle = idle_;
    bool redraw = restarted;
    if (came) {
        sinceValues_ = 0.0;
        idle_ = false;
    } else {
        sinceValues_ += dt;
        if (sinceValues_ > kQuietSeconds) {  // the engine stopped, or the device is off
            levelOut_ = kMeterFloorDb;
            gainNow_ = 0.0;
            idle_ = true;
        }
    }
    redraw = redraw || idle_ != wasIdle;

    // Scrolling: on by the time since the last tick, eased towards a steady lag behind the newest value.
    const double before = scroll_;
    if (restarted) {
        scroll_ = std::max(double(historyBegin()), double(newest_) - kLagSeconds * rate_);
    } else if (started_) {
        scroll_ += rate_ * dt;
        const double lag = double(newest_) - scroll_;
        if (lag > kMaxLagSeconds * rate_)
            scroll_ = double(newest_) - kMaxLagSeconds * rate_;
        else if (lag < 0.0)
            scroll_ = double(newest_);
        else
            scroll_ += easeFraction(dt, kLagEaseSeconds) * (lag - kLagSeconds * rate_);
    }
    scroll_ = std::min(scroll_, double(newest_));
    const bool scrolled = scroll_ != before;
    if (scrolled) {
        // Scrolling changes the picture only while something that differs is in sight (or flies with it).
        const double shown = kHistorySeconds * rate_ + 2.0 * bucketSize() + 2.0;
        redraw = redraw || double(lastVaried_) >= before - shown || !sparks_.empty() || !strikes_.empty();
    }

    // The sparks and rings age and go.
    for (Spark& spark : sparks_)
        spark.age += dt;
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [](const Spark& s) { return s.age >= s.life; }),
                  sparks_.end());
    for (Ring& ring : strikes_)
        ring.age += dt;
    strikes_.erase(std::remove_if(strikes_.begin(), strikes_.end(), [](const Ring& r) { return r.age >= kRingSeconds; }),
                   strikes_.end());
    redraw = redraw || !sparks_.empty() || !strikes_.empty();

    // The figure (quick up, slower down) and the meter.
    gain_.target = gainNow_;
    const double pace = std::abs(gainNow_) > std::abs(gain_.value) ? 0.02 : 0.15;
    redraw = gain_.step(easeFraction(dt, pace), 5e-3) || redraw;
    outMeter_.update(levelOut_, dt, 24.0, 1.0, kMeterFloorDb);
    const auto y = [this](double db) { return dbToY(db, meter(), kMeterFloorDb, 0.0); };
    const bool meterMoved = std::abs(y(outMeter_.level) - drawnMeter_) > 0.05 || std::abs(y(outMeter_.peak) - drawnPeak_) > 0.05;
    redraw = redraw || meterMoved;
    const bool falling = y(outMeter_.level) < y(levelOut_) - 0.05 || y(outMeter_.peak) < y(outMeter_.level) - 0.05;

    animating_ = redraw || falling;
    if (redraw) {
        drawnMeter_ = y(outMeter_.level);
        drawnPeak_ = y(outMeter_.peak);
        update();
    }
    if (std::abs(gain_.value - emittedGain_) > 0.01 || idle_ != emittedIdle_) {
        emittedGain_ = gain_.value;
        emittedIdle_ = idle_;
        Q_EMIT levelsChanged();
    }
}

// --- Painting ------------------------------------------------------------------------------

void TransientGraph::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const double w = width(), h = height();
    p.fillRoundedRect(QRectF(0, 0, w, h), 4, 4, Theme::meterBg());
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 4, 4, withAlpha(Theme::gridBeat(), 150), 1);
    const QRectF r = plot(), wv = wave(), ln = lane();
    const QFont small = textFont();
    const double centre = wv.center().y();

    // The guides: the waveform's centre and its 0 dBFS edges, the lane's 0 dB and ±12 dB.
    p.drawLine(QPointF(r.left(), centre), QPointF(r.right(), centre), withAlpha(Theme::gridBar(), 70));
    for (const double side : {-1.0, 1.0}) {
        const double y = centre + side * halfHeight(0.0);
        drawDashedPolyline(p, {QPointF(r.left(), y), QPointF(r.right(), y)}, withAlpha(Theme::gridBar(), 70), 1.0);
    }
    p.fillRect(ln, withAlpha(Theme::panel(), 60));
    p.drawLine(QPointF(ln.left(), laneY(0)), QPointF(ln.right(), laneY(0)), withAlpha(Theme::gridBar(), 120));
    for (const double db : {-12.0, 12.0})
        p.drawLine(QPointF(ln.left(), laneY(db)), QPointF(ln.right(), laneY(db)), withAlpha(Theme::gridBar(), 40));

    // The history, newest at the right edge, in columns of whole buckets of values (aligned to the values'
    // absolute index, so nothing shimmers as it scrolls), gliding by fractions of a pixel.
    const int k = bucketSize();
    const double px = r.width() / std::max(1.0, kHistorySeconds * rate_);
    const qint64 begin = historyBegin();
    qint64 first = floorDiv(qint64(std::floor(scroll_ - kHistorySeconds * rate_)), k) - 1;
    first = std::max(first, floorDiv(begin + k - 1, k));
    const qint64 last = std::min(floorDiv(newest_, k), floorDiv(qint64(std::ceil(scroll_)), k) + 1);
    const int count = started_ ? int(std::max<qint64>(0, last - first)) : 0;
    const qint64 mask = capacity_ - 1;
    p.save();
    p.setClipRect(r);
    if (count >= 2) {
        for (auto* v : {&inTops_, &inBottoms_, &outTops_, &outBottoms_, &bandTops_, &bandBottoms_, &laneTops_,
                        &laneBottoms_, &change_, &attackPart_, &sustainPart_})
            v->resize(std::size_t(count));
        outlineTop_.resize(std::size_t(count));
        outlineBottom_.resize(std::size_t(count));
        gainLine_.resize(std::size_t(count));
        const double dx = k * px;
        const double x0 = r.right() - (scroll_ - double(first * k)) * px;
        const double zero = laneY(0.0);
        for (int i = 0; i < count; ++i) {
            const qint64 from = (first + i) * k;
            float in = -1e9f, out = -1e9f, attack = 0.f, sustain = 0.f;
            for (qint64 v = from; v < from + k; ++v) {
                const std::size_t slot = std::size_t(v & mask);
                in = std::max(in, rings_[Input][slot]);
                out = std::max(out, rings_[Output][slot]);
                if (std::abs(rings_[Attack][slot]) > std::abs(attack))
                    attack = rings_[Attack][slot];
                if (std::abs(rings_[Sustain][slot]) > std::abs(sustain))
                    sustain = rings_[Sustain][slot];
            }
            const std::size_t at = std::size_t(i);
            const double hin = halfHeight(in), hout = halfHeight(out);
            inTops_[at] = float(centre - hin);
            inBottoms_[at] = float(centre + hin);
            outTops_[at] = float(centre - hout);
            outBottoms_[at] = float(centre + hout);
            change_[at] = float(hout - hin);
            attackPart_[at] = attack;
            sustainPart_[at] = sustain;
            const double gy = laneY(attack + sustain);
            laneTops_[at] = float(std::min(zero, gy));
            laneBottoms_[at] = float(std::max(zero, gy));
            const double x = x0 + (i + 0.5) * dx;
            outlineTop_[at] = QPointF(x, outTops_[at]);
            outlineBottom_[at] = QPointF(x, outBottoms_[at]);
            gainLine_[at] = QPointF(x, gy);
        }

        // The input, a ghost; the output over it.
        p.fillBand(x0, dx, inTops_.data(), inBottoms_.data(), count, withAlpha(Theme::textDim(), 42));
        p.fillBand(x0, dx, outTops_.data(), outBottoms_.data(), count, withAlpha(Theme::text(), 95));

        // Where the shaping changed it, between the two outlines (above and below), and in the lane: the colour
        // of what did more of it, brighter turning up than down, and brighter again for a big change; a run of
        // columns of one kind at once.
        const auto kindOf = [&](int i, bool lane) {
            const std::size_t at = std::size_t(i);
            const double gain = attackPart_[at] + sustainPart_[at];
            if (lane ? std::abs(gain) < 0.1 : std::abs(change_[at]) < kShownPixels)
                return 0;
            const bool byAttack = std::abs(attackPart_[at]) >= std::abs(sustainPart_[at]);
            const bool up = lane ? gain > 0 : change_[at] > 0;
            const bool strong = std::abs(gain) >= kStrongDb;
            return 1 + (byAttack ? 0 : 4) + (up ? 0 : 2) + (strong ? 1 : 0);
        };
        const auto colorOf = [](int kind) {
            const int bits = kind - 1;
            const QColor base = bits & 4 ? sustainColor() : attackColor();
            const int alpha = (bits & 2 ? 70 : 150) + (bits & 1 ? 60 : 0);
            return withAlpha(base, alpha);
        };
        const auto runs = [&](bool lane, auto&& draw) {
            for (int i = 0; i < count;) {
                const int kind = kindOf(i, lane);
                int j = i + 1;
                while (j < count && kindOf(j, lane) == kind)
                    ++j;
                if (kind != 0)
                    draw(i, j, colorOf(kind));
                i = j;
            }
        };
        runs(false, [&](int i, int j, const QColor& color) {
            for (const bool top : {true, false}) {
                for (int c = i; c < j; ++c) {
                    const std::size_t at = std::size_t(c);
                    const float a = top ? inTops_[at] : inBottoms_[at], b = top ? outTops_[at] : outBottoms_[at];
                    bandTops_[at] = std::min(a, b);
                    bandBottoms_[at] = std::max(a, b);
                }
                p.fillBand(x0 + i * dx, dx, bandTops_.data() + i, bandBottoms_.data() + i, j - i, color);
            }
        });
        p.drawPolyline(outlineTop_.data(), count, withAlpha(Theme::text(), 190), 1.0);
        p.drawPolyline(outlineBottom_.data(), count, withAlpha(Theme::text(), 190), 1.0);

        p.save();
        p.setClipRect(ln);
        runs(true, [&](int i, int j, const QColor& color) {
            p.fillBand(x0 + i * dx, dx, laneTops_.data() + i, laneBottoms_.data() + i, j - i, color);
        });
        drawGlowPolyline(p, gainLine_, withAlpha(Theme::text(), 210), 1.1);
        p.restore();
    }

    // The strikes: a ring swelling out from the outline where a transient hit, and sparks flying off it,
    // both scrolling away with the sound.
    for (const Ring& ring : strikes_) {
        const double t = std::clamp(ring.age / kRingSeconds, 0.0, 1.0), eased = 1.0 - (1.0 - t) * (1.0 - t);
        const double x = xOfIndex(ring.index);
        const double rx = 2.0 + 12.0 * eased, ry = ring.y * (0.6 + 0.4 * eased) + 4.0 + 8.0 * eased;
        const int alpha = int(std::lround(200 * (1.0 - t) * ring.strength));
        p.drawEllipse(QRectF(x - rx, centre - ry, 2 * rx, 2 * ry), withAlpha(attackColor(), alpha), 1.3);
        p.fillRect(QRectF(x - 0.75, wv.top(), 1.5, wv.height()), withAlpha(attackColor(), alpha / 4));
    }
    for (const Spark& spark : sparks_) {
        const double travel = kSparkDrag * (1.0 - std::exp(-spark.age / kSparkDrag));  // (slowing as it goes)
        const QPointF at(xOfIndex(spark.index) + spark.vx * travel, centre + spark.y + spark.vy * travel);
        const double life = 1.0 - spark.age / spark.life;
        const QColor color = mixColor(attackColor(), Theme::text(), 0.35 * life);
        p.fillEllipse(at, spark.size * 2.8, spark.size * 2.8,
                      withAlpha(attackColor(), int(std::lround(70 * life * spark.strength))));
        p.fillEllipse(at, spark.size * (0.5 + 0.5 * life), spark.size * (0.5 + 0.5 * life),
                      withAlpha(color, int(std::lround(255 * life))));
    }
    p.restore();

    // Nothing heard yet: a hint where the sound will be.
    if (!started_)
        p.drawText(wv, Qt::AlignCenter, tr("Play something to see it shaped"), Theme::textDisabled(), small);

    // The legend (top left) and the gain now (the lane's top right), on dark backings.
    const QString attackText = tr("Attack"), sustainText = tr("Sustain");
    const double aw = SgPainter::textWidth(attackText, small), sw = SgPainter::textWidth(sustainText, small);
    const QRectF legend(r.left() + 2, r.top() + 2, 8 + aw + 12 + sw + 6, 12);
    p.fillRoundedRect(legend, 3, 3, withAlpha(Theme::meterBg(), 190));
    p.fillEllipse(QPointF(legend.left() + 5, legend.center().y()), 2.5, 2.5, attackColor());
    p.drawText(QRectF(legend.left() + 9, legend.top(), aw + 2, 12), Qt::AlignLeft | Qt::AlignVCenter, attackText,
               Theme::textDim(), small);
    const double sx = legend.left() + 9 + aw + 6;
    p.fillEllipse(QPointF(sx + 2.5, legend.center().y()), 2.5, 2.5, sustainColor());
    p.drawText(QRectF(sx + 6.5, legend.top(), sw + 2, 12), Qt::AlignLeft | Qt::AlignVCenter, sustainText,
               Theme::textDim(), small);

    const double shown = std::abs(gain_.value) < 0.05 ? 0.0 : gain_.value;
    const QString figure = QStringLiteral("%1 dB").arg(pythonSigned(shown, 1));
    const double fw = SgPainter::textWidth(figure, small) + 8;
    const QRectF box(ln.right() - fw - 1, ln.top() + 1, fw, 12);
    p.fillRoundedRect(box, 3, 3, withAlpha(Theme::meterBg(), 200));
    const QColor ink = idle_ || shown == 0.0 ? Theme::textDim() : attackLeads_ ? attackColor() : sustainColor();
    p.drawText(box, Qt::AlignCenter, figure, ink, small);

    // The output's meter, red at its top once it reaches full scale.
    const QRectF m = meter();
    drawLevelMeter(p, m, outMeter_.level, outMeter_.peak, kMeterFloorDb, 0.0, MeterWell::Panel);
    if (outMeter_.peak >= -0.1)
        p.fillRect(QRectF(m.left(), m.top(), m.width(), 3), Theme::meterHigh());
}

}  // namespace sub::ui
