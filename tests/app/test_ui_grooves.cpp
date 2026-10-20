// Edit › Grooves… (ui/qml/dialogs/GrooveDialog.qml), driven the way a user
// would: the project's grooves listed and chosen, a built-in one added from the
// Add menu, a step dragged late and its velocity up (one undo step each, the
// project's groove changing as the mouse moves), a double-click putting it back,
// lanes added for keys and removed, the cycle's bars, renaming, the default, and
// removing (the groove shown going). Runs on a display (xvfb here). With
// $SUBSTATION_UI_SCREENSHOTS set, it saves screenshots there.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <cmath>
#include <memory>
#include <vector>

#include "UiTestSupport.h"
#include "editor/ProjectEditor.h"
#include "model/Project.h"
#include "session/GrooveLibrary.h"

namespace test = sub::app::test;

namespace {

// The window (at the end of the file: moc skips what follows a raw string).
extern const char* const kWindow;

}  // namespace

class TestUiGrooves : public QObject {
    Q_OBJECT

    std::unique_ptr<test::UiSession> ui_;
    QQuickWindow* window_ = nullptr;
    QObject* dialog_ = nullptr;

    sub::app::Session& session() { return ui_->session(); }
    sub::app::Project& project() { return *session().project(); }
    sub::app::GrooveLibrary& grooves() { return *session().grooves(); }
    QUndoStack& undo() { return *session().undoStack(); }
    // The item named `name` (visible ones first), through the items the window shows (a Repeater's
    // aren't the QObject children of anything), then its QObjects.
    QQuickItem* item(const QString& name) {
        QQuickItem* hidden = nullptr;
        std::vector<QQuickItem*> todo{window_->contentItem()};
        while (!todo.empty()) {
            QQuickItem* at = todo.back();
            todo.pop_back();
            if (at->objectName() == name) {
                if (at->isVisible()) return at;
                hidden = hidden ? hidden : at;
            }
            for (QQuickItem* child : at->childItems()) todo.push_back(child);
        }
        return hidden ? hidden : window_->findChild<QQuickItem*>(name);
    }
    QQuickItem* item(const char* name) { return item(QString::fromLatin1(name)); }
    const sub::groove::Groove& groove(int index = 0) { return project().grooves()[size_t(index)].groove; }

    void openDialog() {
        QMetaObject::invokeMethod(dialog_, "open");
        QTRY_VERIFY(dialog_->property("opened").toBool());
    }

private Q_SLOTS:
    void initTestCase() {
        test::prepareApplication();
        if (!test::haveDisplay()) QSKIP("needs a display: Qt Quick's software renderer draws none of this geometry");
        sub::ui::setUpApplication();
        ui_ = std::make_unique<test::UiSession>();
        window_ = ui_->show(kWindow);
        QVERIFY(window_);
        dialog_ = window_->findChild<QObject*>(QStringLiteral("grooveDialog"));
        QVERIFY(dialog_);
    }

    void cleanupTestCase() { ui_.reset(); }

    void init() {
        QMetaObject::invokeMethod(dialog_, "close");
        QTRY_VERIFY(!dialog_->property("visible").toBool());
        project().clear();
        undo().clear();
    }

    void addingAndChoosing() {
        openDialog();
        QCOMPARE(dialog_->property("grooveId").toString(), QString());
        QVERIFY(!dialog_->property("hasGroove").toBool());
        // Add › Built-in › Triplet Shuffle 16: added, and shown.
        test::click(window_, test::centerOf(item("addGroove")));
        QObject* menu = window_->findChild<QObject*>(QStringLiteral("addMenu"));
        QTRY_VERIFY(menu->property("opened").toBool());
        QMetaObject::invokeMethod(window_->findChild<QObject*>(QStringLiteral("add_builtin:shuffle-16t")), "triggered");
        QCOMPARE(project().grooves().size(), size_t(1));
        const QString id = project().grooves().front().id;
        QCOMPARE(dialog_->property("grooveId").toString(), id);
        QCOMPARE(item("grooveName")->property("text").toString(), QStringLiteral("Triplet Shuffle 16"));
        test::screenshot(window_, QStringLiteral("groove-dialog"));
        QTRY_VERIFY(item("handle1"));

        // Another, then the first chosen again from the list.
        const QString other = grooves().addSwing(58.0, 2);
        QCOMPARE(project().grooves().size(), size_t(2));
        QTRY_VERIFY(item(QStringLiteral("grooveItem_") + id));
        test::click(window_, test::centerOf(item(QStringLiteral("grooveItem_") + id)));
        QCOMPARE(dialog_->property("grooveId").toString(), id);

        // Made the default; removed (the dialog shows what is left).
        test::click(window_, test::centerOf(item("makeDefault")));
        QCOMPARE(grooves().defaultGrooveId(), id);
        test::click(window_, test::centerOf(item("removeGroove")));
        QCOMPARE(project().grooves().size(), size_t(1));
        QTRY_COMPARE(dialog_->property("grooveId").toString(), other);
        QVERIFY(grooves().defaultGrooveId().isEmpty());  // (the default keeps its own copy)
        QCOMPARE(grooves().defaultKind(), QStringLiteral("own"));
    }

    void stepsAreDraggedLateAndLouder() {
        const QString id = grooves().addSwing(50.0, 2);  // straight 16ths
        openDialog();
        dialog_->setProperty("grooveId", id);
        QQuickItem* area = nullptr;
        QTRY_VERIFY((area = item("timingArea0")));
        QQuickItem* handle = item("handle2");
        QTRY_VERIFY(handle && handle->width() > 0);
        const double stepWidth = (area->width() - 2) / 16;

        // Dragged a quarter of a step late: one undo step, the project's groove following the mouse.
        const int depth = undo().index();
        const QPoint from = test::centerOf(handle);
        test::press(window_, from);
        test::moveTo(window_, from + QPoint(int(stepWidth * 0.125), 0));
        QVERIFY(groove().lanes[0].steps[2].timing > 0.05);
        test::moveTo(window_, from + QPoint(int(stepWidth * 0.25), 0));
        test::release(window_, from + QPoint(int(stepWidth * 0.25), 0));
        QVERIFY(std::abs(groove().lanes[0].steps[2].timing - 0.25) < 1.5 / stepWidth);
        QCOMPARE(undo().index(), depth + 1);
        QCOMPARE(groove().lanes[0].steps[1].timing, 0.0);
        // The handle went with it (the lane drawn again, the drag not broken).
        QTRY_VERIFY(std::abs(test::centerOf(item("handle2")).x() - (from.x() + stepWidth * 0.25)) < 2);
        // Double-click: back on the grid.
        test::doubleClick(window_, test::centerOf(item("handle2")));
        QCOMPARE(groove().lanes[0].steps[2].timing, 0.0);

        // Its velocity dragged up, below.
        QQuickItem* velocity = item("velocityArea0");
        const QPoint bottom = test::at(velocity, QPointF(1 + 2.5 * stepWidth, velocity->height() / 2));
        test::drag(window_, bottom, bottom - QPoint(0, int(velocity->height() / 4)));
        QVERIFY(groove().lanes[0].steps[2].velocity > 0.3);
        QCOMPARE(groove().lanes[0].steps[3].velocity, 0.0);
        undo().undo();
        QCOMPARE(groove().lanes[0].steps[2].velocity, 0.0);
    }

    void lanesAndBars() {
        const QString id = grooves().addSwing(58.0, 2);
        openDialog();
        dialog_->setProperty("grooveId", id);
        // A lane of its own for the hats (42 at first), then for the snare.
        QTRY_VERIFY(item("addLane"));
        test::click(window_, test::centerOf(item("addLane")));
        QCOMPARE(groove().lanes.size(), size_t(2));
        QCOMPARE(groove().lanes[1].keys, std::vector<int>{42});
        QCOMPARE(groove().lanes[1].steps, groove().lanes[0].steps);
        QTRY_VERIFY(item("laneKeys1"));
        grooves().setLaneKeys(id, 1, QVariantList{38, 40});
        QCOMPARE(groove().lanes[1].keys, (std::vector<int>{38, 40}));
        QTRY_COMPARE(item("laneKeys1")->property("text").toString(), QStringLiteral("38 40"));
        // Straight again; removed.
        test::click(window_, test::centerOf(item("resetLane1")));
        QCOMPARE(groove().lanes[1].steps[1].timing, 0.0);
        QVERIFY(groove().lanes[0].steps[1].timing > 0.1);
        test::click(window_, test::centerOf(item("removeLane1")));
        QCOMPARE(groove().lanes.size(), size_t(1));

        // Two bars: the steps repeated.
        QMetaObject::invokeMethod(item("grooveBars"), "activated", Q_ARG(int, 1));
        QCOMPARE(groove().bars, 2);
        QCOMPARE(groove().lanes[0].steps.size(), size_t(32));
        QCOMPARE(groove().lanes[0].steps[17].timing, groove().lanes[0].steps[1].timing);
        QTRY_VERIFY(item("handle31"));

        // Renamed.
        QQuickItem* name = item("grooveName");
        name->setProperty("text", QStringLiteral("Hats"));
        QMetaObject::invokeMethod(name, "editingFinished");
        QCOMPARE(project().grooves().front().name, QStringLiteral("Hats"));
    }
};

namespace {

const char* const kWindow = R"(
import QtQuick
import QtQuick.Controls
import SUBstation

ApplicationWindow {
    width: 980
    height: 620

    GrooveDialog {
        objectName: "grooveDialog"
    }
}
)";

}  // namespace

QTEST_MAIN(TestUiGrooves)
#include "test_ui_grooves.moc"
