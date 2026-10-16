#include "devices/AutoFilterGraph.h"

#include "devices/EditorPaint.h"
#include "input/GestureKey.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

const QString kDisplayIds[AutoFilterGraph::kStreams] = {QStringLiteral("cutoff_l"), QStringLiteral("cutoff_r"),
                                                        QStringLiteral("env"), QStringLiteral("lfo")};

double log2Of(double hz) { return std::log2(std::max(hz, 1.0)); }

}  // namespace

AutoFilterGraph::AutoFilterGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::SizeAllCursor);
    cutoffLeft_.snap(log2Of(filter_.cutoffHz));
    cutoffRight_.snap(log2Of(filter_.cutoffHz));
}

QRectF AutoFilterGraph::plot() const { return QRectF(0, 0, width(), height()).adjusted(1, 1, -1, -1); }

LogAxis AutoFilterGraph::frequencyAxis() const {
    const QRectF r = plot();
    return {kLow, kHigh, r.left(), r.width()};
}

double AutoFilterGraph::xOf(double freq) const { return frequencyAxis().position(freq); }

double AutoFilterGraph::freqAt(double x) const { return frequencyAxis().valueAt(x); }

double AutoFilterGraph::yOfDb(double db) const {
    const QRectF r = plot();
    return r.top() + (kTopDb - std::clamp(db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb) * r.height();
}

double AutoFilterGraph::yOfResonance(double resonance) const {
    const QRectF r = plot().adjusted(0, kDotRadius + 2, 0, -(kDotRadius + 2 + kSweepBar));
    return r.bottom() - std::clamp(resonance / sub::app::kAutoFilterMaxResonance, 0.0, 1.0) * r.height();
}

double AutoFilterGraph::resonanceAt(double y) const {
    const QRectF r = plot().adjusted(0, kDotRadius + 2, 0, -(kDotRadius + 2 + kSweepBar));
    return std::clamp((r.bottom() - y) / r.height(), 0.0, 1.0) * sub::app::kAutoFilterMaxResonance;
}

QPointF AutoFilterGraph::dot() const { return {xOf(filter_.cutoffHz), yOfResonance(filter_.resonance)}; }

double AutoFilterGraph::cutoffLeft() const { return std::exp2(cutoffLeft_.value); }

double AutoFilterGraph::cutoffRight() const { return std::exp2(cutoffRight_.value); }

std::pair<double, double> AutoFilterGraph::reach() const {
    const double rate = sampleRate();
    const double env = envAmount_ / 100.0 * sub::app::kAutoFilterEnvelopeOctaves;
    const double lfo = lfoAmount_ / 100.0 * sub::app::kAutoFilterLfoOctaves;
    return {sub::app::autoFilterCutoffHz(filter_.cutoffHz, std::min(env, 0.0) - lfo, rate),
            sub::app::autoFilterCutoffHz(filter_.cutoffHz, std::max(env, 0.0) + lfo, rate)};
}

void AutoFilterGraph::sync() {
    filter_.type = int(std::lround(value(QStringLiteral("type"))));
    filter_.slope = int(std::lround(value(QStringLiteral("slope"))));
    filter_.cutoffHz = value(QStringLiteral("freq"));
    filter_.resonance = value(QStringLiteral("resonance"));
    filter_.morph = value(QStringLiteral("morph"));
    envAmount_ = value(QStringLiteral("env_amount"));
    lfoAmount_ = value(QStringLiteral("lfo_amount"));
    // At rest the curve glides to a new Frequency (refreshDisplays()); the first time the device is there, or
    // hidden, it is there at once.
    if (!live_) {
        cutoffLeft_.target = cutoffRight_.target = log2Of(filter_.cutoffHz);
        if (!synced_ || !isVisible()) {
            cutoffLeft_.snap(cutoffLeft_.target);
            cutoffRight_.snap(cutoffRight_.target);
        }
    }
    synced_ = synced_ || device() != nullptr;
    updateCurves();
    update();
}

std::vector<QPointF> AutoFilterGraph::curveAt(double cutoff) const {
    const QRectF r = plot();
    const int points = std::max(2, int(r.width() / kColumn) + 1);
    QList<double> frequencies;
    frequencies.reserve(points);
    for (int i = 0; i < points; ++i) frequencies.append(freqAt(r.left() + i * r.width() / (points - 1)));
    sub::app::AutoFilterCurve curve = filter_;
    curve.cutoffHz = cutoff;
    const QList<double> db = sub::app::autoFilterResponseDb(curve, sampleRate(), frequencies);
    std::vector<QPointF> out;
    out.reserve(std::size_t(points));
    for (int i = 0; i < points; ++i) out.emplace_back(r.left() + i * r.width() / (points - 1), yOfDb(db[i]));
    return out;
}

void AutoFilterGraph::updateCurves() {
    curveLeft_ = curveAt(cutoffLeft());
    showRight_ = std::abs(cutoffRight_.value - cutoffLeft_.value) > 0.01;  // (a hundredth of an octave)
    if (showRight_)
        curveRight_ = curveAt(cutoffRight());
    else
        curveRight_.clear();
}

void AutoFilterGraph::updateColumns() {
    const int columns = std::max(2, int(plot().width() / kColumn));
    columns_ = spectrum_.columns(columns, kLow, kHigh);
}

void AutoFilterGraph::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    DeviceCanvas::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateCurves();
        updateColumns();
    }
}

void AutoFilterGraph::refreshDisplays() {
    const double dt = tickSeconds();
    bool moving = false;

    // The input's spectrum.
    const std::vector<float> samples = readDisplay(QStringLiteral("input"));
    if (spectrum_.add(samples.data(), samples.size(), sampleRate(), dt)) {
        updateColumns();
        moving = true;
    }

    // The modulation's values, as whole frames (a read between two streams leaves the rest for the next tick).
    for (int s = 0; s < kStreams; ++s) {
        const std::vector<float> values = readDisplay(kDisplayIds[s]);
        pending_[size_t(s)].insert(pending_[size_t(s)].end(), values.begin(), values.end());
    }
    size_t count = pending_[0].size();
    for (const std::vector<float>& values : pending_) count = std::min(count, values.size());
    for (size_t i = 0; i < count; ++i) {
        Playback::Frame frame;
        for (int s = 0; s < kStreams; ++s) {
            const float v = pending_[size_t(s)][i];
            frame[size_t(s)] = std::isfinite(v) ? v : 0.0f;
        }
        playback_.append(frame);
    }
    for (std::vector<float>& values : pending_) values.erase(values.begin(), values.begin() + std::ptrdiff_t(count));
    playback_.endBatch(int(count));

    const bool wasLive = live_;
    if (count > 0) {
        sinceArrival_ = 0.0;
        if (!live_) {
            live_ = true;
            playback_.snapToNewest();
        }
    } else {
        sinceArrival_ += dt;
    }
    playback_.advance(dt, sampleRate() / kDisplaySamples);
    if (live_ && sinceArrival_ > kStaleSeconds)
        live_ = false;

    if (live_) {
        // Where the engine has the cutoff, eased a little (the playback is already smooth: this only rounds
        // off coming back from rest).
        const double fraction = easeFraction(dt, 0.02);
        cutoffLeft_.target = log2Of(playback_.logValue(0));
        cutoffRight_.target = log2Of(playback_.logValue(1));
        envelope_.target = std::clamp(playback_.value(2), 0.0, 1.0);
        lfo_.target = std::clamp(playback_.value(3), -1.0, 1.0);
        moving |= cutoffLeft_.step(fraction, 1e-5) | cutoffRight_.step(fraction, 1e-5);
        moving |= envelope_.step(easeFraction(dt, 0.04), 1e-4) | lfo_.step(fraction, 1e-4);
    } else {
        // Quiet: back to where the parameters put it.
        const double fraction = easeFraction(dt, 0.08);
        cutoffLeft_.target = cutoffRight_.target = log2Of(filter_.cutoffHz);
        envelope_.target = lfo_.target = 0.0;
        moving |= cutoffLeft_.step(fraction, 1e-5) | cutoffRight_.step(fraction, 1e-5);
        moving |= envelope_.step(fraction, 1e-4) | lfo_.step(fraction, 1e-4);
    }
    if (moving)
        updateCurves();
    if (live_ != wasLive)
        Q_EMIT liveChanged();
    animating_ = moving || live_ != wasLive;
    if (animating_)
        update();
}

// --- Dragging -------------------------------------------------------------------------

void AutoFilterGraph::setDragging(bool dragging) {
    if (dragging_ == dragging)
        return;
    dragging_ = dragging;
    Q_EMIT draggingChanged();
}

void AutoFilterGraph::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (secondPressOfDoubleClick(event))
        return;
    gesture_ = lastGesture_ = newGestureKey();
    setDragging(true);
    touch(QStringLiteral("freq"));
    dragTo(event->position());
}

void AutoFilterGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!gesture_.isEmpty())
        dragTo(event->position());
}

void AutoFilterGraph::mouseReleaseEvent(QMouseEvent*) {
    gesture_.clear();
    setDragging(false);
}

void AutoFilterGraph::mouseUngrabEvent() {
    gesture_.clear();
    setDragging(false);
}

void AutoFilterGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    gesture_.clear();
    setDragging(false);
    // Under the first click's key: its jump and the reset are one undo step.
    setParams({{QStringLiteral("freq"), defaultValue(QStringLiteral("freq"))},
               {QStringLiteral("resonance"), defaultValue(QStringLiteral("resonance"))}},
              lastGesture_.isEmpty() ? newGestureKey() : lastGesture_);
}

void AutoFilterGraph::dragTo(const QPointF& pos) {
    const double freq = std::clamp(freqAt(pos.x()), sub::app::kAutoFilterMinFrequency,
                                   sub::app::kAutoFilterMaxFrequency);
    const double resonance = std::nearbyint(resonanceAt(pos.y()) * 10.0) / 10.0;
    setParams({{QStringLiteral("freq"), freq}, {QStringLiteral("resonance"), resonance}}, gesture_);
}

// --- Painting ---------------------------------------------------------------------------

void AutoFilterGraph::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const QRectF r = plot();
    p.fillRect(QRectF(0, 0, width(), height()), Theme::meterBg());
    drawDecadeGrid(p, r, frequencyAxis());
    const double zero = yOfDb(0.0);
    p.drawLine(QPointF(r.left(), zero), QPointF(r.right(), zero), withAlpha(Theme::text(), 30), 1.0);

    // The input's spectrum, behind it all.
    const int columns = int(columns_.size());
    const double floor = sub::app::analysis::FallingSpectrum::kFloorDb;
    if (columns >= 2 && *std::max_element(columns_.begin(), columns_.end()) > floor) {
        std::vector<QPointF> shape;
        shape.reserve(std::size_t(columns) + 2);
        shape.emplace_back(r.left(), r.bottom());
        for (int i = 0; i < columns; ++i) {
            const double h = std::clamp((columns_[std::size_t(i)] - floor) / -floor, 0.0, 1.0) * r.height();
            shape.emplace_back(r.left() + (i + 0.5) * r.width() / columns, r.bottom() - h);
        }
        shape.emplace_back(r.right(), r.bottom());
        p.fillToBaseline(shape.data(), int(shape.size()), r.bottom(), withAlpha(Theme::text(), 30));
    }

    // The reach: the cutoffs the modulation can take it to, a thin bar along the bottom, the cutoff's tick on it.
    const auto [low, high] = reach();
    const QRectF bar(xOf(low), r.bottom() - kSweepBar, std::max(1.0, xOf(high) - xOf(low)), kSweepBar);
    if (high > low * 1.001)
        p.fillRect(bar, withAlpha(Theme::accent(), 60 + int(50 * envelope_.value)));
    const double tick = xOf(cutoffLeft());
    p.fillRect(QRectF(tick - 1, r.bottom() - kSweepBar - 2, 2, kSweepBar + 2), withAlpha(Theme::accent(), 220));

    // The right side's curve, thin, where it differs.
    if (showRight_ && curveRight_.size() >= 2)
        p.drawPolyline(curveRight_.data(), int(curveRight_.size()), withAlpha(Theme::scopeLine(), 150), 1.0);

    // The curve: filled faintly, glowing as hard as the envelope opens it.
    if (curveLeft_.size() >= 2) {
        const QColor color = Theme::scopeLine();
        p.fillToBaseline(curveLeft_.data(), int(curveLeft_.size()), r.bottom(),
                         withAlpha(color, 26 + int(30 * envelope_.value)));
        if (envelope_.value > 0.02)
            p.drawPolyline(curveLeft_.data(), int(curveLeft_.size()),
                           withAlpha(color, int(70 * envelope_.value)), 4.0);
        p.drawPolyline(curveLeft_.data(), int(curveLeft_.size()), color, 1.5);
    }

    // The dot: where Frequency and Resonance are, its halo breathing with the LFO.
    const QPointF at = dot();
    const double breath = 0.5 + 0.5 * lfo_.value;
    if (live_)
        p.fillRadialGlow(at, kDotRadius + 3.0 + 4.0 * breath, withAlpha(Theme::accent(), 40 + int(50 * breath)));
    p.drawEllipse(QRectF(at.x() - kDotRadius, at.y() - kDotRadius, 2 * kDotRadius, 2 * kDotRadius),
                  dragging_ ? Theme::text() : Theme::accent(), 2);
}

}  // namespace sub::ui
