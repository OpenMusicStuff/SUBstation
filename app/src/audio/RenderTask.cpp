#include "audio/RenderTask.h"

#include "RenderJob.h"

#include <exception>
#include <utility>

namespace sub::app {

RenderTask::RenderTask(const QString& path, QObject* parent) : QObject(parent), path_(path) {
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, &RenderTask::poll);
}

RenderTask::~RenderTask() = default;

void RenderTask::cancel() {
    if (cancelled()) return;
    cancelJob();
    Q_EMIT cancelledChanged();
}

void RenderTask::follow() { timer_.start(); }

void RenderTask::poll() {
    const double now = progress();
    if (now != reported_) {
        reported_ = now;
        Q_EMIT progressChanged(now);
    }
    if (done()) {
        timer_.stop();
        Q_EMIT ended();
    }
}

EngineRender::EngineRender(std::shared_ptr<sub::RenderJob> job, QObject* parent)
    : RenderTask(QString::fromStdString(job->path()), parent), job_(std::move(job)) {
    follow();
}

// Let go of unfinished, the engine's job cancels and finishes itself.
EngineRender::~EngineRender() = default;

double EngineRender::progress() const { return job_->progress(); }

bool EngineRender::done() const { return job_->done(); }

bool EngineRender::cancelled() const { return job_->cancelled(); }

void EngineRender::cancelJob() { job_->cancel(); }

std::optional<qint64> EngineRender::finish() {
    if (finished_) throw EditError(QStringLiteral("The render is finished already"));
    finished_ = true;
    try {
        const std::optional<int64_t> frames = job_->finish();
        if (!frames) return std::nullopt;
        return static_cast<qint64>(*frames);
    } catch (const std::exception& error) {
        throw EditError(QString::fromStdString(error.what()));
    }
}

FreezeRender::FreezeRender(const QStringList& trackIds, std::shared_ptr<sub::RenderJob> job, double tempo,
                           QObject* parent)
    : EngineRender(std::move(job), parent), trackIds_(trackIds), tempo_(tempo) {}

QStringList FreezeRender::paths() const {
    QStringList paths;
    for (const std::string& path : job().paths()) paths.append(QString::fromStdString(path));
    return paths;
}

QList<qint64> FreezeRender::written() const {
    QList<qint64> frames;
    for (const int64_t written : job().written()) frames.append(static_cast<qint64>(written));
    return frames;
}

}  // namespace sub::app
