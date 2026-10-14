// Loading plug-ins without holding up the window.
//
// In sandboxes (the plug-in hosting preference: docs/engine/sandboxes.md), a
// plug-in loads in its sandbox's process while the window goes on: it is in its
// chain at once, and the engine says when it has loaded (or couldn't, or its
// sandbox ended). The bridge counts those still loading (sandboxLoading).
//
// In this process, a project's plug-ins load after it opens: the project shows
// (and plays, without them) at once, and its plug-ins load one at a time, each
// in a turn of the event loop of its own, a little apart, so the window goes on
// between them. They load on the UI thread all the same: plug-ins must be
// created and set up there (VST3 says so, and JUCE plug-ins take the thread that
// creates them for their message thread), and some hang when loaded on another.
// What takes long is each plug-in, not the project around it. Devices whose
// plug-in waits (in the order they load) are in the engine with no processor, as
// a plug-in that isn't installed is (none in their chains), until their turn:
// they are then new to their chain (wherever they are by then) and get their
// processor as a device added does.
//
// Either way pluginsLoading() reports how far they got, and renders wait for them.

#include "audio/BridgePrivate.h"

#include "model/Project.h"

#include <algorithm>

namespace sub::app {

void EngineBridge::deferPlugin(const QString& deviceId) {
    if (!d_->pendingPlugins.contains(deviceId)) d_->pendingPlugins.append(deviceId);
    ++d_->pluginsTotal;
}

void EngineBridge::startLoadingPlugins() {
    if (pluginsPending() == 0) return;
    Q_EMIT pluginsLoading(d_->pluginsTotal - pluginsPending(), d_->pluginsTotal);
    Q_EMIT pluginsPendingChanged();
    if (!d_->pendingPlugins.isEmpty()) d_->pluginTimer.start(0);
}

void EngineBridge::stopLoadingPlugins() {
    d_->pluginTimer.stop();
    const bool had = pluginsPending() > 0;
    d_->pendingPlugins.clear();
    d_->sandboxLoading.clear();
    d_->pluginsTotal = 0;
    if (had) Q_EMIT pluginsPendingChanged();
}

bool EngineBridge::pluginPending(const QString& deviceId) const {
    return d_->pendingPlugins.contains(deviceId) || d_->sandboxLoading.contains(deviceId);
}

int EngineBridge::pluginsPending() const {
    return static_cast<int>(d_->pendingPlugins.size() + d_->sandboxLoading.size());
}

void EngineBridge::noteSandboxLoading(const QString& deviceId, quint32 processorId) {
    if (engine_.processorInfo(processorId).status != sub::PluginStatus::Loading) return;
    if (d_->sandboxLoading.contains(deviceId)) return;
    d_->sandboxLoading.insert(deviceId);
    ++d_->pluginsTotal;
    if (!d_->deferring) reportPlugins();  // (a project opening says so once its tracks are there)
}

void EngineBridge::sandboxLoaded(const QString& deviceId) {
    if (d_->sandboxLoading.remove(deviceId)) reportPlugins();
}

void EngineBridge::prioritizePlugins(const QString& trackId) {
    QStringList first;
    QStringList rest;
    for (const QString& deviceId : d_->pendingPlugins) {
        const bool on = d_->chainOwner.value(d_->where.value(deviceId)) == trackId;
        (on ? first : rest).append(deviceId);
    }
    if (!first.isEmpty()) d_->pendingPlugins = first + rest;
    // Those loading in sandboxes: first in theirs.
    std::vector<uint32_t> loading;
    for (const QString& deviceId : d_->sandboxLoading) {
        if (d_->chainOwner.value(d_->where.value(deviceId)) != trackId) continue;
        if (const std::optional<quint32> processorId = d_->pids.value(deviceId)) loading.push_back(*processorId);
    }
    if (!loading.empty()) engine_.prioritizeProcessors(loading);
}

void EngineBridge::loadPluginNow(const QString& deviceId) {
    if (d_->sandboxLoading.contains(deviceId)) {  // (it loads in its sandbox: next there)
        if (const std::optional<quint32> processorId = d_->pids.value(deviceId)) engine_.prioritizeProcessors({*processorId});
        return;
    }
    if (!d_->pendingPlugins.removeOne(deviceId)) return;
    loadDeferred(deviceId);
    reportPlugins();
}

void EngineBridge::loadPendingPlugins() {
    const QStringList pending = d_->pendingPlugins;
    for (const QString& deviceId : pending) {
        if (d_->pendingPlugins.removeOne(deviceId)) loadDeferred(deviceId);
    }
    reportPlugins();
}

// The timer's: the next plug-in that waits loads (one a turn).
void EngineBridge::loadNextPlugin() {
    if (d_->busy || !d_->syncing.isEmpty()) {  // (inside a plug-in's message loop, or a chain being synced)
        d_->pluginTimer.start(kPluginRetryMs);
        return;
    }
    while (!d_->pendingPlugins.isEmpty()) {
        const QString deviceId = d_->pendingPlugins.takeFirst();
        if (loadDeferred(deviceId)) break;
    }
    reportPlugins();
    if (!d_->pendingPlugins.isEmpty()) d_->pluginTimer.start(kPluginGapMs);
}

void EngineBridge::reportPlugins() {
    if (!d_->pluginsTotal) return;
    if (const int pending = pluginsPending(); pending > 0) {
        Q_EMIT pluginsLoading(d_->pluginsTotal - pending, d_->pluginsTotal);
        Q_EMIT pluginsPendingChanged();
    } else {
        stopLoadingPlugins();
        Q_EMIT pluginsLoading(0, 0);  // all done
        Q_EMIT pluginsPendingChanged();
    }
}

// A waiting plug-in gets its processor where its device is now. False if there
// was nothing to load: it went (or came back loaded: undo).
bool EngineBridge::loadDeferred(const QString& deviceId) {
    Private& d = *d_;
    const auto key = d.where.constFind(deviceId);
    if (key == d.where.constEnd() || !d.pids.contains(deviceId) || d.pids.value(deviceId)) return false;
    const QString chainKey = *key;
    const auto owner = d.chainOwner.constFind(chainKey);
    if (owner == d.chainOwner.constEnd() || !d.chains.contains(*owner)) return false;
    const QString trackId = *owner;
    if (d.syncing.contains(trackId)) {  // (its chain is being synced: it waits a little longer)
        if (!d.pendingPlugins.contains(deviceId)) d.pendingPlugins.append(deviceId);
        return false;
    }
    // New to its chain: the sync gives it its processor, in its place, with its automation and sidechain.
    std::vector<ChainEntry>& entries = d.devices[chainKey];
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const ChainEntry& e) { return e.first == deviceId; }),
                  entries.end());
    d.where.remove(deviceId);
    d.pids.remove(deviceId);
    syncDevices(trackId);
    return true;
}

}  // namespace sub::app
