#include "devices/ModulatorValues.h"

#include "audio/EngineBridge.h"
#include "devices/DisplayClock.h"
#include "session/Session.h"

#include <QCoreApplication>

#include <algorithm>
#include <vector>

namespace sub::ui {

namespace {

constexpr int kKeep = 64;  // of a backlog (read after a while): the newest values only

}  // namespace

namespace {

QPointer<ModulatorValues>& theInstance() {
    static QPointer<ModulatorValues> values;
    return values;
}

}  // namespace

ModulatorValues* ModulatorValues::instance() {
    if (!theInstance())
        theInstance() = new ModulatorValues();  // (the application's child: it goes with it)
    return theInstance();
}

void ModulatorValues::release(const QString& modulatorId) {
    if (theInstance())
        theInstance()->unwatch(modulatorId);
}

ModulatorValues::ModulatorValues() : QObject(QCoreApplication::instance()) {}

void ModulatorValues::watch(sub::app::Session* session, const QString& trackId, const QString& modulatorId) {
    if (session == nullptr || modulatorId.isEmpty())
        return;
    Watched& watched = watched_[modulatorId];
    if (watched.session != session || watched.trackId != trackId) {  // (a modulator moved to another track)
        watched.session = session;
        watched.trackId = trackId;
        watched.processor = 0;
    }
    ++watched.count;
    if (!ticking_) {
        clock_.start();
        ticking_ = connect(DisplayClock::instance(), &DisplayClock::tick, this, [this] {
            tick(DisplayClock::instance()->elapsed(clock_));
        });
    }
}

void ModulatorValues::unwatch(const QString& modulatorId) {
    const auto found = watched_.find(modulatorId);
    if (found == watched_.end())
        return;
    if (--found->count <= 0)
        watched_.erase(found);
    if (watched_.isEmpty() && ticking_) {
        disconnect(ticking_);
        ticking_ = {};
    }
}

std::optional<double> ModulatorValues::value(const QString& modulatorId) const {
    const auto found = watched_.constFind(modulatorId);
    if (found == watched_.constEnd() || found->playback.empty())
        return std::nullopt;
    return std::clamp(found->playback.value(0), 0.0, 1.0);
}

void ModulatorValues::read(const QString& modulatorId, Watched& watched) {
    sub::app::EngineBridge* bridge = watched.session ? watched.session->bridge() : nullptr;
    const auto processor = bridge ? bridge->engineDeviceId(watched.trackId, modulatorId) : std::nullopt;
    if (!processor) {
        watched.processor = 0;
        return;
    }
    if (*processor != watched.processor) {  // (made again: its displays start from 0)
        watched.processor = *processor;
        watched.position = 0;
        watched.display = -1;
        watched.playback.clear();
        const QList<sub::app::ProcessorDisplay> displays = bridge->processorDisplays(watched.trackId, modulatorId);
        for (int i = 0; i < displays.size(); ++i) {
            if (displays[i].id == QLatin1String("value")) {
                watched.display = i;
                watched.samplesPerValue = std::max(1, displays[i].samplesPerValue);
            }
        }
    }
    if (watched.display < 0)
        return;
    std::vector<float> values;
    watched.position = bridge->readProcessorDisplay(watched.trackId, modulatorId, watched.display, watched.position, values);
    const size_t from = values.size() > size_t(kKeep) ? values.size() - size_t(kKeep) : 0;
    for (size_t i = from; i < values.size(); ++i)
        watched.playback.append({values[i]});
    watched.playback.endBatch(int(values.size() - from));
}

void ModulatorValues::tick(double seconds) {
    for (auto it = watched_.begin(); it != watched_.end(); ++it) {
        read(it.key(), *it);
        const double rate = it->session ? it->session->bridge()->sampleRate() : 48000.0;
        it->playback.advance(seconds, rate / it->samplesPerValue);
    }
    if (!watched_.isEmpty())
        Q_EMIT ticked();
}

}  // namespace sub::ui
