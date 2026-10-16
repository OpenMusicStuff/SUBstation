#include "devices/ModulatorGraph.h"

#include "audio/EngineBridge.h"
#include "audio/ModulatorRandom.h"
#include "devices/CurveGraph.h"
#include "devices/EditorPaint.h"
#include "input/GestureKey.h"
#include "model/Automation.h"
#include "model/Device.h"
#include "model/Project.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace sub::ui {

using Values = sub::app::OrderedMap<QString, double>;

namespace {

const QColor kBackgroundTop(0x1b, 0x1e, 0x25), kBackgroundBottom(0x0f, 0x11, 0x15);
const QColor kGridMajor(255, 255, 255, 30), kGridMinor(255, 255, 255, 10);
const QColor kLabelColor(255, 255, 255, 90);
const QColor kKeyColor(0xff, 0x9f, 0x5a);

// Automation's shape (straight below |a| = 1e-4), as the engine bends segments.
double shape(double x, double bend) {
    const double a = -bend * sub::app::automation::kCurvature;
    if (std::abs(a) < 1e-4)
        return x;
    return std::expm1(a * x) / std::expm1(a);
}

// The value each point snaps to with Ctrl: sixteenths across, eighths up.
double snapped(double value, double grid) { return std::round(value * grid) / grid; }

}  // namespace

ModulatorGraph::ModulatorGraph(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setCursor(Qt::CrossCursor);
}

QString ModulatorGraph::pointParam(int index, const QString& name) { return sub::app::modulatorPointParam(index, name); }

double ModulatorGraph::curveValue(const std::vector<Point>& points, double x) {
    if (points.empty())
        return 0.0;
    if (x <= points.front().x)
        return points.front().y;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[i + 1];
        if (x < b.x) {
            const double span = b.x - a.x;
            if (span <= 0)
                return b.y;
            const double bend = b.y >= a.y ? a.curve : -a.curve;
            return a.y + (b.y - a.y) * shape((x - a.x) / span, bend);
        }
    }
    return points.back().y;
}

Values ModulatorGraph::pointsValues(const std::vector<Point>& points) { return sub::app::modulatorPointValues(points); }


QStringList ModulatorGraph::shapes() const {
    QStringList names;
    for (const sub::app::ModulatorShape& shape : sub::app::modulatorShapes(periodic_))
        names << shape.name;
    return names;
}

double ModulatorGraph::syncedBeats(const QString& label) {
    static const QRegularExpression fraction(QStringLiteral("^(\\d+)/(\\d+)$"));
    static const QRegularExpression bars(QStringLiteral("^([\\d.]+) Bars?$"));
    if (const auto m = fraction.match(label); m.hasMatch() && m.captured(2).toDouble() > 0)
        return 4.0 * m.captured(1).toDouble() / m.captured(2).toDouble();
    if (const auto m = bars.match(label); m.hasMatch())
        return 4.0 * m.captured(1).toDouble();
    return 0.0;
}

QString ModulatorGraph::undoName(const QString& what) const {
    return (periodic_ ? QStringLiteral("LFO ") : QStringLiteral("Envelope ")) + what;
}

// --- The model --------------------------------------------------------------------------------

void ModulatorGraph::sync() {
    const sub::app::Device* found = device();
    periodic_ = !found || found->kind != QLatin1String("envelope");
    std::vector<Point> points;
    if (alive()) {
        for (int i = 0; i < kPoints; ++i) {
            if (value(pointParam(i, QStringLiteral("used"))) >= 0.5)
                points.push_back({value(pointParam(i, QStringLiteral("x"))), value(pointParam(i, QStringLiteral("y"))),
                                  value(pointParam(i, QStringLiteral("curve")))});
        }
        std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
    }
    points_ = points;
    random_ = alive() && value(QStringLiteral("random")) >= 0.5;
    if (random_ && randomPoints_.empty())
        showRandom(0, 0.5);  // (until one plays: a random curve to show what it does)
    depth_ = std::clamp(value(QStringLiteral("depth")) / 100.0, 0.0, 1.0);
    offset_ = periodic_ ? std::clamp(value(QStringLiteral("offset")) / 200.0, -0.5, 0.5) : 0.0;
    threshold_ = value(QStringLiteral("threshold"));
    phaseShift_ = periodic_ ? std::clamp(value(QStringLiteral("phase")) / 360.0, 0.0, 1.0) : 0.0;
    if (phaseShift_ >= 1.0)
        phaseShift_ = 0.0;  // (360°: where it starts at 0°)
    smoothMs_ = std::max(0.0, value(QStringLiteral("smooth")));
    // Its length: a cycle (the LFO's), or the curve's from a trigger (the Envelope's).
    const sub::app::Project* project = session() ? session()->project() : nullptr;
    const double tempo = project && project->tempo() > 0 ? project->tempo() : 120.0;
    const bool synced = value(QStringLiteral("sync")) >= 0.5;
    QString syncedLabel;
    if (synced && session() && alive()) {
        for (const sub::app::ProcessorParam& param : session()->bridge()->deviceParams(trackId(), deviceId())) {
            if (param.id != QLatin1String("synced"))
                continue;
            const int index = int(std::nearbyint(value(QStringLiteral("synced"))));
            if (index >= 0 && index < param.valueLabels.size())
                syncedLabel = param.valueLabels.at(index);
        }
    }
    const double beats = syncedBeats(syncedLabel);
    if (synced && beats > 0) {
        lengthMs_ = beats * 60000.0 / tempo;
        lengthText_ = syncedLabel + QStringLiteral("  ·  ") + CurveGraph::formatMs(lengthMs_);
    } else if (periodic_) {
        const double rate = std::max(0.001, value(QStringLiteral("rate")));
        lengthMs_ = 1000.0 / rate;
        lengthText_ = (rate < 10 ? pythonFixed(rate, 2) : pythonFixed(rate, 1)) + QStringLiteral(" Hz  ·  ") +
                      CurveGraph::formatMs(lengthMs_);
    } else {
        lengthMs_ = std::max(1.0, value(QStringLiteral("length")));
        lengthText_ = CurveGraph::formatMs(lengthMs_);
    }
    // An Envelope with a sidechain chosen: its hits trigger it (notes always do).
    keyed_ = !periodic_ && found && found->sidechain.has_value();
    hint_.clear();
    if (selected_ >= int(points_.size()))
        selected_ = -1;
    update();
    Q_EMIT curveChanged();
}

void ModulatorGraph::setPoints(const std::vector<Point>& points, const QString& mergeKey, const QString& text) {
    sub::app::OrderedMap<QString, double> values = pointsValues(points);
    if (random_)
        values.insert(QStringLiteral("random"), 0.0);  // (a curve given: it plays it)
    setParams(values, mergeKey, text);
}

void ModulatorGraph::showRandom(int seed, double start) {
    randomSeed_ = seed;
    randomStart_ = start;
    randomPoints_.clear();
    for (const sub::app::ModulatorPoint& p : sub::app::modulatorRandomPoints(seed, start))
        randomPoints_.push_back({p.x, p.y, p.curve});
}

void ModulatorGraph::removePoint(int index) {
    std::vector<Point> points = points_;
    if (!(0 < index && index < int(points.size()) - 1))
        return;  // (the ends stay)
    points.erase(points.begin() + index);
    selected_ = -1;
    hover_ = {QString(), -1};
    setPoints(points, QString(), undoName(QStringLiteral("Delete Point")));
    update();
}

void ModulatorGraph::applyShape(int index) {
    const auto& list = sub::app::modulatorShapes(periodic_);
    if (index < 0 || index >= int(list.size()))
        return;
    const sub::app::ModulatorShape& shape = list[std::size_t(index)];
    setPoints(shape.points, QString(), undoName(QStringLiteral("Shape: ")) + shape.name);
}

void ModulatorGraph::flip() {
    std::vector<Point> points = points_;
    for (Point& p : points)
        p.y = 1.0 - p.y;
    setPoints(points, QString(), undoName(QStringLiteral("Flip Curve")));
}

void ModulatorGraph::reverse() {
    // Back to front. A segment played backwards is the same exponential with its bend the other way
    // (1 - s(1 - u, a) = s(u, -a)), and its direction turns too, so its curve value stays: it moves to the
    // point the segment now starts at.
    std::vector<Point> points;
    for (int i = int(points_.size()) - 1; i >= 0; --i) {
        const double bend = i > 0 ? points_[std::size_t(i) - 1].curve : 0.0;
        points.push_back({1.0 - points_[std::size_t(i)].x, points_[std::size_t(i)].y, bend});
    }
    setPoints(points, QString(), undoName(QStringLiteral("Reverse Curve")));
}

void ModulatorGraph::resetCurve() {
    setPoints(sub::app::modulatorShapes(periodic_).front().points, QString(), undoName(QStringLiteral("Reset Curve")));
}

double ModulatorGraph::played(double y) const {
    return periodic_ ? std::clamp(0.5 + (y - 0.5) * depth_ + offset_, 0.0, 1.0) : std::clamp(y * depth_, 0.0, 1.0);
}

std::vector<double> ModulatorGraph::outputCurve(int count) const {
    count = std::max(1, count);
    const std::vector<Point>& points = shown();
    std::vector<double> out(std::size_t(count) + 1);
    for (int i = 0; i <= count; ++i) {  // (Phase: the cycle starts that far along the curve)
        const double x = double(i) / count + phaseShift_;
        out[std::size_t(i)] = played(curveValue(points, x > 1.0 ? x - 1.0 : x));
    }
    if (smoothMs_ <= 0.01 || lengthMs_ <= 0)
        return out;
    // The one-pole, a step at a time (y = x + c (y - x)); the LFO's runs a few cycles first, to where it settles.
    const double c = std::exp(-(lengthMs_ / count) / smoothMs_);
    double y = periodic_ ? out.back() : played(points.empty() ? 0.0 : points.back().y);
    const std::vector<double> target = out;
    for (int pass = periodic_ ? 4 : 1; pass > 0; --pass) {
        for (int i = 0; i <= count; ++i) {
            y = target[std::size_t(i)] + c * (y - target[std::size_t(i)]);
            out[std::size_t(i)] = y;
        }
    }
    return out;
}

// --- Displays ----------------------------------------------------------------------------------

void ModulatorGraph::refreshDisplays() { advance(tickSeconds()); }

void ModulatorGraph::advance(double seconds) {
    const std::vector<float> values = readDisplay(QStringLiteral("value"));
    const std::vector<float> phases = readDisplay(QStringLiteral("phase"));
    const std::vector<float> keys = readDisplay(QStringLiteral("key"));
    const std::vector<float> seeds = readDisplay(QStringLiteral("seed"));
    const std::vector<float> starts = readDisplay(QStringLiteral("start"));
    const std::size_t count = std::min({values.size(), phases.size(), keys.size(), seeds.size(), starts.size()});
    const std::size_t from = count > std::size_t(DisplayPlayback<5>::kCapacity) ? count - DisplayPlayback<5>::kCapacity : 0;
    double keyPeak = 0.0;
    bool noted = false;
    for (std::size_t i = from; i < count; ++i) {
        playback_.append({values[i], phases[i], keys[i], seeds[i], starts[i]});
        keyPeak = std::max(keyPeak, double(keys[i]));
        noted = noted || keys[i] >= 1.0f;
    }
    playback_.endBatch(int(count - from));
    playback_.advance(seconds, sampleRate() / 64.0);
    if (playback_.empty()) {
        Q_EMIT ticked();
        return;
    }
    output_ = std::clamp(playback_.value(0), 0.0, 1.0);
    // Where on the curve it is: the LFO's phase the short way round; the Envelope's, at rest (-1) or along.
    double phase;
    if (periodic_) {
        phase = playback_.phase(1);
    } else {
        const auto at = static_cast<qint64>(std::floor(playback_.head()));
        const double a = playback_.at(at, 1), b = playback_.at(std::min(at + 1, playback_.newest()), 1);
        const double t = playback_.head() - double(at);
        phase = a < 0 || b < 0 || b < a ? (t < 0.5 ? a : b) : a + (b - a) * t;
    }
    const bool triggered = phase >= 0 && (lastPhase_ < 0 || phase < lastPhase_ - 0.05);
    // Random: the curve playing at the playhead (its frame's seed: never between two).
    if (random_) {
        const auto at = static_cast<qint64>(std::floor(playback_.head() + 0.5));
        const auto seed = static_cast<int>(playback_.at(std::min(at, playback_.newest()), 3));
        const double start = playback_.at(std::min(at, playback_.newest()), 4);
        if (seed >= 0 && (seed != randomSeed_ || start != randomStart_))
            showRandom(seed, start);
    }
    // The trail: a point a step (TrailPace), the newest following the playhead between steps.
    const bool step = trailPace_.step(seconds);
    if (phase >= 0) {
        const QPointF at(phase, curveValue(shown(), phase));
        if (step || trail_.empty())
            trail_.push_back(at);
        else
            trail_.back() = at;
        if (trail_.size() > std::size_t(kTrail))
            trail_.erase(trail_.begin(), trail_.end() - kTrail);
    } else if (!trail_.empty() && step) {
        trail_.erase(trail_.begin());
    }
    if ((!periodic_ && triggered) || (periodic_ && noted))
        flash_ = 1.0;
    else
        flash_ *= decayOver(seconds, kFlashDecay, kFlashDecaySeconds);
    lastPhase_ = phase;
    if (keyed_) {
        const double level = keyPeak > 1e-6 ? 20 * std::log10(keyPeak) : kMeterFloor;
        level_ = std::max(level, level_ - kKeyFallDbPerSecond * seconds);
    }
    update();
    Q_EMIT ticked();
}

// --- Geometry -------------------------------------------------------------------------------------

QRectF ModulatorGraph::plot() const {
    return QRectF(0, 0, width(), height()).adjusted(7, 15, keyed_ ? -(kMeterWidth + 14) : -7, -14);
}

QRectF ModulatorGraph::meterRect() const {
    const QRectF r = plot();
    return QRectF(width() - kMeterWidth - 6, r.top(), kMeterWidth, r.height());
}

QPointF ModulatorGraph::toScreen(double x, double y) const {
    const QRectF r = plot();
    return QPointF(r.left() + x * r.width(), r.bottom() - y * r.height());
}

QPointF ModulatorGraph::fromScreen(const QPointF& pos) const {
    const QRectF r = plot();
    return QPointF((pos.x() - r.left()) / std::max(r.width(), 1.0), (r.bottom() - pos.y()) / std::max(r.height(), 1.0));
}

std::pair<QString, int> ModulatorGraph::hit(const QPointF& pos) const {
    if (random_)
        return {QString(), -1};  // (nothing to edit)
    int best = -1;
    double bestDistance = kHitRadius;
    for (std::size_t i = 0; i < points_.size(); ++i) {
        const QPointF at = toScreen(points_[i].x, points_[i].y);
        const double d = std::hypot(pos.x() - at.x(), pos.y() - at.y());
        if (d < bestDistance) {
            best = int(i);
            bestDistance = d;
        }
    }
    if (best >= 0)
        return {QStringLiteral("point"), best};
    const double x = fromScreen(pos).x();
    if (!(0.0 <= x && x <= 1.0) || points_.size() < 2)
        return {QString(), -1};
    if (std::abs(toScreen(x, curveValue(points_, x)).y() - pos.y()) > kCurveHit)
        return {QString(), -1};
    const int before = int(std::count_if(points_.begin(), points_.end(), [x](const Point& p) { return p.x <= x; }));
    return {QStringLiteral("segment"), std::clamp(before - 1, 0, int(points_.size()) - 2)};
}

QRectF ModulatorGraph::hintRect() const {
    if (hint_.isEmpty())
        return QRectF();
    const QRectF r = plot();
    const double w = SgPainter::textWidth(hint_, uiFont(8)) + 20;
    return QRectF(r.center().x() - w / 2, r.center().y() - 11, w, 22);
}

QString ModulatorGraph::timeText(double x) const { return CurveGraph::formatMs(x * lengthMs_); }

QString ModulatorGraph::readoutText(const Point& point) const {
    return timeText(point.x) + QStringLiteral("  ·  ") + pythonFixed(point.y * 100, 0) + QStringLiteral(" %");
}

// --- Editing ----------------------------------------------------------------------------------------

void ModulatorGraph::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    const bool second = secondPressOfDoubleClick(event);
    if (event->button() == Qt::RightButton) {
        const auto found = hit(pos);
        const bool inner =
            found.first == QLatin1String("point") && 0 < found.second && found.second < int(points_.size()) - 1;
        Q_EMIT contextMenuRequested(pos, inner ? found.second : -1);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    if (second)
        return;  // (the double-click follows)
    forceActiveFocus(Qt::MouseFocusReason);  // (Delete removes the selected point)
    if (hintRect().contains(pos)) {
        Q_EMIT sidechainMenuRequested(plot().center());
        return;
    }
    if (random_)
        return;  // (Random's curve isn't to edit)
    std::vector<Point> points = points_;
    const auto found = hit(pos);
    const bool onPoint = found.first == QLatin1String("point");
    if (onPoint && (event->modifiers() & Qt::AltModifier)) {
        removePoint(found.second);
        return;
    }
    if (!found.first.isEmpty())
        added_ = -1;
    if (onPoint) {
        selected_ = found.second;
        drag_ = Drag{QStringLiteral("point"), found.second, pos, points, newGestureKey(), undoName(QStringLiteral("Move Point"))};
    } else if (!found.first.isEmpty()) {
        drag_ = Drag{QStringLiteral("bend"), found.second, pos, points, newGestureKey(), undoName(QStringLiteral("Bend Curve"))};
    } else if (int(points.size()) < kPoints && plot().adjusted(-4, -4, 4, 4).contains(pos)) {
        const QPointF at = fromScreen(pos);
        double x = std::clamp(at.x(), 0.0, 1.0), y = std::clamp(at.y(), 0.0, 1.0);
        if (event->modifiers() & Qt::ControlModifier) {
            x = snapped(x, 16);
            y = snapped(y, 8);
        }
        if (!points.empty() && !(0.0 < x && x < 1.0))
            return;
        const int index = int(std::count_if(points.begin(), points.end(), [x](const Point& p) { return p.x <= x; }));
        const Point point{x, y, index > 0 ? points[std::size_t(index) - 1].curve : 0.0};
        points.insert(points.begin() + index, point);
        const QString gesture = newGestureKey();
        const QString text = undoName(QStringLiteral("Add Point"));
        setPoints(points, gesture, text);
        added_ = index;
        addedClock_.start();
        selected_ = index;
        drag_ = Drag{QStringLiteral("point"), index, pos, points, gesture, text};
    }
    update();
    Q_EMIT curveChanged();
}

void ModulatorGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!drag_) {
        hoverAt(event->position());
        return;
    }
    const QPointF pos = event->position();
    const Drag& drag = *drag_;
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.2 : 1.0;
    const bool snap = event->modifiers() & Qt::ControlModifier;
    std::vector<Point> points = drag.points;
    const int index = drag.index;
    if (index < 0 || index >= int(points.size()))
        return;
    const QRectF r = plot();
    const double dx = (pos.x() - drag.origin.x()) * fine / std::max(r.width(), 1.0);
    const double dy = (pos.y() - drag.origin.y()) * fine;
    if (drag.kind == QLatin1String("point")) {
        const Point old = points[std::size_t(index)];
        double x = old.x;  // the ends stay at the ends
        double y = old.y - dy / std::max(r.height(), 1.0);
        if (index != 0 && index != int(points.size()) - 1) {
            x = old.x + dx;
            if (snap)
                x = snapped(x, 16);
            x = std::clamp(x, points[std::size_t(index) - 1].x, points[std::size_t(index) + 1].x);
        }
        if (snap)
            y = snapped(y, 8);
        y = std::clamp(y, 0.0, 1.0);
        points[std::size_t(index)] = {x, y, old.curve};
        readout_ = std::pair{toScreen(x, y), readoutText(points[std::size_t(index)])};
    } else {
        const Point old = points[std::size_t(index)];
        const double curve = std::clamp(old.curve - dy / 60.0, -1.0, 1.0);
        points[std::size_t(index)] = {old.x, old.y, curve};
        readout_ = std::pair{pos, QStringLiteral("Bend ") + pythonSigned(curve, 2)};
    }
    setPoints(points, drag.gesture, drag.text);
    update();
}

void ModulatorGraph::endDrag() {
    drag_.reset();
    readout_.reset();
    update();
}

void ModulatorGraph::mouseReleaseEvent(QMouseEvent*) { endDrag(); }

void ModulatorGraph::mouseUngrabEvent() { endDrag(); }

void ModulatorGraph::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || random_)
        return;
    const auto found = hit(event->position());
    if (found.first.isEmpty() || (found.first == QLatin1String("point") && found.second == added_ &&
                                  addedClock_.isValid() && addedClock_.elapsed() < 500))
        return;
    endDrag();
    if (found.first == QLatin1String("point")) {
        removePoint(found.second);
    } else {
        std::vector<Point> points = points_;
        points[std::size_t(found.second)].curve = 0.0;
        setPoints(points, QString(), undoName(QStringLiteral("Straighten Curve")));
    }
}

void ModulatorGraph::hoverMoveEvent(QHoverEvent* event) {
    if (!drag_)
        hoverAt(event->position());
}

void ModulatorGraph::hoverAt(const QPointF& pos) {
    const auto found = hit(pos);
    if (found != hover_) {
        hover_ = found;
        update();
    }
    if (hintRect().contains(pos))
        setCursor(Qt::PointingHandCursor);
    else if (found.first == QLatin1String("point"))
        setCursor(Qt::SizeAllCursor);
    else if (!found.first.isEmpty())
        setCursor(Qt::SizeVerCursor);
    else
        setCursor(Qt::CrossCursor);
}

void ModulatorGraph::hoverLeaveEvent(QHoverEvent*) {
    if (!drag_) {
        hover_ = {QString(), -1};
        update();
    }
}

void ModulatorGraph::wheelEvent(QWheelEvent* event) {
    const auto found = hit(event->position());
    const double notches = event->angleDelta().y() / 120.0;
    if (found.first.isEmpty() || notches == 0.0) {
        event->ignore();
        return;
    }
    std::vector<Point> points = points_;
    const int index =
        found.first == QLatin1String("segment") ? found.second : std::min(found.second, int(points.size()) - 2);
    if (index < 0)
        return;
    const double fine = event->modifiers() & Qt::ShiftModifier ? 0.25 : 1.0;
    // Notches closer than 0.6 s are one gesture.
    if (wheelGesture_.isEmpty() || !wheelClock_.isValid() || wheelClock_.elapsed() > 600)
        wheelGesture_ = newGestureKey();
    wheelClock_.start();
    Point& a = points[std::size_t(index)];
    a.curve = std::clamp(a.curve + 0.08 * notches * fine, -1.0, 1.0);
    setPoints(points, wheelGesture_, undoName(QStringLiteral("Bend Curve")));
    event->accept();
}

bool ModulatorGraph::event(QEvent* event) {
    // Delete removes the selected point, not whatever the app has Delete for.
    if (event->type() == QEvent::ShortcutOverride && selected_ >= 0) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            event->accept();
            return true;
        }
    }
    return DeviceCanvas::event(event);
}

void ModulatorGraph::keyPressEvent(QKeyEvent* event) {
    if (!random_ && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selected_ >= 0) {
        removePoint(selected_);
        return;
    }
    event->ignore();
}

// --- Painting -------------------------------------------------------------------------------------

void ModulatorGraph::paint(SgPainter& p) {
    p.setAntialiasing(true);
    const QRectF rect(0, 0, width(), height());
    QLinearGradient background(rect.topLeft(), rect.bottomLeft());
    background.setColorAt(0, kBackgroundTop);
    background.setColorAt(1, kBackgroundBottom);
    p.fillRect(rect, background);
    const QRectF r = plot();
    const QFont small = uiFont(7);
    const QColor mod = Theme::modulation();

    // A trigger lights the frame up.
    if (flash_ > 0.02)
        p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3, withAlpha(mod, int(70 * flash_)), 1);

    // The grid: quarters across (sixteenths faint, as Ctrl snaps to them), with their times; eighths up.
    for (int i = 0; i <= 16; ++i) {
        const double x = r.left() + i / 16.0 * r.width();
        const bool major = i % 4 == 0;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()), major ? kGridMajor : kGridMinor);
        if (major && 0 < i && i < 16)
            p.drawText(QRectF(x - 26, r.bottom() + 1, 52, 12), Qt::AlignCenter, timeText(i / 16.0), kLabelColor, small);
    }
    for (int i = 0; i <= 8; ++i) {
        const double y = r.bottom() - i / 8.0 * r.height();
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y), i % 4 == 0 ? kGridMajor : kGridMinor);
    }

    // The LFO's output range (Depth, Offset), shaded behind the curve.
    if (periodic_ && (depth_ < 0.999 || std::abs(offset_) > 0.001)) {
        const double top = toScreen(0, played(1.0)).y(), bottom = toScreen(0, played(0.0)).y();
        p.fillRect(QRectF(r.left(), top, r.width(), std::max(1.0, bottom - top)), withAlpha(mod, 18));
    }

    // The curve: what it takes from the bottom up, a glow, the line.
    const int steps = std::max(2, int(r.width()));
    const std::vector<double> outputs = outputCurve(steps);
    std::vector<QPointF> curve, output;
    curve.reserve(std::size_t(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double x = double(i) / steps;
        curve.push_back(toScreen(x, curveValue(shown(), x)));
        output.push_back(toScreen(x, outputs[std::size_t(i)]));
    }
    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    fill.setColorAt(0, withAlpha(mod, 80));
    fill.setColorAt(1, withAlpha(mod, 8));
    p.fillToBaseline(curve.data(), int(curve.size()), r.bottom(), fill);
    p.drawPolyline(curve.data(), int(curve.size()), withAlpha(mod, 45), 6, Qt::RoundCap);
    p.drawPolyline(curve.data(), int(curve.size()), mod, 1.8, Qt::RoundCap);
    if (depth_ < 0.999 || std::abs(offset_) > 0.001 || phaseShift_ > 0.0005 || smoothMs_ > 0.01)  // as it plays it, when that isn't the curve
        drawDashedPolyline(p, output, QColor(255, 255, 255, 120), 1.0);
    const bool bending = !random_ && drag_ && drag_->kind == QLatin1String("bend");
    const bool hovering = !random_ && !drag_ && hover_.first == QLatin1String("segment");
    if (bending || hovering) {
        const int index = bending ? drag_->index : hover_.second;
        if (index >= 0 && index + 1 < int(points_.size())) {
            const Point a = points_[std::size_t(index)], b = points_[std::size_t(index) + 1];
            const int count = std::max(2, int((b.x - a.x) * r.width()));
            std::vector<QPointF> segment;
            for (int i = 0; i <= count; ++i) {
                const double x = a.x + (b.x - a.x) * i / count;
                segment.push_back(toScreen(x, i < count ? curveValue(points_, std::min(x, b.x - 1e-9)) : b.y));
            }
            p.drawPolyline(segment.data(), int(segment.size()), QColor(255, 255, 255, 200), 2.6, Qt::RoundCap);
        }
    }

    // The playhead: a short trail (not across a wrap or a retrigger), a softly glowing head.
    if (!trail_.empty()) {
        const QPointF head = toScreen(trail_.back().x(), trail_.back().y());
        for (std::size_t i = 1; i < trail_.size(); ++i) {
            if (trail_[i].x() < trail_[i - 1].x())
                continue;
            const double fade = double(i) / double(trail_.size());
            p.drawLine(toScreen(trail_[i - 1].x(), trail_[i - 1].y()), toScreen(trail_[i].x(), trail_[i].y()),
                       withAlpha(Qt::white, int(110 * fade * fade)), 1.0 + 1.2 * fade, Qt::RoundCap);
        }
        p.fillRadialGlow(head, 7, withAlpha(mod, 70), 3);
        p.fillEllipse(head, 3.2, 3.2, Qt::white);
    }

    // The points (Random's curve has none to drag).
    for (std::size_t i = 0; i < (random_ ? 0 : points_.size()); ++i) {
        const QPointF at = toScreen(points_[i].x, points_[i].y);
        const bool hovered = (hover_.first == QLatin1String("point") && hover_.second == int(i)) ||
                             (drag_ && drag_->index == int(i) && drag_->kind == QLatin1String("point"));
        const double radius = hovered ? kPointHoverRadius : kPointRadius;
        if (hovered)
            p.fillEllipse(at, radius + 4, radius + 4, withAlpha(mod, 70));
        const QRectF disc(at.x() - radius, at.y() - radius, 2 * radius, 2 * radius);
        p.fillEllipse(disc, hovered || int(i) == selected_ ? mod : QColor(0x14, 0x17, 0x1d));
        p.drawEllipse(disc, QColor(255, 255, 255, 230), 1.3);
    }

    // The key against the threshold (an Envelope a sidechain triggers), at the right.
    if (keyed_) {
        const QRectF key = meterRect();
        auto keyY = [&](double db) {
            return key.bottom() - (std::min(0.0, std::max(kMeterFloor, db)) - kMeterFloor) / -kMeterFloor * key.height();
        };
        p.fillRoundedRect(key, 2, 2, QColor(255, 255, 255, 14));
        const double top = keyY(level_);
        if (level_ > kMeterFloor)
            p.fillRoundedRect(QRectF(key.left(), top, key.width(), key.bottom() - top), 2, 2,
                              level_ >= threshold_ ? withAlpha(kKeyColor, 210) : QColor(255, 255, 255, 90));
        const double at = keyY(threshold_);
        p.drawLine(QPointF(key.left() - 2, at), QPointF(key.right() + 1, at), QColor(255, 255, 255, 220), 1.5);
    }

    // The labels: what it is, its length; the hint.
    p.drawText(QRectF(r.left() + 1, 1, 120, 13), Qt::AlignLeft | Qt::AlignVCenter,
               random_ ? (periodic_ ? QStringLiteral("random, each cycle") : QStringLiteral("random, each trigger"))
                       : (periodic_ ? QStringLiteral("cycle") : QStringLiteral("from each trigger")),
               kLabelColor, small);
    p.drawText(QRectF(r.right() - 170, 1, 168, 13), Qt::AlignRight | Qt::AlignVCenter, lengthText_, kLabelColor, small);
    if (!hint_.isEmpty()) {
        const QRectF box = hintRect();
        p.fillRoundedRect(box, 11, 11, QColor(10, 11, 14, 210));
        p.drawRoundedRect(box, 11, 11, QColor(255, 255, 255, 40), 1);
        p.drawText(box, Qt::AlignCenter, hint_, QColor(255, 255, 255, 200), uiFont(8));
    }

    // While dragging: where the point is, or the bend.
    if (readout_) {
        const auto& [point, text] = *readout_;
        const double w = SgPainter::textWidth(text, small) + 10;
        QRectF badge(point.x() + 10, point.y() - 22, w, 15);
        if (badge.right() > r.right())
            badge.moveRight(point.x() - 10);
        if (badge.top() < 0)
            badge.moveTop(point.y() + 8);
        p.fillRoundedRect(badge, 4, 4, QColor(10, 11, 14, 220));
        p.drawText(badge, Qt::AlignCenter, text, QColor(255, 255, 255, 220), small);
    }
}

}  // namespace sub::ui
