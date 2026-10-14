#pragma once

// What Vital plays, live, in its editor: the device's output (its display
// "output", mono) as an oscilloscope (the latest kWindow samples, starting at a
// rising zero crossing so a steady tone stands still) or as a spectrum (20 Hz
// ..20 kHz, falling 1 dB a refresh); "badge" draws the scope faint and without
// its well or grid, behind the preset's name. It reads how many voices play
// (the display "voices") too. Once the sound stops it eases flat and goes
// still (nothing repaints while silent).

#include "devices/VitalGraph.h"

#include "analysis/Spectrum.h"

#include <vector>

namespace sub::ui {

class VitalScope : public VitalGraph {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)  // "scope", "spectrum" or "badge"
    Q_PROPERTY(int voices READ voices NOTIFY voicesChanged)

public:
    static constexpr int kWindow = 1024;  // samples shown (about 21 ms at 48 kHz)
    static constexpr int kKept = 4096;    // samples kept, to find a crossing in

    explicit VitalScope(QQuickItem* parent = nullptr);

    QString mode() const { return mode_; }
    void setMode(const QString& mode);
    int voices() const { return voices_; }
    // The samples drawn (kWindow, or none yet), and how loud they are drawn (eases to 0 once silent).
    const std::vector<float>& shown() const { return shown_; }
    double level() const { return level_.value; }

Q_SIGNALS:
    void modeChanged();
    void voicesChanged();

protected:
    bool hasSwitch() const override { return false; }
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    void updateShown();

    QString mode_ = QStringLiteral("scope");
    int voices_ = 0;
    std::vector<float> kept_;  // the newest samples, oldest first
    std::vector<float> shown_;
    double stale_ = 0.0;
    Eased level_;  // 1 while sound comes, easing to 0 after
    sub::app::analysis::FallingSpectrum spectrum_;
    std::vector<double> columns_;
};

}  // namespace sub::ui
