#pragma once
// What each track and device costs the audio thread (View › CPU Usage): a
// list model of the project's tracks (in arrangement order, then the returns
// and the master), each followed by its devices, with how much of one CPU
// core each took over the last updates. While `active` (its window open) the
// engine times every track and device it renders live (EngineBridge::
// setMeasuringCpu); closed, nothing is measured and it costs nothing.
//
// A track's time includes its devices' and its clips' (a group's: what it adds
// to its tracks, not theirs). A rack's is what its devices add up to; those
// follow it, named after their chain ("Kick › Sampler"). A plug-in in a
// sandbox counts what the audio thread waits for it. A device that rests in
// silence (Renderer::rests) costs nothing. Each value is smoothed over a few
// updates; `peak` holds the highest lately, decaying. With `sortByCpu` the
// tracks come costliest first, each with its devices costliest first (rows
// moved, not reset, so a list scrolled stays put).

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QTimer>

#include <vector>

namespace sub::app {

class EngineBridge;
class Project;
struct Device;

class CpuUsage : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool sortByCpu READ sortByCpu WRITE setSortByCpu NOTIFY sortByCpuChanged)
    Q_PROPERTY(double total READ total NOTIFY updated)  // every track's, in % of a core

public:
    enum Role { RowIdRole = Qt::UserRole + 1, TrackIdRole, NameRole, ColorRole, DepthRole, IsTrackRole, CpuRole, PeakRole };
    static constexpr int kUpdateMs = 500;

    CpuUsage(Project* project, EngineBridge* bridge, QObject* parent = nullptr);
    ~CpuUsage() override;

    bool active() const { return active_; }
    void setActive(bool active);
    bool sortByCpu() const { return sortByCpu_; }
    void setSortByCpu(bool sort);
    double total() const { return total_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // One update (the timer's): reads what the engine measured since the last.
    Q_INVOKABLE void update();

Q_SIGNALS:
    void activeChanged();
    void sortByCpuChanged();
    void updated();

private:
    struct Row {
        QString id;       // the track's or the device's
        QString trackId;  // the track it is on
        QString name;
        QString color;
        int depth = 0;
        bool track = false;
        double cpu = 0.0;   // % of a core
        double peak = 0.0;
    };
    // A track's rows: its own, then its devices'.
    struct Group {
        Row track;
        std::vector<std::vector<Row>> devices;  // each top-level device with what is in it (a rack's)
    };
    double cpuOf(const QString& id) const { return cpu_.value(id); }
    double peakOf(const QString& id) const { return peak_.value(id); }
    // A device's rows (a rack's devices after it), and its time (a rack's: theirs).
    double deviceRows(const Device& device, const QString& trackId, int depth, const QString& prefix,
                      std::vector<Row>& out) const;
    std::vector<Row> buildRows() const;

    Project* project_;
    EngineBridge* bridge_;
    QTimer timer_;
    bool active_ = false;
    bool sortByCpu_ = false;
    double total_ = 0.0;
    QHash<QString, double> cpu_, peak_;  // track or device id -> smoothed %, its peak
    std::vector<Row> rows_;
};

}  // namespace sub::app
