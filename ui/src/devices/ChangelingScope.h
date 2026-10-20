#pragma once

// Changeling's display: the last kHistorySeconds scrolling right to left on a pitch axis, a line a semitone (the C's
// brighter, named at the left): the pitch heard as a dim line, the pitch that comes out as the accent's, and where
// Quantize or Robot holds the voice, that note as a band. In its corner the note that comes out now, and the cents
// off it. The axis follows the voice: it eases to take in what the history shows (an octave and a fifth at the
// least), and stays where it was while nothing is heard.
//
// It reads the device's displays "pitch", "out" and "note" (one value per kDisplaySamples; pitch 0 and note -1:
// unvoiced) by absolute index into a ring per stream, and scrolls as the Vocal Rider's graph does: on by the time
// since the last tick, a steady lag behind the newest value, in columns of whole buckets of values.

#include "devices/DeviceCanvas.h"

#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace sub::ui {

class ChangelingScope : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool voiced READ voiced NOTIFY heardChanged)
    Q_PROPERTY(double heard READ heard NOTIFY heardChanged)  // the pitch heard now (MIDI; 0: none)
    Q_PROPERTY(double out READ out NOTIFY heardChanged)      // the pitch out now (MIDI; 0: none)

public:
    static constexpr int kMinimumWidth = 200;
    static constexpr int kMinimumHeight = 96;
    static constexpr int kDisplaySamples = 256;  // Changeling's audio per display value
    static constexpr double kHistorySeconds = 3.0;
    static constexpr double kLeastSpan = 19.0;  // semitones the axis shows at the least
    static constexpr double kSilentSeconds = 0.25;
    enum Stream { Heard = 0, Out, Note, kStreams };

    explicit ChangelingScope(QQuickItem* parent = nullptr);

    bool voiced() const { return voiced_; }
    double heard() const { return heard_; }
    double out() const { return out_; }
    // The axis (MIDI pitch at the bottom and the top), eased; y of a pitch.
    double low() const { return low_; }
    double high() const { return high_; }
    double yOf(double pitch) const;

Q_SIGNALS:
    void heardChanged();

protected:
    void refreshDisplays() override;
    void paint(SgPainter& painter) override;

private:
    void fitRings(double valuesPerSecond);
    bool readStreams(bool& restarted);
    qint64 historyBegin() const;
    int bucketSize() const;
    void fitAxis(double dt);

    std::array<std::vector<float>, kStreams> rings_;
    qint64 capacity_ = 0;
    std::array<qint64, kStreams> ends_{};
    qint64 begin_ = 0;
    qint64 newest_ = 0;
    bool started_ = false;
    double rate_ = 0.0;
    double scroll_ = 0.0;

    bool voiced_ = false;
    double heard_ = 0.0, out_ = 0.0;
    double silentFor_ = 1e9;
    double low_ = 48.0, high_ = 72.0;  // C3 to C5 until a voice is heard
    bool axisSet_ = false;

    std::vector<QPointF> heardLine_, outLine_;
    std::vector<float> noteTops_, noteBottoms_;
};

}  // namespace sub::ui
