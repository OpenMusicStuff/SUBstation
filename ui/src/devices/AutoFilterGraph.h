#pragma once

// The Auto Filter's display: the filter's response on a 20 Hz..20 kHz log axis
// (+24..-36 dB), over a spectrum of the input (the display `input`).
//
// - The curve is the engine's own (sub::app::autoFilterResponseDb) at the cutoff
//   the modulation has reached: Frequency as it is now, moved by the sweep the
//   engine publishes (`sweep_l`, `sweep_r`: octaves) with the envelope (`env`)
//   and the LFO (`lfo`). A DisplayPlayback plays those back at the audio's pace,
//   so the curve sweeps smoothly as the sound does; the parameters themselves
//   are never taken from the engine, so an edit is drawn at once (the engine
//   publishes while stopped too, its values arriving a block at a time and
//   played back a little late). Where the right side's cutoff differs (Phase,
//   Spin) its curve is drawn thin beside the left's. Without values for
//   kStaleSeconds (bypassed, its track done) the sweep eases back to none.
// - Its subtle life: the curve glows as hard as the envelope opens it, the
//   dot's halo breathes with the LFO, and along the bottom a thin bar spans the
//   cutoffs the modulation can reach, a tick riding it where the cutoff is now.
// - Drag (anywhere on it) for the Frequency across and the Resonance up and
//   down, one undo step per drag; double-click resets both (one step with the
//   first click's jump).

#include "analysis/Spectrum.h"
#include "audio/AutoFilterResponse.h"
#include "devices/DeviceCanvas.h"
#include "devices/DisplayPlayback.h"
#include "devices/EditorPaint.h"

#include <QPointF>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class AutoFilterGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    // Values coming from the engine (the curve is where the modulation has it; else the parameters').
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)

public:
    static constexpr int kStreams = 4;  // sweep_l, sweep_r, env, lfo
    using Playback = DisplayPlayback<kStreams>;
    static constexpr int kDisplaySamples = 128;  // the engine's samples per value

    static constexpr int kWidth = 184;
    static constexpr int kMinimumHeight = 60;
    static constexpr double kLow = 20.0;  // Hz across the plot
    static constexpr double kHigh = 20000.0;
    static constexpr double kTopDb = 24.0;  // dB up the plot
    static constexpr double kBottomDb = -36.0;
    static constexpr double kSweepBar = 3.0;      // the reach bar along the bottom
    static constexpr double kDotRadius = 5.0;
    static constexpr double kStaleSeconds = 0.3;  // no display values this long: not live
    static constexpr int kColumn = 2;             // px per point of the curves and the spectrum

    explicit AutoFilterGraph(QQuickItem* parent = nullptr);

    QRectF plot() const;
    LogAxis frequencyAxis() const;
    double xOf(double freq) const;
    double freqAt(double x) const;
    double yOfDb(double db) const;
    // Where the dot is for a Resonance (%): the bottom of the plot at 0, its top at 125 %.
    double yOfResonance(double resonance) const;
    double resonanceAt(double y) const;  // held to 0..125
    QPointF dot() const;

    bool live() const { return live_; }
    bool dragging() const { return dragging_; }
    // The cutoffs drawn (Hz): where the modulation has them while live, else Frequency.
    double cutoffLeft() const;
    double cutoffRight() const;
    // The envelope (0..1) and the LFO (-1..1) as drawn: eased while live, back to rest when not.
    double envelope() const { return envelope_.value; }
    double lfo() const { return lfo_.value; }
    // The lowest and highest cutoffs the envelope and the LFO can take the filter to (Hz).
    std::pair<double, double> reach() const;
    // The curve's points (the left side's), as last drawn.
    const std::vector<QPointF>& curve() const { return curveLeft_; }
    // Whether the last refresh moved anything (and so repainted).
    bool animating() const { return animating_; }

Q_SIGNALS:
    void liveChanged();
    void draggingChanged();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void updateCurves();
    void updateColumns();
    void dragTo(const QPointF& pos);
    void setDragging(bool dragging);
    std::vector<QPointF> curveAt(double cutoff) const;

    sub::app::AutoFilterCurve filter_;  // the parameters' (its cutoff: Frequency)
    double envAmount_ = 0.0, lfoAmount_ = 0.0;
    Playback playback_;
    std::array<std::vector<float>, kStreams> pending_;  // values read but not yet whole frames
    bool live_ = false;
    double sinceArrival_ = 1.0;
    Eased sweepLeft_, sweepRight_;  // how far the modulation moves each side's cutoff from Frequency (octaves)
    Eased envelope_, lfo_;
    std::vector<QPointF> curveLeft_, curveRight_;
    bool showRight_ = false;
    sub::app::analysis::FallingSpectrum spectrum_;
    std::vector<double> columns_;
    bool animating_ = false;
    bool dragging_ = false;
    QString gesture_, lastGesture_;
};

}  // namespace sub::ui
