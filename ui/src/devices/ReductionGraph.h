#pragma once

// The Compressor's display, as Ableton's Activity view: the last
// kHistorySeconds scrolling right to left on one dB axis (kFloorDb at the
// bottom to 0 dBFS at the top): the level going in (what keys it) filled grey
// from the bottom, the output outlined over it (showOutput), and the gain
// reduction as an orange line hanging from the top (showReduction: 6 dB of
// reduction 6 dB down). Across it the threshold, a blue line dragged up and
// down (one undo step per drag, relative to where the press was; Shift finely;
// a press anywhere in it takes the line), double-clicked back to its default,
// right-clicked for its parameter's menu. The reduction now shows in figures at
// the bottom right.
//
// It reads the device's displays "reduction", "input" and "output" (one value
// per kDisplaySamples) by absolute index into a ring per stream, and scrolls as
// the Gate's display does: on by the time since the last tick, a steady 30 ms
// behind the newest value (which absorbs the audio's block-sized bursts), in
// columns of whole buckets of values aligned to their index (so nothing
// shimmers as it moves), gliding by fractions of a pixel.

#include "devices/DeviceCanvas.h"

#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class ReductionGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double reduction READ reduction NOTIFY levelsChanged)  // dB, the latest
    Q_PROPERTY(double levelIn READ levelIn NOTIFY levelsChanged)      // dBFS
    Q_PROPERTY(double levelOut READ levelOut NOTIFY levelsChanged)
    Q_PROPERTY(bool showReduction READ showReduction WRITE setShowReduction NOTIFY showChanged)
    Q_PROPERTY(bool showOutput READ showOutput WRITE setShowOutput NOTIFY showChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)  // the threshold is being dragged

public:
    static constexpr int kMinimumWidth = 200;
    static constexpr int kMinimumHeight = 80;
    static constexpr int kDisplaySamples = 256;  // the Compressor's audio per display value
    static constexpr double kHistorySeconds = 2.5;
    static constexpr double kFloorDb = -60.0;
    static constexpr double kLineGrab = 6.0;  // px either side of the threshold line that pick it up
    enum Stream { Reduction = 0, Input, Output, kStreams };

    explicit ReductionGraph(QQuickItem* parent = nullptr);

    double reduction() const { return reduction_; }
    double levelIn() const { return levelIn_; }
    double levelOut() const { return levelOut_; }
    bool showReduction() const { return showReduction_; }
    void setShowReduction(bool show);
    bool showOutput() const { return showOutput_; }
    void setShowOutput(bool show);
    bool dragging() const { return dragging_; }

    double yOf(double db) const;  // held to the plot
    double thresholdY() const { return yOf(threshold_); }
    double scroll() const { return scroll_; }  // where the right edge is, in values (absolute index)
    qint64 newest() const { return newest_; }  // the index after the latest value all three streams have

Q_SIGNALS:
    void levelsChanged();
    void showChanged();
    void draggingChanged();
    // The threshold line right-clicked: its parameter's menu (the editor opens it).
    void paramMenuRequested(const QString& paramId);

protected:
    void sync() override;
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    // The rate the values come at, and the rings sized for it (resized, the history starts again).
    void fitRings(double valuesPerSecond);
    // Reads what the displays published since the last tick; whether anything came.
    bool readStreams(bool& restarted);
    qint64 historyBegin() const;
    int bucketSize() const;  // values per drawn column, so a column is at least a pixel wide
    bool onLine(const QPointF& pos) const;
    void endDrag();

    // The history: a ring per stream, by absolute index.
    std::array<std::vector<float>, kStreams> rings_;
    qint64 capacity_ = 0;                  // each ring's size (a power of two)
    std::array<qint64, kStreams> ends_{};  // after each stream's latest value (-1: not known)
    qint64 begin_ = 0;                     // the first index every stream has
    qint64 newest_ = 0;
    bool started_ = false;
    double rate_ = 0.0;    // values a second
    double scroll_ = 0.0;

    double reduction_ = 0.0;
    double levelIn_ = kFloorDb;
    double levelOut_ = kFloorDb;
    double threshold_ = -18.0;
    bool showReduction_ = true;
    bool showOutput_ = false;

    bool dragging_ = false;
    bool hovered_ = false;  // the mouse is on the threshold line
    QString gesture_;       // the drag's merge key
    double dragValue_ = 0;  // the threshold as the drag has it (unclamped)
    double lastY_ = 0;

    // paint()'s scratch, sized once and reused.
    std::vector<float> inTops_, bottoms_;
    std::vector<QPointF> outline_, reductionLine_;
};

}  // namespace sub::ui
