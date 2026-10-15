#pragma once

// The Vocoder's display, as Ableton's: its filter bank across a log frequency axis (20 Hz to 20 kHz), each band a
// bar as tall as the modulator's level in it (the display "bands": kDisplayBands values a time, the bands' levels in
// dB from the lowest), the bands' filters drawn faintly over them (the engine's own, VocoderResponse), and where
// Formant moves the carrier's bands, marked along the bottom. Along the top the bands' count and range, and while
// the Pitch Tracking carrier follows a pitch, that pitch (the display "pitch") and its note.
//
// Its animation, in refreshDisplays() by tickSeconds(): each bar rises at once and falls smoothly; the bars fade
// out while nothing comes. It repaints only while something moves.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class VocoderGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int bandCount READ bandCount NOTIFY layoutChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY pitchChanged)  // Hz the carrier follows (0: none)

public:
    static constexpr int kMinimumWidth = 240;
    static constexpr int kMinimumHeight = 96;
    static constexpr int kDisplayBands = 64;  // values per frame of the display "bands"
    static constexpr int kMaxBands = 40;
    static constexpr double kFloorDb = -72.0;
    static constexpr double kLowHz = 20.0, kHighHz = 20000.0;
    static constexpr int kCurvePoints = 160;

    explicit VocoderGraph(QQuickItem* parent = nullptr);

    int bandCount() const { return int(centres_.size()); }
    double pitch() const { return pitch_; }
    // For tests: band `k`'s level as drawn (dB, eased), and its centre (Hz).
    Q_INVOKABLE double bandLevel(int k) const;
    Q_INVOKABLE double bandCentre(int k) const;
    // The newest frame of band levels (dB, from the lowest) and `dt` seconds on: the bars ease. True while any moves.
    bool advance(const std::vector<float>* frame, double dt);

Q_SIGNALS:
    void layoutChanged();
    void pitchChanged();

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    std::vector<double> centres_;         // Hz
    std::vector<double> carrierCentres_;  // Hz, Formant's
    std::vector<std::vector<double>> curves_;  // each band's response (dB) at curveFrequencies_
    std::vector<double> curveFrequencies_;
    std::array<Eased, kMaxBands> levels_{};
    double low_ = 80.0, high_ = 12000.0;
    double formant_ = 0.0;
    double pitch_ = 0.0;
    double silent_ = 0.0;  // seconds since a frame came
    Eased presence_;
};

}  // namespace sub::ui
