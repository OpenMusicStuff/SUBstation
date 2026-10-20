#include "devices/DeviceInfo.h"

#include "audio/EngineBridge.h"
#include "controls/Automation.h"
#include "editor/ProjectEditor.h"
#include "model/Device.h"
#include "model/Devices.h"
#include "model/Project.h"
#include "session/DeviceSelection.h"

#include <QVariantMap>

#include <algorithm>
#include <tuple>

namespace sub::ui {

using sub::app::Device;
using sub::app::EngineBridge;
using sub::app::Project;
using sub::app::Sidechain;
using sub::app::Track;

namespace {

QString latencyLine(int latency) { return QStringLiteral("Latency: %1 samples (compensated)").arg(latency); }

QString constrainedLine() {
    return QStringLiteral("Bypassed while monitoring: Constrain Delay Compensation is on");
}

// Devices' names in a chain, those of the same name numbered ("EQ (2)").
QStringList numberedNames(const std::vector<Device>& devices) {
    QStringList names;
    for (const Device& device : devices)
        names << sub::app::deviceName(device);
    QStringList numbered;
    for (int i = 0; i < int(names.size()); ++i) {
        numbered << (names.count(names[i]) > 1
                         ? QStringLiteral("%1 (%2)").arg(names[i]).arg(names.mid(0, i + 1).count(names[i]))
                         : names[i]);
    }
    return numbered;
}

using TapChoice = DeviceInfo::TapChoice;
using PadTaps = std::pair<QString, std::vector<TapChoice>>;

std::vector<TapChoice> padTaps(const sub::app::Chain& pad, const QString& name, bool rackNames,
                               std::vector<PadTaps>& pads);

// "After <device>" for each device of a chain along the signal: a rack's
// chains' devices ("Rack › Device", "Rack › Chain › Device" with several
// chains) before the rack itself. Not the track's (or a pad's) instrument:
// after it is Pre FX. A Drum Rack's pads have taps of their own, onto `pads`,
// not among these: named `padPath` and the pad's name ("Rack › Pad" with
// `rackNames`, for several Drum Racks), each before the pads of Drum Racks in it.
void addTaps(const std::vector<Device>& devices, const QString& path, bool own, std::vector<TapChoice>& choices,
             std::vector<PadTaps>& pads, const QString& padPath, bool rackNames) {
    const QStringList names = numberedNames(devices);
    for (int i = 0; i < int(devices.size()); ++i) {
        const Device& device = devices[size_t(i)];
        for (const sub::app::Chain& chain : device.chains) {
            const QString inner = path + names[i] + QStringLiteral(" › ") +
                                  (device.chains.size() > 1 ? chain.name + QStringLiteral(" › ") : QString());
            if (!device.isDrumRack()) {
                addTaps(chain.devices, inner, false, choices, pads, padPath, rackNames);
                continue;
            }
            const QString rackPath = rackNames ? path + names[i] + QStringLiteral(" › ") : QString();
            const QString name = padPath + rackPath + chain.name;
            const size_t at = pads.size();
            pads.emplace_back(name, std::vector<TapChoice>{});
            std::vector<TapChoice> taps = padTaps(chain, name, rackNames, pads);
            pads[at].second = std::move(taps);
        }
        if (!(own && i == 0 && sub::app::deviceIsInstrument(device)))
            choices.push_back({QStringLiteral("After ") + path + names[i], device.id, QString()});
    }
}

// A pad's own taps, along its signal: Pre FX (after its instrument), after each of its effects, Post FX, Post Mixer.
std::vector<TapChoice> padTaps(const sub::app::Chain& pad, const QString& name, bool rackNames,
                               std::vector<PadTaps>& pads) {
    std::vector<TapChoice> choices{{QStringLiteral("Pre FX"), sub::app::kPreFx, pad.id}};
    addTaps(pad.devices, QString(), true, choices, pads, name + QStringLiteral(" › "), rackNames);
    choices.push_back({QStringLiteral("Post FX"), sub::app::kPreFader, pad.id});
    choices.push_back({QStringLiteral("Post Mixer"), sub::app::kPostFader, pad.id});
    return choices;
}

}  // namespace

DeviceInfo::DeviceInfo(QObject* parent) : QObject(parent) {}

void DeviceInfo::setSession(sub::app::Session* session) {
    if (session == session_)
        return;
    session_ = session;
    connectSession();
    Q_EMIT targetChanged();
    refresh();
}

void DeviceInfo::setTrackId(const QString& trackId) {
    if (trackId == trackId_)
        return;
    trackId_ = trackId;
    Q_EMIT targetChanged();
    refresh();
}

void DeviceInfo::setDeviceId(const QString& deviceId) {
    if (deviceId == deviceId_)
        return;
    deviceId_ = deviceId;
    Q_EMIT targetChanged();
    refresh();
}

void DeviceInfo::connectSession() {
    for (const QMetaObject::Connection& connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    if (!session_)
        return;
    Project* project = session_->project();
    EngineBridge* bridge = session_->bridge();
    auto refreshAll = [this] { refresh(); };
    auto ofTrack = [this](const QString& trackId) {
        if (trackId == trackId_)
            refresh();
    };
    auto ofDevice = [this](const QString& trackId, const QString& deviceId) {
        if (trackId == trackId_ && deviceId == deviceId_)
            refresh();
    };
    // Its track's devices changed (it may have moved, gone, been switched or
    // renamed), or another track's: a sidechain's source may have lost (or got
    // back) the device it is taken after. Its sidechain's (or MIDI input's) source renamed (or going).
    connections_ << connect(project, &Project::devicesChanged, this, refreshAll);
    connections_ << connect(project, &Project::trackChanged, this, [this](const QString& trackId) {
        if (trackId == sidechainSource_ || trackId == midiSource_)
            refresh();
    });
    // A pad of its sidechain's source renamed (the tooltip names the pad it is taken from).
    connections_ << connect(project, &Project::chainChanged, this, [this](const QString& trackId, const QString&) {
        if (trackId == sidechainSource_)
            refresh();
    });
    connections_ << connect(project, &Project::devicesFolded, this, ofTrack);
    connections_ << connect(project, &Project::rackViewChanged, this, ofTrack);
    connections_ << connect(project, &Project::reset, this, refreshAll);
    for (auto removed : {&Project::trackRemoved, &Project::returnRemoved}) {
        connections_ << connect(project, removed, this, [this](const QString&, int) { refresh(); });
    }
    connections_ << connect(project, &Project::deviceStateChanged, this, ofDevice);
    // Its processor was made again (a plug-in loaded, or failed), or reports something new.
    connections_ << connect(bridge, &EngineBridge::devicesLoaded, this, ofTrack);
    connections_ << connect(bridge, &EngineBridge::pluginParamsChanged, this, ofDevice);
    connections_ << connect(bridge, &EngineBridge::pluginParamsRebuilt, this, ofDevice);
    connections_ << connect(bridge, &EngineBridge::pluginEditorChanged, this, ofDevice);
    connections_ << connect(bridge, &EngineBridge::pluginsPendingChanged, this, refreshAll);
    connections_ << connect(bridge, &EngineBridge::deviceChanged, this, refreshAll);  // (new latencies)
    connections_ << connect(bridge, &EngineBridge::latenciesChanged, this, refreshAll);
    connections_ << connect(bridge, &EngineBridge::constrainLatencyChanged, this, refreshAll);  // (bypassed or not)
    // Its switch's automation: playing, overridden, or gone; and the switch following it.
    connections_ << connect(bridge, &EngineBridge::automationStateChanged, this, [this](const QString& owner) {
        if (owner == trackId_ || owner.isEmpty())
            refreshSwitch();
    });
    connections_ << connect(bridge, &EngineBridge::positionChanged, this, [this] {
        if (state_.enabledAutomation == QLatin1String("on"))
            refreshSwitch();
    });
}

QString DeviceInfo::switchKey() const { return sub::app::automation::deviceOnKey(deviceId_); }

std::pair<bool, QString> DeviceInfo::switchState(bool own) const {
    const EngineBridge& bridge = *session_->bridge();
    const QString key = switchKey();
    if (bridge.isOverridden(trackId_, key))
        return {own, QStringLiteral("off")};
    if (!bridge.isAutomated(trackId_, key))
        return {own, QString()};
    const std::optional<double> value = session_->bridge()->currentValue(trackId_, key);
    return {value ? *value >= 0.5 : own, QStringLiteral("on")};
}

void DeviceInfo::refreshSwitch() {
    const Device* device = session_ ? session_->project()->findDevice(trackId_, deviceId_) : nullptr;
    if (device == nullptr)
        return;
    const auto [enabled, automation] = switchState(device->enabled);
    if (enabled == state_.enabled && automation == state_.enabledAutomation)
        return;
    state_.enabled = enabled;
    state_.enabledAutomation = automation;
    Q_EMIT changed();
}

std::optional<Sidechain> DeviceInfo::currentSidechain() const {
    const Device* device = session_ ? session_->project()->findDevice(trackId_, deviceId_) : nullptr;
    if (device == nullptr || !device->sidechain || !session_->project()->hasOwner(device->sidechain->trackId))
        return std::nullopt;  // (its source is going: so is the sidechain)
    return device->sidechain;
}

DeviceInfo::TapChoices DeviceInfo::tapChoices(const QString& sourceTrackId) const {
    TapChoices choices;
    choices.track.push_back({QStringLiteral("Pre FX"), sub::app::kPreFx, QString()});
    const Track* source = session_ ? session_->project()->findTrack(sourceTrackId) : nullptr;
    if (source != nullptr) {
        const auto drumRacks = std::ranges::count_if(sub::app::iterDevices(source->devices),
                                                     [](const Device* device) { return device->isDrumRack(); });
        addTaps(source->devices, QString(), true, choices.track, choices.pads, QString(), drumRacks > 1);
    }
    choices.track.push_back({QStringLiteral("Post FX"), sub::app::kPreFader, QString()});
    choices.track.push_back({QStringLiteral("Post Mixer"), sub::app::kPostFader, QString()});
    return choices;
}

std::pair<QString, QString> DeviceInfo::tapOf(const Sidechain& sidechain) const {
    const Track* source = session_ ? session_->project()->findTrack(sidechain.trackId) : nullptr;
    if (source == nullptr)
        return {sidechain.tap, sidechain.chain};
    if (!sidechain.chain.isEmpty()) {
        if (sub::app::findChain(source->devices, sidechain.chain).chain == nullptr)
            return {sub::app::kPreFader, QString()};
    } else if (sidechain.tapDevice()) {
        if (sub::app::findDevice(source->devices, sidechain.tap) == nullptr)
            return {sub::app::kPreFader, QString()};
        // After the instrument the track (or one of its pads) starts with: its Pre FX.
        const std::optional<QString> container = sub::app::containerOf(source->devices, sidechain.tap);
        const std::vector<Device>* chain = sub::app::chainDevices(source->devices, container);
        if (chain != nullptr && chain->front().id == sidechain.tap && sub::app::deviceIsInstrument(chain->front())) {
            if (!container)
                return {sub::app::kPreFx, QString()};
            if (const sub::app::ConstRackChain pad = sub::app::findChain(source->devices, *container);
                pad.rack != nullptr && pad.rack->isDrumRack())
                return {sub::app::kPreFx, *container};
        }
    }
    return {sidechain.tap, sidechain.chain};
}

QString DeviceInfo::tapLabel(const Sidechain& sidechain) const {
    const auto [tap, chain] = tapOf(sidechain);
    const TapChoices choices = tapChoices(sidechain.trackId);
    for (const TapChoice& choice : choices.track) {
        if (choice.tap == tap && choice.chain == chain)
            return choice.label;
    }
    for (const auto& [pad, taps] : choices.pads) {
        for (const TapChoice& choice : taps) {
            if (choice.tap == tap && choice.chain == chain)
                return pad + QStringLiteral(" › ") + choice.label;
        }
    }
    // A chain of another rack (none of the menu's): by its name.
    const Track* source = session_ ? session_->project()->findTrack(sidechain.trackId) : nullptr;
    const sub::app::Chain* other = source ? sub::app::findChain(source->devices, chain).chain : nullptr;
    if (other == nullptr)
        return {};
    const QString where = tap == sub::app::kPreFx      ? QStringLiteral("Pre FX")
                          : tap == sub::app::kPreFader ? QStringLiteral("Post FX")
                                                       : QStringLiteral("Post Mixer");
    return other->name + QStringLiteral(" › ") + where;
}

void DeviceInfo::refresh() {
    State s;
    const Project* project = session_ ? session_->project() : nullptr;
    const Track* track = project ? project->findTrack(trackId_) : nullptr;
    const Device* device = track ? sub::app::findDevice(track->devices, deviceId_) : nullptr;
    if (device != nullptr) {
        EngineBridge* bridge = session_->bridge();
        s.exists = true;
        s.kind = device->kind;
        s.isRack = device->isRack();
        s.isDrumRack = device->isDrumRack();
        s.isPlugin = device->isPlugin();
        s.name = s.isPlugin && device->plugin ? device->plugin->name : sub::app::deviceName(*device);
        s.pluginFormat = s.isPlugin && device->plugin ? device->plugin->format : QString();
        s.instrument = sub::app::deviceIsInstrument(*device);
        std::tie(s.enabled, s.enabledAutomation) = switchState(device->enabled);
        s.folded = project->isDeviceFolded(deviceId_);
        s.chainListShown = s.isRack && project->isChainListShown(deviceId_);
        s.rackDevicesShown = project->areRackDevicesShown(deviceId_);
        const std::optional<QString> chain = sub::app::containerOf(track->devices, deviceId_);
        s.chainId = chain.value_or(QString());
        if (const auto* siblings = sub::app::chainDevices(track->devices, chain); siblings && !s.instrument) {
            const auto at = std::find_if(siblings->begin(), siblings->end(),
                                         [this](const Device& d) { return d.id == deviceId_; });
            const int index = int(at - siblings->begin());
            const int first = !siblings->empty() && sub::app::deviceIsInstrument(siblings->front()) ? 1 : 0;
            s.canMoveLeft = index > first;
            s.canMoveRight = index < int(siblings->size()) - 1;
        }
        s.loaded = bridge->pluginRunning(trackId_, deviceId_);  // (one loading in its sandbox: not yet)
        const int latency = s.loaded ? bridge->deviceLatency(trackId_, deviceId_) : 0;
        s.constrained = s.loaded && bridge->deviceConstrained(trackId_, deviceId_);
        if (s.isPlugin && device->plugin) {
            const sub::app::PluginRef& plugin = *device->plugin;
            s.pending = !s.loaded && bridge->pluginPending(deviceId_);
            s.reloadable = !s.loaded && bridge->pluginReloadable(trackId_, deviceId_);
            if (s.pending) {
                s.pluginMessage = plugin.name + QStringLiteral(" is loading…");
            } else if (!s.loaded) {
                s.pluginMessage = bridge->pluginError(deviceId_);
                if (s.pluginMessage.isEmpty())
                    s.pluginMessage = plugin.name + QStringLiteral(" is not loaded.");
            }
            s.editorOpen = s.loaded && bridge->isPluginEditorOpen(trackId_, deviceId_);
            QStringList lines{plugin.name, plugin.vendor, bridge->pluginPath(plugin).value_or(plugin.path)};
            lines.removeAll(QString());
            if (s.loaded)
                lines << latencyLine(latency);
            if (s.constrained)
                lines << constrainedLine();
            s.toolTip = lines.join(u'\n');
        } else {  // a built-in device or a rack: its name and latency (a rack's: its slowest chain's)
            s.toolTip = sub::app::deviceName(*device) + u'\n' + latencyLine(latency);
            if (s.constrained)
                s.toolTip += u'\n' + constrainedLine();
        }
        s.hasSidechainInput = bridge->hasSidechainInput(trackId_, deviceId_);
        if (const std::optional<Sidechain> sidechain = currentSidechain()) {
            s.sidechainOn = true;
            s.sidechainToolTip = QStringLiteral("Sidechain: %1, %2")
                                     .arg(project->track(sidechain->trackId).name, tapLabel(*sidechain));
        } else {
            s.sidechainToolTip = QStringLiteral("Sidechain: none (click to choose a track)");
        }
        s.acceptsMidi = bridge->acceptsMidi(trackId_, deviceId_);
        const Track* source = device->midiFrom.isEmpty() ? nullptr : project->findTrack(device->midiFrom);
        s.midiFromOn = source != nullptr;
        s.midiFromToolTip = source ? QStringLiteral("MIDI From: %1 (it plays that track's notes)").arg(source->name)
                                   : QStringLiteral("MIDI From: its own track (click to take another track's notes)");
    }
    const std::optional<Sidechain> sidechain = currentSidechain();
    sidechainSource_ = sidechain ? sidechain->trackId : QString();
    midiSource_ = device ? device->midiFrom : QString();
    if (s == state_)
        return;
    state_ = s;
    Q_EMIT changed();
}

void DeviceInfo::setEnabled(bool enabled) {
    if (!session_ || !session_->project()->hasDevice(trackId_, deviceId_))
        return;
    session_->editor()->setDeviceEnabled(trackId_, deviceId_, enabled);  // (overriding its automation)
    refreshSwitch();
}

void DeviceInfo::showSwitchAutomation() {
    if (session_ && session_->project()->hasDevice(trackId_, deviceId_))
        session_->editor()->showAutomation(trackId_, switchKey());
}

void DeviceInfo::deleteSwitchAutomation() { AutomationTarget{session_, trackId_, switchKey()}.deleteEnvelope(); }

void DeviceInfo::reEnableAutomation() { AutomationTarget{session_, trackId_, switchKey()}.reEnable(); }

bool DeviceInfo::hasSwitchEnvelope() const { return AutomationTarget{session_, trackId_, switchKey()}.hasEnvelope(); }

void DeviceInfo::moveLeft() {
    if (!session_ || !state_.canMoveLeft)
        return;
    const auto ids = session_->deviceSelection()->chainDevices(state_.chainId);
    session_->editor()->moveDevice(trackId_, deviceId_, int(ids.indexOf(deviceId_)) - 1);
}

void DeviceInfo::moveRight() {
    if (!session_ || !state_.canMoveRight)
        return;
    const auto ids = session_->deviceSelection()->chainDevices(state_.chainId);
    session_->editor()->moveDevice(trackId_, deviceId_, int(ids.indexOf(deviceId_)) + 1);
}

void DeviceInfo::showEditor(bool show) {
    if (!session_ || !session_->project()->hasDevice(trackId_, deviceId_))
        return;
    if (show)
        session_->bridge()->openPluginEditor(trackId_, deviceId_);  // (open already: brought to the front)
    else
        session_->bridge()->closePluginEditor(trackId_, deviceId_);
    refresh();
}

void DeviceInfo::openEditor() {
    refresh();
    if (state_.isPlugin && state_.loaded)
        showEditor(true);
}

QVariantList DeviceInfo::sidechainMenu() const {
    QVariantList entries;
    if (!session_ || !session_->project()->hasDevice(trackId_, deviceId_))
        return entries;
    const Project* project = session_->project();
    const std::optional<Sidechain> current = currentSidechain();
    auto entry = [](const QString& text, bool checked, bool enabled, const QString& source, const QString& tap,
                    const QString& chain = {}) {
        return QVariantMap{{QStringLiteral("text"), text},          {QStringLiteral("checkable"), true},
                           {QStringLiteral("checked"), checked},    {QStringLiteral("enabled"), enabled},
                           {QStringLiteral("source"), source},      {QStringLiteral("tap"), tap},
                           {QStringLiteral("chain"), chain}};
    };
    const QVariantMap separator{{QStringLiteral("separator"), true}};
    entries << entry(QStringLiteral("No Sidechain"), !current, true, QString(), QString());
    // The tracks: a search field's list (MenuSearch.qml), which has the keyboard as the menu opens.
    QVariantList tracks;
    for (const Track* source : project->sidechainSources(trackId_)) {
        const bool usable = !project->sidechainWouldCycle(trackId_, source->id);
        // A new source: taken where the old one was (after its fader if after a device or from a pad of it).
        const QString tap =
            !current || current->tapDevice() || !current->chain.isEmpty() ? sub::app::kPostFader : current->tap;
        tracks << entry(usable ? source->name : source->name + QStringLiteral(" (this track feeds it)"),
                        current && current->trackId == source->id, usable, source->id, tap);
    }
    if (tracks.isEmpty())
        entries << separator;
    else
        entries << QVariantMap{{QStringLiteral("search"), true}, {QStringLiteral("children"), tracks}};
    if (current) {
        entries << separator;
        const auto [tap, chain] = tapOf(*current);
        const TapChoices choices = tapChoices(current->trackId);
        for (const TapChoice& choice : choices.track)
            entries << entry(choice.label, choice.tap == tap && choice.chain == chain, true, current->trackId,
                             choice.tap, choice.chain);
        // Then each Drum Rack pad's own, in a submenu.
        if (!choices.pads.empty())
            entries << separator;
        for (const auto& [pad, taps] : choices.pads) {
            QVariantList children;
            for (const TapChoice& choice : taps)
                children << entry(choice.label, choice.tap == tap && choice.chain == chain, true, current->trackId,
                                  choice.tap, choice.chain);
            entries << QVariantMap{{QStringLiteral("submenu"), pad}, {QStringLiteral("children"), children}};
        }
    }
    return entries;
}

QVariantList DeviceInfo::midiFromMenu() const {
    QVariantList entries;
    const Device* device = session_ ? session_->project()->findDevice(trackId_, deviceId_) : nullptr;
    if (device == nullptr)
        return entries;
    auto entry = [](const QString& text, bool checked, const QString& source) {
        return QVariantMap{{QStringLiteral("text"), text},    {QStringLiteral("checkable"), true},
                           {QStringLiteral("checked"), checked}, {QStringLiteral("enabled"), true},
                           {QStringLiteral("source"), source}};
    };
    const Project* project = session_->project();
    const bool own = device->midiFrom.isEmpty() || !project->hasTrack(device->midiFrom);
    entries << entry(QStringLiteral("Own Track"), own, QString());
    QVariantList tracks;
    for (const Track* source : project->midiSources(trackId_))
        tracks << entry(source->name, !own && device->midiFrom == source->id, source->id);
    if (!tracks.isEmpty())
        entries << QVariantMap{{QStringLiteral("search"), true}, {QStringLiteral("children"), tracks}};
    return entries;
}

void DeviceInfo::setMidiFrom(const QString& sourceTrackId) {
    if (!session_ || !session_->project()->hasDevice(trackId_, deviceId_))
        return;
    session_->editor()->trySetDeviceMidiFrom(trackId_, deviceId_, sourceTrackId);  // (refusals on the status line)
    refresh();
}

void DeviceInfo::setSidechain(const QString& sourceTrackId, const QString& tap, const QString& chain) {
    if (!session_ || !session_->project()->hasDevice(trackId_, deviceId_))
        return;
    // (A source gone meanwhile is refused: the editor says so, on the status line.)
    session_->editor()->trySetDeviceSidechain(trackId_, deviceId_, sourceTrackId,
                                              tap.isEmpty() ? sub::app::kPostFader : tap, chain);
    refresh();
}

}  // namespace sub::ui
