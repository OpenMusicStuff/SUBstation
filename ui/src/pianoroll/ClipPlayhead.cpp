#include "pianoroll/ClipPlayhead.h"

#include <algorithm>

#include "model/Numbers.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QPolygonF>

namespace sub::ui {

ClipPlayhead::ClipPlayhead(QQuickItem* parent) : SgCanvas(parent) {}

void ClipPlayhead::setWaveform(ClipWaveform* waveform) {
    if (waveform == waveform_) return;
    if (waveform_) disconnect(waveform_, nullptr, this, nullptr);
    waveform_ = waveform;
    if (waveform) connect(waveform, &ClipWaveform::playheadChanged, this, &QQuickItem::update);
    Q_EMIT waveformChanged();
    update();
}

void ClipPlayhead::paint(SgPainter& p) {
    if (!waveform_) return;
    const std::vector<ClipWaveform::Placed> areas = waveform_->placed();
    const auto& secs = waveform_->playheadSecs();
    for (size_t i = 0; i < areas.size() && i < secs.size(); ++i) {
        const ClipWaveform::Placed& placed = areas[i];
        if (!secs[i] || placed.totalSec <= 0 || placed.spanSec <= 0) continue;
        const double x = app::roundHalfEven(ClipWaveform::xAt(placed, *secs[i]));
        if (x < placed.area.left() - 1 || x > placed.area.right() + 1) continue;  // (scrolled away)
        // (Several clips' bands scroll under the ruler: what of each is below it.)
        const double top = std::max(placed.area.top(), waveform_->rulerShown() ? double(ClipWaveform::kRulerHeight) : 0.0);
        if (placed.area.bottom() > top) p.fillRect(QRectF(x, top, 1, placed.area.bottom() - top), Theme::playhead());
        if (waveform_->rulerShown()) {
            const double foot = ClipWaveform::kRulerHeight;
            p.fillPolygon(QPolygonF({QPointF(x - 5, foot - 8), QPointF(x + 6, foot - 8), QPointF(x + 0.5, foot - 1)}),
                          Theme::playhead());
        }
    }
}

}  // namespace sub::ui
