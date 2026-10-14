// Vital's editor (ui/qml/devices/editors/VitalEditor.qml, VitalPatchInfo and
// its panels, VitalWavetableGraph, VitalEnvelopeGraph, VitalLfoGraph,
// VitalFilterGraph, VitalScope): loaded as the device view loads it, over a
// real engine, as wide as it was; every page and each of its choices showing
// Vital's parameters (each one the device has, in the body's height), its
// knobs editing the model undoably, Vital's texts in the readouts, knobs that
// do nothing dimmed, the modulation matrix adding, showing and removing
// modulations (one undo step each), the preset's name; the panels drawing the
// section chosen (the wavetable's frames, the envelope's stages, the LFO's
// shape, the filter's response), their drags one undo step each, and what the
// engine plays reaching them (the envelopes' and LFOs' positions, the output,
// the voices). With SUBSTATION_UI_SCREENSHOTS set to a folder, its pages are
// saved there as PNGs.

#include <QQuickItem>
#include <cmath>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include "EditorHarness.h"
#include "devices/DeviceParam.h"
#include "devices/VitalEnvelopeGraph.h"
#include "devices/VitalFilterGraph.h"
#include "devices/VitalLfoGraph.h"
#include "devices/VitalScope.h"
#include "devices/VitalWavetableGraph.h"
#include "model/Clip.h"
#include "editor/ProjectEditor.h"
#include "model/Vital.h"

using namespace sub::app;
using namespace sub::ui;
using sub::app::test::kSampleRate;

class TestUiDeviceEditorsVital : public QObject, public sub::app::test::EditorHarness {
    Q_OBJECT

    QString track_, device_;
    QString error_;  // the screenshot's wavetable's load

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

    // A note held from the start for `beats` (at the project's tempo), on the Vital's track.
    void holdNote(double beats) {
        editor()->commitClips(QStringLiteral("Add"), {{track_, {Clip::midi(QStringLiteral("held"), QStringLiteral("held"),
                                                                           0.0, beats, 0.0, {Note{60, 0.0, beats}})}}});
    }
    // Renders `seconds` of the arrangement, then ticks the displays' clock until the editor has read it all.
    void play(double seconds) {
        session_->bridge()->waitForDeviceStates();
        engine_->renderOffline(0.0, static_cast<int64_t>(seconds * kSampleRate));
        for (int i = 0; i < 8; ++i)
            refreshDisplays();
    }
    // A drag on `item` from `from` by `by` (item coordinates), with `modifiers`.
    void drag(QQuickItem* item, QPointF from, QPointF by, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::mousePress(window_, Qt::LeftButton, modifiers, scenePoint(item, from));
        dragTo(window_, scenePoint(item, from + by / 2), modifiers);
        dragTo(window_, scenePoint(item, from + by), modifiers);
        QTest::mouseRelease(window_, Qt::LeftButton, modifiers, scenePoint(item, from + by));
    }

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
        QCOMPARE(view->implicitWidth(), 996.0);  // (as wide as it ever was)
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
                    // Its caption and its readout whole.
                    for (QQuickItem* text : cell->findChildren<QQuickItem*>()) {
                        if (text->isVisible() && text->property("truncated").toBool())
                            QFAIL(qPrintable(QStringLiteral("%1: \"%2\" cut short").arg(id, text->property("text").toString())));
                    }
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

    void knobsThatDoNothingAreDimmed() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        setPage(view, 2);  // Filter 1: Analog, so the formant's knobs and the comb's do nothing
        auto* formant = find<QQuickItem>(view, QStringLiteral("param_filter_1_formant_x"));
        auto* comb = find<QQuickItem>(view, QStringLiteral("param_filter_1_blend_transpose"));
        auto* cutoff = find<QQuickItem>(view, QStringLiteral("param_filter_1_cutoff"));
        QVERIFY(formant && comb && cutoff);
        QTRY_VERIFY(formant->opacity() < 0.5);
        QVERIFY(comb->opacity() < 0.5);
        QCOMPARE(cutoff->opacity(), 1.0);
        editor()->setDeviceParam(track_, device_, QStringLiteral("filter_1_model"), 5.0);  // Formant
        QTRY_COMPARE(formant->opacity(), 1.0);
        QTRY_VERIFY(comb->opacity() < 0.5);
        // An LFO's rate while it follows the tempo, its tempo while it doesn't.
        setPage(view, 4);
        auto* rate = find<QQuickItem>(view, QStringLiteral("param_lfo_1_frequency"));
        auto* tempo = find<QQuickItem>(view, QStringLiteral("param_lfo_1_tempo"));
        QVERIFY(rate && tempo);
        const bool synced = param(track_, device_, QStringLiteral("lfo_1_sync")) != 0.0;
        QTRY_COMPARE(rate->opacity() < 0.5, synced);
        QTRY_COMPARE(tempo->opacity() < 0.5, !synced);
        // The same for a random LFO, coming from an LFO's page (what decides it follows the page).
        setPage(view, 5, 1);
        auto* randomRate = find<QQuickItem>(view, QStringLiteral("param_random_2_frequency"));
        auto* randomTempo = find<QQuickItem>(view, QStringLiteral("param_random_2_tempo"));
        QVERIFY(randomRate && randomTempo);
        const bool randomSynced = param(track_, device_, QStringLiteral("random_2_sync")) != 0.0;
        QTRY_COMPARE(randomRate->opacity() < 0.5, randomSynced);
        QTRY_COMPARE(randomTempo->opacity() < 0.5, !randomSynced);
    }

    void panelsDrawTheSectionChosen() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        // Oscillator 2: its wavetable's frames; a basic shape set, they are the shape's.
        setPage(view, 0, 1);
        auto* wavetable = find<VitalWavetableGraph>(view, QStringLiteral("wavetableGraph"));
        QVERIFY(wavetable && wavetable->isVisible());
        QCOMPARE(wavetable->prefix(), QStringLiteral("osc_2_"));
        QCOMPARE(wavetable->index(), 1);
        QVERIFY(wavetable->frames() >= 1);
        bool set = false;
        QMetaObject::invokeMethod(patch(view), "setWavetableShape", Q_RETURN_ARG(bool, set), Q_ARG(int, 1), Q_ARG(int, 5));
        QVERIFY(set);  // a square
        QTRY_COMPARE(wavetable->frames(), 1);
        const std::vector<float> square = wavetable->frameValues(0);
        QCOMPARE(int(square.size()), VitalWavetableGraph::kPoints);
        QVERIFY(square[VitalWavetableGraph::kPoints / 4] > 0.5f);   // up for its first half,
        QVERIFY(square[3 * VitalWavetableGraph::kPoints / 4] < -0.5f);  // down for its second
        // Dragging up moves through the frames: one undo step.
        const int steps = undo()->count();
        drag(wavetable, QPointF(wavetable->width() / 2, wavetable->height() - 10), QPointF(0, -40));
        QCOMPARE(undo()->count(), steps + 1);
        QVERIFY(param(track_, device_, QStringLiteral("osc_2_wave_frame")) > 10.0);
        QCOMPARE(param(track_, device_, QStringLiteral("osc_1_wave_frame")), 0.0);
        // Switched off, it says so.
        QVERIFY(wavetable->on() == (param(track_, device_, QStringLiteral("osc_2_on")) >= 0.5));

        // Envelope 3: the attack's end dragged right, the decay's down (the sustain), each one undo step.
        setPage(view, 3, 2);
        auto* envelope = find<VitalEnvelopeGraph>(view, QStringLiteral("envelopeGraph"));
        QVERIFY(envelope && envelope->isVisible());
        QCOMPARE(envelope->prefix(), QStringLiteral("env_3_"));
        const double attack = param(track_, device_, QStringLiteral("env_3_attack"));
        drag(envelope, envelope->handlePosition(VitalEnvelopeGraph::Attack), QPointF(20, 0));
        QVERIFY(param(track_, device_, QStringLiteral("env_3_attack")) > attack);
        QCOMPARE(param(track_, device_, QStringLiteral("env_1_attack")), attack);  // (another envelope's: as it was)
        const int before = undo()->count();
        drag(envelope, envelope->handlePosition(VitalEnvelopeGraph::Decay), QPointF(0, 20));
        QCOMPARE(undo()->count(), before + 1);
        QVERIFY(param(track_, device_, QStringLiteral("env_3_sustain")) < 1.0);
        // Its curve is Vital's: linear at power 0, bowed either way otherwise.
        QCOMPARE(VitalEnvelopeGraph::attackValue(0.5, 0.0), 0.5);
        QVERIFY(VitalEnvelopeGraph::attackValue(0.5, 4.0) > 0.5);
        QVERIFY(VitalEnvelopeGraph::decayValue(0.5, 4.0, 0.0) > 0.5);
        QVERIFY(VitalEnvelopeGraph::releaseValue(0.5, -4.0, 1.0) < 0.5);
        // A double-click on a handle sets it back.
        QTest::mouseDClick(window_, Qt::LeftButton, Qt::NoModifier,
                           scenePoint(envelope, envelope->handlePosition(VitalEnvelopeGraph::Attack)));
        QTRY_COMPARE(param(track_, device_, QStringLiteral("env_3_attack")), attack);

        // LFO 1: its shape (Vital's triangle: three points, up then down).
        setPage(view, 4, 0);
        auto* lfo = find<VitalLfoGraph>(view, QStringLiteral("lfoGraph"));
        QVERIFY(lfo && lfo->isVisible());
        QCOMPARE(lfo->points().size(), size_t(6));
        QVERIFY(std::abs(lfo->valueAt(0.0)) < 0.01 && std::abs(lfo->valueAt(0.5) - 1.0) < 0.01);
        QMetaObject::invokeMethod(patch(view), "setLfoShape", Q_RETURN_ARG(bool, set), Q_ARG(int, 0), Q_ARG(int, 3));
        QTRY_VERIFY(lfo->valueAt(0.25) < lfo->valueAt(0.75));  // a saw up

        // Filter 1: Analog, 12 dB, low-pass; across, the cutoff, up and down the resonance: one step.
        setPage(view, 2, 0);
        auto* filter = find<VitalFilterGraph>(view, QStringLiteral("filterGraph"));
        QVERIFY(filter && filter->isVisible());
        const VitalFilterGraph::Settings s = filter->settings();
        QVERIFY(VitalFilterGraph::response(s, s.cutoffHz / 10) > -1.0);
        QVERIFY(VitalFilterGraph::response(s, s.cutoffHz * 10) < -30.0);
        VitalFilterGraph::Settings high = s;
        high.blend = 2.0;
        QVERIFY(VitalFilterGraph::response(high, s.cutoffHz / 10) < -30.0);
        QVERIFY(VitalFilterGraph::response(high, s.cutoffHz * 10) > -1.0);
        const double cutoff = param(track_, device_, QStringLiteral("filter_1_cutoff"));
        const int filterSteps = undo()->count();
        drag(filter, filter->handlePosition(), QPointF(30, -10));
        QCOMPARE(undo()->count(), filterSteps + 1);
        QVERIFY(param(track_, device_, QStringLiteral("filter_1_cutoff")) > cutoff);
    }

    void whatPlaysReachesThePanels() {
        QQuickItem* view = showVital();
        QVERIFY(view);
        holdNote(4.0);
        setPage(view, 3, 0);  // Envelope 1: the amplitude's, always moving with a note
        auto* envelope = find<VitalEnvelopeGraph>(view, QStringLiteral("envelopeGraph"));
        QVERIFY(envelope);
        play(0.5);
        QTRY_VERIFY(envelope->playing());
        setPage(view, 4, 0);
        auto* lfo = find<VitalLfoGraph>(view, QStringLiteral("lfoGraph"));
        QVERIFY(lfo);
        play(0.5);
        QTRY_VERIFY(lfo->playing());
        QVERIFY(lfo->phase() >= 0.0 && lfo->phase() <= 1.0);
        setPage(view, 8);
        auto* scope = find<VitalScope>(view, QStringLiteral("scope"));
        QVERIFY(scope && scope->isVisible());
        play(0.5);
        QTRY_VERIFY(scope->voices() >= 1);
        // Silence: the dot goes.
        editor()->setDeviceParam(track_, device_, QStringLiteral("volume"), 0.0);
        setPage(view, 3, 0);
        for (int i = 0; i < 60; ++i) refreshDisplays();
        QTRY_VERIFY(!envelope->playing());
    }

    void screenshot() {
        const QString folder = qEnvironmentVariable("SUBSTATION_UI_SCREENSHOTS");
        if (folder.isEmpty())
            QSKIP("set SUBSTATION_UI_SCREENSHOTS to a folder to save the editor's pages");
        QQuickItem* view = showVital();
        QVERIFY(view);
        editor()->addVitalModulation(track_, device_, QStringLiteral("lfo_1"), QStringLiteral("filter_1_cutoff"));
        editor()->addVitalModulation(track_, device_, QStringLiteral("env_2"), QStringLiteral("osc_1_wave_frame"));
        // A wavetable of 16 frames, from a sine to a saw (spliced, 2048 samples a frame), at its sixth.
        std::vector<float> frames;
        for (int f = 0; f < 16; ++f) {
            for (int i = 0; i < 2048; ++i) {
                double v = 0.0;
                for (int h = 1; h <= 1 + f * 2; ++h) v += std::sin(2 * M_PI * h * i / 2048.0) / h;
                frames.push_back(float(0.55 * v));
            }
        }
        QMetaObject::invokeMethod(patch(view), "loadWavetable", Q_RETURN_ARG(QString, error_), Q_ARG(int, 0),
                                  Q_ARG(QString, sub::app::test::writeWav(dir_.path(QStringLiteral("table.wav")), frames)), Q_ARG(int, 0));
        editor()->setDeviceParam(track_, device_, QStringLiteral("osc_1_wave_frame"), 90.0);
        editor()->setDeviceParam(track_, device_, QStringLiteral("filter_1_on"), 1.0);
        editor()->setDeviceParam(track_, device_, QStringLiteral("filter_1_resonance"), 0.7);
        editor()->setDeviceParam(track_, device_, QStringLiteral("env_1_decay_power"), -6.0);
        editor()->setDeviceParam(track_, device_, QStringLiteral("env_1_sustain"), 0.55);
        editor()->setDeviceParam(track_, device_, QStringLiteral("reverb_on"), 1.0);
        holdNote(8.0);
        const QStringList names = view->property("pageNames").toStringList();
        for (int page = 0; page < names.size(); ++page) {
            setPage(view, page);
            play(0.35);
            QTest::qWait(150);
            save(window_->grabWindow(), QStringLiteral("vital-%1.png").arg(names[page].toLower()));
        }
    }
};

QTEST_MAIN(TestUiDeviceEditorsVital)
#include "test_ui_device_editors_vital.moc"
