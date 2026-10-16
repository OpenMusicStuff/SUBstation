#include "devices/VitalScope.h"

#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <QLinearGradient>

#include <algorithm>
#include <cmath>

namespace sub::ui {

namespace {
constexpr double kStaleSeconds = 0.15;  // without samples this long: the sound stopped
constexpr double kFadeSeconds = 0.12;
constexpr double kLow = 20.0, kHigh = 20000.0;
constexpr float kQuiet = 1e-5f;  // -100 dBFS: silence, not a signal
}  // namespace

VitalScope::VitalScope(QQuickItem* parent) : VitalGraph(parent) { setImplicitSize(240, 100); }

void VitalScope::setMode(const QString& mode) {
    if (mode_ == mode)
        return;
    mode_ = mode;
    Q_EMIT modeChanged();
    update();
}

void VitalScope::refreshDisplays() {
    const double seconds = tickSeconds();
    bool moved = false;

    const std::vector<float> voices = readDisplay(QStringLiteral("voices"));
    if (!voices.empty() && int(voices.back()) != voices_) {
        voices_ = int(voices.back());
        Q_EMIT voicesChanged();
    }

    const std::vector<float> samples = readDisplay(QStringLiteral("output"));
    float loudest = 0.0f;
    for (const float v : samples) loudest = std::max(loudest, std::abs(v));
    if (!samples.empty()) {
        // (Only the newest kKept matter: a backlog's past isn't drawn.)
        const std::size_t take = std::min<std::size_t>(samples.size(), kKept);
        kept_.insert(kept_.end(), samples.end() - std::ptrdiff_t(take), samples.end());
        if (kept_.size() > std::size_t(kKept))
            kept_.erase(kept_.begin(), kept_.end() - kKept);
    }
    if (loudest > kQuiet) {
        stale_ = 0.0;
        level_.target = 1.0;
        if (mode_ == QLatin1String("spectrum")) {
            if (spectrum_.add(samples.data(), samples.size(), sampleRate(), seconds))
                columns_ = spectrum_.columns(std::max(2, int(plot().width())), kLow, kHigh);
        } else {
            updateShown();
        }
        moved = true;
    } else if ((stale_ += seconds) > kStaleSeconds) {
        level_.target = 0.0;
        if (mode_ == QLatin1String("spectrum") && !columns_.empty()) {
            // Falling back, until nothing shows.
            if (spectrum_.add(nullptr, 0, sampleRate(), seconds)) {
                columns_ = spectrum_.columns(std::max(2, int(plot().width())), kLow, kHigh);
                moved = std::any_of(columns_.begin(), columns_.end(), [](double v) { return v > -90.0; });
            }
        }
    }
    moved |= level_.step(easeFraction(seconds, kFadeSeconds), 1e-3);
    if (moved)
        update();
}

void VitalScope::updateShown() {
    if (kept_.size() < std::size_t(kWindow)) {
        shown_.assign(kept_.begin(), kept_.end());
        return;
    }
    // The latest rising zero crossing with a whole window after it, else the newest window.
    std::size_t start = kept_.size() - kWindow;
    for (std::size_t i = kept_.size() - kWindow; i > 0; --i) {
        if (kept_[i - 1] < 0.0f && kept_[i] >= 0.0f) {
            start = i;
            break;
        }
    }
    shown_.assign(kept_.begin() + std::ptrdiff_t(start), kept_.begin() + std::ptrdiff_t(start) + kWindow);
}

void VitalScope::paint(SgPainter& p) {
    const bool badge = mode_ == QLatin1String("badge");
    const QRectF r = plot();
    const QColor color = Theme::accent();
    if (!badge) {
        paintBackground(p);
        for (int i = 1; i < 4; ++i) {
            const double y = r.top() + i / 4.0 * r.height();
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y), i == 2 ? gridMajor() : gridMinor());
        }
    }

    if (mode_ == QLatin1String("spectrum")) {
        drawDecadeGrid(p, r, LogAxis{kLow, kHigh, r.left(), r.width()});
        if (!columns_.empty()) {
            std::vector<QPointF> line;
            const double step = r.width() / double(std::max<std::size_t>(1, columns_.size() - 1));
            for (std::size_t i = 0; i < columns_.size(); ++i) {
                const double level = std::clamp((columns_[i] + 90.0) / 90.0, 0.0, 1.0);
                line.emplace_back(r.left() + double(i) * step, r.bottom() - level * r.height());
            }
            QLinearGradient fill(r.topLeft(), r.bottomLeft());
            fill.setColorAt(0, withAlpha(color, 90));
            fill.setColorAt(1, withAlpha(color, 6));
            p.setAntialiasing(true);
            p.fillToBaseline(line.data(), int(line.size()), r.bottom(), fill);
            drawGlowPolyline(p, line, color, 1.4);
        }
    } else if (!shown_.empty() && level() > 0.0) {
        // The scope: the samples across, scaled so the loudest nearly fills the height (but quiet ones stay small).
        float peak = 0.0f;
        for (const float v : shown_) peak = std::max(peak, std::abs(v));
        const double gain = level() * 0.9 / std::max(0.25f, peak);
        const double middle = r.center().y(), half = r.height() / 2;
        std::vector<QPointF> line;
        const int columns = std::max(2, int(r.width()));
        line.reserve(std::size_t(columns) + 1);
        for (int c = 0; c <= columns; ++c) {
            const std::size_t i = std::min(shown_.size() - 1, std::size_t(double(c) / columns * double(shown_.size() - 1)));
            line.emplace_back(r.left() + c * r.width() / columns, middle - std::clamp(shown_[i] * gain, -1.0, 1.0) * half);
        }
        p.setAntialiasing(true);
        if (badge) {
            p.drawPolyline(line.data(), int(line.size()), withAlpha(color, 70), 1.2);
        } else {
            drawGlowPolyline(p, line, color, 1.5);
        }
    }

    if (!badge && voices_ > 0) {
        p.drawText(QRectF(r.left() + 5, r.top() + 1, r.width() - 10, 12), Qt::AlignRight | Qt::AlignVCenter,
                   voices_ == 1 ? QStringLiteral("1 voice") : QStringLiteral("%1 voices").arg(voices_), labelColor(),
                   labelFont());
    }
}

}  // namespace sub::ui
