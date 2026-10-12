#include "editor/AgentHistory.h"

#include "editor/EditorSupport.h"
#include "editor/ProjectEditor.h"
#include "model/Commands.h"
#include "model/Errors.h"
#include "model/Project.h"

#include <QSet>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <exception>
#include <utility>

namespace sub::app {

namespace {

const QString kEverything = QStringLiteral("*");

QString trackRegion(const QString& trackId) { return QStringLiteral("track/") + trackId; }
QString clipsRegion(const QString& trackId) { return trackRegion(trackId) + QStringLiteral("/clips"); }
QString devicesRegion(const QString& trackId) { return trackRegion(trackId) + QStringLiteral("/devices"); }
QString deviceRegion(const QString& trackId, const QString& deviceId) {
    return devicesRegion(trackId) + u'/' + deviceId;
}
QString paramRegion(const QString& trackId, const QString& deviceId, const QString& paramId) {
    return deviceRegion(trackId, deviceId) + QStringLiteral("/param/") + paramId;
}
QString fieldRegion(const QString& trackId, TrackField field) {
    return trackRegion(trackId) + QStringLiteral("/field/") + trackFieldName(field);
}
QString laneRegion(const QString& owner, const QString& key) {
    return QStringLiteral("auto/") + owner + u'/' + key;
}
QString settingsRegion(SettingsField field) { return QStringLiteral("settings/") + settingsFieldName(field); }
// Where a track is in the arrangement (inserted, removed); "order" alone: all of it (arranged).
QString orderRegion(const QString& trackId = {}) {
    return trackId.isEmpty() ? QStringLiteral("order") : QStringLiteral("order/") + trackId;
}

void addClipLists(QStringList& out, const ClipLists& lists) {
    for (auto it = lists.constBegin(); it != lists.constEnd(); ++it) out.append(clipsRegion(it.key()));
}

void addDeviceLists(QStringList& out, const DeviceLists& lists) {
    for (const auto& [trackId, devices] : lists) out.append(devicesRegion(trackId));
}

void addParams(QStringList& out, const QString& trackId, const QList<DeviceParam>& params) {
    for (const auto& [deviceId, paramId] : params) out.append(paramRegion(trackId, deviceId, paramId));
}

// The regions of one command, not a macro's.
std::optional<QStringList> ownFootprint(const QUndoCommand& command, const Project& project) {
    QStringList out;
    if (const auto* c = dynamic_cast<const SetClipsCommand*>(&command)) {
        addClipLists(out, c->before());
        addClipLists(out, c->after());
        for (const QString& id : c->frozenBefore().keys() + c->frozenAfter().keys()) {
            out.append(trackRegion(id) + QStringLiteral("/frozen"));
        }
    } else if (const auto* c = dynamic_cast<const ReplaceFilesCommand*>(&command)) {
        addClipLists(out, c->before());
        addClipLists(out, c->after());
        for (const DeviceTarget& device : c->statesBefore().keys() + c->statesAfter().keys()) {
            out.append(deviceRegion(device.first, device.second));
        }
    } else if (const auto* c = dynamic_cast<const InsertTrackCommand*>(&command)) {
        out << trackRegion(c->track().id) << orderRegion(c->track().id);
    } else if (const auto* c = dynamic_cast<const RemoveTrackCommand*>(&command)) {
        out << trackRegion(c->trackId()) << orderRegion(c->trackId());
    } else if (const auto* c = dynamic_cast<const InsertReturnCommand*>(&command)) {
        out << trackRegion(c->track().id) << orderRegion(c->track().id);
    } else if (const auto* c = dynamic_cast<const RemoveReturnCommand*>(&command)) {
        out << trackRegion(c->trackId()) << orderRegion(c->trackId());
    } else if (const auto* c = dynamic_cast<const ReplaceTrackCommand*>(&command)) {
        out << trackRegion(c->newValue().id);
    } else if (const auto* c = dynamic_cast<const SetFreezeCommand*>(&command)) {
        // Freezing a group freezes what is in it too (its edits are refused, its audio is the group's).
        out << trackRegion(c->trackId());
        if (project.hasOwner(c->trackId())) {
            for (const Track* inner : project.descendants(c->trackId())) out << trackRegion(inner->id);
        }
    } else if (const auto* c = dynamic_cast<const UpdateTrackCommand*>(&command)) {
        out << fieldRegion(c->trackId(), c->field());
    } else if (const auto* c = dynamic_cast<const UpdateTrackFieldsCommand*>(&command)) {
        for (TrackField field : c->target().second) out << fieldRegion(c->trackId(), field);
    } else if (const auto* c = dynamic_cast<const UpdateTracksCommand*>(&command)) {
        for (const QString& id : c->target().second) out << fieldRegion(id, c->field());
    } else if (const auto* c = dynamic_cast<const SetTempoCommand*>(&command)) {
        out << settingsRegion(SettingsField::Tempo);
        addClipLists(out, c->oldValue().clips);
        addClipLists(out, c->newValue().clips);
    } else if (const auto* c = dynamic_cast<const UpdateSettingsCommand*>(&command)) {
        for (SettingsField field : c->target()) out << settingsRegion(field);
    } else if (const auto* c = dynamic_cast<const SetDevicesCommand*>(&command)) {
        out << devicesRegion(c->trackId());
    } else if (const auto* c = dynamic_cast<const SetChainsCommand*>(&command)) {
        addDeviceLists(out, c->oldValue());
        addDeviceLists(out, c->newValue());
    } else if (const auto* c = dynamic_cast<const SetDeviceParamCommand*>(&command)) {
        out << paramRegion(c->trackId(), c->deviceId(), c->paramId());
    } else if (const auto* c = dynamic_cast<const SetDeviceParamsCommand*>(&command)) {
        addParams(out, c->trackId(), c->target().second);
    } else if (const auto* c = dynamic_cast<const UpdateChainCommand*>(&command)) {
        out << devicesRegion(c->trackId()) + QStringLiteral("/chains/") + c->chainId() + u'/' +
                   chainFieldName(c->field());
    } else if (const auto* c = dynamic_cast<const SetDeviceNameCommand*>(&command)) {
        out << deviceRegion(c->trackId(), c->deviceId()) + QStringLiteral("/name");
    } else if (const auto* c = dynamic_cast<const SetMacrosCommand*>(&command)) {
        out << deviceRegion(c->trackId(), c->rackId()) + QStringLiteral("/macros");
        addParams(out, c->trackId(), c->oldValue().values.keys() + c->newValue().values.keys());
    } else if (const auto* c = dynamic_cast<const SetDeviceStateCommand*>(&command)) {
        out << deviceRegion(c->trackId(), c->deviceId());  // (a plug-in's state holds all its parameters)
    } else if (const auto* c = dynamic_cast<const SetDeviceEnabledCommand*>(&command)) {
        out << deviceRegion(c->trackId(), c->deviceId()) + QStringLiteral("/on");
    } else if (const auto* c = dynamic_cast<const SetDeviceSidechainCommand*>(&command)) {
        out << deviceRegion(c->trackId(), c->deviceId()) + QStringLiteral("/sidechain");
    } else if (const auto* c = dynamic_cast<const SetDeviceMidiFromCommand*>(&command)) {
        out << deviceRegion(c->trackId(), c->deviceId()) + QStringLiteral("/midi");
    } else if (const auto* c = dynamic_cast<const SetEnvelopesCommand*>(&command)) {
        for (const LaneRef& lane : c->target()) out << laneRegion(lane.first, lane.second);
    } else if (const auto* c = dynamic_cast<const SetEnvelopeCommand*>(&command)) {
        out << laneRegion(c->owner(), c->automationKey());
    } else if (dynamic_cast<const ArrangeTracksCommand*>(&command) != nullptr) {
        out << orderRegion();
    } else {
        return std::nullopt;  // not one of the model's
    }
    return out;
}

void collect(const QUndoCommand& command, const Project& project, QStringList& out) {
    if (const auto own = ownFootprint(command, project)) {
        out.append(*own);
        return;
    }
    if (command.childCount() == 0) {
        out.append(kEverything);  // a command of another kind: it may touch anything
        return;
    }
    for (int i = 0; i < command.childCount(); ++i) collect(*command.child(i), project, out);
}

bool within(const QString& region, const QString& outer) {
    return region.size() > outer.size() && region.startsWith(outer) && region.at(outer.size()) == u'/';
}

// The owners a footprint's regions name: tracks, returns, the master.
QStringList ownersOf(const QStringList& regions) {
    QStringList owners;
    for (const QString& region : regions) {
        const QStringList parts = region.split(u'/');
        if (parts.size() < 2 || (parts[0] != u"track" && parts[0] != u"auto")) continue;
        if (!owners.contains(parts[1])) owners.append(parts[1]);
    }
    return owners;
}

QString quoted(const QString& text) { return QStringLiteral("“%1”").arg(text); }

}  // namespace

AgentHistory::AgentHistory(Project* project, QUndoStack* userStack, QObject* parent)
    : QAbstractListModel(parent),
      project_(project),
      userStack_(userStack),
      stack_(new QUndoStack(this)),
      editor_(new ProjectEditor(project, stack_, this)) {
    userIndex_ = userStack_->index();
    connect(userStack_, &QUndoStack::indexChanged, this, &AgentHistory::userIndexChanged);
}

AgentHistory::~AgentHistory() = default;

QStringList AgentHistory::footprint(const QUndoCommand& command, const Project& project) {
    QStringList out;
    collect(command, project, out);
    out.removeDuplicates();
    return out;
}

bool AgentHistory::overlaps(const QStringList& a, const QStringList& b) {
    for (const QString& x : a) {
        for (const QString& y : b) {
            if (x == kEverything || y == kEverything || x == y || within(x, y) || within(y, x)) return true;
        }
    }
    return false;
}

std::optional<int> AgentHistory::record(const QString& text, const QString& tool,
                                        const std::function<void(ProjectEditor&)>& edit) {
    const int before = stack_->count();
    QString refusal;
    const auto refused = connect(editor_, &ProjectEditor::refused, this, [&refusal](const QString& message) {
        if (refusal.isEmpty()) refusal = message;
    });
    std::optional<EditError> error;
    {
        const editing::Macro macro(stack_, text);
        try {
            edit(*editor_);
        } catch (const EditError& e) {
            error = e;
        } catch (const std::exception& e) {  // (a track or device that went: the change goes as a whole)
            error = EditError(QString::fromUtf8(e.what()));
        }
    }
    disconnect(refused);
    if (!error && !refusal.isEmpty()) error = EditError(refusal);
    // (An empty macro stays on the agents' stack, which never undoes: it lists nothing.)
    QUndoCommand* command =
        stack_->count() > before ? const_cast<QUndoCommand*>(stack_->command(stack_->count() - 1)) : nullptr;
    if (error) {
        if (command != nullptr) command->undo();  // (what it did before it was refused)
        throw *error;
    }
    if (command == nullptr || command->childCount() == 0) return std::nullopt;

    Change change;
    change.id = nextId_++;
    change.text = text;
    change.tool = tool;
    change.time = QDateTime::currentDateTime();
    change.tracks = ownersOf(footprint(*command, *project_));
    change.step = ++clock_;
    change.command = command;
    const int row = count();
    beginInsertRows({}, row, row);
    changes_.push_back(std::move(change));
    endInsertRows();
    Q_EMIT countChanged();
    markDirty();
    Q_EMIT changed(changes_.back().id);
    return changes_.back().id;
}

const AgentHistory::Change* AgentHistory::find(int id) const {
    const auto it = std::find_if(changes_.begin(), changes_.end(), [id](const Change& c) { return c.id == id; });
    return it == changes_.end() ? nullptr : &*it;
}

int AgentHistory::rowOf(int id) const {
    for (int row = 0; row < count(); ++row) {
        if (changes_[static_cast<size_t>(row)].id == id) return row;
    }
    return -1;
}

int AgentHistory::appliedCount() const {
    return static_cast<int>(std::count_if(changes_.begin(), changes_.end(), [](const Change& c) { return c.applied; }));
}

QString AgentHistory::rewind(int id) { return transition(id, false); }

QString AgentHistory::restore(int id) { return transition(id, true); }

QString AgentHistory::transition(int id, bool apply) {
    const int row = rowOf(id);
    if (row < 0) return QStringLiteral("There is no change %1.").arg(id);
    Change& change = changes_[static_cast<size_t>(row)];
    if (change.applied == apply) {
        return apply ? QStringLiteral("%1 is in effect already.").arg(quoted(change.text))
                     : QStringLiteral("%1 was rewound already.").arg(quoted(change.text));
    }
    if (const QString why = problem(*change.command, change.step, apply, &change); !why.isEmpty()) {
        return (apply ? QStringLiteral("%1 can't be restored: %2") : QStringLiteral("%1 can't be rewound: %2"))
            .arg(quoted(change.text), why);
    }
    if (apply) {
        change.command->redo();
    } else {
        change.command->undo();
    }
    change.applied = apply;
    change.step = ++clock_;
    const QModelIndex index = this->index(row);
    Q_EMIT dataChanged(index, index, {AppliedRole});
    Q_EMIT countChanged();
    markDirty();
    Q_EMIT changed(id);
    return {};
}

QString AgentHistory::rewindFrom(int id, QList<int>* rewound) {
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) {
        if (it->id < id || !it->applied) continue;
        const int changeId = it->id;
        if (const QString why = rewind(changeId); !why.isEmpty()) return why;
        if (rewound != nullptr) rewound->append(changeId);
    }
    return {};
}

QString AgentHistory::undoProblem() const {
    const int index = userStack_->index();
    if (index <= 0) return {};
    const QUndoCommand* command = userStack_->command(index - 1);
    const QString why = problem(*command, userSteps_.value(command), false, nullptr);
    if (why.isEmpty()) return {};
    return QStringLiteral("Can't undo %1: %2").arg(command->text(), why);
}

QString AgentHistory::redoProblem() const {
    const int index = userStack_->index();
    if (index >= userStack_->count()) return {};
    const QUndoCommand* command = userStack_->command(index);
    const QString why = problem(*command, userSteps_.value(command), true, nullptr);
    if (why.isEmpty()) return {};
    return QStringLiteral("Can't redo %1: %2").arg(command->text(), why);
}

QString AgentHistory::problem(const QUndoCommand& command, quint64 since, bool applying, const Change* self) const {
    QStringList regions;
    // (Worked out once, and only if something moved after it.)
    const auto touches = [&](const QUndoCommand& other) {
        if (regions.isEmpty()) regions = footprint(command, *project_);
        return overlaps(regions, footprint(other, *project_));
    };
    // Steps after it: what is in effect now (taking it back would undo them
    // too), and, putting it back, what was taken back too (it was made over them).
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) {
        const Change& other = *it;
        if (&other == self || other.step <= since || !(applying || other.applied)) continue;
        if (!touches(*other.command)) continue;
        if (self == nullptr) {
            return QStringLiteral("the agent's change %1 changed the same since. Rewind it first (View › Agent "
                                  "Changes).")
                .arg(quoted(other.text));
        }
        return QStringLiteral("the later change %1 changed the same; %2 it first.")
            .arg(quoted(other.text), other.applied ? QStringLiteral("rewind") : QStringLiteral("restore"));
    }
    if (self == nullptr) return {};  // (the user's steps go in order)
    const int index = userStack_->index();
    for (int i = userStack_->count() - 1; i >= 0; --i) {
        const QUndoCommand* other = userStack_->command(i);
        if (userSteps_.value(other) <= since || !(applying || i < index)) continue;
        if (touches(*other)) return QStringLiteral("you changed the same since (%1).").arg(other->text());
    }
    return {};
}

void AgentHistory::userIndexChanged(int index) {
    // Commands a push dropped (those undone, past the new one) are gone.
    QSet<const QUndoCommand*> present;
    for (int i = 0; i < userStack_->count(); ++i) present.insert(userStack_->command(i));
    for (auto it = userSteps_.begin(); it != userSteps_.end();) {
        it = present.contains(it.key()) ? std::next(it) : userSteps_.erase(it);
    }
    if (index > userIndex_) {  // done, or redone
        for (int i = userIndex_; i < index; ++i) userSteps_.insert(userStack_->command(i), ++clock_);
    } else if (index < userIndex_) {  // undone
        for (int i = std::min(userIndex_, static_cast<int>(userStack_->count())) - 1; i >= index; --i) {
            userSteps_.insert(userStack_->command(i), ++clock_);
        }
    } else if (index > 0) {  // the last one merged with another step (a drag)
        userSteps_.insert(userStack_->command(index - 1), ++clock_);
    }
    userIndex_ = index;
}

void AgentHistory::markDirty() {
    if (!clean_) return;
    clean_ = false;
    Q_EMIT cleanChanged();
}

void AgentHistory::setClean() {
    if (clean_) return;
    clean_ = true;
    Q_EMIT cleanChanged();
}

void AgentHistory::clear() {
    beginResetModel();
    changes_.clear();
    stack_->clear();
    endResetModel();
    userSteps_.clear();
    userIndex_ = userStack_->index();
    Q_EMIT countChanged();
    setClean();
}

int AgentHistory::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant AgentHistory::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const Change& change = changes_[static_cast<size_t>(index.row())];
    switch (role) {
    case IdRole: return change.id;
    case Qt::DisplayRole:
    case TextRole: return change.text;
    case ToolRole: return change.tool;
    case TimeRole: return change.time.toString(QStringLiteral("HH:mm:ss"));
    case AppliedRole: return change.applied;
    default: return {};
    }
}

QHash<int, QByteArray> AgentHistory::roleNames() const {
    return {{IdRole, "changeId"}, {TextRole, "text"}, {ToolRole, "tool"}, {TimeRole, "time"}, {AppliedRole, "applied"}};
}

}  // namespace sub::app
