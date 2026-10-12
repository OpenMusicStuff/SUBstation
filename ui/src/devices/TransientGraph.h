#pragma once

// The Transient Shaper's display: the last kHistorySeconds of the sound
// scrolling right to left, as a waveform's outline mirrored about a centre
// line (its level in dB, so quiet tails show): the input as a faint ghost, the
// output over it, and between them, where the shaping turned the sound up or
// down, a glow in the colour of what did it (Attack's orange, Sustain's blue;
// bright where it turned it up, dimmer where down). Under it a lane of the gain
// itself, about 0 dB, filled in the same colours, with the gain now as a figure.
// Each transient the device detects sets off sparks from the output's edge and
// a ring where it struck, which scroll away with the sound and fade; a slim
// meter at the right shows the output's level (red once it reaches full scale).
//
// It reads the device's displays "input", "output", "attack", "sustain" and
// "detect" (one value per 128 samples, pushed together) by absolute index into a
// ring per stream, so they line up whatever each read got. It scrolls smoothly,
// a steady 30 ms behind the newest value, and stops where the values stop.
// Every animation steps in refreshDisplays() by the time since the last tick;
// it repaints only while something moves or changes what is drawn.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QFont>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class TransientGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double gainDb READ gainDb NOTIFY levelsChanged)  // the gain now, eased (dB)
    Q_PROPERTY(bool idle READ idle NOTIFY levelsChanged)        // no values for a while

public:
    static constexpr int kWidth = 268;
    static constexpr int kMinimumHeight = 96;
    static constexpr double kHistorySeconds = 2.0;
    static constexpr double kWaveFloorDb = -48.0;   // the waveform's outline: its centre line
    static constexpr double kWaveCeilingDb = 6.0;   // and its edges
    static constexpr double kLaneDb = 18.0;         // the gain lane: ± this
    static constexpr int kMaxSparks = 96;
    enum Stream { Input = 0, Output, Attack, Sustain, Detect, kStreams };

    // A spark: where it started (an absolute display index, so it scrolls with the sound, and a height on
    // the waveform), how it flies (px/s), how long it has flown and lives (s), and how bright it is.
    struct Spark {
        double index = 0.0;
        double y = 0.0;
        double vx = 0.0, vy = 0.0;
        double age = 0.0, life = 0.5;
        double size = 1.5;
        double strength = 1.0;
    };
    // A ring where a transient struck.
    struct Ring {
        double index = 0.0;
        double y = 0.0;
        double age = 0.0;
        double strength = 1.0;
    };

    explicit TransientGraph(QQuickItem* parent = nullptr);

    static QFont textFont();  // its labels: the UI font at 7 pt
    static qint64 ringCapacity(double valuesPerSecond);

    double gainDb() const { return gain_.value; }
    bool idle() const { return idle_; }

    QRectF plot() const;         // the waveform and the lane, left of the meter
    QRectF wave() const;         // the waveform's part of it
    QRectF lane() const;         // the gain lane's
    QRectF meter() const;        // the output meter's well
    double xOfIndex(double index) const;  // a display index's place, as the history scrolls
    double halfHeight(double db) const;   // the outline's distance from the centre line for a level
    double laneY(double db) const;        // the lane's place for a gain
    double scroll() const { return scroll_; }
    qint64 newest() const { return newest_; }
    int historySize() const;
    float historyAt(int stream, qint64 index) const;
    const std::vector<Spark>& sparks() const { return sparks_; }
    const std::vector<Ring>& rings() const { return strikes_; }
    bool animating() const { return animating_; }

Q_SIGNALS:
    void levelsChanged();

protected:
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    void fitRings(double valuesPerSecond);
    bool readStreams(bool& restarted);
    void write(int stream, qint64 index, float value);
    qint64 historyBegin() const;
    int bucketSize() const;
    void strike(qint64 index, float detectedDb);  // a transient at this index: sparks and a ring

    // The history: a ring per stream, by absolute index.
    std::array<std::vector<float>, kStreams> rings_;
    qint64 capacity_ = 0;
    std::array<qint64, kStreams> ends_{};
    qint64 begin_ = 0;
    qint64 newest_ = 0;
    qint64 lastVaried_ = 0;  // the latest index where the picture changes
    std::array<float, kStreams> last_{};
    bool started_ = false;
    double rate_ = 0.0;
    double scroll_ = 0.0;
    bool armed_ = true;     // the transient fell back since the last strike
    quint32 seed_ = 1;      // the sparks' scatter (the same each run)

    // What the ticks move on.
    double sinceValues_ = 1e9;
    bool idle_ = true;
    Eased gain_;            // dB, the figure
    double gainNow_ = 0.0;  // dB, the latest
    bool attackLeads_ = true;  // whether Attack made more of the gain now (the figure's colour)
    MeterBallistics outMeter_;
    double levelOut_ = kWaveFloorDb;
    double drawnMeter_ = 0.0, drawnPeak_ = 0.0;
    double emittedGain_ = 0.0;
    bool emittedIdle_ = true;
    std::vector<Spark> sparks_;
    std::vector<Ring> strikes_;
    bool animating_ = false;

    // paint()'s scratch, sized once and reused.
    std::vector<float> inTops_, inBottoms_, outTops_, outBottoms_, bandTops_, bandBottoms_, laneTops_, laneBottoms_;
    std::vector<float> change_, attackPart_, sustainPart_;
    std::vector<QPointF> outlineTop_, outlineBottom_, gainLine_;
};

}  // namespace sub::ui
