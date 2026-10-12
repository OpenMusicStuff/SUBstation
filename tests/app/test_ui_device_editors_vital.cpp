// Vital's editor (ui/qml/devices/editors/VitalEditor.qml, VitalPatchInfo):
// loaded as the device view loads it, over a real engine; every page and each
// of its choices showing Vital's parameters (each one the device has, in the
// body's height), its knobs editing the model undoably, Vital's texts in the
// readouts, the modulation matrix adding, showing and removing modulations
// (one undo step each), and the preset's name. With SUBSTATION_UI_SCREENSHOTS
// set to a folder, its pages are saved there as PNGs.

#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include "EditorHarness.h"
#include "devices/DeviceParam.h"
#include "editor/ProjectEditor.h"
#include "model/Vital.h"

using namespace sub::app;
using namespace sub::ui;

class TestUiDeviceEditorsVital : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;

    QQuickItem* showVital() {
        track_ = editor()->addMidiTrack(-1, {}, QStringLiteral("vital"));
        device_ = project()->track(track_).devices.front().id;
        return show(QStringLiteral("vital"), track_, device_);
    }

    static void setPage(QQuickItem* view, int page, int choice = 0) {
        view->setProperty("page", page);
        QMetaObject::invokeMethod(view, "choose", Q_ARG(QVariant, choice));
        QTest::qWait(20);
    }

    // The parameters the page shows (its cells' ids).
    QStringList shown(QQuickItem* view) {
        QStringList ids;
        for (QQuickItem* cell : itemsNamed(view, QStringLiteral("param_"))) {
            if (cell->isVisible())
                ids << cell->property("paramId").toString();
        }
        return ids;
    }

    QObject* patch(QQuickItem* view) { return qvariant_cast<QObject*>(view->property("patch")); }

private Q_SLOTS:
    void initTestCase() {
        if (!haveDisplay())
            QSKIP("needs a display: the offscreen platform renders Qt Quick in software, without this geometry");
        startHost();
    }

    void cleanupTestCase() { stopHost(); }

    void init() { clearHost(); }

    void registry() {
        QVariant url;
        QMetaObject::invokeMethod(root_.get(), "editorFor", Q_RETURN_ARG(QVariant, url),
                                  Q_ARG(QVariant, QStringLiteral("vital")));
        QVERIFY(url.toString().endsWith(QStringLiteral("VitalEditor.qml")));
    }

    void everyPageShowsVitalsParameters() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        QCOMPARE(view->property("pages").toInt(), 9);
        QVERIFY(view->implicitHeight() <= bodyHeight());
        const QStringList all = [this] {
            QStringList ids;
            for (const auto& [id, value] : project()->device(track_, device_).params.asKeyValueRange()) ids << id;
            return ids;
        }();
        QSet<QString> seen;
        for (int page = 0; page < 9; ++page) {
            if (page == 7)
                continue;  // (the matrix)
            setPage(view, page);
            const int choices = view->property("choices").toList().size();
            for (int choice = 0; choice < std::max(1, choices); ++choice) {
                setPage(view, page, choice);
                const QStringList ids = shown(view);
                QVERIFY2(!ids.isEmpty(), qPrintable(QStringLiteral("page %1, choice %2").arg(page).arg(choice)));
                for (const QString& id : ids) {
                    QVERIFY2(all.contains(id), qPrintable(id));  // one of Vital's
                    seen.insert(id);
                    auto* cell = find<QQuickItem>(view, QStringLiteral("param_") + id);
                    QVERIFY(cell);
                    auto* param = qvariant_cast<sub::ui::DeviceParam*>(cell->property("param"));
                    QVERIFY2(param && param->valid(), qPrintable(id));
                    // Inside the editor.
                    const QPointF corner = cell->mapToItem(view, QPointF(cell->width(), cell->height()));
                    QVERIFY2(corner.x() <= view->width() + 0.5 && corner.y() <= view->height() + 0.5, qPrintable(id));
                }
            }
        }
        // The synth's every section has its controls somewhere.
        for (const char* id : {"osc_3_unison_voices", "sample_level", "filter_2_cutoff", "env_6_release", "lfo_8_tempo",
                               "random_4_style", "reverb_size", "compressor_high_lower_ratio", "filter_fx_cutoff",
                               "portamento_time", "macro_control_4", "volume"}) {
            QVERIFY2(seen.contains(QString::fromLatin1(id)), id);
        }
    }

    void knobsEditTheModelUndoably() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        setPage(view, 2, 1);  // Filter 2
        auto* cell = find<QQuickItem>(view, QStringLiteral("param_filter_2_cutoff"));
        QVERIFY(cell);
        auto* cutoff = qvariant_cast<sub::ui::DeviceParam*>(cell->property("param"));
        QVERIFY(cutoff);
        QCOMPARE(cutoff->text(), QStringLiteral("262 Hz"));  // (60: C3, shown in hertz as Vital shows it)
        cutoff->set(69.0);
        QCOMPARE(param(track_, device_, QStringLiteral("filter_2_cutoff")), 69.0);
        QCOMPARE(cutoff->text(), QStringLiteral("440 Hz"));
        undo()->undo();
        QCOMPARE(param(track_, device_, QStringLiteral("filter_2_cutoff")), 60.0);
        // Its caption is the editor's short one.
        QCOMPARE(cell->property("name").toString(), QStringLiteral("Cutoff"));
    }

    void matrixAddsAndRemovesModulations() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        setPage(view, 7);
        QObject* info = patch(view);
        QVERIFY(info);
        QCOMPARE(info->property("modulations").toList().size(), 0);
        QCOMPARE(info->property("freeSlots").toInt(), kVitalModulationSlots);
        int slot = -1;
        QMetaObject::invokeMethod(info, "add", Q_RETURN_ARG(int, slot), Q_ARG(QString, QStringLiteral("lfo_2")),
                                  Q_ARG(QString, QStringLiteral("osc_1_wave_frame")));
        QCOMPARE(slot, 0);
        QTRY_COMPARE(info->property("modulations").toList().size(), 1);
        const QVariantMap row = info->property("modulations").toList().front().toMap();
        QCOMPARE(row.value(QStringLiteral("sourceName")).toString(), QStringLiteral("LFO 2"));
        QCOMPARE(row.value(QStringLiteral("destinationName")).toString(), QStringLiteral("Oscillator 1 Wave Frame"));
        QTRY_VERIFY(find<QQuickItem>(view, QStringLiteral("modulation_0")) != nullptr);
        // Its amount and switches are the slot's parameters.
        QQuickItem* rowItem = find<QQuickItem>(view, QStringLiteral("modulation_0"));
        auto* amount = find<QQuickItem>(rowItem, QStringLiteral("amount"));
        QVERIFY(amount);
        auto* amountParam = qvariant_cast<sub::ui::DeviceParam*>(amount->property("param"));
        QVERIFY(amountParam);
        QCOMPARE(amountParam->paramId(), QStringLiteral("modulation_1_amount"));
        amountParam->set(0.5);
        QCOMPARE(param(track_, device_, QStringLiteral("modulation_1_amount")), 0.5);
        QCOMPARE(param(track_, device_, QStringLiteral("modulation_1_bipolar")), 1.0);  // (an LFO's: bipolar)

        // Removed, one undo step; back on undo.
        bool removed = false;
        QMetaObject::invokeMethod(info, "remove", Q_RETURN_ARG(bool, removed), Q_ARG(int, 0));
        QVERIFY(removed);
        QTRY_COMPARE(info->property("modulations").toList().size(), 0);
        QCOMPARE(param(track_, device_, QStringLiteral("modulation_1_amount")), 0.0);
        undo()->undo();
        QTRY_COMPARE(info->property("modulations").toList().size(), 1);
        QCOMPARE(param(track_, device_, QStringLiteral("modulation_1_amount")), 0.5);
        // Every source and destination there is to choose.
        QCOMPARE(info->property("sources").toList().size(), vitalSources().size());
        QCOMPARE(info->property("destinations").toList().size(), vitalDestinations().size());
    }

    void showsThePresetsName() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        auto* name = find<QQuickItem>(view, QStringLiteral("presetName"));
        QVERIFY(name);
        QCOMPARE(name->property("text").toString(), QStringLiteral("Init"));
    }

    void showsWavetablesAndLfoShapes() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        setPage(view, 0, 1);  // Osc 2
        auto* shape = find<QQuickItem>(view, QStringLiteral("shapeName"));
        auto* button = find<QQuickItem>(view, QStringLiteral("shapeButton"));
        QVERIFY(shape && button && shape->isVisible() && button->isVisible());
        QCOMPARE(shape->property("text").toString(), QStringLiteral("Init"));
        QCOMPARE(button->property("text").toString(), QStringLiteral("Wavetable…"));
        bool set = false;
        QMetaObject::invokeMethod(patch(view), "setWavetableShape", Q_RETURN_ARG(bool, set), Q_ARG(int, 1), Q_ARG(int, 2));
        QVERIFY(set);
        QTRY_COMPARE(shape->property("text").toString(), QStringLiteral("Triangle"));
        setPage(view, 0, 0);
        QCOMPARE(shape->property("text").toString(), QStringLiteral("Init"));  // (oscillator 1's)
        setPage(view, 4, 2);  // LFO 3
        QCOMPARE(shape->property("text").toString(), QStringLiteral("Triangle"));
        QCOMPARE(button->property("text").toString(), QStringLiteral("Shape…"));
        QMetaObject::invokeMethod(patch(view), "setLfoShape", Q_RETURN_ARG(bool, set), Q_ARG(int, 2), Q_ARG(int, 4));
        QTRY_COMPARE(shape->property("text").toString(), QStringLiteral("Saw Down"));
        setPage(view, 1);  // (not on Sample)
        QVERIFY(!shape->isVisible() && !button->isVisible());
        // Its side column fits the body.
        auto* side = find<QQuickItem>(view, QStringLiteral("side"));
        QVERIFY(side && side->y() + side->height() <= view->height());
    }

    void screenshot() {
        const QString folder = qEnvironmentVariable("SUBSTATION_UI_SCREENSHOTS");
        if (folder.isEmpty())
            QSKIP("set SUBSTATION_UI_SCREENSHOTS to a folder to save the editor's pages");
        QQuickItem* view = showVital();
        QVERIFY(view);
        editor()->addVitalModulation(track_, device_, QStringLiteral("lfo_1"), QStringLiteral("filter_1_cutoff"));
        editor()->addVitalModulation(track_, device_, QStringLiteral("env_2"), QStringLiteral("osc_1_wave_frame"));
        const QStringList names = view->property("pageNames").toStringList();
        for (int page = 0; page < names.size(); ++page) {
            setPage(view, page);
            QTest::qWait(100);
            save(window_->grabWindow(), QStringLiteral("vital-%1.png").arg(names[page].toLower()));
        }
    }
};

QTEST_MAIN(TestUiDeviceEditorsVital)
#include "test_ui_device_editors_vital.moc"
