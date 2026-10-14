#pragma once

// Which keys trigger which command: the window's menu Actions (their
// objectName is the command's id) read their shortcuts here, and so do the
// context menus' shortcut hints. A profile (keymaps/*.json in the resources:
// "ableton", the default) gives every command its keys; the user's own changes
// (Preferences › Keyboard) are kept apart from it, command by command, so a
// profile's later fixes still reach the commands the user left alone.
//
// Keys are QKeySequence's portable text ("Ctrl+Shift+S", "Del", "Space"): the
// first a command has is its Action's shortcut (the one the menus show), the
// others the window's Shortcuts that trigger it (Main.qml). A key belongs to
// one command at most: a key the user gives a command is taken from the
// command that had it.

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

class QJSEngine;
class QQmlEngine;

namespace sub::ui {

class KeyMap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // The profile the keys start from (its id: "ableton").
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY changed)
    // The profiles: [{id, name}].
    Q_PROPERTY(QVariantList profiles READ profilesForQml CONSTANT)
    // The commands, in the menus' order: [{id, name, category}].
    Q_PROPERTY(QVariantList commands READ commandsForQml CONSTANT)
    // Every command's keys: {id: [key, ...]} (none: [] or not there).
    Q_PROPERTY(QVariantMap bindings READ bindingsForQml NOTIFY changed)
    // The commands whose keys the user changed from the profile's.
    Q_PROPERTY(QStringList customizedCommands READ customizedCommands NOTIFY changed)

public:
    struct Command {
        const char* id;        // the Action's objectName
        const char* category;  // its menu
        const char* name;      // as the menu says it, without mnemonics
    };

    static constexpr const char* kDefaultProfile = "ableton";
    static constexpr const char* kProfileKey = "keys/profile";      // QSettings: the profile's id
    static constexpr const char* kOverridesKey = "keys/overrides";  // QSettings: the user's keys, as JSON {id: [keys]}

    static KeyMap* instance();
    static KeyMap* create(QQmlEngine*, QJSEngine*);

    // Every command there is.
    static const std::vector<Command>& allCommands();
    static const Command* command(const QString& id);
    // The profiles' ids, the default first.
    static QStringList profileIds();
    static QString profileName(const QString& id);
    // A profile's keys, as its file has them (normalized). `unknown`: the
    // commands it names that there aren't.
    static QHash<QString, QStringList> profileBindings(const QString& id, QStringList* unknown = nullptr);
    // A key's portable text, as the keys are kept ("ctrl+s" → "Ctrl+S"); "" if it isn't one.
    static QString normalized(const QString& key);

    QString profile() const { return profile_; }
    void setProfile(const QString& id);

    // The command's keys, its menu's first.
    QStringList keys(const QString& id) const { return bindings_.value(id); }
    Q_INVOKABLE QString primary(const QString& id) const { return keys(id).value(0); }
    // The command's key as a context menu hints at it (its primary, now).
    static QString hint(const char* id) { return instance()->primary(QLatin1String(id)); }
    // The command whose key this is ("" if none's).
    Q_INVOKABLE QString commandFor(const QString& key) const;
    // Whether the user changed the command's keys from the profile's.
    Q_INVOKABLE bool customized(const QString& id) const { return overrides_.contains(id); }
    Q_INVOKABLE QString commandName(const QString& id) const;
    QStringList customizedCommands() const { return overrides_.keys(); }

    // Gives the command one more key, taking it from the command that had it:
    // that command's id ("" if none had it, or for a key that isn't one).
    Q_INVOKABLE QString assign(const QString& id, const QString& key);
    // Takes the key from the command.
    Q_INVOKABLE void unassign(const QString& id, const QString& key);
    // The profile's keys again, for one command or all.
    Q_INVOKABLE void reset(const QString& id);
    Q_INVOKABLE void resetAll();

    // The key a key press is, as a shortcut ("Ctrl+K"), from QML's key event
    // (event.key, event.modifiers); "" for a modifier alone. A shifted
    // symbol is the symbol ("+", not "Shift++"), as shortcuts match it; Shift+Tab
    // is Shift+Tab (not Backtab).
    Q_INVOKABLE static QString keyFor(int key, int modifiers);

    // The window's Action for a command (in `root` or under it), or null.
    Q_INVOKABLE static QObject* findAction(QObject* root, const QString& id);

    // Reads the profile and the user's keys from QSettings again.
    void reload();

Q_SIGNALS:
    void changed();

private:
    KeyMap();

    QVariantList profilesForQml() const;
    QVariantList commandsForQml() const;
    QVariantMap bindingsForQml() const;

    void setOverride(const QString& id, const QStringList& keys);
    void rebuild();
    void save() const;

    QString profile_;
    QHash<QString, QStringList> defaults_;   // the profile's
    QHash<QString, QStringList> overrides_;  // the user's: a command's keys in place of the profile's
    QHash<QString, QStringList> bindings_;   // what applies
};

}  // namespace sub::ui
