// The key map (input/KeyMap): the profiles in the resources (every command
// they name is one, no key twice), the Ableton profile giving every command the
// keys the menus had before there were profiles, keys read as Qt reads them,
// the user's keys (a key given to a command taken from the one that had it,
// kept in QSettings apart from the profile's, reset), and a key press as a
// shortcut's text.

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

#include "TestSupport.h"
#include "input/KeyMap.h"

using sub::ui::KeyMap;

class TestUiKeyMap : public QObject {
    Q_OBJECT

    KeyMap& keys() { return *KeyMap::instance(); }

private Q_SLOTS:
    void initTestCase() { sub::app::test::prepareApplication(); }

    // Every test starts from the default profile, nothing the user's.
    void init() {
        QSettings().remove(QStringLiteral("keys"));
        keys().reload();
    }

    // The profiles: the default first; each names only commands there are,
    // with keys Qt reads, none of them a key of two commands.
    void profiles() {
        const QStringList ids = KeyMap::profileIds();
        QVERIFY(!ids.isEmpty());
        QCOMPARE(ids.front(), QStringLiteral("ableton"));
        QCOMPARE(KeyMap::profileName(QStringLiteral("ableton")), QStringLiteral("Ableton Live"));
        for (const QString& id : ids) {
            QStringList unknown;
            const QHash<QString, QStringList> bindings = KeyMap::profileBindings(id, &unknown);
            QVERIFY2(unknown.isEmpty(), qPrintable(id + QStringLiteral(": ") + unknown.join(QStringLiteral(", "))));
            QVERIFY(!bindings.isEmpty());
            QHash<QString, QString> owners;
            for (auto it = bindings.begin(); it != bindings.end(); ++it) {
                for (const QString& key : it.value()) {
                    QVERIFY2(!owners.contains(key), qPrintable(QStringLiteral("%1: %2 is %3's and %4's")
                                                                   .arg(id, key, owners.value(key), it.key())));
                    owners.insert(key, it.key());
                }
            }
        }
    }

    // Ableton's profile: the keys the menus had (the menu's first, then the others).
    void abletonProfile() {
        const QHash<QString, QStringList> expected{
            {"newProject", {"Ctrl+N"}},
            {"openProject", {"Ctrl+O"}},
            {"save", {"Ctrl+S"}},
            {"saveAs", {"Ctrl+Shift+S"}},
            {"exportAudio", {"Ctrl+Shift+R"}},
            {"quit", {"Ctrl+Q"}},
            {"undo", {"Ctrl+Z"}},
            {"redo", {"Ctrl+Y", "Ctrl+Shift+Z"}},
            {"cut", {"Ctrl+X"}},
            {"copy", {"Ctrl+C"}},
            {"paste", {"Ctrl+V"}},
            {"duplicate", {"Ctrl+D"}},
            {"rename", {"Ctrl+R"}},
            {"split", {"Ctrl+E"}},
            {"consolidate", {"Ctrl+J"}},
            {"reverseClips", {"R"}},
            {"toggleClipActivation", {"0"}},
            {"freeze", {"Ctrl+Shift+F"}},
            {"delete", {"Del", "Backspace"}},
            {"selectAll", {"Ctrl+A"}},
            {"soloSelectedTracks", {"S"}},
            {"playStop", {"Space"}},
            {"record", {"F9"}},
            {"goToStart", {"Home"}},
            {"loop", {"Ctrl+L"}},
            {"findInBrowser", {"Ctrl+F"}},
            {"insertAudioTrack", {"Ctrl+T"}},
            {"insertMidiTrack", {"Ctrl+Shift+T"}},
            {"insertReturnTrack", {"Ctrl+Alt+T"}},
            {"insertMidiClip", {"Ctrl+Shift+D", "Ctrl+Shift+M"}},
            {"groupTracks", {"Ctrl+G"}},
            {"ungroupTracks", {"Ctrl+Shift+G"}},
            {"browser", {"Ctrl+Alt+B"}},
            {"fileManager", {"Ctrl+Alt+F"}},
            {"agentChangesView", {"Ctrl+Alt+A"}},
            {"deviceView", {"Ctrl+Alt+L"}},
            {"clipView", {"Shift+Tab"}},
            {"findTrack", {"Q"}},
            {"automation", {"A"}},
            {"inOut", {"Ctrl+Alt+I"}},
            {"showHarmony", {"C"}},
            {"closePluginEditor", {"Ctrl+W"}},
            {"reopenPluginEditor", {"Ctrl+Shift+W"}},
            {"zoomIn", {"+", "=", "Ctrl++"}},
            {"zoomOut", {"-", "Ctrl+-"}},
            {"zoomToArrangement", {"Z"}},
            {"narrowGrid", {"Ctrl+1"}},
            {"widenGrid", {"Ctrl+2"}},
            {"snapToGrid", {"Ctrl+4"}},
            {"preferences", {"Ctrl+,"}},
            {"computerMidiKeyboard", {"M"}},
        };
        QCOMPARE(keys().profile(), QStringLiteral("ableton"));
        for (const KeyMap::Command& command : KeyMap::allCommands()) {
            const QString id = QLatin1String(command.id);
            QCOMPARE(keys().keys(id), expected.value(id));
            QCOMPARE(keys().primary(id), expected.value(id).value(0));
            QVERIFY(!keys().customized(id));
        }
        for (auto it = expected.begin(); it != expected.end(); ++it) QVERIFY2(KeyMap::command(it.key()), qPrintable(it.key()));
        QCOMPARE(keys().commandFor(QStringLiteral("ctrl+e")), QStringLiteral("split"));
        QCOMPARE(keys().commandFor(QStringLiteral("Ctrl+K")), QString());
        QCOMPARE(keys().commandName(QStringLiteral("split")), QStringLiteral("Split"));
    }

    // Keys as Qt reads a shortcut's text, kept in its portable text.
    void normalized() {
        QCOMPARE(KeyMap::normalized(QStringLiteral("ctrl+shift+s")), QStringLiteral("Ctrl+Shift+S"));
        QCOMPARE(KeyMap::normalized(QStringLiteral("Delete")), QStringLiteral("Del"));
        QCOMPARE(KeyMap::normalized(QStringLiteral("+")), QStringLiteral("+"));
        QCOMPARE(KeyMap::normalized(QStringLiteral("Ctrl++")), QStringLiteral("Ctrl++"));
        QCOMPARE(KeyMap::normalized(QStringLiteral(" Space ")), QStringLiteral("Space"));
        QCOMPARE(KeyMap::normalized(QStringLiteral("Ctrl+Nonsense")), QString());
        QCOMPARE(KeyMap::normalized(QString()), QString());
    }

    // A key press as a shortcut: modifiers alone are none; a shifted symbol is
    // the symbol; Backtab is Shift+Tab.
    void keyFor() {
        QCOMPARE(KeyMap::keyFor(Qt::Key_K, Qt::ControlModifier), QStringLiteral("Ctrl+K"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_A, Qt::ShiftModifier), QStringLiteral("Shift+A"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_Space, Qt::NoModifier), QStringLiteral("Space"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_Space, Qt::ShiftModifier), QStringLiteral("Shift+Space"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_F9, Qt::NoModifier), QStringLiteral("F9"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_Plus, Qt::ShiftModifier), QStringLiteral("+"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier), QStringLiteral("Ctrl++"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_Backtab, Qt::ShiftModifier), QStringLiteral("Shift+Tab"));
        QCOMPARE(KeyMap::keyFor(Qt::Key_Control, Qt::ControlModifier), QString());
        QCOMPARE(KeyMap::keyFor(Qt::Key_Shift, Qt::ShiftModifier), QString());
        QCOMPARE(KeyMap::keyFor(Qt::Key_Alt, Qt::AltModifier), QString());
        QCOMPARE(KeyMap::keyFor(Qt::Key_unknown, Qt::NoModifier), QString());
    }

    // A key given to a command is taken from the command that had it; both are
    // the user's then, kept apart from the profile's; reset, the profile's again.
    void assigning() {
        QSignalSpy changed(&keys(), &KeyMap::changed);
        QCOMPARE(keys().assign(QStringLiteral("split"), QStringLiteral("Ctrl+D")), QStringLiteral("duplicate"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(keys().keys(QStringLiteral("split")), (QStringList{"Ctrl+E", "Ctrl+D"}));
        QCOMPARE(keys().keys(QStringLiteral("duplicate")), QStringList());
        QVERIFY(keys().customized(QStringLiteral("split")));
        QVERIFY(keys().customized(QStringLiteral("duplicate")));
        QCOMPARE(keys().customizedCommands().size(), 2);
        QCOMPARE(keys().commandFor(QStringLiteral("Ctrl+D")), QStringLiteral("split"));

        // A key the command has already, or that isn't one: nothing.
        QCOMPARE(keys().assign(QStringLiteral("split"), QStringLiteral("Ctrl+D")), QString());
        QCOMPARE(keys().assign(QStringLiteral("split"), QStringLiteral("Nonsense")), QString());
        QCOMPARE(keys().assign(QStringLiteral("noSuchCommand"), QStringLiteral("Ctrl+K")), QString());
        QCOMPARE(changed.count(), 1);

        // Kept: the next run has them.
        const QJsonObject saved =
            QJsonDocument::fromJson(QSettings().value(QLatin1String(KeyMap::kOverridesKey)).toString().toUtf8()).object();
        QCOMPARE(saved.keys(), (QStringList{"duplicate", "split"}));
        keys().reload();
        QCOMPARE(keys().keys(QStringLiteral("split")), (QStringList{"Ctrl+E", "Ctrl+D"}));
        QCOMPARE(keys().keys(QStringLiteral("duplicate")), QStringList());

        // Reset: Split has the profile's key again; Duplicate stays without until it is reset too.
        keys().reset(QStringLiteral("split"));
        QCOMPARE(keys().keys(QStringLiteral("split")), QStringList{"Ctrl+E"});
        QVERIFY(!keys().customized(QStringLiteral("split")));
        QCOMPARE(keys().keys(QStringLiteral("duplicate")), QStringList());
        keys().reset(QStringLiteral("duplicate"));
        QCOMPARE(keys().keys(QStringLiteral("duplicate")), QStringList{"Ctrl+D"});
        QVERIFY(keys().customizedCommands().isEmpty());

        // A command without keys gets one; taken back, then given again as the
        // profile has it, it isn't the user's any more.
        QCOMPARE(keys().assign(QStringLiteral("flatten"), QStringLiteral("Ctrl+Shift+L")), QString());
        QCOMPARE(keys().primary(QStringLiteral("flatten")), QStringLiteral("Ctrl+Shift+L"));
        keys().unassign(QStringLiteral("split"), QStringLiteral("Ctrl+E"));
        QCOMPARE(keys().keys(QStringLiteral("split")), QStringList());
        QVERIFY(keys().customized(QStringLiteral("split")));
        keys().assign(QStringLiteral("split"), QStringLiteral("Ctrl+E"));
        QVERIFY(!keys().customized(QStringLiteral("split")));

        keys().resetAll();
        QVERIFY(keys().customizedCommands().isEmpty());
        QCOMPARE(keys().keys(QStringLiteral("flatten")), QStringList());
        QCOMPARE(QSettings().value(QLatin1String(KeyMap::kOverridesKey)).toString(), QStringLiteral("{}"));
    }

    // The user's keys as QSettings has them: they win over the profile's; what
    // isn't a command or a key is left out.
    void savedKeys() {
        QSettings().setValue(QLatin1String(KeyMap::kOverridesKey),
                             QStringLiteral(R"({"flatten": ["ctrl+e", "Nonsense"], "noSuchCommand": ["Ctrl+K"],)"
                                            R"( "loop": "Ctrl+Shift+L"})"));
        QSettings().setValue(QLatin1String(KeyMap::kProfileKey), QStringLiteral("noSuchProfile"));
        keys().reload();
        QCOMPARE(keys().profile(), QStringLiteral("ableton"));
        QCOMPARE(keys().keys(QStringLiteral("flatten")), QStringList{"Ctrl+E"});
        QCOMPARE(keys().keys(QStringLiteral("split")), QStringList());  // (the profile's Ctrl+E is Flatten's now)
        QVERIFY(!keys().customized(QStringLiteral("split")));
        QCOMPARE(keys().keys(QStringLiteral("loop")), QStringList{"Ctrl+Shift+L"});
        QCOMPARE(keys().customizedCommands().size(), 2);
        // An unknown profile isn't chosen.
        keys().setProfile(QStringLiteral("noSuchProfile"));
        QCOMPARE(keys().profile(), QStringLiteral("ableton"));
    }
};

QTEST_MAIN(TestUiKeyMap)
#include "test_ui_keymap.moc"
