#include "devices/VitalGraph.h"

#include "input/GestureKey.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QLinearGradient>

namespace sub::ui {

namespace {
constexpr double kDimSeconds = 0.04;  // a switch eases over about 120 ms
}

VitalGraph::VitalGraph(QQuickItem* parent) : DeviceCanvas(parent) {}

QFont VitalGraph::labelFont() { return uiFont(7); }

void VitalGraph::setPrefix(const QString& prefix) {
    if (prefix_ == prefix)
        return;
    prefix_ = prefix;
    synced_ = false;  // (another section: shown as it is, not eased into)
    Q_EMIT sectionChanged();
    refresh();
}

void VitalGraph::setIndex(int index) {
    if (index_ == index)
        return;
    index_ = index;
    synced_ = false;
    Q_EMIT sectionChanged();
    refresh();
}

QRectF VitalGraph::plot() const { return QRectF(0, 0, width(), height()).adjusted(1, 1, -1, -1); }

void VitalGraph::sync() {
    const bool on = !hasSwitch() || prefix_.isEmpty() || param(QStringLiteral("on")) >= 0.5;
    dim_.target = on ? 0.0 : 1.0;
    if (!synced_)
        dim_.snap(dim_.target);
    synced_ = true;
    if (on != on_) {
        on_ = on;
        Q_EMIT onChanged();
    }
    readSection();
    update();
}

bool VitalGraph::advanceDim(double seconds) { return dim_.step(easeFraction(seconds, kDimSeconds)); }

void VitalGraph::paintBackground(SgPainter& p) const {
    const QRectF rect(0, 0, width(), height());
    QLinearGradient background(rect.topLeft(), rect.bottomLeft());
    background.setColorAt(0, backgroundTop());
    background.setColorAt(1, backgroundBottom());
    p.fillRect(rect, background);
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2, QColor(255, 255, 255, 16), 1);
}

void VitalGraph::beginGesture(const QString& suffix) {
    gesture_ = newGestureKey();
    touch(prefix_ + suffix);
}

void VitalGraph::setDragged(const sub::app::OrderedMap<QString, double>& values) {
    if (!gesture_.isEmpty())
        setParams(values, gesture_);
}

}  // namespace sub::ui
