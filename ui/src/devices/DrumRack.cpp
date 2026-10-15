#include "devices/DrumRack.h"

#include "audio/EngineBridge.h"
#include "browser/BrowserMime.h"
#include "editor/ProjectEditor.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Project.h"
#include "model/SamplerState.h"
#include "session/DeviceSelection.h"

#include <QDragEnterEvent>
#include <QDropEvent>

#include <algorithm>

namespace sub::ui {

using sub::app::Project;

DrumRack::DrumRack(QObject* parent) : QObject(parent) {}

void DrumRack::setSession(sub::app::Session* session) {
    if (session == session_)
        return;
    for (const QMetaObject::Connection& connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    session_ = session;
    if (session_) {
        const auto ours = [this](const QString& trackId) {
            if (trackId == trackId_)
                refresh();
        };
        connections_ << connect(session_->project(), &Project::devicesChanged, this, ours);
        connections_ << connect(session_->project(), &Project::chainChanged, this, ours);
        connections_ << connect(session_->project(), &Project::deviceStateChanged, this, ours);
        connections_ << connect(session_->project(), &Project::reset, this, &DrumRack::refresh);
        connections_ << connect(session_->deviceSelection(), &sub::app::DeviceSelection::changed, this,
                                &DrumRack::refresh);
    }
    Q_EMIT targetChanged();
    refresh();
}

void DrumRack::setTrackId(const QString& trackId) {
    if (trackId == trackId_)
        return;
    trackId_ = trackId;
    Q_EMIT targetChanged();
    refresh();
}

void DrumRack::setRackId(const QString& rackId) {
    if (rackId == rackId_)
        return;
    rackId_ = rackId;
    Q_EMIT targetChanged();
    refresh();
}

void DrumRack::setFirstNote(int note) {
    note = std::clamp(note / kColumns * kColumns, 0, kLastFirstNote);
    if (note == firstNote_)
        return;
    firstNote_ = note;
    Q_EMIT firstNoteChanged();
    refresh();
}

void DrumRack::scroll(int rows) { setFirstNote(firstNote_ + rows * kColumns); }

const sub::app::Device* DrumRack::rack() const {
    const sub::app::Device* found = session_ ? session_->project()->findDevice(trackId_, rackId_) : nullptr;
    return found != nullptr && found->isDrumRack() ? found : nullptr;
}

QString DrumRack::chainOf(int note) const {
    const sub::app::Device* drums = rack();
    const sub::app::Chain* pad = drums ? sub::app::drumPad(*drums, note) : nullptr;
    return pad ? pad->id : QString();
}

QString DrumRack::samplerOf(int note) const {
    const sub::app::Device* drums = rack();
    const sub::app::Chain* pad = drums ? sub::app::drumPad(*drums, note) : nullptr;
    const sub::app::Device* sampler = pad ? sub::app::padSampler(*pad) : nullptr;
    return sampler ? sampler->id : QString();
}

QString DrumRack::loadSample(int note, const QString& path, double start, double end) {
    if (!rack())
        return {};
    const QString chain = session_->editor()->loadDrumPad(trackId_, rackId_, note, path, start, end);
    if (!chain.isEmpty())
        session_->deviceSelection()->clickChain(rackId_, chain);  // (its Sampler shows beside the rack)
    return chain;
}

void DrumRack::select(int note) {
    const QString chain = chainOf(note);
    if (!chain.isEmpty())
        session_->deviceSelection()->clickChain(rackId_, chain);
}

void DrumRack::play(int note, int velocity) {
    if (session_ && rack())
        session_->bridge()->previewNote(trackId_, note, velocity);
}

void DrumRack::toggleMute(int note) {
    const sub::app::Device* drums = rack();
    const sub::app::Chain* pad = drums ? sub::app::drumPad(*drums, note) : nullptr;
    if (pad)
        session_->editor()->setChainParam(trackId_, pad->id, QStringLiteral("mute"), pad->mute ? 0.0 : 1.0);
}

void DrumRack::toggleSolo(int note) {
    const sub::app::Device* drums = rack();
    const sub::app::Chain* pad = drums ? sub::app::drumPad(*drums, note) : nullptr;
    if (pad)
        session_->editor()->setChainParam(trackId_, pad->id, QStringLiteral("solo"), pad->solo ? 0.0 : 1.0);
}

void DrumRack::toggleLock(int note) {
    const sub::app::Device* drums = rack();
    const sub::app::Chain* pad = drums ? sub::app::drumPad(*drums, note) : nullptr;
    if (pad)
        session_->editor()->setChainParam(trackId_, pad->id, QStringLiteral("locked"), pad->locked ? 0.0 : 1.0);
}

void DrumRack::remove(int note) {
    const QString chain = chainOf(note);
    if (!chain.isEmpty())
        session_->editor()->removeRackChains(trackId_, {chain});
}

void DrumRack::refresh() {
    QVariantList pads;
    QVariantList filled;
    int count = 0;
    int locked = 0;
    const sub::app::Device* drums = rack();
    const QString shown = drums && session_->deviceSelection()->trackId() == trackId_
                              ? session_->deviceSelection()->shownChain(rackId_)
                              : QString();
    for (int note = 0; note < 128; ++note) filled.append(false);
    if (drums) {
        for (const sub::app::Chain& chain : drums->chains) {
            if (chain.receiveNote < 0)
                continue;
            filled[chain.receiveNote] = true;
            if (sub::app::padSampler(chain) != nullptr) {
                ++count;
                locked += chain.locked ? 1 : 0;
            }
        }
    }
    // Top row first, each left to right: the lowest key at the bottom left.
    for (int row = kPads / kColumns - 1; row >= 0; --row) {
        for (int column = 0; column < kColumns; ++column) {
            const int note = firstNote_ + row * kColumns + column;
            QVariantMap pad{{QStringLiteral("note"), note},
                            {QStringLiteral("noteName"), sub::app::drumNoteName(note)},
                            {QStringLiteral("chainId"), QString()},
                            {QStringLiteral("name"), QString()},
                            {QStringLiteral("sample"), QString()},
                            {QStringLiteral("mute"), false},
                            {QStringLiteral("solo"), false},
                            {QStringLiteral("locked"), false},
                            {QStringLiteral("selected"), false}};
            if (const sub::app::Chain* chain = drums ? sub::app::drumPad(*drums, note) : nullptr) {
                pad[QStringLiteral("chainId")] = chain->id;
                pad[QStringLiteral("name")] = chain->name;
                pad[QStringLiteral("mute")] = chain->mute;
                pad[QStringLiteral("solo")] = chain->solo;
                pad[QStringLiteral("locked")] = chain->locked;
                pad[QStringLiteral("selected")] = chain->id == shown;
                if (const sub::app::Device* sampler = sub::app::padSampler(*chain)) {
                    pad[QStringLiteral("sample")] =
                        sub::app::samplerState::sample(sub::app::deviceState::fromModel(sampler->state));
                }
            }
            pads.append(pad);
        }
    }
    if (pads == pads_ && filled == filled_ && count == padCount_ && locked == lockedCount_)
        return;
    pads_ = pads;
    filled_ = filled;
    padCount_ = count;
    lockedCount_ = locked;
    Q_EMIT changed();
}

// --- DrumPadItem ------------------------------------------------------------------------------

DrumPadItem::DrumPadItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemAcceptsDrops, true); }

void DrumPadItem::setDrumRack(DrumRack* rack) {
    if (rack == drumRack_)
        return;
    drumRack_ = rack;
    Q_EMIT drumRackChanged();
}

void DrumPadItem::setNote(int note) {
    if (note == note_)
        return;
    note_ = note;
    Q_EMIT noteChanged();
}

void DrumPadItem::setDropping(bool dropping) {
    if (dropping == dropping_)
        return;
    dropping_ = dropping;
    Q_EMIT droppingChanged();
}

void DrumPadItem::dragEnterEvent(QDragEnterEvent* event) {
    if (drumRack_ && sub::app::droppedAudio(event->mimeData())) {
        event->acceptProposedAction();
        setDropping(true);
    } else {
        event->ignore();  // (devices and presets: the device view's)
    }
}

void DrumPadItem::dragMoveEvent(QDragMoveEvent* event) {
    if (drumRack_ && sub::app::droppedAudio(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void DrumPadItem::dragLeaveEvent(QDragLeaveEvent*) { setDropping(false); }

void DrumPadItem::dropEvent(QDropEvent* event) {
    setDropping(false);
    const std::optional<sub::app::DroppedAudio> audio = sub::app::droppedAudio(event->mimeData());
    if (!drumRack_ || !audio) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    drumRack_->loadSample(note_, audio->path, audio->start, audio->end);
}

}  // namespace sub::ui
