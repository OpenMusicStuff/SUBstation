#include "session/CpuUsage.h"

#include <algorithm>
#include <climits>

#include "audio/EngineBridge.h"
#include "model/Automation.h"  // kMaster
#include "model/Devices.h"
#include "model/Project.h"

namespace sub::app {
namespace {

constexpr double kSmoothing = 0.4;   // of each new reading in the shown value
constexpr double kPeakDecay = 0.85;  // per update
constexpr double kSortStep = 0.5;    // % of a core: costs within one sort as equal

}  // namespace

CpuUsage::CpuUsage(Project* project, EngineBridge* bridge, QObject* parent)
    : QAbstractListModel(parent), project_(project), bridge_(bridge) {
    timer_.setInterval(kUpdateMs);
    connect(&timer_, &QTimer::timeout, this, &CpuUsage::update);
}

CpuUsage::~CpuUsage() {
    if (active_) bridge_->setMeasuringCpu(false);
}

void CpuUsage::setActive(bool active) {
    if (active == active_) return;
    active_ = active;
    bridge_->setMeasuringCpu(active);
    cpu_.clear();
    peak_.clear();
    total_ = 0.0;
    if (active) {
        bridge_->takeCpu();  // (starts the measurement: the first reading has no time gone by)
        timer_.start();
        update();
    } else {
        timer_.stop();
    }
    Q_EMIT activeChanged();
}

void CpuUsage::setSortByCpu(bool sort) {
    if (sort == sortByCpu_) return;
    sortByCpu_ = sort;
    Q_EMIT sortByCpuChanged();
    update();
}

void CpuUsage::update() {
    if (active_) {
        const EngineBridge::CpuTimes times = bridge_->takeCpu();
        if (times.wallNs > 0) {
            const double wall = static_cast<double>(times.wallNs);
            const auto take = [&](const QHash<QString, quint64>& measured) {
                for (auto it = measured.constBegin(); it != measured.constEnd(); ++it) {
                    const double now = 100.0 * static_cast<double>(it.value()) / wall;
                    const auto shown = cpu_.constFind(it.key());
                    cpu_.insert(it.key(), shown == cpu_.constEnd() ? now : *shown + kSmoothing * (now - *shown));
                    peak_.insert(it.key(), std::max(now, peak_.value(it.key()) * kPeakDecay));
                }
            };
            take(times.tracks);
            take(times.devices);
            // Those not measured now (unloaded, loading, gone): read as nothing, so they fade out.
            for (auto it = cpu_.begin(); it != cpu_.end(); ++it) {
                if (times.tracks.contains(it.key()) || times.devices.contains(it.key())) continue;
                it.value() *= 1.0 - kSmoothing;
                peak_[it.key()] *= kPeakDecay;
            }
        }
    }
    std::vector<Row> rows = buildRows();
    total_ = 0.0;
    for (const Row& row : rows) {
        if (row.track) total_ += row.cpu;
    }
    const auto alike = [](const Row& a, const Row& b) {
        return a.id == b.id && a.name == b.name && a.color == b.color && a.depth == b.depth;
    };
    // The same rows, in another order (costliest first): moved, so a list
    // scrolled stays where it is. Other rows: a reset.
    bool sameRows = rows.size() == rows_.size();
    if (sameRows) {
        QHash<QString, const Row*> before;
        for (const Row& row : rows_) before.insert(row.id, &row);
        for (const Row& row : rows) {
            const Row* old = before.value(row.id);
            if (!old || !alike(*old, row)) {
                sameRows = false;
                break;
            }
        }
    }
    if (sameRows) {
        for (size_t i = 0; i < rows.size(); ++i) {
            size_t from = i;
            while (from < rows_.size() && rows_[from].id != rows[i].id) ++from;
            if (from == i || from >= rows_.size()) continue;
            beginMoveRows({}, static_cast<int>(from), static_cast<int>(from), {}, static_cast<int>(i));
            std::rotate(rows_.begin() + static_cast<std::ptrdiff_t>(i), rows_.begin() + static_cast<std::ptrdiff_t>(from),
                        rows_.begin() + static_cast<std::ptrdiff_t>(from) + 1);
            endMoveRows();
        }
        rows_ = std::move(rows);
        if (!rows_.empty()) Q_EMIT dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1), {CpuRole, PeakRole});
    } else {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
    }
    Q_EMIT updated();
}

double CpuUsage::deviceRows(const Device& device, const QString& trackId, int depth, const QString& prefix,
                            std::vector<Row>& out) const {
    Row row;
    row.id = device.id;
    row.trackId = trackId;
    row.name = prefix + deviceName(device);
    row.depth = depth;
    const size_t at = out.size();
    out.push_back(row);
    double cpu = cpuOf(device.id), peak = peakOf(device.id);
    if (device.isRack()) {
        cpu = peak = 0.0;
        for (const Chain& chain : device.chains) {
            const QString inner = chain.name.isEmpty() ? QString() : chain.name + QStringLiteral(" › ");
            for (const Device& d : chain.devices) {
                const size_t first = out.size();
                cpu += deviceRows(d, trackId, depth + 1, inner, out);
                peak += out[first].peak;
            }
        }
    }
    out[at].cpu = cpu;
    out[at].peak = peak;
    return cpu;
}

std::vector<CpuUsage::Row> CpuUsage::buildRows() const {
    std::vector<Group> groups;
    const auto add = [&](const Track& track, const QString& id, int depth) {
        Group group;
        group.track.id = id;
        group.track.trackId = id;
        group.track.name = id == kMaster ? tr("Main") : track.name;
        group.track.color = track.color;
        group.track.depth = depth;
        group.track.track = true;
        group.track.cpu = cpuOf(id);
        group.track.peak = peakOf(id);
        for (const Device& device : track.devices) {
            std::vector<Row> rows;
            deviceRows(device, id, depth + 1, {}, rows);
            group.devices.push_back(std::move(rows));
        }
        groups.push_back(std::move(group));
    };
    for (const Track& track : project_->tracks()) {
        int depth = 0;  // the groups it is in
        for (const Track* t = &track; t && t->parent && depth < 64; t = project_->findTrack(*t->parent)) ++depth;
        add(track, track.id, depth);
    }
    for (const Track& track : project_->returns()) add(track, track.id, 0);
    add(project_->master(), kMaster, 0);

    if (sortByCpu_) {
        // Costliest first, by steps of kSortStep: within a step the order stays
        // as it was (rows of nearly the same cost don't swap at every update).
        QHash<QString, int> was;
        for (size_t i = 0; i < rows_.size(); ++i) was.insert(rows_[i].id, static_cast<int>(i));
        const auto costlier = [&was](const Row& a, const Row& b) {
            const auto step = [](double cpu) { return static_cast<long>(cpu / kSortStep); };
            if (step(a.cpu) != step(b.cpu)) return step(a.cpu) > step(b.cpu);
            return was.value(a.id, INT_MAX) < was.value(b.id, INT_MAX);
        };
        std::stable_sort(groups.begin(), groups.end(),
                         [&](const Group& a, const Group& b) { return costlier(a.track, b.track); });
        for (Group& group : groups) {
            std::stable_sort(group.devices.begin(), group.devices.end(),
                             [&](const std::vector<Row>& a, const std::vector<Row>& b) { return costlier(a.front(), b.front()); });
            // A flat list: depths from the track, not from its groups.
            const int shift = group.track.depth;
            group.track.depth = 0;
            for (auto& device : group.devices) {
                for (Row& row : device) row.depth -= shift;
            }
        }
    }
    std::vector<Row> rows;
    for (const Group& group : groups) {
        rows.push_back(group.track);
        for (const auto& device : group.devices) rows.insert(rows.end(), device.begin(), device.end());
    }
    return rows;
}

int CpuUsage::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant CpuUsage::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const Row& row = rows_[static_cast<size_t>(index.row())];
    switch (role) {
    case RowIdRole: return row.id;
    case TrackIdRole: return row.trackId;
    case NameRole: return row.name;
    case ColorRole: return row.color;
    case DepthRole: return row.depth;
    case IsTrackRole: return row.track;
    case CpuRole: return row.cpu;
    case PeakRole: return row.peak;
    default: return {};
    }
}

QHash<int, QByteArray> CpuUsage::roleNames() const {
    return {{RowIdRole, "rowId"}, {TrackIdRole, "trackId"}, {NameRole, "name"}, {ColorRole, "color"},
            {DepthRole, "depth"}, {IsTrackRole, "isTrack"}, {CpuRole, "cpu"},   {PeakRole, "peak"}};
}

}  // namespace sub::app
