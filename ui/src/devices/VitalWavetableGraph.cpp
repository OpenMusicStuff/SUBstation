#include "devices/VitalWavetableGraph.h"

#include "model/Vital.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QLinearGradient>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {
// The stack: the first frame's rect is the plot's lower left, the last's this far up and to the right.
constexpr double kFrontWidth = 0.78, kFrontHeight = 0.58;  // of the plot
constexpr double kFineRatio = 0.2;  // Shift
}  // namespace

VitalWavetableGraph::VitalWavetableGraph(QQuickItem* parent) : VitalGraph(parent) {
    setImplicitSize(240, 100);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::SizeVerCursor);
}

int VitalWavetableGraph::frame() const {
    return frames_ <= 0 ? 0 : std::clamp(int(std::floor(position_)), 0, frames_ - 1);
}

std::vector<float> VitalWavetableGraph::frameValues(int index) const {
    if (index < 0 || index >= frames_)
        return {};
    const auto begin = values_.begin() + std::ptrdiff_t(index) * kPoints;
    return {begin, begin + kPoints};
}

QRectF VitalWavetableGraph::frameRect(int index) const {
    const QRectF r = plot().adjusted(6, 14, -6, -6);
    const double depth = frames_ > 1 ? double(index) / (frames_ - 1) : 0.0;
    const QRectF front(r.left(), r.bottom() - r.height() * kFrontHeight, r.width() * kFrontWidth,
                       r.height() * kFrontHeight);
    return front.translated(depth * (r.width() - front.width()), -depth * (r.height() - front.height()));
}

void VitalWavetableGraph::readSection() {
    const sub::app::Device* d = device();
    // (The state is read again only when it is another: a copy of it kept, its data is the same while it is.)
    const QString state = d && d->state ? *d->state : QString();
    if (d && (state.constData() != state_.constData() || state.size() != state_.size() || tableIndex_ != index() ||
              values_.empty())) {
        state_ = state;
        const QString table = sub::app::vitalStateValue(*d, QStringLiteral("wavetables"));
        if (table != table_ || tableIndex_ != index() || values_.empty()) {
            table_ = table;
            tableIndex_ = index();
            values_ = sub::app::vitalWavetableFrames(*d, index(), kPoints);
            frames_ = int(values_.size() / kPoints);
            Q_EMIT tableChanged();
        }
    }
    position_ = param(QStringLiteral("wave_frame"));
    if (frame() != shownFrame_) {
        shownFrame_ = frame();
        Q_EMIT frameChanged();
    }
}

void VitalWavetableGraph::refreshDisplays() {
    if (advanceDim(tickSeconds()))
        update();
}

void VitalWavetableGraph::drawFrame(SgPainter& p, int index, const QColor& color, double width) const {
    const std::vector<float> values = frameValues(index);
    if (values.empty())
        return;
    const QRectF rect = frameRect(index);
    const double middle = rect.center().y(), half = rect.height() / 2;
    std::vector<QPointF> line;
    line.reserve(values.size() + 1);
    for (std::size_t i = 0; i <= values.size(); ++i) {
        const float v = values[i % values.size()];  // (round to the start again: a cycle)
        line.emplace_back(rect.left() + rect.width() * double(i) / double(values.size()), middle - v * half);
    }
    p.drawPolyline(line.data(), int(line.size()), color, width);
}

void VitalWavetableGraph::paint(SgPainter& p) {
    paintBackground(p);
    if (frames_ <= 0)
        return;
    p.setAntialiasing(true);
    p.save();
    p.setOpacity(1.0 - 0.6 * dim());
    const QColor accent = Theme::accent();
    const int current = frame();

    // The stack, back to front: evenly through the frames, the one playing among them.
    if (frames_ > 1) {
        const int slices = std::min(frames_, kStackSlices);
        for (int s = slices - 1; s >= 0; --s) {
            const int index = slices > 1 ? int(std::lround(double(s) * (frames_ - 1) / (slices - 1))) : 0;
            const double near = 1.0 - double(index) / (frames_ - 1);
            drawFrame(p, index, withAlpha(mixColor(Qt::white, accent, 0.35), int(8 + 26 * near * near)), 1.0);
        }
        drawFrame(p, current, withAlpha(accent, 150), 1.2);
        // Its edge across the stack: where it is.
        const QRectF first = frameRect(0), last = frameRect(frames_ - 1), at = frameRect(current);
        p.drawLine(first.bottomRight(), last.bottomRight(), gridMinor());
        p.fillEllipse(at.bottomRight(), 2.4, 2.4, accent);
    }
    // The frame playing, in front.
    {
        const QRectF front = frameRect(0);
        const std::vector<float> values = frameValues(current);
        std::vector<QPointF> line;
        const double middle = front.center().y(), half = front.height() / 2;
        for (std::size_t i = 0; i <= values.size(); ++i) {
            const float v = values[i % values.size()];
            line.emplace_back(front.left() + front.width() * double(i) / double(values.size()), middle - v * half);
        }
        QLinearGradient gradient(front.topLeft(), front.bottomLeft());
        gradient.setColorAt(0, withAlpha(accent, 90));
        gradient.setColorAt(0.5, withAlpha(accent, 10));
        gradient.setColorAt(1, withAlpha(accent, 90));
        std::vector<QPointF> above(line), below(line);
        for (QPointF& point : above) point.setY(std::min(point.y(), middle));
        for (QPointF& point : below) point.setY(std::max(point.y(), middle));
        p.fillToBaseline(above.data(), int(above.size()), middle, gradient);
        p.fillToBaseline(below.data(), int(below.size()), middle, gradient);
        p.drawLine(QPointF(front.left(), middle), QPointF(front.right(), middle), gridMajor());
        drawGlowPolyline(p, line, accent, 1.6);
    }
    p.restore();

    // Which frame, of how many.
    const QRectF r = plot();
    p.drawText(QRectF(r.left() + 5, r.top() + 1, r.width() - 10, 12), Qt::AlignLeft | Qt::AlignVCenter,
               frames_ > 1 ? QStringLiteral("Frame %1 / %2").arg(current + 1).arg(frames_) : QStringLiteral("1 frame"),
               labelColor(), labelFont());
}

void VitalWavetableGraph::mousePressEvent(QMouseEvent* event) {
    if (secondPressOfDoubleClick(event))
        return;
    pressY_ = event->position().y();
    pressPosition_ = position_;
    beginGesture(QStringLiteral("wave_frame"));
    event->accept();
}

void VitalWavetableGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging())
        return;
    const double span = std::max(20.0, plot().height() - 20);
    const double fine = event->modifiers() & Qt::ShiftModifier ? kFineRatio : 1.0;
    const double moved = (pressY_ - event->position().y()) / span * kMaxPosition * fine;
    setDragged({{prefix() + QStringLiteral("wave_frame"), std::clamp(pressPosition_ + moved, 0.0, kMaxPosition)}});
}

void VitalWavetableGraph::mouseReleaseEvent(QMouseEvent*) { endGesture(); }

void VitalWavetableGraph::mouseUngrabEvent() { endGesture(); }

void VitalWavetableGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    const QString id = prefix() + QStringLiteral("wave_frame");
    setParamValue(id, defaultValue(id));
    event->accept();
}

}  // namespace sub::ui
