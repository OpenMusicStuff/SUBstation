#pragma once

// The base of the Vital editor's drawn panels (VitalWavetableGraph,
// VitalEnvelopeGraph, VitalLfoGraph, VitalFilterGraph, VitalScope): a
// DeviceCanvas over one of its sections, named by its parameters' prefix
// ("osc_1_", "env_2_", "filter_fx_") and its index among its kind (oscillator
// 0..2, LFO 0..7: what its state values are kept by). It draws the house's
// display well (a dark gradient, its grid faint) and dims what it draws while
// the section is switched off (eased over 120 ms), and gives its subclasses a
// drag's gesture: one undo step for all a drag sets.

#include "devices/DeviceCanvas.h"
#include "devices/EditorPaint.h"

#include <QColor>
#include <QFont>
#include <QRectF>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace sub::ui {

class VitalGraph : public DeviceCanvas {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("VitalGraph is a base class for the Vital editor's panels")
    Q_PROPERTY(QString prefix READ prefix WRITE setPrefix NOTIFY sectionChanged)
    Q_PROPERTY(int index READ index WRITE setIndex NOTIFY sectionChanged)
    // Whether the section is on (its "on" parameter; a section without one always is).
    Q_PROPERTY(bool on READ on NOTIFY onChanged)

public:
    explicit VitalGraph(QQuickItem* parent = nullptr);

    QString prefix() const { return prefix_; }
    void setPrefix(const QString& prefix);
    int index() const { return index_; }
    void setIndex(int index);
    bool on() const { return on_; }

    // The section's parameter `suffix` ("cutoff": "filter_1_cutoff") as it is now.
    double param(const QString& suffix) const { return value(prefix_ + suffix); }
    // Where it draws: the item less a pixel all round.
    QRectF plot() const;

    // The house's colours for these panels.
    static QColor backgroundTop() { return {0x1b, 0x1e, 0x25}; }
    static QColor backgroundBottom() { return {0x0f, 0x11, 0x15}; }
    static QColor gridMajor() { return {255, 255, 255, 28}; }
    static QColor gridMinor() { return {255, 255, 255, 10}; }
    static QColor labelColor() { return {255, 255, 255, 96}; }
    static QFont labelFont();

Q_SIGNALS:
    void sectionChanged();
    void onChanged();

protected:
    void sync() override;
    // The section's parameters as they are now: read what is drawn (the base reads `on`).
    virtual void readSection() {}
    // Whether the section has an "on" parameter.
    virtual bool hasSwitch() const { return true; }
    // The well, its border; `dim()` is how far it is switched off (0..1, eased).
    void paintBackground(SgPainter& painter) const;
    double dim() const { return dim_.value; }
    // Moves the dimming on by a tick; whether it still moves.
    bool advanceDim(double seconds);

    // A drag: begin (touching `suffix`'s parameter, so its automation shows), set (one undo step per drag), end.
    void beginGesture(const QString& suffix);
    void setDragged(const sub::app::OrderedMap<QString, double>& values);
    void endGesture() { gesture_.clear(); }
    bool dragging() const { return !gesture_.isEmpty(); }

private:
    QString prefix_;
    int index_ = 0;
    bool on_ = true;
    bool synced_ = false;
    Eased dim_;
    QString gesture_;
};

}  // namespace sub::ui
