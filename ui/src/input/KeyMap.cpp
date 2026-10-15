#include "input/KeyMap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSettings>

namespace sub::ui {

namespace {

constexpr const char* kProfilesFolder = ":/qt/qml/SUBstation/keymaps";

QString profilePath(const QString& id) { return QStringLiteral("%1/%2.json").arg(QLatin1String(kProfilesFolder), id); }

QJsonObject readProfile(const QString& id) {
    QFile file(profilePath(id));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// A command's keys as JSON has them (a list of keys, or one), normalized; the ones that aren't keys left out.
QStringList keysFrom(const QJsonValue& value) {
    QStringList keys;
    const QJsonArray list = value.isArray() ? value.toArray() : QJsonArray{value};
    for (const QJsonValue& each : list) {
        const QString key = KeyMap::normalized(each.toString());
        if (!key.isEmpty() && !keys.contains(key)) keys << key;
    }
    return keys;
}

}  // namespace

KeyMap::KeyMap() { reload(); }

KeyMap* KeyMap::instance() {
    static KeyMap* keyMap = new KeyMap;  // for the process's life, as QML's singletons are
    return keyMap;
}

KeyMap* KeyMap::create(QQmlEngine*, QJSEngine*) {
    KeyMap* keyMap = instance();
    QJSEngine::setObjectOwnership(keyMap, QJSEngine::CppOwnership);  // shared by every engine
    return keyMap;
}

const std::vector<KeyMap::Command>& KeyMap::allCommands() {
    static const std::vector<Command> commands{
        {"newProject", "File", "New Project"},
        {"openProject", "File", "Open…"},
        {"importLiveSet", "File", "Import Ableton Live Set…"},
        {"save", "File", "Save"},
        {"saveAs", "File", "Save As…"},
        {"saveAsTemplate", "File", "Save as Template"},
        {"clearTemplate", "File", "Clear Template"},
        {"exportAudio", "File", "Export Audio…"},
        {"quit", "File", "Quit"},
        {"undo", "Edit", "Undo"},
        {"redo", "Edit", "Redo"},
        {"cut", "Edit", "Cut"},
        {"copy", "Edit", "Copy"},
        {"paste", "Edit", "Paste"},
        {"duplicate", "Edit", "Duplicate"},
        {"rename", "Edit", "Rename"},
        {"split", "Edit", "Split"},
        {"consolidate", "Edit", "Consolidate"},
        {"reverseClips", "Edit", "Reverse Clips"},
        {"toggleClipActivation", "Edit", "Deactivate / Activate Clips"},
        {"freeze", "Edit", "Freeze / Unfreeze Track"},
        {"flatten", "Edit", "Flatten Track"},
        {"delete", "Edit", "Delete"},
        {"selectAll", "Edit", "Select All"},
        {"reEnableAutomation", "Edit", "Re-Enable Automation"},
        {"soloSelectedTracks", "Edit", "Solo Selected Tracks"},
        {"armSelectedTracks", "Edit", "Arm Selected Tracks"},
        {"playStop", "Edit", "Play / Stop"},
        {"record", "Edit", "Record"},
        {"goToStart", "Edit", "Go to Start"},
        {"loop", "Edit", "Loop"},
        {"findInBrowser", "Edit", "Find in Browser"},
        {"insertAudioTrack", "Create", "Insert Audio Track"},
        {"insertMidiTrack", "Create", "Insert MIDI Track"},
        {"insertReturnTrack", "Create", "Insert Return Track"},
        {"insertMidiClip", "Create", "Insert MIDI Clip"},
        {"groupTracks", "Create", "Group Tracks"},
        {"ungroupTracks", "Create", "Ungroup Tracks"},
        {"deleteSelectedTracks", "Create", "Delete Selected Tracks"},
        {"browser", "View", "Browser"},
        {"fileManager", "View", "File Manager"},
        {"agentChangesView", "View", "Agent Changes"},
        {"deviceView", "View", "Device View"},
        {"infoView", "View", "Info View"},
        {"clipView", "View", "Clip View"},
        {"findTrack", "View", "Find Track"},
        {"automation", "View", "Automation"},
        {"inOut", "View", "In/Out"},
        {"showHarmony", "View", "Chords and Key"},
        {"closePluginEditor", "View", "Close Plug-in Editor"},
        {"reopenPluginEditor", "View", "Reopen Closed Plug-in Editor"},
        {"zoomIn", "View", "Zoom In"},
        {"zoomOut", "View", "Zoom Out"},
        {"zoomToArrangement", "View", "Zoom to Arrangement"},
        {"narrowGrid", "View", "Narrow Grid"},
        {"widenGrid", "View", "Widen Grid"},
        {"snapToGrid", "View", "Snap to Grid"},
        {"preferences", "Options", "Preferences…"},
        {"rescanPlugins", "Options", "Rescan Plug-ins"},
        {"computerMidiKeyboard", "Options", "Computer MIDI Keyboard"},
        {"mcpServer", "Options", "MCP Server"},
        {"lockEnvelopes", "Options", "Lock Envelopes"},
        {"checkForUpdates", "Help", "Check for Updates…"},
        {"about", "Help", "About SUBstation"},
    };
    return commands;
}

const KeyMap::Command* KeyMap::command(const QString& id) {
    for (const Command& each : allCommands())
        if (id == QLatin1String(each.id)) return &each;
    return nullptr;
}

QStringList KeyMap::profileIds() {
    static const QStringList ids = [] {
        QStringList found;
        for (const QFileInfo& file : QDir(QLatin1String(kProfilesFolder)).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
            found << file.completeBaseName();
        const QString preferred = QLatin1String(kDefaultProfile);
        if (found.removeAll(preferred) > 0) found.prepend(preferred);
        return found;
    }();
    return ids;
}

QString KeyMap::profileName(const QString& id) {
    const QString name = readProfile(id).value(QLatin1String("name")).toString();
    return name.isEmpty() ? id : name;
}

QHash<QString, QStringList> KeyMap::profileBindings(const QString& id, QStringList* unknown) {
    QHash<QString, QStringList> bindings;
    const QJsonObject keys = readProfile(id).value(QLatin1String("bindings")).toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        if (!command(it.key())) {
            if (unknown) *unknown << it.key();
            continue;
        }
        bindings.insert(it.key(), keysFrom(it.value()));
    }
    return bindings;
}

QString KeyMap::normalized(const QString& key) {
    const QKeySequence sequence = QKeySequence::fromString(key.trimmed(), QKeySequence::PortableText);
    if (sequence.isEmpty()) return {};
    for (int i = 0; i < sequence.count(); ++i)
        if (sequence[i].key() == Qt::Key_unknown || sequence[i].key() == 0) return {};
    return sequence.toString(QKeySequence::PortableText);
}

void KeyMap::setProfile(const QString& id) {
    if (id == profile_ || !profileIds().contains(id)) return;
    profile_ = id;
    defaults_ = profileBindings(id);
    rebuild();
    save();
    Q_EMIT changed();
}

QString KeyMap::commandFor(const QString& key) const {
    const QString wanted = normalized(key);
    if (wanted.isEmpty()) return {};
    for (const Command& each : allCommands()) {
        const QString id = QLatin1String(each.id);
        if (bindings_.value(id).contains(wanted)) return id;
    }
    return {};
}

QString KeyMap::commandName(const QString& id) const {
    const Command* found = command(id);
    return found ? QString::fromUtf8(found->name) : QString();
}

QString KeyMap::assign(const QString& id, const QString& key) {
    const QString wanted = normalized(key);
    if (wanted.isEmpty() || !command(id) || keys(id).contains(wanted)) return {};
    const QString owner = commandFor(wanted);
    if (!owner.isEmpty()) {
        QStringList left = keys(owner);
        left.removeAll(wanted);
        setOverride(owner, left);
    }
    setOverride(id, keys(id) << wanted);
    rebuild();
    save();
    Q_EMIT changed();
    return owner;
}

void KeyMap::unassign(const QString& id, const QString& key) {
    const QString wanted = normalized(key);
    QStringList left = keys(id);
    if (wanted.isEmpty() || !left.removeAll(wanted)) return;
    setOverride(id, left);
    rebuild();
    save();
    Q_EMIT changed();
}

void KeyMap::reset(const QString& id) {
    if (!overrides_.remove(id)) return;
    rebuild();
    save();
    Q_EMIT changed();
}

void KeyMap::resetAll() {
    if (overrides_.isEmpty()) return;
    overrides_.clear();
    rebuild();
    save();
    Q_EMIT changed();
}

QString KeyMap::keyFor(int key, int modifiers) {
    switch (key) {
        case Qt::Key_Control:
        case Qt::Key_Shift:
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
        case Qt::Key_Meta:
        case Qt::Key_Super_L:
        case Qt::Key_Super_R:
        case Qt::Key_CapsLock:
        case Qt::Key_NumLock:
        case Qt::Key_ScrollLock:
        case Qt::Key_unknown:
        case 0:
            return {};
        default:
            break;
    }
    Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers) & (Qt::ControlModifier | Qt::ShiftModifier |
                                                                      Qt::AltModifier | Qt::MetaModifier |
                                                                      Qt::KeypadModifier);
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
        mods |= Qt::ShiftModifier;
    }
    // A shifted symbol is what Shift made of its key: the symbol is the shortcut.
    const bool symbol = key > Qt::Key_Space && key < 0x01000000 && !(key >= Qt::Key_A && key <= Qt::Key_Z);
    if (symbol) mods &= ~Qt::ShiftModifier;
    return normalized(QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText));
}

QObject* KeyMap::findAction(QObject* root, const QString& id) {
    if (!root || id.isEmpty()) return nullptr;
    for (QObject* object : root->findChildren<QObject*>(id))
        if (object->inherits("QQuickAction")) return object;
    return nullptr;
}

void KeyMap::reload() {
    QSettings settings;
    profile_ = settings.value(QLatin1String(kProfileKey)).toString();
    if (!profileIds().contains(profile_)) profile_ = QLatin1String(kDefaultProfile);
    defaults_ = profileBindings(profile_);
    overrides_.clear();
    const QJsonObject saved =
        QJsonDocument::fromJson(settings.value(QLatin1String(kOverridesKey)).toString().toUtf8()).object();
    for (auto it = saved.begin(); it != saved.end(); ++it)
        if (command(it.key())) overrides_.insert(it.key(), keysFrom(it.value()));
    rebuild();
    Q_EMIT changed();
}

QVariantList KeyMap::profilesForQml() const {
    QVariantList list;
    for (const QString& id : profileIds())
        list << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), profileName(id)}};
    return list;
}

QVariantList KeyMap::commandsForQml() const {
    QVariantList list;
    for (const Command& each : allCommands())
        list << QVariantMap{{QStringLiteral("id"), QLatin1String(each.id)},
                            {QStringLiteral("name"), QString::fromUtf8(each.name)},
                            {QStringLiteral("category"), QLatin1String(each.category)}};
    return list;
}

QVariantMap KeyMap::bindingsForQml() const {
    QVariantMap map;
    for (auto it = bindings_.begin(); it != bindings_.end(); ++it) {
        QVariantList keys;
        for (const QString& key : it.value()) keys << key;
        map.insert(it.key(), keys);
    }
    return map;
}

void KeyMap::setOverride(const QString& id, const QStringList& keys) {
    if (keys == defaults_.value(id))
        overrides_.remove(id);
    else
        overrides_.insert(id, keys);
}

void KeyMap::rebuild() {
    bindings_ = defaults_;
    for (auto it = overrides_.begin(); it != overrides_.end(); ++it) bindings_.insert(it.key(), it.value());
    // The user's keys win: a key the user gave a command leaves the profile's commands that had it.
    for (auto it = overrides_.begin(); it != overrides_.end(); ++it) {
        for (const QString& key : it.value()) {
            for (auto other = bindings_.begin(); other != bindings_.end(); ++other)
                if (other.key() != it.key() && !overrides_.contains(other.key())) other.value().removeAll(key);
        }
    }
}

void KeyMap::save() const {
    QJsonObject saved;
    for (auto it = overrides_.begin(); it != overrides_.end(); ++it)
        saved.insert(it.key(), QJsonArray::fromStringList(it.value()));
    QSettings settings;
    settings.setValue(QLatin1String(kProfileKey), profile_);
    settings.setValue(QLatin1String(kOverridesKey), QString::fromUtf8(QJsonDocument(saved).toJson(QJsonDocument::Compact)));
}

}  // namespace sub::ui
