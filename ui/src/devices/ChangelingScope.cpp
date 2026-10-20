#include "devices/ChangelingScope.h"

#include "devices/EditorPaint.h"
#include "sg/SgPainter.h"
#include "theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sub::ui {

namespace {

constexpr const char* kStreamIds[ChangelingScope::kStreams] = {"pitch", "out", "note"};
constexpr double kLagSeconds = 0.03;
constexpr double kLagEaseSeconds = 0.16;
constexpr double kMaxLagSeconds = 0.1;
constexpr double kAxisSeconds = 0.35;  // the axis easing to what the history shows
constexpr double kMargin = 2.5;        // semitones above and below what it shows

const QString kNames[12] = {QStringLiteral("C"),  QStringLiteral("C#"), QStringLiteral("D"),  QStringLiteral("D#"),
                            QStringLiteral("E"),  QStringLiteral("F"),  QStringLiteral("F#"), QStringLiteral("G"),
                            QStringLiteral("G#"), QStringLiteral("A"),  QStringLiteral("A#"), QStringLiteral("B")};

qint64 floorDiv(qint64 a, qint64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

float clearValue(int stream) { return stream == ChangelingScope::Note ? -1.f : 0.f; }

float clean(int stream, float value) {
    if (std::isnan(value)) return clearValue(stream);
    if (stream == ChangelingScope::Note) return value < 0.f ? -1.f : std::clamp(value, 0.f, 127.f);
    return value <= 0.f ? 0.f : std::clamp(value, 1.f, 127.f);
}

QString noteName(int note) { return kNames[((note % 12) + 12) % 12] + QString::number(note / 12 - 1); }

// "+12 ¢", "−3 ¢", "0 ¢"
QString centsText(double cents) {
    const long rounded = std::lround(cents);
    if (rounded == 0) return QStringLiteral("0 ¢");
    return (rounded > 0 ? QStringLiteral("+%1 ¢") : QStringLiteral("−%1 ¢")).arg(std::labs(rounded));
}

}  // namespace

ChangelingScope::ChangelingScope(QQuickItem* parent) : DeviceCanvas(parent) {
    setImplicitSize(kMinimumWidth, kMinimumHeight);
    setAcceptedMouseButtons(Qt::NoButton);  // (clicks go on to the frame)
    ends_.fill(-1);
}

double ChangelingScope::yOf(double pitch) const {
    const double top = 3.0, bottom = height() - 3.0;
    const double t = (pitch - low_) / std::max(1.0, high_ - low_);
    return bottom - t * (bottom - top);
}

// --- The history ---------------------------------------------------------------------------

void ChangelingScope::fitRings(double valuesPerSecond) {
    rate_ = std::max(1.0, valuesPerSecond);
    const double values = (kHistorySeconds * 1.1 + kMaxLagSeconds) * rate_ + 64.0;
    qint64 capacity = 64;
    while (double(capacity) < values) capacity *= 2;
    if (capacity == capacity_) return;
    capacity_ = capacity;
    for (int s = 0; s < kStreams; ++s) rings_[std::size_t(s)].assign(std::size_t(capacity_), clearValue(s));
    started_ = false;
}

qint64 ChangelingScope::historyBegin() const {
    qint64 latest = newest_;
    for (const qint64 end : ends_) latest = std::max(latest, end);
    return std::max(begin_, latest - capacity_);
}

int ChangelingScope::bucketSize() const {
    const double px = width() / std::max(1.0, kHistorySeconds * rate_);
    return px >= 1.0 ? 1 : int(std::ceil(1.0 / px - 1e-9));
}

bool ChangelingScope::readStreams(bool& restarted) {
    std::array<std::pair<qint64, std::vector<float>>, kStreams> reads;
    bool came = false;
    for (int s = 0; s < kStreams; ++s) {
        reads[std::size_t(s)] = readDisplayAt(QString::fromLatin1(kStreamIds[s]));
        came = came || !reads[std::size_t(s)].second.empty();
    }
    if (!came) return false;
    restarted = !started_;
    for (int s = 0; s < kStreams; ++s) {
        const auto& [first, values] = reads[std::size_t(s)];
        if (!values.empty() && ends_[std::size_t(s)] >= 0 && first < ends_[std::size_t(s)]) restarted = true;
    }
    if (restarted) {
        for (int s = 0; s < kStreams; ++s)
            std::fill(rings_[std::size_t(s)].begin(), rings_[std::size_t(s)].end(), clearValue(s));
        ends_.fill(-1);
        begin_ = std::numeric_limits<qint64>::min();
        started_ = true;
    }
    const qint64 mask = capacity_ - 1;
    for (int s = 0; s < kStreams; ++s) {
        const auto& [first, values] = reads[std::size_t(s)];
        if (values.empty()) continue;
        const qint64 end = first + qint64(values.size());
        if (ends_[std::size_t(s)] < 0) begin_ = std::max(begin_, first);
        std::vector<float>& ring = rings_[std::size_t(s)];
        if (ends_[std::size_t(s)] >= 0) {
            for (qint64 i = std::max(ends_[std::size_t(s)], first - capacity_); i < first; ++i)
                ring[std::size_t(i & mask)] = clearValue(s);
        }
        for (qint64 i = std::max(first, end - capacity_); i < end; ++i)
            ring[std::size_t(i & mask)] = clean(s, values[std::size_t(i - first)]);
        ends_[std::size_t(s)] = end;
        const float latest = clean(s, values.back());
        if (s == Heard) heard_ = latest;
        if (s == Out) out_ = latest;
    }
    newest_ = std::numeric_limits<qint64>::max();
    for (const qint64 end : ends_)
        if (end >= 0) newest_ = std::min(newest_, end);
    newest_ = std::max(newest_, begin_);
    return true;
}

// The axis: what the shown history spans (heard and out), with a margin, kLeastSpan at the least, eased to.
void ChangelingScope::fitAxis(double dt) {
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    if (started_ && capacity_ > 0) {
        const qint64 mask = capacity_ - 1;
        const qint64 from = std::max(historyBegin(), qint64(std::floor(scroll_ - kHistorySeconds * rate_)));
        for (qint64 v = from; v < newest_; ++v)
            for (const int s : {int(Heard), int(Out)}) {
                const float value = rings_[std::size_t(s)][std::size_t(v & mask)];
                if (value > 0.f) {
                    lo = std::min(lo, double(value));
                    hi = std::max(hi, double(value));
                }
            }
    }
    if (!(hi >= lo)) return;  // nothing heard: stay
    double wantLow = lo - kMargin, wantHigh = hi + kMargin;
    if (wantHigh - wantLow < kLeastSpan) {
        const double middle = 0.5 * (wantLow + wantHigh);
        wantLow = middle - 0.5 * kLeastSpan;
        wantHigh = middle + 0.5 * kLeastSpan;
    }
    if (!axisSet_) {
        low_ = wantLow;
        high_ = wantHigh;
        axisSet_ = true;
        return;
    }
    const double k = easeFraction(dt, kAxisSeconds);
    low_ += k * (wantLow - low_);
    high_ += k * (wantHigh - high_);
}

void ChangelingScope::refreshDisplays() {
    const double dt = tickSeconds();
    fitRings(sampleRate() / kDisplaySamples);
    bool restarted = false;
    const bool came = readStreams(restarted);
    const double before = scroll_, lowBefore = low_, highBefore = high_;
    if (restarted) {
        scroll_ = std::max(double(historyBegin()), double(newest_) - kLagSeconds * rate_);
    } else if (started_) {
        scroll_ += rate_ * dt;
        const double lag = double(newest_) - scroll_;
        if (lag > kMaxLagSeconds * rate_)
            scroll_ = double(newest_) - kMaxLagSeconds * rate_;
        else if (lag < 0.0)
            scroll_ = double(newest_);
        else
            scroll_ += easeFraction(dt, kLagEaseSeconds) * (lag - kLagSeconds * rate_);
    }
    scroll_ = std::min(scroll_, double(newest_));
    fitAxis(dt);
    const bool wasVoiced = voiced_;
    silentFor_ = came && heard_ > 0.0 ? 0.0 : silentFor_ + dt;
    voiced_ = silentFor_ < kSilentSeconds;
    if (came || scroll_ != before || low_ != lowBefore || high_ != highBefore) update();
    if (came || voiced_ != wasVoiced) Q_EMIT heardChanged();
}

// --- Painting ------------------------------------------------------------------------------

void ChangelingScope::paint(SgPainter& p) {
    const double w = width(), h = height();
    if (w < 40.0 || h < 30.0) return;
    p.setAntialiasing(true);
    const QRectF outer = QRectF(0, 0, w, h).adjusted(0.5, 0.5, -0.5, -0.5);
    p.fillRoundedRect(outer, 4, 4, Theme::meterBg());
    p.drawRoundedRect(outer, 4, 4, withAlpha(Theme::gridBeat(), 150));
    p.save();
    p.setClipRect(QRectF(1, 1, w - 2, h - 2));

    // The grid: a line a semitone, the C's brighter and named.
    const QFont small = uiFont(7);
    const int first = int(std::ceil(low_)), last = int(std::floor(high_));
    const bool every = (h - 6.0) / std::max(1.0, high_ - low_) >= 4.0;  // room for a line each
    for (int n = first; n <= last; ++n) {
        const bool c = n % 12 == 0;
        if (!c && !every) continue;
        const double y = std::round(yOf(n)) + 0.5;
        p.drawLine(QPointF(1, y), QPointF(w - 1, y), withAlpha(Theme::gridBeat(), c ? 150 : 45));
        if (c) p.drawText(QRectF(4, y - 11, 30, 10), Qt::AlignLeft | Qt::AlignBottom, noteName(n), Theme::textDim(), small);
    }

    int count = 0;
    double x0 = 0.0, dx = 1.0;
    std::vector<float> heard, out, note;
    if (started_ && capacity_ > 0) {
        const int k = bucketSize();
        const double px = w / std::max(1.0, kHistorySeconds * rate_);
        qint64 from = floorDiv(qint64(std::floor(scroll_ - kHistorySeconds * rate_)), k) - 1;
        from = std::max(from, floorDiv(historyBegin() + k - 1, k));
        const qint64 to = std::min(floorDiv(newest_, k), floorDiv(qint64(std::ceil(scroll_)), k) + 1);
        count = int(std::max<qint64>(0, to - from));
        if (count >= 2) {
            dx = k * px;
            x0 = w - (scroll_ - double(from * k)) * px;
            heard.resize(std::size_t(count));
            out.resize(std::size_t(count));
            note.resize(std::size_t(count));
            const qint64 mask = capacity_ - 1;
            for (int i = 0; i < count; ++i) {
                // a bucket: its last voiced values (the lines are breaks where a bucket has none)
                float hv = 0.f, ov = 0.f, nv = -1.f;
                for (qint64 v = (from + i) * k; v < (from + i + 1) * k; ++v) {
                    const std::size_t slot = std::size_t(v & mask);
                    if (rings_[Heard][slot] > 0.f) hv = rings_[Heard][slot];
                    if (rings_[Out][slot] > 0.f) ov = rings_[Out][slot];
                    if (rings_[Note][slot] >= 0.f) nv = rings_[Note][slot];
                }
                heard[std::size_t(i)] = hv;
                out[std::size_t(i)] = ov;
                note[std::size_t(i)] = nv;
            }
        }
    }
    if (count >= 2) {
        // The note held (Quantize, Robot): a band a semitone wide.
        noteTops_.assign(std::size_t(count), 0.f);
        noteBottoms_.assign(std::size_t(count), 0.f);
        bool held = false;
        for (int i = 0; i < count; ++i) {
            if (note[std::size_t(i)] < 0.f) continue;
            held = true;
            noteTops_[std::size_t(i)] = float(yOf(note[std::size_t(i)] + 0.5));
            noteBottoms_[std::size_t(i)] = float(yOf(note[std::size_t(i)] - 0.5));
        }
        if (held) p.fillBand(x0, dx, noteTops_.data(), noteBottoms_.data(), count, withAlpha(Theme::accent(), 34));
        // The lines, broken where unvoiced.
        const auto drawRuns = [&](const std::vector<float>& values, std::vector<QPointF>& line, const QColor& color,
                                  double width, bool glow) {
            line.clear();
            const auto flush = [&] {
                if (line.size() >= 2) {
                    if (glow) drawGlowPolyline(p, line, color, width);
                    else p.drawPolyline(line.data(), int(line.size()), color, width);
                }
                line.clear();
            };
            for (int i = 0; i < count; ++i) {
                const float v = values[std::size_t(i)];
                if (v <= 0.f) {
                    flush();
                    continue;
                }
                line.emplace_back(x0 + (i + 0.5) * dx, yOf(v));
            }
            flush();
        };
        drawRuns(heard, heardLine_, withAlpha(Theme::textDim(), 170), 1.0, false);
        drawRuns(out, outLine_, Theme::accent(), 1.5, true);
    }
    p.restore();

    // The corner: the note out now and the cents off it, while a voice is heard.
    if (voiced_ && out_ > 0.0) {
        const int n = int(std::lround(out_));
        const QFont name = uiFont(9, true), figures = uiFont(7.5);
        const QRectF corner(w - 70, 4, 64, 13);
        p.drawText(corner, Qt::AlignRight | Qt::AlignVCenter, noteName(n), Theme::text(), name);
        p.drawText(QRectF(w - 70, 16, 64, 11), Qt::AlignRight | Qt::AlignVCenter, centsText(100.0 * (out_ - n)),
                   withAlpha(Theme::accent(), 230), figures);
    }
}

}  // namespace sub::ui
