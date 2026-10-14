#include "arrangement/ArrangementRuler.h"

#include "editor/ProjectEditor.h"
#include "input/GestureKey.h"
#include "model/Numbers.h"
#include "model/Project.h"
#include "model/Timebase.h"
#include "session/Selection.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"
#include "timeline/Timeline.h"

#include <QCursor>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {

QColor mixed(const QColor& a, const QColor& b) {
    return QColor::fromRgbF(0.5f * (a.redF() + b.redF()), 0.5f * (a.greenF() + b.greenF()),
                            0.5f * (a.blueF() + b.blueF()));
}

}  // namespace

ArrangementRuler::ArrangementRuler(QQuickItem* parent) : ArrangementItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setImplicitHeight(kHeight);
}

void ArrangementRuler::connectSession(app::Session* session) {
    const auto repaint = [this] { this->repaint(); };
    connect(session->project(), &app::Project::settingsChanged, this, repaint);
    connect(session->project(), &app::Project::reset, this, repaint);
    connect(session->selection(), &app::Selection::insertChanged, this, repaint);
}

void ArrangementRuler::paint(SgPainter& p) {
    // As Ableton's: the bar numbers on top; under them a band with the grid's
    // lines, the loop brace along its top and the start marker at its foot.
    const QRectF rect = p.rect();
    const double h = height();
    const QColor ground = Theme::window();
    p.fillRect(rect, ground);
    p.fillRect(QRectF(rect.left(), 0, rect.width(), 1), ground.lighter(150));  // (its lit top edge)
    p.fillRect(QRectF(rect.left(), h - kBase, rect.width(), kBase), Theme::border());
    if (!ready()) return;
    const app::Project& project = *session()->project();
    const timeline::Timeline& view = arrangement()->view();
    const double bandTop = kNumbers, bandBottom = h - kBase;

    // The numbers: a bar's right of a tick down to the band; bars between them a short tick.
    // The band: every line of the grid, darker than the ground, as the lanes' below.
    const double step = view.gridStep();
    const double every = timeline::labelStep(view, step);
    const QFont font = uiFont(8);
    const QColor ink = mixed(Theme::text(), Theme::textDim());
    const app::TimeSignature ts = project.timeSignature();
    for (const timeline::GridLine& line : timeline::gridLines(view, rect.left() - 60, rect.right() + 1, step)) {
        const double x = app::roundHalfEven(line.x);
        p.fillRect(QRectF(x, bandTop, 1, bandBottom - bandTop), ground.darker(122));
        if (line.kind != timeline::LineKind::Bar) continue;
        if (std::abs(line.beat / every - std::round(line.beat / every)) < 1e-6) {
            p.fillRect(QRectF(x, bandTop - 8, 1, 8), ink);
            p.drawText(QPointF(x + 3, bandTop - 4), app::formatBarLabel(line.beat, ts), ink, font);
        } else {
            p.fillRect(QRectF(x, bandTop - 3, 1, 3), ink.darker(150));
        }
    }

    // The loop brace: lit edges (on down the band, and the lanes: ArrangementLanes),
    // and a hollow triangle inside each end, pointing in.
    const double lx0 = app::roundHalfEven(view.beatToX(project.loopStart()));
    const double lx1 = app::roundHalfEven(view.beatToX(project.loopEnd()));
    if (lx1 > rect.left() && lx0 < rect.right()) {
        const QColor fill = loopColor(project.loopEnabled());
        const QColor edge = loopEdgeColor(project.loopEnabled());
        const double top = kLoopTop, bottom = kLoopTop + kLoopStrip;
        p.fillRect(QRectF(lx0, top, lx1 - lx0, kLoopStrip), fill);
        p.fillRect(QRectF(lx0, top, lx1 - lx0, 1), fill.lighter(125));
        p.fillRect(QRectF(lx0, bottom - 1, lx1 - lx0, 1), edge);
        // (Its edges run on down the band, and the lanes, while the loop is on.)
        const double edgeBottom = project.loopEnabled() ? bandBottom : bottom;
        p.fillRect(QRectF(lx0, top, 1, edgeBottom - top), edge);
        p.fillRect(QRectF(lx1 - 1, top, 1, edgeBottom - top), edge);
        if (lx1 - lx0 > 20) {
            const double mid = top + kLoopStrip / 2.0, half = kLoopStrip / 2.0 - 2.0;
            p.save();
            p.setAntialiasing(true);
            p.drawPolyline(QPolygonF({QPointF(lx0 + 2.5, mid - half), QPointF(lx0 + 3.5 + half, mid),
                                      QPointF(lx0 + 2.5, mid + half)}),
                           edge, 1.0);
            p.drawPolyline(QPolygonF({QPointF(lx1 - 3.5, mid - half), QPointF(lx1 - 4.5 - half, mid),
                                      QPointF(lx1 - 3.5, mid + half)}),
                           edge, 1.0);
            p.restore();
        }
    }

    // The start marker (insert position): a triangle pointing down at the band's foot.
    const double sx = view.beatToX(session()->selection()->insertBeat());
    p.save();
    p.setAntialiasing(true);
    p.fillPolygon(QPolygonF({QPointF(sx - 4.5, bandBottom - 5), QPointF(sx + 4.5, bandBottom - 5),
                             QPointF(sx, bandBottom)}),
                  Theme::insertMarker());
    p.restore();
}

QColor ArrangementRuler::loopColor(bool enabled) { return enabled ? Theme::loopOn() : Theme::loopOff(); }

QColor ArrangementRuler::loopEdgeColor(bool enabled) { return loopColor(enabled).lighter(145); }

bool ArrangementRuler::inLoopStrip(double y) { return kLoopTop <= y && y < kLoopTop + kLoopStrip; }

ArrangementRuler::Zone ArrangementRuler::loopZone(double x) const {
    const app::Project& project = *session()->project();
    const timeline::Timeline& view = arrangement()->view();
    const double x0 = view.beatToX(project.loopStart()), x1 = view.beatToX(project.loopEnd());
    if (std::abs(x - x0) <= kEdgeGrab) return Zone::Start;
    if (std::abs(x - x1) <= kEdgeGrab) return Zone::End;
    if (x0 < x && x < x1) return Zone::Body;
    return Zone::None;
}

void ArrangementRuler::mousePressEvent(QMouseEvent* event) {
    if (event->flags() & Qt::MouseEventCreatedDoubleClick) return;  // (the double-click stands for it)
    if (event->button() != Qt::LeftButton || !ready()) return;
    const QPointF pos = event->position();
    const double beat = arrangement()->view().xToBeat(pos.x());
    const app::Project& project = *session()->project();
    Drag drag;
    drag.beat = beat;
    if (inLoopStrip(pos.y())) {
        switch (loopZone(pos.x())) {
            case Zone::Start: drag.mode = Mode::LoopStart; break;
            case Zone::End: drag.mode = Mode::LoopEnd; break;
            case Zone::Body: drag.mode = Mode::LoopBody; break;
            case Zone::None: drag.mode = Mode::LoopNew; break;
        }
        drag.start = project.loopStart();
        drag.end = project.loopEnd();
        drag.key = newGestureKey();
    } else {
        drag.mode = Mode::Scrub;
        drag.x = pos.x();
        drag.y = drag.lastY = pos.y();
        drag.scroll = arrangement()->scrollBeats();
    }
    drag_ = drag;
}

void ArrangementRuler::mouseMoveEvent(QMouseEvent* event) {
    if (!drag_ || !ready()) return;
    Drag& d = *drag_;
    const QPointF pos = event->position();
    const bool bypass = event->modifiers() & Qt::AltModifier;
    Arrangement& a = *arrangement();
    const timeline::Timeline& view = a.view();
    const double beat = view.xToBeat(pos.x());
    const auto snap = [&](double b) { return view.snapBeat(b, bypass); };
    app::ProjectEditor& editor = *session()->editor();
    const bool enabled = session()->project()->loopEnabled();
    switch (d.mode) {
        case Mode::LoopStart: editor.setLoop(enabled, std::min(snap(beat), d.end - 0.25), d.end, d.key); break;
        case Mode::LoopEnd: editor.setLoop(enabled, d.start, std::max(snap(beat), d.start + 0.25), d.key); break;
        case Mode::LoopBody: {
            const double start = std::max(0.0, snap(d.start + beat - d.beat));
            editor.setLoop(enabled, start, start + d.end - d.start, d.key);
            break;
        }
        case Mode::LoopNew: {
            const double a0 = snap(d.beat), b0 = snap(beat);
            const double lo = std::min(a0, b0), hi = std::max(a0, b0);
            if (hi - lo >= 0.25) editor.setLoop(true, lo, hi, d.key);
            break;
        }
        case Mode::Scrub: {
            const double dx = pos.x() - d.x, dy = pos.y() - d.y;
            if (!d.moved && std::max(std::abs(dx), std::abs(dy)) > 3) {
                d.moved = true;
                d.pan = std::abs(dx) > std::abs(dy);
            }
            if (d.moved) {
                if (d.pan) {
                    a.scrollByHand(d.scroll - dx / a.pxPerBeat());
                } else {
                    a.zoomAt(d.x, std::pow(1.012, pos.y() - d.lastY));
                    d.lastY = pos.y();
                }
            }
            break;
        }
    }
}

void ArrangementRuler::mouseReleaseEvent(QMouseEvent* event) {
    const std::optional<Drag> d = drag_;
    drag_.reset();
    if (d && d->mode == Mode::Scrub && !d->moved && ready()) {
        const bool bypass = event->modifiers() & Qt::AltModifier;
        session()->locate(std::max(0.0, arrangement()->view().snapBeat(d->beat, bypass)));
    }
}

void ArrangementRuler::mouseUngrabEvent() { drag_.reset(); }

void ArrangementRuler::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!ready() || event->button() != Qt::LeftButton) return;
    if (inLoopStrip(event->position().y()) && loopZone(event->position().x()) != Zone::None)
        session()->editor()->setLoopEnabled(!session()->project()->loopEnabled());
}

void ArrangementRuler::hoverMoveEvent(QHoverEvent* event) { updateCursor(event->position()); }

void ArrangementRuler::updateCursor(const QPointF& pos) {
    if (!ready()) return;
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (inLoopStrip(pos.y())) {
        switch (loopZone(pos.x())) {
            case Zone::Start:
            case Zone::End: shape = Qt::SizeHorCursor; break;
            case Zone::Body: shape = Qt::OpenHandCursor; break;
            case Zone::None: break;
        }
    }
    setCursor(shape);
}

}  // namespace sub::ui
