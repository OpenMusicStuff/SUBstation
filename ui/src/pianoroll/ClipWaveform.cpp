#include "pianoroll/ClipWaveform.h"

#include "arrangement/WaveformCache.h"
#include "audio/EngineBridge.h"
#include "audio/Waveform.h"
#include "input/GestureKey.h"
#include "input/Modifiers.h"
#include "model/Numbers.h"
#include "model/Project.h"
#include "model/Timebase.h"
#include "session/Selection.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QFontMetrics>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace sub::ui {

namespace {

constexpr std::array<double, 19> kTimeSteps{0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1,
                                            2,     5,     10,    15,   30,   60,   120, 300,  600};

// A source's waveform over `placed`'s area, the part of it shown across the
// width, as the arrangement draws it (arrangement::waveformOutline), scaled by
// `gain`; both channels apart when `split` (and stereo).
void drawWaveform(SgPainter& p, const app::Waveform& source, const ClipWaveform::Placed& placed, const QColor& color,
                  bool split, double gain) {
    const QRectF& area = placed.area;
    const int columns = static_cast<int>(std::ceil(area.width()));
    if (source.frames() <= 0 || columns <= 0 || placed.spanSec <= 0) return;
    const double rate = source.sampleRate();
    const double fpp = placed.spanSec * rate / std::max(1.0, area.width());
    // Columns are counted from the file's first frame; the first shown starts part of one in.
    const double startColumn = placed.fromSec * rate / fpp;
    const auto first = static_cast<qint64>(std::floor(startColumn)) - 1;
    const arrangement::WaveformOutline outline =
        arrangement::waveformOutline(source, fpp, first, columns + 3, static_cast<int>(area.height()), split, gain);
    outline.draw(p, area.left() + (static_cast<double>(first) - startColumn), area.top(), color);
}

}  // namespace

ClipWaveform::ClipWaveform(QQuickItem* parent) : SgCanvas(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

// --- Zooming and scrolling -----------------------------------------------------------------------

double ClipWaveform::minSpan() const {
    const qint64 frames = bands_.empty() || bands_.front().source.isNull() ? 0 : bands_.front().source.frames();
    if (frames <= 0) return 1e-3;
    return std::clamp(std::max(1.0, width()) / kMaxPixelsPerFrame / static_cast<double>(frames), 1e-9, 1.0);
}

void ClipWaveform::setView(double from, double to) {
    double span = std::clamp(to - from, minSpan(), 1.0);
    from = std::clamp(from, 0.0, 1.0 - span);
    to = from + span;
    if (span >= 1.0 - 1e-12) {
        from = 0.0;
        to = 1.0;
    }
    if (from == viewFrom_ && to == viewTo_) return;
    viewFrom_ = from;
    viewTo_ = to;
    Q_EMIT viewChanged();
    Q_EMIT playheadChanged();  // (where it is drawn)
    update();
}

void ClipWaveform::zoomAt(double x, double factor) {
    if (factor <= 0 || width() <= 0) return;
    const double span = viewTo_ - viewFrom_;
    const double at = viewFrom_ + std::clamp(x / width(), 0.0, 1.0) * span;  // stays under the mouse
    const double newSpan = std::clamp(span / factor, minSpan(), 1.0);
    const double from = at - (at - viewFrom_) * newSpan / span;
    setView(from, from + newSpan);
}

void ClipWaveform::scrollTo(double from) { setView(from, from + (viewTo_ - viewFrom_)); }

void ClipWaveform::zoomToFit() { setView(0.0, 1.0); }

void ClipWaveform::setSession(app::Session* session) {
    if (session == session_) return;
    if (session_) {
        disconnect(session_->bridge(), nullptr, this, nullptr);
        disconnect(session_->selection(), nullptr, this, nullptr);
        disconnect(session_->project(), nullptr, this, nullptr);
    }
    session_ = session;
    if (session) {
        app::EngineBridge* bridge = session->bridge();
        connect(bridge, &app::EngineBridge::sourceReady, this, &ClipWaveform::refreshBands);
        connect(bridge, &app::EngineBridge::sourceFailed, this, &ClipWaveform::refreshBands);
        connect(bridge, &app::EngineBridge::positionChanged, this, &ClipWaveform::onPosition);
        connect(bridge, &app::EngineBridge::transportChanged, this, &ClipWaveform::onPosition);
        connect(session->selection(), &app::Selection::insertChanged, this, &ClipWaveform::onInsert);
        // (An unwarped clip's seconds follow the tempo.)
        connect(session->project(), &app::Project::settingsChanged, this, [this] {
            onPosition();
            onInsert();
        });
    }
    Q_EMIT sessionChanged();
    refreshBands();
}

void ClipWaveform::setController(ClipViewController* controller) {
    if (controller == controller_) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller) {
        connect(controller, &ClipViewController::changed, this, &ClipWaveform::refreshBands);
        connect(controller, &ClipViewController::opened, this, &ClipWaveform::zoomToFit);  // (other clips)
    }
    Q_EMIT controllerChanged();
    refreshBands();
}

void ClipWaveform::refreshBands() {
    bands_.clear();
    if (controller_ && session_) {
        const app::EngineBridge* bridge = session_->bridge();
        for (const auto& [clip, color] : controller_->audioClips())
            bands_.push_back({clip, color, bridge->waveform(clip.path), bridge->loadError(clip.path), {}});
        if (controller_->warpMarkable() && bands_.size() == 1)
            bands_.front().transients = controller_->transients(bands_.front().clip.path);
    }
    onPosition();
    onInsert();
    update();
}

std::optional<double> ClipWaveform::secAtBeat(const app::Clip& clip, double beat, bool endIncluded) const {
    if (!session_) return std::nullopt;
    const app::Tempo& tempo = session_->project()->tempoMap();
    const double end = clip.endBeat(tempo);
    if (beat < clip.startBeat - 1e-9 || beat > end + 1e-9 || (!endIncluded && beat >= end - 1e-9)) return std::nullopt;
    return clip.offsetSec + clip.beatsToSource(beat - clip.startBeat, tempo);
}

void ClipWaveform::onPosition() {
    std::vector<std::optional<double>> secs(bands_.size());
    const app::EngineBridge* bridge = session_ ? session_->bridge() : nullptr;
    if (bridge && bridge->isPlaying()) {
        const double beat = bridge->position();
        for (size_t i = 0; i < bands_.size(); ++i) secs[i] = secAtBeat(bands_[i].clip, beat, false);
    }
    if (secs == playheadSecs_) return;
    playheadSecs_ = std::move(secs);
    Q_EMIT playheadChanged();
}

void ClipWaveform::onInsert() {
    std::vector<std::optional<double>> secs(bands_.size());
    if (session_) {
        const double beat = session_->selection()->insertBeat();
        for (size_t i = 0; i < bands_.size(); ++i) secs[i] = secAtBeat(bands_[i].clip, beat, true);
    }
    if (secs == startSecs_) return;
    startSecs_ = std::move(secs);
    update();
}

std::vector<ClipWaveform::Placed> ClipWaveform::placed() const {
    std::vector<Placed> result;
    const auto lengthOf = [](const Band& band) {
        return band.source.isNull() || band.source.frames() <= 0
                   ? 0.0
                   : static_cast<double>(band.source.frames()) / band.source.sampleRate();
    };
    const auto add = [&](const QRectF& area, const Band& band) {
        const double total = lengthOf(band);
        result.push_back({area, total, viewFrom_ * total, (viewTo_ - viewFrom_) * total});
    };
    if (bands_.size() == 1) {
        add(QRectF(0, kRulerHeight, width(), height() - kRulerHeight), bands_.front());
        return result;
    }
    const double bandHeight = std::max<double>(kMinBandHeight, height() / static_cast<double>(bands_.size()));
    const size_t shown = std::min(bands_.size(), static_cast<size_t>(std::max(1.0, std::floor(height() / bandHeight))));
    for (size_t i = 0; i < shown; ++i)
        add(QRectF(0, static_cast<double>(i) * bandHeight, width(), bandHeight - 1), bands_[i]);
    return result;
}

QString ClipWaveform::formatTime(double seconds, double step) {
    const double minutes = std::floor(seconds / 60.0);
    const double secs = seconds - minutes * 60.0;
    const int decimals = step >= 1 ? 0 : std::min(3, static_cast<int>(std::ceil(-std::log10(step) - 1e-9)));
    QString text = app::formatFixed(secs, decimals);
    if (!minutes) return text;
    const int width = decimals ? decimals + 3 : 2;
    if (text.size() < width) text.prepend(QString(width - text.size(), u'0'));
    return QStringLiteral("%1:%2").arg(static_cast<long long>(minutes)).arg(text);
}

void ClipWaveform::paint(SgPainter& p) {
    const QRectF rect = p.rect();
    p.fillRect(rect, Theme::lane());
    if (bands_.empty()) return;
    const std::vector<Placed> areas = placed();
    if (bands_.size() == 1) {
        const double totalSec = drawBand(p, 0, areas.front());
        if (totalSec > 0) drawRuler(p, areas.front());
        return;
    }
    const size_t shown = areas.size();
    for (size_t i = 0; i < shown; ++i) {
        const QRectF& band = areas[i].area;
        drawBand(p, i, areas[i]);
        drawLabel(p, band, bands_[i].clip.name);
        p.fillRect(QRectF(0, band.bottom(), width(), 1), Theme::border());
    }
    if (shown < bands_.size()) {
        p.drawText(rect.adjusted(0, 0, -8, -4), Qt::AlignRight | Qt::AlignBottom,
                   QStringLiteral("+%1 more").arg(bands_.size() - shown), Theme::text(), uiFont(8));
    }
}

double ClipWaveform::drawBand(SgPainter& p, size_t index, const Placed& placed) const {
    const Band& band = bands_[index];
    const QRectF& area = placed.area;
    const app::Clip& clip = band.clip;
    const QColor& color = band.color;
    const app::Waveform& source = band.source;
    if (source.isNull() || source.frames() <= 0) {
        p.drawText(area, Qt::AlignCenter,
                   band.loadError.isEmpty() ? QStringLiteral("Loading…") : QStringLiteral("Missing file"),
                   Theme::textDim(), uiFont());
        return 0.0;
    }
    const double totalSec = placed.totalSec;
    const auto xAt = [&](double seconds) { return ClipWaveform::xAt(placed, seconds); };
    p.save();
    p.setClipRect(area);  // (zoomed in, what is scrolled away)

    // Clip region, waveform, then dim what the clip doesn't play.
    const double x0 = xAt(clip.offsetSec), x1 = xAt(clip.offsetSec + clip.durationSec);
    QColor region = color;
    region.setAlpha(45);
    p.fillRect(QRectF(x0, area.top(), x1 - x0, area.height()), region);
    const bool split = area.height() >= 60;
    std::vector<double> mids;
    if (split)
        mids = {area.top() + area.height() / 4, area.top() + area.height() * 3 / 4};
    else
        mids = {area.center().y()};
    for (double mid : mids) p.drawLine(QPointF(area.left(), mid), QPointF(area.right(), mid), Theme::gridBeat());
    QColor opaque = color;
    opaque.setAlpha(255);
    drawWaveform(p, source, placed, opaque, split, arrangement::quantizedGain(app::dbToGain(clip.gainDb)));
    const QColor dim(0, 0, 0, 120);
    if (x0 > area.left()) p.fillRect(QRectF(area.left(), area.top(), x0 - area.left(), area.height()), dim);
    if (x1 < area.right()) p.fillRect(QRectF(x1, area.top(), area.right() - x1, area.height()), dim);

    // Start and end markers with flags, as in Ableton's sample editor.
    const QFont font = uiFont(7.5, true);
    for (const auto& [x, label] : {std::pair{x0, QStringLiteral("S")}, std::pair{x1, QStringLiteral("E")}}) {
        const bool end = label == u"E";
        p.fillRect(QRectF(app::roundHalfEven(x) - (end ? 1 : 0), area.top(), 1, area.height()), Theme::text());
        const QRectF flag(end ? x - 12 : x, area.top(), 12, 12);
        p.fillRect(flag, Theme::text());
        p.drawText(flag, Qt::AlignCenter, label, Theme::accentText(), font);
    }

    // Transients: ticks along the top.
    QColor tick = Theme::text();
    tick.setAlpha(110);
    for (double sec : band.transients) p.fillRect(QRectF(app::roundHalfEven(xAt(sec)), area.top(), 1, 5), tick);

    // Warp markers: a line through the waveform under a tab.
    if (clip.isWarped()) {
        QColor line = Theme::accent();
        line.setAlpha(170);
        for (const app::WarpMarker& m : clip.warpMarkers) {
            const double x = app::roundHalfEven(xAt(m.sec));
            p.fillRect(QRectF(x, area.top(), 1, area.height()), line);
            p.fillRect(QRectF(x - kMarkerTab / 2.0, area.top(), kMarkerTab, kMarkerTab), Theme::accent());
        }
    }

    // The start marker, where it is in the part the clip plays.
    if (index < startSecs_.size() && startSecs_[index])
        p.fillRect(QRectF(app::roundHalfEven(xAt(*startSecs_[index])), area.top(), 1, area.height()),
                   Theme::insertMarker());
    p.restore();
    return totalSec;
}

// --- Editing warp markers ----------------------------------------------------------------------

double ClipWaveform::editableSec() const {
    if (!controller_ || bands_.size() != 1) return 0.0;
    const Band& band = bands_.front();
    const app::Clip& clip = band.clip;
    if (!clip.isWarped() || (clip.warpMarkers.empty() && clip.sourceMap) || band.source.isNull() ||
        band.source.frames() <= 0)
        return 0.0;
    return static_cast<double>(band.source.frames()) / band.source.sampleRate();
}

double ClipWaveform::editableSecAt(double x) const {
    const double totalSec = editableSec();
    if (totalSec <= 0 || width() <= 0) return 0.0;
    return std::clamp(secAt(placed().front(), x), 0.0, totalSec);
}

int ClipWaveform::markerAt(double x) const {
    if (editableSec() <= 0) return -1;
    const Placed lead = placed().front();
    int best = -1;
    double nearest = kMarkerTab / 2.0 + 1;
    const auto& markers = bands_.front().clip.warpMarkers;
    for (size_t i = 0; i < markers.size(); ++i) {
        const double distance = std::abs(xAt(lead, markers[i].sec) - x);
        if (distance <= nearest) {
            nearest = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

double ClipWaveform::snappedSecAt(double x) const {
    double best = editableSecAt(x);
    if (editableSec() <= 0) return best;
    const Placed lead = placed().front();
    double nearest = kMarkerTab / 2.0 + 1;
    for (double sec : bands_.front().transients) {
        const double distance = std::abs(xAt(lead, sec) - x);
        if (distance <= nearest) {
            nearest = distance;
            best = sec;
        }
    }
    return best;
}

void ClipWaveform::mousePressEvent(QMouseEvent* event) {
    if (event->flags() & Qt::MouseEventCreatedDoubleClick) return;  // (the double-click stands for it)
    if (event->button() != Qt::LeftButton || bands_.empty()) {
        event->ignore();
        return;
    }
    const QPointF pos = event->position();
    const bool onRuler = rulerShown() && pos.y() < kRulerHeight;
    if (isPanModifier(event->modifiers()) || onRuler) {
        viewDrag_ = ViewDrag{pos, pos.y(), viewFrom_, onRuler && !isPanModifier(event->modifiers())};
        if (!viewDrag_->ruler) setCursor(Qt::ClosedHandCursor);
        return;
    }
    const int index = markerAt(pos.x());
    if (index < 0) {
        event->ignore();
        return;
    }
    drag_ = Drag{index, newGestureKey()};
}

void ClipWaveform::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (viewDrag_) {
        ViewDrag& d = *viewDrag_;
        const double dx = pos.x() - d.press.x(), dy = pos.y() - d.press.y();
        if (!d.moved && std::max(std::abs(dx), std::abs(dy)) > 3) {
            d.moved = true;
            d.zoom = d.ruler && std::abs(dy) > std::abs(dx);  // (as the piano roll's ruler decides)
        }
        if (d.zoom) {
            zoomAt(d.press.x(), std::pow(1.012, pos.y() - d.lastY));  // down: in
            d.lastY = pos.y();
        } else if (d.moved && width() > 0) {
            scrollTo(d.from - dx / width() * (viewTo_ - viewFrom_));
        }
        return;
    }
    if (drag_ && editableSec() > 0) controller_->moveWarpMarker(drag_->index, editableSecAt(pos.x()), drag_->key);
}

void ClipWaveform::mouseReleaseEvent(QMouseEvent*) {
    const std::optional<ViewDrag> view = std::exchange(viewDrag_, std::nullopt);
    drag_.reset();
    if (view && !view->ruler) setCursor(Qt::ArrowCursor);
    if (!view || !view->ruler || view->moved || !session_ || bands_.empty()) return;
    // A click on the ruler: play from the moment clicked (held to the part the clip plays).
    const Placed lead = placed().front();
    if (lead.totalSec <= 0 || lead.area.width() <= 0) return;
    const app::Clip& clip = bands_.front().clip;
    const double sec = std::clamp(secAt(lead, view->press.x()), 0.0, lead.totalSec);
    const double into = std::clamp(sec - clip.offsetSec, 0.0, clip.durationSec);
    Q_EMIT locateRequested(clip.startBeat + clip.sourceToBeats(into, session_->project()->tempoMap()));
}

void ClipWaveform::mouseUngrabEvent() {
    if (viewDrag_ && !viewDrag_->ruler) setCursor(Qt::ArrowCursor);
    viewDrag_.reset();
    drag_.reset();
}

void ClipWaveform::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    if (bands_.empty() || delta.isNull() || width() <= 0) {
        event->ignore();
        return;
    }
    // (Qt may report a wheel turned with a modifier held as horizontal: either axis counts.)
    const double notches = (delta.y() ? delta.y() : delta.x()) / 120.0;
    if (event->modifiers() & Qt::ControlModifier) {
        zoomAt(event->position().x(), std::pow(kWheelZoom, notches));
    } else {
        // Shift, a horizontal wheel or a plain one: sideways (there is nothing to scroll up and down).
        const double pixels = -(delta.x() ? delta.x() : delta.y()) / 120.0 * kWheelScroll;
        scrollTo(viewFrom_ + pixels / width() * (viewTo_ - viewFrom_));
    }
    event->accept();
}

void ClipWaveform::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || editableSec() <= 0) return;
    const double x = event->position().x();
    if (rulerShown() && event->position().y() < kRulerHeight) return;
    if (const int index = markerAt(x); index >= 0)
        controller_->removeWarpMarker(index);
    else
        controller_->addWarpMarker(snappedSecAt(x));
}

void ClipWaveform::hoverMoveEvent(QHoverEvent* event) {
    if (viewDrag_) return;
    const bool onRuler = rulerShown() && event->position().y() < kRulerHeight;
    if (isPanModifier(event->modifiers()))
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(!onRuler && markerAt(event->position().x()) >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void ClipWaveform::drawLabel(SgPainter& p, const QRectF& band, const QString& name) const {
    const QFont font = uiFont(8);
    const QFontMetrics metrics(font);
    const QString text = metrics.elidedText(name, Qt::ElideRight, static_cast<int>(band.width() / 2));
    const QRectF box(band.left() + 4, band.bottom() - metrics.height() - 5, metrics.horizontalAdvance(text) + 8,
                     metrics.height() + 2);
    p.fillRect(box, QColor(0, 0, 0, 150));
    p.drawText(box, Qt::AlignCenter, text, Theme::text(), font);
}

void ClipWaveform::drawRuler(SgPainter& p, const Placed& placed) const {
    p.fillRect(QRectF(0, 0, width(), kRulerHeight), Theme::panel());
    const double pxPerSec = placed.area.width() / placed.spanSec;
    double step = kTimeSteps.back();
    for (double s : kTimeSteps) {
        if (s * pxPerSec >= 70) {
            step = s;
            break;
        }
    }
    const QFont font = uiFont(7.5);
    const auto firstTick = static_cast<long long>(std::floor(placed.fromSec / step));
    const auto lastTick = static_cast<long long>(std::min(placed.totalSec, placed.fromSec + placed.spanSec) / step);
    for (long long i = std::max(0LL, firstTick); i <= lastTick; ++i) {
        const double seconds = static_cast<double>(i) * step;
        const double x = app::roundHalfEven(xAt(placed, seconds));
        p.fillRect(QRectF(x, kRulerHeight - 6, 1, 6), Theme::textDim());
        p.drawText(QPointF(x + 3, kRulerHeight - 7), formatTime(seconds, step), Theme::textDim(), font);
    }
    p.fillRect(QRectF(0, kRulerHeight - 1, width(), 1), Theme::border());  // the 1 px line at 19.5
    // The start marker: a triangle at the top, as the arrangement's ruler has it.
    if (!startSecs_.empty() && startSecs_.front()) {
        const double sx = app::roundHalfEven(xAt(placed, *startSecs_.front()));
        p.fillPolygon(QPolygonF({QPointF(sx - 5, 1), QPointF(sx + 5, 1), QPointF(sx, 8)}), Theme::insertMarker());
    }
}

}  // namespace sub::ui
