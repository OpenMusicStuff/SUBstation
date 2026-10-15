#pragma once

// The Vocal Rider's display: the last kHistorySeconds scrolling right to left. Above, on one dB axis (kFloorDb at
// the bottom to 0 dB at the top), the vocal's level as the rider hears it filled grey from the bottom, what comes out
// (that level and the ride) outlined over it, the music on the sidechain as a dim line, and the target as a blue
// line dragged up and down (one undo step per drag, relative to where the press was; Shift finely; a press anywhere
// above takes it), double-clicked back to its default, right-clicked for its parameter's menu (it moves with Music
// Sens. as the device moves it). Below, a lane of the ride itself: 0 dB across its middle, up for gain, down for
// cut, Range Min and Max marked (the editor shows the ride now in figures, from gain()).
//
// It reads the device's displays "level", "gain", "target" and "music" (one value per kDisplaySamples) by absolute
// index into a ring per stream, and scrolls as the Compressor's ReductionGraph does: on by the time since the last
// tick, a steady lag behind the newest value, in columns of whole buckets of values aligned to their index.

#include "devices/DeviceCanvas.h"

#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class VocalRiderGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double gain READ gain NOTIFY levelsChanged)    // dB, the ride now
    Q_PROPERTY(double level READ level NOTIFY levelsChanged)  // dB, the vocal's level now
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)

public:
    static constexpr int kMinimumWidth = 220;
    static constexpr int kMinimumHeight = 100;
    static constexpr int kDisplaySamples = 256;  // the Vocal Rider's audio per display value
    static constexpr double kHistorySeconds = 5.0;
    static constexpr double kFloorDb = -60.0;
    static constexpr double kLaneShare = 0.3;  // of the height: the ride's lane
    static constexpr double kLineGrab = 6.0;
    enum Stream { Level = 0, Gain, Target, Music, kStreams };

    explicit VocalRiderGraph(QQuickItem* parent = nullptr);

    double gain() const { return gain_; }
    double level() const { return level_; }
    bool dragging() const { return dragging_; }

    double yOf(double db) const;      // the levels' axis, held to it
    double laneYOf(double db) const;  // the ride's lane
    double targetY() const { return yOf(target_); }

Q_SIGNALS:
    void levelsChanged();
    void draggingChanged();
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
    void fitRings(double valuesPerSecond);
    bool readStreams(bool& restarted);
    qint64 historyBegin() const;
    int bucketSize() const;
    double levelsBottom() const;
    double laneRange() const;  // dB either way the lane shows
    bool onLine(const QPointF& pos) const;
    void endDrag();

    std::array<std::vector<float>, kStreams> rings_;
    qint64 capacity_ = 0;
    std::array<qint64, kStreams> ends_{};
    qint64 begin_ = 0;
    qint64 newest_ = 0;
    bool started_ = false;
    double rate_ = 0.0;
    double scroll_ = 0.0;

    double gain_ = 0.0;
    double level_ = kFloorDb;
    double target_ = -18.0;       // the parameter
    double liveTarget_ = -18.0;   // as the device has it now (Music Sens. moving it)
    double minGain_ = -12.0, maxGain_ = 12.0;

    bool dragging_ = false;
    bool hovered_ = false;
    QString gesture_;
    double dragValue_ = 0;
    double lastY_ = 0;

    std::vector<float> inTops_, bottoms_;
    std::vector<QPointF> outline_, music_, ride_, targets_;
};

}  // namespace sub::ui
