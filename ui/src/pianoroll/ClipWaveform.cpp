#include "pianoroll/ClipWaveform.h"

#include "arrangement/WaveformCache.h"
#include "audio/EngineBridge.h"
#include "audio/Waveform.h"
#include "input/GestureKey.h"
#include "model/Numbers.h"
#include "model/Timebase.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QCursor>
#include <QFontMetrics>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sub::ui {

namespace {

constexpr std::array<double, 19> kTimeSteps{0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1,
                                            2,     5,     10,    15,   30,   60,   120, 300,  600};

// A source's waveform over `area`, its whole length fitted to the width, as
// the arrangement draws it (arrangement::waveformOutline), scaled by `gain`;
// both channels apart when `split` (and stereo).
void drawWaveform(SgPainter& p, const app::Waveform& source, const QRectF& area, const QColor& color, bool split,
                  double gain) {
    const int columns = static_cast<int>(std::ceil(area.width()));
    if (source.frames() <= 0 || columns <= 0) return;
    const double fpp = static_cast<double>(source.frames()) / std::max(1.0, area.width());
    const arrangement::WaveformOutline outline =
        arrangement::waveformOutline(source, fpp, -1, columns + 2, static_cast<int>(area.height()), split, gain);
    outline.draw(p, area.left() - 1.0, area.top(), color);
}

}  // namespace

ClipWaveform::ClipWaveform(QQuickItem* parent) : SgCanvas(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

void ClipWaveform::setSession(app::Session* session) {
    if (session == session_) return;
    if (session_) disconnect(session_->bridge(), nullptr, this, nullptr);
    session_ = session;
    if (session) {
        connect(session->bridge(), &app::EngineBridge::sourceReady, this, &ClipWaveform::refreshBands);
        connect(session->bridge(), &app::EngineBridge::sourceFailed, this, &ClipWaveform::refreshBands);
    }
    Q_EMIT sessionChanged();
    refreshBands();
}

void ClipWaveform::setController(ClipViewController* controller) {
    if (controller == controller_) return;
    if (controller_) disconnect(controller_, nullptr, this, nullptr);
    controller_ = controller;
    if (controller) connect(controller, &ClipViewController::changed, this, &ClipWaveform::refreshBands);
    Q_EMIT controllerChanged();
    refreshBands();
}

void ClipWaveform::refreshBands() {
    bands_.clear();
    if (controller_ && session_) {
        const app::EngineBridge* bridge = session_->bridge();
        for (const auto& [clip, color] : controller_->audioClips())
            bands_.push_back({clip, color, bridge->waveform(clip.path), bridge->loadError(clip.path)});
    }
    update();
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
    if (bands_.size() == 1) {
        const QRectF area(0, kRulerHeight, width(), height() - kRulerHeight);
        const double totalSec = drawBand(p, bands_.front(), area);
        if (totalSec > 0) drawRuler(p, totalSec);
        return;
    }
    const double bandHeight = std::max<double>(kMinBandHeight, height() / static_cast<double>(bands_.size()));
    const size_t shown = std::min(bands_.size(), static_cast<size_t>(std::max(1.0, std::floor(height() / bandHeight))));
    for (size_t i = 0; i < shown; ++i) {
        const QRectF band(0, static_cast<double>(i) * bandHeight, width(), bandHeight - 1);
        drawBand(p, bands_[i], band);
        drawLabel(p, band, bands_[i].clip.name);
        p.fillRect(QRectF(0, band.bottom(), width(), 1), Theme::border());
    }
    if (shown < bands_.size()) {
        p.drawText(rect.adjusted(0, 0, -8, -4), Qt::AlignRight | Qt::AlignBottom,
                   QStringLiteral("+%1 more").arg(bands_.size() - shown), Theme::text(), uiFont(8));
    }
}

double ClipWaveform::drawBand(SgPainter& p, const Band& band, const QRectF& area) const {
    const app::Clip& clip = band.clip;
    const QColor& color = band.color;
    const app::Waveform& source = band.source;
    if (source.isNull() || source.frames() <= 0) {
        p.drawText(area, Qt::AlignCenter,
                   band.loadError.isEmpty() ? QStringLiteral("Loading…") : QStringLiteral("Missing file"),
                   Theme::textDim(), uiFont());
        return 0.0;
    }
    const double totalSec = static_cast<double>(source.frames()) / source.sampleRate();
    const auto xAt = [&](double seconds) { return area.left() + seconds / totalSec * area.width(); };

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
    p.save();
    p.setClipRect(area);
    QColor opaque = color;
    opaque.setAlpha(255);
    drawWaveform(p, source, area, opaque, split, arrangement::quantizedGain(app::dbToGain(clip.gainDb)));
    p.restore();
    const QColor dim(0, 0, 0, 120);
    p.fillRect(QRectF(area.left(), area.top(), x0 - area.left(), area.height()), dim);
    p.fillRect(QRectF(x1, area.top(), area.right() - x1, area.height()), dim);

    // Start and end markers with flags, as in Ableton's sample editor.
    const QFont font = uiFont(7.5, true);
    for (const auto& [x, label] : {std::pair{x0, QStringLiteral("S")}, std::pair{x1, QStringLiteral("E")}}) {
        const bool end = label == u"E";
        p.fillRect(QRectF(app::roundHalfEven(x) - (end ? 1 : 0), area.top(), 1, area.height()), Theme::text());
        const QRectF flag(end ? x - 12 : x, area.top(), 12, 12);
        p.fillRect(flag, Theme::text());
        p.drawText(flag, Qt::AlignCenter, label, Theme::accentText(), font);
    }

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

double ClipWaveform::secAt(double x) const {
    return width() > 0 ? std::clamp(x / width(), 0.0, 1.0) * editableSec() : 0.0;
}

int ClipWaveform::markerAt(double x) const {
    const double totalSec = editableSec();
    if (totalSec <= 0) return -1;
    int best = -1;
    double nearest = kMarkerTab / 2.0 + 1;
    const auto& markers = bands_.front().clip.warpMarkers;
    for (size_t i = 0; i < markers.size(); ++i) {
        const double distance = std::abs(markers[i].sec / totalSec * width() - x);
        if (distance <= nearest) {
            nearest = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void ClipWaveform::mousePressEvent(QMouseEvent* event) {
    if (event->flags() & Qt::MouseEventCreatedDoubleClick) return;  // (the double-click stands for it)
    const int index = markerAt(event->position().x());
    if (event->button() != Qt::LeftButton || index < 0) {
        event->ignore();
        return;
    }
    drag_ = Drag{index, newGestureKey()};
}

void ClipWaveform::mouseMoveEvent(QMouseEvent* event) {
    if (drag_ && editableSec() > 0) controller_->moveWarpMarker(drag_->index, secAt(event->position().x()), drag_->key);
}

void ClipWaveform::mouseReleaseEvent(QMouseEvent*) { drag_.reset(); }

void ClipWaveform::mouseUngrabEvent() { drag_.reset(); }

void ClipWaveform::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || editableSec() <= 0) return;
    const double x = event->position().x();
    if (const int index = markerAt(x); index >= 0)
        controller_->removeWarpMarker(index);
    else
        controller_->addWarpMarker(secAt(x));
}

void ClipWaveform::hoverMoveEvent(QHoverEvent* event) {
    setCursor(markerAt(event->position().x()) >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
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

void ClipWaveform::drawRuler(SgPainter& p, double totalSec) const {
    p.fillRect(QRectF(0, 0, width(), kRulerHeight), Theme::panel());
    const double pxPerSec = width() / totalSec;
    double step = kTimeSteps.back();
    for (double s : kTimeSteps) {
        if (s * pxPerSec >= 70) {
            step = s;
            break;
        }
    }
    const QFont font = uiFont(7.5);
    const auto ticks = static_cast<long long>(totalSec / step);
    for (long long i = 0; i <= ticks; ++i) {
        const double seconds = static_cast<double>(i) * step;
        const double x = app::roundHalfEven(seconds * pxPerSec);
        p.fillRect(QRectF(x, kRulerHeight - 6, 1, 6), Theme::textDim());
        p.drawText(QPointF(x + 3, kRulerHeight - 7), formatTime(seconds, step), Theme::textDim(), font);
    }
    p.fillRect(QRectF(0, kRulerHeight - 1, width(), 1), Theme::border());  // the 1 px line at 19.5
}

}  // namespace sub::ui
