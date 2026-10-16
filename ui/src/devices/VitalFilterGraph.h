#pragma once

// One of Vital's filters in its editor (filter 1, 2, or the effects' filter):
// its response over 20 Hz..20 kHz (log) and -42..+30 dB, sketched from its
// model, style, cutoff, resonance and blend (response(): the classic models
// as a state-variable filter blending low-pass, band-pass and high-pass, or
// two in series for 24 dB; notches, the diode's low cut, the formant's three
// peaks, the comb's teeth, the phaser's notches: the shape Vital's own window
// draws, not its exact curve), over the device's output spectrum (the display
// "output", falling 62.5 dB a second). The handle at the cutoff drags across for
// the cutoff and up and down for the resonance (one undo step a drag, Shift
// finely); double-clicking it resets both. Dimmed while the filter is off.

#include "devices/VitalGraph.h"

#include "analysis/Spectrum.h"

#include <cmath>
#include <vector>

namespace sub::ui {

class VitalFilterGraph : public VitalGraph {
    Q_OBJECT
    QML_ELEMENT

public:
    static constexpr double kLow = 20.0, kHigh = 20000.0;
    static constexpr double kFloorDb = -42.0, kCeilingDb = 30.0;
    static constexpr double kMinCutoff = 8.0, kMaxCutoff = 136.0;  // the cutoff's range, in MIDI notes

    // Vital's models (synth_strings.h's kFilterModelNames).
    enum Model { Analog = 0, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser };

    struct Settings {
        int model = Analog;
        int style = 0;
        double cutoffHz = 261.6;
        double resonance = 0.5;  // 0..1
        double blend = 0.0;      // 0..2: low-pass, band-pass, high-pass (the classic models)
        double formantX = 0.5, formantY = 0.5, formantShift = 0.0, formantResonance = 0.85, formantSpread = 0.0;
        double combOffset = 42.0;  // semitones ("blend_transpose")
    };

    explicit VitalFilterGraph(QQuickItem* parent = nullptr);

    // The response's gain in dB at `hz`.
    static double response(const Settings& settings, double hz);
    static double hzOfNote(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }
    const Settings& settings() const { return s_; }

    double xOf(double hz) const;
    double yOf(double db) const;
    QPointF handlePosition() const;

protected:
    void readSection() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRectF area() const;
    void updateCurve();

    Settings s_;
    double cutoffNote_ = 60.0;
    std::vector<double> curve_;  // dB per column
    sub::app::analysis::FallingSpectrum spectrum_;
    std::vector<double> columns_;
    QPointF anchor_;
    double startNote_ = 60.0, startResonance_ = 0.5;
};

}  // namespace sub::ui
