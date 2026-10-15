// Drum Racks in the device view (DrumRackBody, DrumPad, DrumRack): sixteen
// pads, the lowest key at the bottom left, moved a row of keys at a time;
// audio files and clips from the arrangement dropped on a pad load onto its
// key (a pad with a Sampler, named after the sample), devices dropped on a pad
// with a chain go into it; a pad clicked shows its devices; its lock, mute and
// solo; its menu; the rack's similar-sound arrows. Driven in a window with a
// real session, with synthesized mouse and drag events. With
// SUBSTATION_UI_SCREENSHOTS set, screenshots go there.

#include <QMimeData>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>
#include <QUrl>

#include "DevicePanelTestSupport.h"
#include "TestSupport.h"
#include "browser/BrowserMime.h"
#include "devices/DeviceChainArea.h"
#include "editor/ProjectEditor.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/Project.h"
#include "model/SamplerState.h"
#include "session/DeviceSelection.h"
#include "session/Selection.h"

namespace test = sub::app::test;

class TestUiDevicePanelDrums : public QObject {
    Q_OBJECT

    test::PanelWindow ui_;
    std::unique_ptr<test::TempDir> dir_;

    sub::app::Session& session() { return ui_.session(); }
    sub::app::Project& project() { return *session().project(); }
    sub::app::ProjectEditor& editor() { return *session().editor(); }
    QUndoStack& undo() { return *session().undoStack(); }
    QQuickWindow* window() { return ui_.window(); }

    // A MIDI track with a Drum Rack, selected: (track, rack).
    std::pair<QString, QString> drums() {
        const QString track = editor().addMidiTrack(-1, {}, sub::app::kDrumRackKind);
        session().selection()->selectTrack(track);
        ui_.polish();
        return {track, project().track(track).devices.at(0).id};
    }
    QQuickItem* pad(const QString& rack, int note) {
        ui_.polish();
        QQuickItem* frame = ui_.frame(rack);
        return frame ? test::itemNamed(frame, QStringLiteral("pad_%1").arg(note)) : nullptr;
    }
    QQuickItem* bodyPart(const QString& rack, const QString& name) {
        ui_.polish();
        return test::itemNamed(ui_.frame(rack), name);
    }
    QString wav(const QString& name) {
        return test::writeWav(dir_->path(name), std::vector<float>(size_t(test::kSampleRate) / 4, 0.25f));
    }
    std::unique_ptr<QMimeData> filesMime(const QStringList& paths) {
        auto mime = std::make_unique<QMimeData>();
        QList<QUrl> urls;
        for (const QString& path : paths) urls << QUrl::fromLocalFile(path);
        mime->setUrls(urls);
        return mime;
    }
    const sub::app::Chain* padChain(const QString& track, const QString& rack, int note) {
        return sub::app::drumPad(project().device(track, rack), note);
    }

private Q_SLOTS:
    void initTestCase() {
        test::prepareApplication();
        if (!test::haveDisplay()) QSKIP("needs a display: Qt Quick's software renderer draws none of this geometry");
        sub::ui::setUpApplication();
        QVERIFY(ui_.open());
        dir_ = std::make_unique<test::TempDir>();
    }

    void cleanupTestCase() { ui_.close(); }

    void init() {
        if (ui_.menu()->property("visible").toBool()) QVERIFY(ui_.closeMenu());
        project().clear();
        undo().clear();
        ui_.messages.clear();
        ui_.area()->setContentX(0);
        test::moveTo(window(), QPoint(5, 5), {}, Qt::NoButton);
    }

    void sixteenPadsTheLowestAtTheBottomLeft() {
        const auto [track, rack] = drums();
        QQuickItem* frame = ui_.frame(rack);
        QVERIFY(frame);
        QVERIFY(frame->property("info").value<QObject*>()->property("isDrumRack").toBool());
        for (int note = 36; note < 52; ++note) QVERIFY2(pad(rack, note), qPrintable(QString::number(note)));
        QVERIFY(!pad(rack, 35) && !pad(rack, 52));
        const QPoint c1 = test::centerOf(pad(rack, 36)), d1s = test::centerOf(pad(rack, 39));
        const QPoint e1 = test::centerOf(pad(rack, 40)), d2s = test::centerOf(pad(rack, 51));
        QVERIFY(c1.y() == d1s.y() && c1.x() < d1s.x());  // a row of four, left to right
        QVERIFY(e1.y() < c1.y());                         // the next row above
        QVERIFY(d2s.y() < e1.y() && d2s.x() == d1s.x());
        QCOMPARE(test::part(pad(rack, 36), "nameLabel")->property("text").toString(), QString());
        // The arrows move them a row of keys.
        test::click(window(), test::centerOf(bodyPart(rack, QStringLiteral("padsUp"))));
        QVERIFY(pad(rack, 40) && pad(rack, 55) && !pad(rack, 36));
        test::click(window(), test::centerOf(bodyPart(rack, QStringLiteral("padsDown"))));
        QVERIFY(pad(rack, 36));
        ui_.screenshot(QStringLiteral("drum-rack-empty"));
    }

    void samplesDroppedOnPadsLoadOntoTheirKeys() {
        const auto [track, rack] = drums();
        const QString kick = wav(QStringLiteral("Kick.wav"));
        auto mime = filesMime({dir_->path(QStringLiteral("notes.txt")), kick});
        QVERIFY(test::dragEnter(window(), test::centerOf(pad(rack, 38)), mime.get()));
        QVERIFY(pad(rack, 38)->property("dropping").toBool());
        test::dragMove(window(), test::centerOf(pad(rack, 38)), mime.get());
        test::dropAt(window(), test::centerOf(pad(rack, 38)), mime.get());
        const sub::app::Chain* chain = padChain(track, rack, 38);
        QVERIFY(chain);
        QCOMPARE(chain->name, QStringLiteral("Kick"));
        QCOMPARE(chain->devices.size(), size_t(1));
        QCOMPARE(chain->devices[0].kind, sub::app::kSamplerKind);
        QCOMPARE(chain->devices[0].params.value(QStringLiteral("mode")), sub::app::kSamplerOneShot);
        QCOMPARE(sub::app::samplerState::sample(sub::app::deviceState::fromModel(chain->devices[0].state)), kick);
        QCOMPARE(undo().undoText(), QStringLiteral("Load Pad"));
        QVERIFY(!pad(rack, 38)->property("dropping").toBool());
        QCOMPARE(test::part(pad(rack, 38), "nameLabel")->property("text").toString(), QStringLiteral("Kick"));
        // Its Sampler shows beside the rack.
        QCOMPARE(session().deviceSelection()->shownChain(rack), chain->id);
        QVERIFY(pad(rack, 38)->property("modelData").toMap().value(QStringLiteral("selected")).toBool());
        QVERIFY(ui_.frame(chain->devices[0].id));
        // A clip dragged out of the arrangement: its part of its file.
        const QString loop = wav(QStringLiteral("Loop.wav"));
        auto clip = sub::app::clipMimeData(loop, 0.05, 0.1, 0.25);
        test::dragAndDrop(window(), test::centerOf(pad(rack, 40)), clip.get());
        const sub::app::Chain* part = padChain(track, rack, 40);
        QVERIFY(part);
        QCOMPARE(part->devices[0].params.value(QStringLiteral("start")), 20.0);
        QCOMPARE(part->devices[0].params.value(QStringLiteral("end")), 60.0);
        // A device dropped on a pad with a chain goes into it; on an empty one, beside the rack.
        auto utility = test::deviceKindsMime({QStringLiteral("utility")});
        test::dragAndDrop(window(), test::centerOf(pad(rack, 38)), utility.get());
        QCOMPARE(padChain(track, rack, 38)->devices.size(), size_t(2));
        ui_.screenshot(QStringLiteral("drum-rack-pads"));
    }

    void aPadsControlsAndMenu() {
        const auto [track, rack] = drums();
        editor().loadDrumPad(track, rack, 36, wav(QStringLiteral("Kick.wav")));
        editor().loadDrumPad(track, rack, 37, wav(QStringLiteral("Snare.wav")));
        QQuickItem* kick = pad(rack, 36);
        // The lock at its top right corner.
        QQuickItem* lock = test::part(kick, "lock");
        const QRectF lockRect = lock->mapRectToItem(kick, QRectF(0, 0, lock->width(), lock->height()));
        QVERIFY(lockRect.right() <= kick->width() && lockRect.right() >= kick->width() - 4);
        QVERIFY(lockRect.top() >= 0 && lockRect.top() <= 4);
        test::click(window(), test::centerOf(lock));
        QVERIFY(padChain(track, rack, 36)->locked);
        QCOMPARE(bodyPart(rack, QStringLiteral("lockedCount"))->property("text").toString(), QStringLiteral("1 locked"));
        test::click(window(), test::centerOf(test::part(pad(rack, 36), "activator")));
        QVERIFY(padChain(track, rack, 36)->mute);
        test::click(window(), test::centerOf(test::part(pad(rack, 37), "solo")));
        QVERIFY(padChain(track, rack, 37)->solo);
        // A click on the snare's pad shows its devices.
        test::click(window(), test::centerOf(test::part(pad(rack, 37), "nameLabel")));
        QCOMPARE(session().deviceSelection()->shownChain(rack), padChain(track, rack, 37)->id);
        // Its menu.
        ui_.rightClick(test::centerOf(pad(rack, 36)));
        QVERIFY(ui_.menuOpened());
        const QStringList texts = test::menuTexts(ui_.menu());
        for (const char* text : {"Load Sample…", "Unlock Pad", "Save as Similar Reference", "Return to Reference",
                                 "Next Similar Sound", "Rename", "Duplicate", "Delete"}) {
            QVERIFY2(texts.contains(QString::fromUtf8(text)), text);
        }
        QVERIFY(!test::menuEnabled(ui_.menu(), QStringLiteral("Return to Reference")));
        QVERIFY(ui_.choose(QStringLiteral("Unlock Pad")));
        QVERIFY(!padChain(track, rack, 36)->locked);
        ui_.rightClick(test::centerOf(pad(rack, 37)));
        QVERIFY(ui_.choose(QStringLiteral("Delete")));
        QVERIFY(!padChain(track, rack, 37));
        // The rack's own menu: its pads' references.
        ui_.rightClick(test::centerOf(test::part(ui_.frame(rack), "title")));
        QVERIFY(ui_.menuOpened());
        QVERIFY(test::menuTexts(ui_.menu()).contains(QStringLiteral("Save Pads as Similar References")));
        QVERIFY(test::menuTexts(ui_.menu()).contains(QStringLiteral("Return Pads to References")));
        QVERIFY(ui_.closeMenu());
    }

    void theRacksSimilarArrows() {
        const auto [track, rack] = drums();
        QQuickItem* next = bodyPart(rack, QStringLiteral("nextSimilar"));
        QVERIFY(next);
        // In the device's title bar, with the macros' collapse arrow.
        QQuickItem* header = test::part(ui_.frame(rack), "header");
        QVERIFY(header);
        const QPointF inHeader = next->mapToItem(header, QPointF(next->width() / 2, next->height() / 2));
        QVERIFY(header->contains(inHeader));
        QVERIFY(bodyPart(rack, QStringLiteral("previousSimilar"))->isVisible());
        QQuickItem* collapse = bodyPart(rack, QStringLiteral("macrosButton"));
        QVERIFY(collapse && collapse->isVisible());
        QVERIFY(header->contains(collapse->mapToItem(header, QPointF(collapse->width() / 2, collapse->height() / 2))));
        QVERIFY(!next->isEnabled());  // no pad yet
        const QString kick = editor().loadDrumPad(track, rack, 36, wav(QStringLiteral("Kick.wav")));
        ui_.polish();
        QVERIFY(next->isEnabled());
        // With no library to find similar sounds in, it says so.
        test::click(window(), test::centerOf(next));
        QTRY_VERIFY(ui_.lastMessage().startsWith(QStringLiteral("No similar sounds")));
        editor().setChainParam(track, kick, QStringLiteral("locked"), 1.0);
        ui_.polish();
        QVERIFY(!next->isEnabled());  // every pad locked
    }

    void theMacrosCollapse() {
        const auto [track, rack] = drums();
        QQuickItem* frame = ui_.frame(rack);
        QQuickItem* macros = bodyPart(rack, QStringLiteral("macroGrid"));
        QVERIFY(macros && macros->isVisible());
        const qreal wide = frame->width();
        test::click(window(), test::centerOf(bodyPart(rack, QStringLiteral("macrosButton"))));
        QVERIFY(!project().areRackMacrosShown(rack));
        ui_.polish();
        QVERIFY(!macros->isVisible());
        QVERIFY(!bodyPart(rack, QStringLiteral("addMacroButton")));  // (no visible one: + and − go with them)
        QVERIFY(ui_.frame(rack)->width() < wide);  // narrower without them
        ui_.screenshot(QStringLiteral("drum-rack-macros-collapsed"));
        test::click(window(), test::centerOf(bodyPart(rack, QStringLiteral("macrosButton"))));
        QVERIFY(project().areRackMacrosShown(rack));
        ui_.polish();
        QVERIFY(macros->isVisible());
        QCOMPARE(ui_.frame(rack)->width(), wide);
    }
};

QTEST_MAIN(TestUiDevicePanelDrums)
#include "test_ui_device_panel_drums.moc"
