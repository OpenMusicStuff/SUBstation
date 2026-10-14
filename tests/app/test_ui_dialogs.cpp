// The dialogs (ui/qml/dialogs) on a real session: Preferences' pages driving
// their controllers (audio threads, MIDI inputs, plug-in folders), Export
// Audio's choices, the render progress (modal, its label and bar, Cancel taking
// no focus, Esc cancelling, "Cancelling…"), and the message boxes' buttons and
// keys. Runs on a display (xvfb here). With $SUBSTATION_SCREENS set, it saves
// screenshots there.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QUndoStack>

#include <memory>
#include <utility>
#include <vector>

#include "UiTestSupport.h"
#include "audio/AudioSettings.h"
#include "audio/EngineBridge.h"
#include "editor/ProjectEditor.h"
#include "input/KeyMap.h"
#include "mcp/McpServer.h"
#include "model/Project.h"
#include "plugins/PluginIndex.h"
#include "session/AudioPreferences.h"
#include "session/MidiPreferences.h"
#include "session/PluginHostingPreferences.h"
#include "session/RenderProgress.h"
#include "session/Selection.h"
#include "theme/Theme.h"
#include "update/Updater.h"

namespace test = sub::app::test;

namespace {

// The window (at the end of the file: moc skips what follows a raw string).
extern const char* const kWindow;

// Copies a folder and everything in it.
bool copyTree(const QString& from, const QString& to) {
    QDir().mkpath(to);
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = to + source.mid(from.size());
        if (QFileInfo(source).isDir()) {
            QDir().mkpath(target);
        } else {
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (!QFile::copy(source, target)) return false;
            QFile::setPermissions(target, QFile::permissions(source));
        }
    }
    return true;
}

}  // namespace

class TestUiDialogs : public QObject {
    Q_OBJECT

    std::unique_ptr<test::TempDir> dir_;
    std::unique_ptr<test::UiSession> ui_;
    QQuickWindow* window_ = nullptr;
    QList<QQmlError> warnings_;

    sub::app::Session& session() { return ui_->session(); }
    sub::app::Project& project() { return *session().project(); }
    sub::app::RenderProgress& render() { return *session().render(); }
    QString vst3() const { return dir_->path(QStringLiteral("VST3")); }

    QObject* object(const QString& name) { return window_->findChild<QObject*>(name); }
    // An item shown in the window by its name (popups' contents and delegates too).
    static QQuickItem* shownItem(QQuickItem* root, const QString& name) {
        if (root->objectName() == name && root->isVisible()) return root;
        for (QQuickItem* child : root->childItems())
            if (QQuickItem* found = shownItem(child, name)) return found;
        return nullptr;
    }
    QQuickItem* shown(const QString& name) { return shownItem(window_->contentItem(), name); }
    bool visible(const QString& dialog) { return object(dialog)->property("visible").toBool(); }
    // Opened, and inside the window (a click on what lies outside it is lost).
    void open(const QString& dialog) {
        QMetaObject::invokeMethod(object(dialog), "open");
        QTRY_VERIFY(visible(dialog));
        auto* popup = object(dialog);
        const QRectF shown(popup->property("x").toReal(), popup->property("y").toReal(),
                           popup->property("width").toReal(), popup->property("height").toReal());
        QVERIFY2(QRectF(QPointF(), window_->size()).contains(shown),
                 qPrintable(QStringLiteral("%1 is %2 x %3 at %4, %5 in a %6 x %7 window")
                                .arg(dialog)
                                .arg(shown.width())
                                .arg(shown.height())
                                .arg(shown.x())
                                .arg(shown.y())
                                .arg(window_->width())
                                .arg(window_->height())));
    }
    void close(const QString& dialog) {
        QMetaObject::invokeMethod(object(dialog), "close");
        QTRY_VERIFY(!visible(dialog));
    }
    void choose(const QString& combo, int index) {
        QQuickItem* box = shown(combo);
        QVERIFY2(box, qPrintable(combo));
        QVERIFY(QMetaObject::invokeMethod(box, "activated", Q_ARG(int, index)));
    }
    QString comboText(const QString& combo) { return shown(combo)->property("displayText").toString(); }
    int indexOfLabel(const QVariantList& choices, const QString& label) {
        for (int i = 0; i < choices.size(); ++i)
            if (choices[i].toMap().value(QStringLiteral("label")).toString() == label) return i;
        return -1;
    }
    QString audioTrack(const QString& name, double startBeat = 0.0) {
        std::vector<float> samples(size_t(2 * test::kSampleRate), 0.5f);
        const QString path = test::writeWav(dir_->path(name + QStringLiteral(".wav")), samples, 2);
        session().bridge()->requestSource(path);
        const sub::app::ClipRefs refs =
            session().editor()->addClips(QString(), startBeat, {{path, 1.0}}, int(project().tracks().size()));
        return refs.isEmpty() ? QString() : refs.front().trackId;
    }

private Q_SLOTS:
    void initTestCase() {
        test::prepareApplication();
        if (!test::haveDisplay()) QSKIP("needs a display: Qt Quick's software renderer draws none of this geometry");
        dir_ = std::make_unique<test::TempDir>();
        QDir().mkpath(vst3());
        qputenv("SUBSTATION_VST3_PATH", vst3().toUtf8());  // the standard folder, here
        QDir().mkpath(dir_->path(QStringLiteral("place")));
        QSettings().setValue(QStringLiteral("browser/places"), QStringList{dir_->path(QStringLiteral("place"))});
        // The updates' feed: a release newer than any build.
        QFile releases(dir_->path(QStringLiteral("releases.json")));
        QVERIFY(releases.open(QIODevice::WriteOnly));
        releases.write("[{\"tag_name\": \"v99.0.0\", \"name\": \"SUBstation 99.0.0\", \"draft\": false, "
                       "\"html_url\": \"https://example.invalid/releases/v99.0.0\", \"assets\": []}]");
        releases.close();
        qputenv("SUBSTATION_UPDATE_FEED", QUrl::fromLocalFile(releases.fileName()).toString().toUtf8());
        sub::ui::setUpApplication();
        ui_ = std::make_unique<test::UiSession>();
        connect(&ui_->qml(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& list) { warnings_ += list; });
        window_ = ui_->show(kWindow);
        QVERIFY(window_);
    }

    void cleanupTestCase() { ui_.reset(); }

    void init() { session().newProject(); }

    void cleanup() {
        const QList<QQmlError> warnings = std::exchange(warnings_, {});
        for (const QQmlError& warning : warnings) qWarning() << warning.toString();
        QVERIFY(warnings.isEmpty());
    }

    // Preferences › Audio: the device's choices, Audio Threads applied at
    // once and kept (the default unless chosen); the status.
    void audioPage() {
        sub::app::EngineBridge& bridge = *session().bridge();
        const int defaultThreads = sub::app::EngineBridge::defaultAudioThreads();
        QCOMPARE(sub::app::audioThreads(), 0);
        open(QStringLiteral("preferencesDialog"));
        QCOMPARE(object(QStringLiteral("preferencesDialog"))->property("title").toString(), QStringLiteral("Preferences"));
        sub::app::AudioPreferences& prefs = *session().audioPreferences();
        QVERIFY(!prefs.driverChoices().isEmpty());
        QVERIFY(shown(QStringLiteral("device"))->property("count").toInt() >= 1);
        QCOMPARE(comboText(QStringLiteral("driver")), prefs.driverChoices()[prefs.driverIndex()].toMap().value(QStringLiteral("label")).toString());
        QVERIFY(!shown(QStringLiteral("audioStatus"))->property("text").toString().isEmpty());
        test::screenshot(window_, QStringLiteral("preferences-audio"));

        const QVariantList threads = prefs.threadChoices();
        QCOMPARE(threads[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("1 (off)"));
        QVERIFY(comboText(QStringLiteral("threads")).contains(QStringLiteral("(default)")));
        const int chosen = defaultThreads != 1 ? 1 : 2;
        choose(QStringLiteral("threads"), chosen - 1);
        QCOMPARE(bridge.audioThreads(), chosen);
        QCOMPARE(sub::app::audioThreads(), chosen);
        QCOMPARE(comboText(QStringLiteral("threads")), threads[chosen - 1].toMap().value(QStringLiteral("label")).toString());
        choose(QStringLiteral("threads"), defaultThreads - 1);
        QCOMPARE(bridge.audioThreads(), defaultThreads);
        QCOMPARE(sub::app::audioThreads(), 0);  // back to the default: not pinned

        // What only a driver of the other kind shows: Output Channels and Hardware
        // Setup for ASIO, Exclusive mode for WASAPI.
        QCOMPARE(shown(QStringLiteral("exclusive")) != nullptr, prefs.exclusiveVisible());
        QCOMPARE(shown(QStringLiteral("outputs")) != nullptr, prefs.outputsVisible());
        QCOMPARE(shown(QStringLiteral("controlPanel")) != nullptr, prefs.controlPanelVisible());
        close(QStringLiteral("preferencesDialog"));
    }

    // Preferences › Look and Feel: the themes; one chosen applies at once (the
    // dialog repaints in it) and is kept.
    void lookAndFeelPage() {
        open(QStringLiteral("preferencesDialog"));
        object(QStringLiteral("preferencesDialog"))->setProperty("currentPage", 3);
        QQuickItem* box = nullptr;
        QTRY_VERIFY((box = shown(QStringLiteral("theme"))) != nullptr);
        QCOMPARE(box->property("count").toInt(), sub::ui::Theme::names().size());
        QCOMPARE(comboText(QStringLiteral("theme")), QStringLiteral("Default"));
        // Below the box: the page, on the dialog's background.
        const QPointF below = box->mapToScene(QPointF(box->width() / 2, box->height() + 60));
        for (int i : {1, 2, 3, 0}) {
            choose(QStringLiteral("theme"), i);
            const QString name = sub::ui::Theme::names()[i];
            QCOMPARE(sub::ui::Theme::name(), name);
            QCOMPARE(comboText(QStringLiteral("theme")), name);
            QCOMPARE(QSettings().value(QLatin1String(sub::ui::Theme::kSettingsKey)).toString(), name);
            QTRY_COMPARE(window_->grabWindow().pixelColor((below * window_->effectiveDevicePixelRatio()).toPoint()),
                         sub::ui::Theme::window());
            test::screenshot(window_, QStringLiteral("preferences-look-and-feel-") + name.toLower());
        }
        close(QStringLiteral("preferencesDialog"));
    }

    // Preferences › Keyboard (KeyMap): the profile, every command with its
    // keys, found by a search. + then a key pressed gives the command that key,
    // taken from the command that had it (the page says so); a modifier alone
    // is no key; Esc cancels, the dialog stays; while it waits, keys are never
    // the window's shortcuts. A key's × takes it back; Reset, Reset All.
    void keyboardPage() {
        sub::ui::KeyMap& keyMap = *sub::ui::KeyMap::instance();
        keyMap.resetAll();
        open(QStringLiteral("preferencesDialog"));
        object(QStringLiteral("preferencesDialog"))->setProperty("currentPage", 4);
        auto* page = object(QStringLiteral("preferencesDialog"))->property("keyboardPage").value<QQuickItem*>();
        QVERIFY(page);
        QQuickItem* list = nullptr;
        QTRY_VERIFY((list = shown(QStringLiteral("keyCommands"))) != nullptr);
        QCOMPARE(list->property("count").toInt(), int(sub::ui::KeyMap::allCommands().size()));
        QCOMPARE(comboText(QStringLiteral("keyProfile")), QStringLiteral("Ableton Live"));
        test::screenshot(window_, QStringLiteral("preferences-keyboard"));

        QQuickItem* search = shown(QStringLiteral("keySearch"));
        QVERIFY(search);
        search->setProperty("text", QStringLiteral("split"));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        search->setProperty("text", QStringLiteral("ctrl+shift+t"));  // (by its key)
        QTRY_COMPARE(list->property("count").toInt(), 1);
        search->setProperty("text", QStringLiteral("split"));
        QTRY_COMPARE(list->property("count").toInt(), 1);

        const int spaces = window_->property("spaces").toInt();
        auto record = [&] {
            QQuickItem* add = nullptr;
            QTRY_VERIFY((add = shown(QStringLiteral("addKey_split"))) != nullptr);
            test::click(window_, test::centerOf(add));
            QTRY_COMPARE(page->property("recording").toString(), QStringLiteral("split"));
            QTRY_VERIFY(shown(QStringLiteral("keyRecorder"))->hasActiveFocus());
        };
        record();
        QTest::keyClick(window_, Qt::Key_Control, Qt::ControlModifier);  // a modifier alone: still waiting
        QCOMPARE(page->property("recording").toString(), QStringLiteral("split"));
        QTest::keyClick(window_, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(page->property("recording").toString(), QString());
        QCOMPARE(keyMap.keys(QStringLiteral("split")), (QStringList{"Ctrl+E", "Ctrl+D"}));
        QCOMPARE(keyMap.keys(QStringLiteral("duplicate")), QStringList());
        QQuickItem* message = nullptr;
        QTRY_VERIFY((message = shown(QStringLiteral("keyMessage"))) != nullptr);
        QVERIFY(message->property("text").toString().contains(QStringLiteral("Duplicate")));
        QTRY_VERIFY(shown(QStringLiteral("resetKeys_split")) != nullptr);

        // Esc: never mind (the dialog stays).
        record();
        QTest::keyClick(window_, Qt::Key_Escape);
        QCOMPARE(page->property("recording").toString(), QString());
        QVERIFY(visible(QStringLiteral("preferencesDialog")));
        QCOMPARE(keyMap.keys(QStringLiteral("split")), (QStringList{"Ctrl+E", "Ctrl+D"}));

        // Space while waiting: the command's key, not the window's play / stop.
        record();
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(keyMap.keys(QStringLiteral("split")), (QStringList{"Ctrl+E", "Ctrl+D", "Space"}));
        QCOMPARE(keyMap.keys(QStringLiteral("playStop")), QStringList());
        QCOMPARE(window_->property("spaces").toInt(), spaces);

        // A key's ×: taken back.
        QQuickItem* remove = nullptr;
        QTRY_VERIFY((remove = shown(QStringLiteral("key_split_Ctrl+D"))) != nullptr);
        test::click(window_, test::centerOf(remove));
        QTRY_COMPARE(keyMap.keys(QStringLiteral("split")), (QStringList{"Ctrl+E", "Space"}));

        // Reset: the profile's keys for Split; Reset All, for every command.
        QQuickItem* reset = shown(QStringLiteral("resetKeys_split"));
        QVERIFY(reset);
        test::click(window_, test::centerOf(reset));
        QTRY_COMPARE(keyMap.keys(QStringLiteral("split")), QStringList{"Ctrl+E"});
        QVERIFY(!keyMap.customizedCommands().isEmpty());  // (Duplicate's and Play / Stop's)
        test::click(window_, test::centerOf(shown(QStringLiteral("resetAllKeys"))));
        QTRY_VERIFY(keyMap.customizedCommands().isEmpty());
        QCOMPARE(keyMap.keys(QStringLiteral("playStop")), QStringList{"Space"});
        QCOMPARE(keyMap.keys(QStringLiteral("duplicate")), QStringList{"Ctrl+D"});

        search->setProperty("text", QString());
        close(QStringLiteral("preferencesDialog"));
    }

    // Preferences › Agents: the MCP server on and off, its port (applied at once,
    // kept), its address and the command adding it to Claude Code; its state.
    void agentsPage() {
        sub::app::McpServer& mcp = *session().mcp();
        const auto freePort = [] {
            QTcpServer probe;
            return probe.listen(QHostAddress::LocalHost) ? int(probe.serverPort()) : 0;
        };
        const int first = freePort();
        QVERIFY(first > 0);
        mcp.setPort(first);
        open(QStringLiteral("preferencesDialog"));
        object(QStringLiteral("preferencesDialog"))->setProperty("currentPage", 5);
        QQuickItem* enabled = nullptr;
        QTRY_VERIFY((enabled = shown(QStringLiteral("mcpEnabled"))) != nullptr);
        QVERIFY(!enabled->property("checked").toBool());
        QCOMPARE(shown(QStringLiteral("mcpStatus"))->property("text").toString(), QStringLiteral("Off."));
        QCOMPARE(shown(QStringLiteral("mcpUrl"))->property("text").toString(), mcp.url());
        QCOMPARE(shown(QStringLiteral("mcpCommand"))->property("text").toString(), mcp.claudeCodeCommand());

        QTest::mouseClick(window_, Qt::LeftButton, {}, enabled->mapToScene(QPointF(10, enabled->height() / 2)).toPoint());
        QTRY_VERIFY(mcp.running());
        QVERIFY(enabled->property("checked").toBool());
        QCOMPARE(shown(QStringLiteral("mcpStatus"))->property("text").toString(), QStringLiteral("Running."));
        test::screenshot(window_, QStringLiteral("preferences-agents"));

        // Another port: it moves there (and says so in the address).
        const int second = freePort();
        QQuickItem* port = shown(QStringLiteral("mcpPort"));
        port->setProperty("text", QString::number(second));
        QVERIFY(QMetaObject::invokeMethod(port, "editingFinished"));
        QCOMPARE(mcp.port(), second);
        QVERIFY(mcp.running());
        QVERIFY(shown(QStringLiteral("mcpUrl"))->property("text").toString().contains(QString::number(second)));
        // Not a port: as it was.
        port->setProperty("text", QStringLiteral("99999"));
        QVERIFY(QMetaObject::invokeMethod(port, "editingFinished"));
        QCOMPARE(mcp.port(), second);
        QCOMPARE(port->property("text").toString(), QString::number(second));

        QTest::mouseClick(window_, Qt::LeftButton, {}, enabled->mapToScene(QPointF(10, enabled->height() / 2)).toPoint());
        QTRY_VERIFY(!mcp.running());
        QVERIFY(!mcp.enabled());
        close(QStringLiteral("preferencesDialog"));
    }

    // Preferences › Updates: the channel's radio buttons, the two check boxes
    // (kept), what they add up to, Check Now finding a release and its page.
    void updatesPage() {
        sub::app::Updater& updates = *session().updates();
        updates.setChannel(QStringLiteral("stable"));
        updates.setAutoCheck(true);
        open(QStringLiteral("preferencesDialog"));
        object(QStringLiteral("preferencesDialog"))->setProperty("currentPage", 6);
        QQuickItem* nightly = nullptr;
        QTRY_VERIFY((nightly = shown(QStringLiteral("updateChannel_nightly"))) != nullptr);
        QVERIFY(shown(QStringLiteral("updateChannel_stable"))->property("checked").toBool());
        QVERIFY(!nightly->property("checked").toBool());
        QCOMPARE(shown(QStringLiteral("updatesSummaryTitle"))->property("text").toString(),
                 QStringLiteral("ⓘ  Selected: Stable"));

        QTest::mouseClick(window_, Qt::LeftButton, {}, nightly->mapToScene(QPointF(7, nightly->height() / 2)).toPoint());
        QTRY_COMPARE(updates.channel(), QStringLiteral("nightly"));
        QVERIFY(!shown(QStringLiteral("updateChannel_stable"))->property("checked").toBool());
        QCOMPARE(QSettings().value(sub::app::Updater::kChannelKey).toString(), QStringLiteral("nightly"));
        QCOMPARE(shown(QStringLiteral("updatesSummaryTitle"))->property("text").toString(),
                 QStringLiteral("ⓘ  Selected: Nightly"));
        QCOMPARE(shown(QStringLiteral("updatesSummary"))->property("text").toString(), updates.summary());

        QQuickItem* autoCheck = shown(QStringLiteral("updatesAutoCheck"));
        QVERIFY(autoCheck->property("checked").toBool());
        QTest::mouseClick(window_, Qt::LeftButton, {}, autoCheck->mapToScene(QPointF(7, autoCheck->height() / 2)).toPoint());
        QTRY_VERIFY(!updates.autoCheck());
        QVERIFY(!QSettings().value(sub::app::Updater::kAutoCheckKey).toBool());
        // (Installed by hand here, but on Windows.)
        QCOMPARE(shown(QStringLiteral("updatesAutoInstall"))->property("enabled").toBool(), updates.canInstall());

        QVERIFY(!shown(QStringLiteral("updatesReleasePage")));
        QQuickItem* check = shown(QStringLiteral("updatesCheckNow"));
        QTest::mouseClick(window_, Qt::LeftButton, {}, check->mapToScene(QPointF(check->width() / 2, check->height() / 2)).toPoint());
        QTRY_COMPARE(updates.state(), QStringLiteral("available"));
        QCOMPARE(updates.availableVersion(), QStringLiteral("99.0.0"));
        QCOMPARE(shown(QStringLiteral("updatesStatus"))->property("text").toString(), updates.status());
        QTRY_VERIFY(shown(QStringLiteral("updatesReleasePage")) != nullptr);
        QVERIFY(shown(QStringLiteral("updatesVersion"))->property("text").toString().contains(updates.currentVersion()));
        test::screenshot(window_, QStringLiteral("preferences-updates"));

        updates.setChannel(QStringLiteral("stable"));
        updates.setAutoCheck(true);
        close(QStringLiteral("preferencesDialog"));
    }

    // Preferences › MIDI: the inputs connected (whatever this computer has), Refresh.
    void midiPage() {
        open(QStringLiteral("preferencesDialog"));
        object(QStringLiteral("preferencesDialog"))->setProperty("currentPage", 1);
        QTRY_VERIFY(shown(QStringLiteral("midiInputs")));
        const QVariantList inputs = session().midiPreferences()->inputs();
        QCOMPARE(shown(QStringLiteral("midiInputs"))->property("count").toInt(), inputs.size());
        if (inputs.isEmpty())
            QCOMPARE(shown(QStringLiteral("midiStatus"))->property("text").toString(), QStringLiteral("No MIDI input is connected."));
        QSignalSpy changed(session().midiPreferences(), &sub::app::MidiPreferences::changed);
        test::click(window_, test::centerOf(shown(QStringLiteral("midiRefresh"))));
        QCOMPARE(changed.size(), 1);
        test::screenshot(window_, QStringLiteral("preferences-midi"));
        close(QStringLiteral("preferencesDialog"));
    }

    // Preferences › Plug-ins (test_plugin_folders_in_preferences): the
    // standard folders stay; a folder added is kept and its plug-ins found (in
    // folders inside it too); a rescan reads everything; a removed folder's
    // plug-ins go. Plug-in Hosting: a choice applies at once, and says what it means.
    void pluginsPage() {
        sub::app::PluginIndex& index = *session().plugins();
        open(QStringLiteral("preferencesDialog"));
        object(QStringLiteral("preferencesDialog"))->setProperty("currentPage", 2);
        QQuickItem* folders = nullptr;
        QTRY_VERIFY((folders = shown(QStringLiteral("pluginFolders"))) != nullptr);
        QObject* page = folders->parentItem()->parentItem();
        QCOMPARE(folders->property("count").toInt(), 1);
        QCOMPARE(index.folderModel()->data(index.folderModel()->index(0), sub::app::PluginFolderModel::DisplayRole).toString(),
                 QDir::toNativeSeparators(vst3()) + QStringLiteral("  (standard)"));
        folders->setProperty("currentIndex", 0);
        QVERIFY(!shown(QStringLiteral("removePluginFolder"))->isEnabled());  // the standard folders stay
        test::screenshot(window_, QStringLiteral("preferences-plugins"));

        sub::app::PluginHostingPreferences& hosting = *session().pluginHosting();
        QCOMPARE(comboText(QStringLiteral("pluginHosting")), QStringLiteral("Within SUBstation"));  // (the tests')
        QCOMPARE(shown(QStringLiteral("pluginHostingStatus"))->property("text").toString(),
                 QStringLiteral("Plug-ins run within SUBstation."));
        choose(QStringLiteral("pluginHosting"), 2);
        QCOMPARE(hosting.mode(), QStringLiteral("manufacturer"));
        QCOMPARE(session().bridge()->pluginHosting(), sub::app::PluginHostingMode::ByManufacturer);
        QCOMPARE(comboText(QStringLiteral("pluginHosting")), QStringLiteral("By Manufacturer"));
        QVERIFY(shown(QStringLiteral("pluginHostingDescription"))->property("text").toString().startsWith(
            QStringLiteral("A sandbox for each plug-in maker")));
        QCOMPARE(shown(QStringLiteral("pluginHostingStatus"))->property("text").toString(),
                 QStringLiteral("No plug-in runs in a sandbox now."));
        QVERIFY(shown(QStringLiteral("reloadPlugins"))->isEnabled());
        choose(QStringLiteral("pluginHosting"), 0);
        QCOMPARE(hosting.mode(), QStringLiteral("within"));
#ifndef SUBSTATION_TEST_PLUGINS_BUNDLE
        close(QStringLiteral("preferencesDialog"));
        QSKIP("the test plug-ins are not built");
#else
        const QString extra = dir_->path(QStringLiteral("More VST3"));
        QVERIFY(copyTree(QStringLiteral(SUBSTATION_TEST_PLUGINS_BUNDLE), extra + QStringLiteral("/Vendor/SUBTestPlugins.vst3")));
        QFile broken(extra + QStringLiteral("/Broken.vst3"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not a plug-in");
        broken.close();

        QVERIFY(QMetaObject::invokeMethod(page, "addFolder", Q_ARG(QVariant, extra)));
        QVERIFY(QMetaObject::invokeMethod(page, "addFolder", Q_ARG(QVariant, extra + QStringLiteral("/"))));  // the same: nothing changes
        QCOMPARE(index.customFolders(), QStringList{QDir::cleanPath(extra)});
        QCOMPARE(folders->property("count").toInt(), 2);
        QCOMPARE(folders->property("currentIndex").toInt(), 1);
        QVERIFY(shown(QStringLiteral("removePluginFolder"))->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!index.scanning(), 30000);
        QCOMPARE(index.pluginCount(), 6);
        QCOMPARE(index.failureCount(), 1);
        QCOMPARE(shown(QStringLiteral("scanStatus"))->property("text").toString(),
                 QStringLiteral("6 plug-ins found · 1 file could not be read"));
        QVERIFY(shown(QStringLiteral("rescanPlugins"))->isEnabled());
        test::screenshot(window_, QStringLiteral("preferences-plugins-found"));

        // (Seen as it starts: a click runs the events that come after it, and a rescan of
        // six plug-ins can have ended in them.)
        QQuickItem* rescan = shown(QStringLiteral("rescanPlugins"));
        bool started = false, disabledWhileScanning = false;
        const auto watch = connect(&index, &sub::app::PluginIndex::updated, this, [&] {
            if (!index.scanning() || started) return;
            started = true;
            disabledWhileScanning = !rescan->isEnabled();
        });
        test::click(window_, test::centerOf(rescan));
        QVERIFY(started);
        QVERIFY(disabledWhileScanning);
        disconnect(watch);
        QTRY_VERIFY_WITH_TIMEOUT(!index.scanning(), 30000);
        QCOMPARE(index.pluginCount(), 6);
        test::click(window_, test::centerOf(shown(QStringLiteral("removePluginFolder"))));
        QVERIFY(index.customFolders().isEmpty());
        QCOMPARE(folders->property("count").toInt(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!index.scanning(), 30000);
        QCOMPARE(index.pluginCount(), 0);
        QCOMPARE(index.failureCount(), 0);
        QCOMPARE(shown(QStringLiteral("scanStatus"))->property("text").toString(), QStringLiteral("0 plug-ins found"));
        close(QStringLiteral("preferencesDialog"));
#endif
    }

    // Export Audio: the range (the loop's only while it is on and has a
    // length, the time selection's only while there is one), the file type
    // (WAV at first) and the bit depth (24 at first) or, for an MP3, the
    // bitrate (320 at first); OK says which.
    void exportDialog() {
        open(QStringLiteral("exportDialog"));
        QCOMPARE(shown(QStringLiteral("exportRange"))->property("count").toInt(), 1);
        QCOMPARE(comboText(QStringLiteral("exportRange")), QStringLiteral("Arrangement (start to end of last clip)"));
        QCOMPARE(comboText(QStringLiteral("exportFileType")), QStringLiteral("WAV"));
        QCOMPARE(comboText(QStringLiteral("exportBitDepth")), QStringLiteral("24-bit"));
        QVERIFY(!shown(QStringLiteral("exportBitrate")));
        test::screenshot(window_, QStringLiteral("export-dialog"));
        close(QStringLiteral("exportDialog"));

        session().editor()->setLoop(true, 0.0, 4.0);
        open(QStringLiteral("exportDialog"));
        QCOMPARE(shown(QStringLiteral("exportRange"))->property("count").toInt(), 2);
        choose(QStringLiteral("exportRange"), 1);
        QCOMPARE(comboText(QStringLiteral("exportRange")), QStringLiteral("Loop region"));
        const int float32 = indexOfLabel(session().exportBitDepthChoices(), QStringLiteral("32-bit float"));
        choose(QStringLiteral("exportBitDepth"), float32);
        QSignalSpy chosen(object(QStringLiteral("exportDialog")), SIGNAL(exportChosen(QString, int, QString, int)));
        QMetaObject::invokeMethod(object(QStringLiteral("exportDialog")), "accept");
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen[0][0].toString(), QStringLiteral("loop"));
        QCOMPARE(chosen[0][1].toInt(), 32);
        QCOMPARE(chosen[0][2].toString(), QStringLiteral("wav"));
        QTRY_VERIFY(!visible(QStringLiteral("exportDialog")));

        // A time selection: its range is there too; MP3 shows the bitrate instead of the bit depth.
        session().selection()->setTimeRange(4.0, 12.0, {session().editor()->addAudioTrack()});  // (bars of 4 beats)
        open(QStringLiteral("exportDialog"));
        QCOMPARE(shown(QStringLiteral("exportRange"))->property("count").toInt(), 3);
        choose(QStringLiteral("exportRange"), 2);
        QCOMPARE(comboText(QStringLiteral("exportRange")), QStringLiteral("Time Selection (2.1.1 to 4.1.1)"));
        choose(QStringLiteral("exportFileType"), 1);
        QCOMPARE(comboText(QStringLiteral("exportFileType")), QStringLiteral("MP3"));
        QVERIFY(!shown(QStringLiteral("exportBitDepth")));
        QCOMPARE(comboText(QStringLiteral("exportBitrate")), QStringLiteral("320 kbps"));
        choose(QStringLiteral("exportBitrate"), indexOfLabel(session().exportBitrateChoices(), QStringLiteral("192 kbps")));
        test::screenshot(window_, QStringLiteral("export-dialog-mp3"));
        QMetaObject::invokeMethod(object(QStringLiteral("exportDialog")), "accept");
        QCOMPARE(chosen.size(), 2);
        QCOMPARE(chosen[1][0].toString(), QStringLiteral("selection"));
        QCOMPARE(chosen[1][2].toString(), QStringLiteral("mp3"));
        QCOMPARE(chosen[1][3].toInt(), 192);
        QTRY_VERIFY(!visible(QStringLiteral("exportDialog")));
        // The file type and bitrate stay as chosen.
        open(QStringLiteral("exportDialog"));
        QCOMPARE(comboText(QStringLiteral("exportFileType")), QStringLiteral("MP3"));
        QCOMPARE(comboText(QStringLiteral("exportBitrate")), QStringLiteral("192 kbps"));
        choose(QStringLiteral("exportFileType"), 0);
        close(QStringLiteral("exportDialog"));
        session().selection()->clear();
    }

    // A render's progress (test_the_window_goes_on_while_it_renders...): modal,
    // its title, label and bar; Cancel takes no focus; Esc cancels; "Cancelling…".
    void renderProgress() {
        audioTrack(QStringLiteral("long"), 10000.0);  // over an hour to render
        QVERIFY(session().exportAudio(dir_->path(QStringLiteral("mix.wav")), QStringLiteral("arrangement"), 24));
        QTRY_VERIFY(visible(QStringLiteral("renderDialog")));
        QObject* dialog = object(QStringLiteral("renderDialog"));
        QCOMPARE(dialog->property("title").toString(), QStringLiteral("Export Audio"));
        QVERIFY(dialog->property("modal").toBool());
        QTRY_COMPARE(shown(QStringLiteral("renderLabel"))->property("text").toString(), QStringLiteral("Exporting mix.wav…"));
        test::screenshot(window_, QStringLiteral("render-dialog"));
        // The window's shortcuts wait (the render is of the project as it was).
        const int spaces = window_->property("spaces").toInt();
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(window_->property("spaces").toInt(), spaces);
        QVERIFY(render().active() && !render().cancelled());  // (Space doesn't cancel either)
        QVERIFY(!shown(QStringLiteral("renderCancel"))->hasActiveFocus());
        QTest::keyClick(window_, Qt::Key_Escape);
        QVERIFY(render().cancelled());
        QTRY_VERIFY_WITH_TIMEOUT(!render().active(), 10000);
        QTRY_VERIFY(!visible(QStringLiteral("renderDialog")));
        QVERIFY(!QFileInfo::exists(dir_->path(QStringLiteral("mix.wav"))));
        QTest::keyClick(window_, Qt::Key_Space);
        QCOMPARE(window_->property("spaces").toInt(), spaces + 1);

        // Waiting for devices: a busy bar; cancelled: "Cancelling…", Cancel disabled.
        render().begin(QStringLiteral("Freeze Tracks"));
        render().setLabel(QStringLiteral("Loading plug-ins (3 to go)…"));
        render().setBusy(true);
        QTRY_VERIFY(visible(QStringLiteral("renderDialog")));
        QVERIFY(shown(QStringLiteral("renderBar"))->property("indeterminate").toBool());
        test::screenshot(window_, QStringLiteral("render-dialog-busy"));
        test::click(window_, test::centerOf(shown(QStringLiteral("renderCancel"))));
        QVERIFY(render().cancelled());
        QCOMPARE(shown(QStringLiteral("renderLabel"))->property("text").toString(), QStringLiteral("Cancelling…"));
        QVERIFY(!shown(QStringLiteral("renderCancel"))->isEnabled());
        render().setLabel(QStringLiteral("Freezing B (2 of 2)…"));  // (once cancelled, the label stays)
        QCOMPARE(shown(QStringLiteral("renderLabel"))->property("text").toString(), QStringLiteral("Cancelling…"));
        render().end();
        QTRY_VERIFY(!visible(QStringLiteral("renderDialog")));
    }

    // Freezing two tracks (test_freezing_shows_its_progress...): one dialog,
    // part by part; cancelled, nothing is frozen.
    void freezing() {
        const QString a = audioTrack(QStringLiteral("A"));
        const QString b = audioTrack(QStringLiteral("B"));
        session().selection()->selectTrack(a, true);
        session().selection()->selectTrack(b, false, sub::app::Selection::Mode::Toggle);
        QStringList labels;
        connect(&render(), &sub::app::RenderProgress::changed, this, [&] {
            if (render().active() && !labels.contains(render().label()) && !render().label().isEmpty())
                labels << render().label();
        });
        session().toggleFreeze();
        QTRY_VERIFY(visible(QStringLiteral("renderDialog")));
        QCOMPARE(object(QStringLiteral("renderDialog"))->property("title").toString(), QStringLiteral("Freeze Tracks"));
        QTRY_VERIFY_WITH_TIMEOUT(!render().active(), 30000);
        disconnect(&render(), nullptr, this, nullptr);
        QVERIFY(labels.contains(QStringLiteral("Freezing %1 (1 of 2)…").arg(project().track(a).name)));  // ("3 A")
        QVERIFY(labels.contains(QStringLiteral("Freezing %1 (2 of 2)…").arg(project().track(b).name)));
        QVERIFY(project().track(a).frozen.has_value() && project().track(b).frozen.has_value());
        QTRY_VERIFY(!visible(QStringLiteral("renderDialog")));
    }

    // The message boxes: their buttons; Return answers the default, Esc the escape.
    void messageBoxes() {
        QObject* unsaved = object(QStringLiteral("unsavedChangesDialog"));
        QSignalSpy answered(unsaved, SIGNAL(answered(QString)));
        open(QStringLiteral("unsavedChangesDialog"));
        QCOMPARE(unsaved->property("text").toString(), QStringLiteral("Save changes to the current project?"));
        for (const char* name : {"choice_save", "choice_discard", "choice_cancel"}) QVERIFY(shown(QString::fromLatin1(name)));
        QCOMPARE(shown(QStringLiteral("choice_discard"))->property("text").toString(), QStringLiteral("Discard"));
        test::click(window_, test::centerOf(shown(QStringLiteral("choice_discard"))));
        QCOMPARE(answered.size(), 1);
        QCOMPARE(answered[0][0].toString(), QStringLiteral("discard"));
        QTRY_VERIFY(!visible(QStringLiteral("unsavedChangesDialog")));
        open(QStringLiteral("unsavedChangesDialog"));
        QTest::keyClick(window_, Qt::Key_Escape);
        QCOMPARE(answered.last()[0].toString(), QStringLiteral("cancel"));
        open(QStringLiteral("unsavedChangesDialog"));
        QTest::keyClick(window_, Qt::Key_Return);
        QCOMPARE(answered.last()[0].toString(), QStringLiteral("save"));

        open(QStringLiteral("aboutDialog"));
        QCOMPARE(object(QStringLiteral("aboutDialog"))->property("title").toString(), session().aboutTitle());
        QVERIFY(shown(QStringLiteral("messageText"))->property("text").toString().startsWith(QStringLiteral("<b>SUBstation</b>")));
        test::screenshot(window_, QStringLiteral("about-dialog"));
        QTest::keyClick(window_, Qt::Key_Return);
        QTRY_VERIFY(!visible(QStringLiteral("aboutDialog")));
    }
};

namespace {

const char* const kWindow = R"(
import QtQuick
import QtQuick.Controls
import SUBstation

ApplicationWindow {
    width: 1000
    height: 700
    visible: true

    // The window's Space, counting (blocked while a modal dialog shows).
    property int spaces: 0
    Shortcut { sequences: ["Space"]; onActivated: spaces++ }

    PreferencesDialog {}
    ExportDialog {}
    RenderDialog {}
    AboutDialog {}
    UnsavedChangesDialog {}
}
)";

}  // namespace

QTEST_MAIN(TestUiDialogs)
#include "test_ui_dialogs.moc"
