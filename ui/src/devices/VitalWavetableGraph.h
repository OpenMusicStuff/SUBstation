#pragma once

// An oscillator's wavetable in Vital's editor: its frames stacked back to
// front, receding up and to the right (the first in front), as Vital's engine
// renders them (sub::app::vitalWavetableFrames: rendered again only when the
// wavetable changes), the frame "wave_frame" plays drawn bright in the stack
// and large in front, filled to its zero line, with its number. Dragging up and
// down moves through the frames (one undo step a drag, Shift finely);
// double-clicking goes back to the first. Dimmed while the oscillator is off.

#include "devices/VitalGraph.h"

#include <vector>

namespace sub::ui {

class VitalWavetableGraph : public VitalGraph {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int frames READ frames NOTIFY tableChanged)  // the wavetable's
    Q_PROPERTY(int frame READ frame NOTIFY frameChanged)    // the one playing (of frames)

public:
    static constexpr int kPoints = 256;        // values drawn of each frame
    static constexpr int kStackSlices = 36;    // at most, of the frames behind
    static constexpr double kMaxPosition = 256.0;  // "wave_frame"'s range: 0..256

    explicit VitalWavetableGraph(QQuickItem* parent = nullptr);

    int frames() const { return frames_; }
    int frame() const;
    // Frame `index`'s values (kPoints of them), or none.
    std::vector<float> frameValues(int index) const;
    // Where frame `index` is drawn: its rect (it spans the rect's width; its zero line is the rect's middle).
    QRectF frameRect(int index) const;

Q_SIGNALS:
    void tableChanged();
    void frameChanged();

protected:
    void readSection() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void drawFrame(SgPainter& painter, int index, const QColor& color, double width) const;

    QString state_;              // the device's state last read
    QString table_;              // the wavetables' state value the frames were rendered from
    int tableIndex_ = -1;        // and the oscillator
    std::vector<float> values_;  // frame after frame, kPoints each
    int frames_ = 0;
    double position_ = 0.0;  // "wave_frame"
    int shownFrame_ = -1;
    double pressY_ = 0.0, pressPosition_ = 0.0;
};

}  // namespace sub::ui
