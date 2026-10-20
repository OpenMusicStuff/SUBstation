// Progression ideas, driven through the clip view the way a user would: a MIDI
// track with the Synth and a one-bar clip opened in the piano roll; Generate ›
// Progressions… opens the panel beside the roll with ideas in the song's key
// (A minor without one); trying an idea writes its chords into the clip (made
// long enough to play them), a run of tries one undo step, the clip's own notes
// kept; ↑ and ↓ step through the ideas; Bass and 7ths; locking a chord,
// keeping an idea, New Ideas; swapping a chord; More Like This; the mood, the
// length and the key; closing it. Runs on a display (xvfb here). With
// $SUBSTATION_UI_SCREENSHOTS set, it saves screenshots there.

#include <QGuiApplication>
#include <QQuickItem>
#include <QStyleHints>
#include <QQuickWindow>
#include <QTest>
#include <QUndoStack>

#include <algorithm>
#include <functional>
#include <memory>

#include "UiTestSupport.h"
#include "audio/EngineBridge.h"
#include "editor/ProjectEditor.h"
#include "harmony/Accompaniment.h"
#include "intelligence/Harmony.h"
#include "model/Clip.h"
#include "model/Project.h"
#include "pianoroll/NoteGrid.h"
#include "pianoroll/PianoRoll.h"
#include "pianoroll/ProgressionIdeas.h"

using sub::app::Note;
using sub::ui::PianoRoll;
using sub::ui::ProgressionIdeas;
namespace harmony = sub::intelligence::harmony;
namespace test = sub::app::test;

namespace {

constexpr double kClipStart = 4.0;  // a bar in

// The window (at the end of the file: moc skips what follows a raw string).
extern const char* const kWindow;

Note note(int pitch, double start, double length, int velocity = 100) {
    Note n;
    n.pitch = pitch;
    n.start = start;
    n.length = length;
    n.velocity = velocity;
    return n;
}

// An item under `root` by its objectName, in the item tree (a Repeater's items
// have no QObject parent: findChild misses them).
QQuickItem* findIn(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    for (QQuickItem* child : root->childItems()) {
        if (child->objectName() == name) return child;
        if (QQuickItem* found = findIn(child, name)) return found;
    }
    return nullptr;
}

std::vector<Note> sorted(std::vector<Note> notes) {
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    return notes;
}

}  // namespace

class TestUiProgressions : public QObject {
    Q_OBJECT

    std::unique_ptr<test::UiSession> ui_;
    QQuickWindow* window_ = nullptr;
    QQuickItem* clipView_ = nullptr;
    QString trackId_;
    QString clipId_;

    sub::app::Session& session() { return ui_->session(); }
    sub::app::Project& project() { return *session().project(); }
    QUndoStack& undo() { return *session().undoStack(); }
    const sub::app::Clip& clip() { return project().clip(trackId_, clipId_); }
    PianoRoll* roll() { return qobject_cast<sub::ui::NoteGrid*>(item("noteGrid"))->roll(); }
    ProgressionIdeas* ideas() { return clipView_->property("progressionIdeas").value<ProgressionIdeas*>(); }
    QQuickItem* panel() { return findIn(clipView_, QStringLiteral("progressionPanel")); }
    QQuickItem* item(const char* name, QQuickItem* in = nullptr) {
        QQuickItem* root = in ? in : clipView_;
        QQuickItem* found = findIn(root, QString::fromLatin1(name));
        return found ? found : root->findChild<QQuickItem*>(QString::fromLatin1(name));  // (a menu's items)
    }
    // An idea's row, and an item in it (its chords "chord0"..., "tryIdea", "keepIdea", "moreLikeIdea").
    QQuickItem* row(int index) { return item(qPrintable(QStringLiteral("idea%1").arg(index)), panel()); }
    QQuickItem* inRow(int index, const char* name) { return item(name, row(index)); }
    void clickIn(int index, const char* name) {
        QQuickItem* target = inRow(index, name);
        QVERIFY(target && target->isVisible());
        test::click(window_, test::centerOf(target));
    }
    QString chipText(int index, int chord) {
        QStringList texts;
        std::function<void(QQuickItem*)> read = [&](QQuickItem* at) {
            for (QQuickItem* child : at->childItems()) {
                const QVariant text = child->property("text");
                if (text.isValid() && !text.toString().isEmpty() && child->isVisible()) texts.append(text.toString());
                read(child);
            }
        };
        read(inRow(index, qPrintable(QStringLiteral("chord%1").arg(chord))));
        return texts.join(QLatin1Char(' '));
    }

    // A MIDI track (with the Synth) and a one-bar clip at beat 4, open in the piano roll.
    void openMidiClip() {
        trackId_ = session().editor()->addMidiTrack(-1, {}, QStringLiteral("synth"));
        const auto ref = session().editor()->addMidiClip(trackId_, kClipStart, 4.0);
        QVERIFY(ref);
        clipId_ = ref->clipId;
        clipView_->setProperty("trackId", trackId_);
        clipView_->setProperty("clipIds", QVariantList{clipId_});
        QTRY_VERIFY(item("noteGrid")->isVisible());
    }

    // Generate › Progressions…, through the clip view's header.
    void openPanel() {
        test::click(window_, test::centerOf(item("generate")));
        QObject* menu = clipView_->findChild<QObject*>(QStringLiteral("generateMenu"));
        QTRY_VERIFY(menu->property("opened").toBool());
        QQuickItem* entry = item("generateProgressions");
        QVERIFY(entry);
        test::click(window_, test::centerOf(entry));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QTRY_VERIFY(panel()->isVisible());
        QTRY_VERIFY(row(ProgressionIdeas::kIdeas - 1) != nullptr);
    }

    // The notes an idea writes into the clip from its start, `length` beats of it.
    std::vector<Note> expected(int index, double length, bool bass = false) {
        const auto spans = harmony::layOut(ideas()->played(index), kClipStart, kClipStart + length, 4.0);
        std::vector<harmony::GeneratedNote> parts = harmony::chordPart(spans, 4.0);
        if (bass) {
            const auto line = harmony::bassPart(spans, 4.0);
            parts.insert(parts.end(), line.begin(), line.end());
        }
        std::vector<Note> notes;
        for (const auto& n : parts) notes.push_back(note(n.pitch, n.start - kClipStart, n.length, n.velocity));
        return sorted(notes);
    }

private Q_SLOTS:
    void initTestCase() {
        test::prepareApplication();
        if (!test::haveDisplay()) QSKIP("needs a display: Qt Quick's software renderer draws none of this geometry");
        sub::ui::setUpApplication();
        ui_ = std::make_unique<test::UiSession>();
        window_ = ui_->show(kWindow);
        QVERIFY(window_);
        clipView_ = window_->findChild<QQuickItem*>(QStringLiteral("clipView"));
        QVERIFY(clipView_);
    }

    void cleanupTestCase() { ui_.reset(); }

    void init() {
        clipView_->setProperty("clipIds", QVariantList());
        project().clear();
        undo().clear();
        session().bridge()->stop();
        panel()->setProperty("wanted", false);
        ideas()->clear();
        ideas()->setMood(0);
        ideas()->setLength(4);
        ideas()->setSevenths(false);
        ideas()->setBass(false);
        ideas()->seedRandom(1);
        openMidiClip();
    }

    void openingThePanelMakesIdeasInTheKey() {
        const double rollWidth = item("pianoRollView")->width();
        openPanel();
        QCOMPARE(ideas()->count(), ProgressionIdeas::kIdeas);
        QVERIFY(item("pianoRollView")->width() < rollWidth);  // beside it
        // No key and no notes yet: A minor.
        QCOMPARE(item("progressionKey", panel())->property("text").toString(), QStringLiteral("In A minor (no key yet)"));
        for (int i = 0; i < ProgressionIdeas::kIdeas; ++i) {
            QCOMPARE(ideas()->chords(i).size(), size_t(4));
            for (int k = 0; k < 4; ++k) QVERIFY(inRow(i, qPrintable(QStringLiteral("chord%1").arg(k))));
        }
        // A chord shows its name and its numeral in the key.
        const harmony::Chord first = ideas()->chords(0)[0];
        QCOMPARE(chipText(0, 0), QString::fromStdString(first.name() + " " + harmony::romanNumeral(first, {9, true})));
        test::screenshot(window_, QStringLiteral("progressions"));

        // Closing it: ×, then Esc once it is open again.
        test::click(window_, test::centerOf(item("closeProgressions", panel())));
        QTRY_VERIFY(!panel()->isVisible());
        QTRY_COMPARE(item("pianoRollView")->width(), rollWidth);
        openPanel();
        QCOMPARE(ideas()->count(), ProgressionIdeas::kIdeas);  // the same ones
        QVERIFY(panel()->hasActiveFocus());
        QTest::keyClick(window_, Qt::Key_Escape);
        QTRY_VERIFY(!panel()->isVisible());
    }

    void tryingWritesTheChordsAndOneUndoStepUndoesTheRun() {
        openPanel();
        clickIn(0, "tryIdea");
        // Four chords a bar each: the clip grows to four bars to play them.
        QCOMPARE(clip().durationBeats, 16.0);
        QCOMPARE(sorted(clip().notes), expected(0, 16.0));
        QCOMPARE(ideas()->tried(), 0);
        QCOMPARE(undo().undoText(), QStringLiteral("Try Progression"));
        QVERIFY(inRow(0, "tryIdea")->property("checked").toBool());
        const int steps = undo().count();

        // The arrow keys step through them, each in place of the last.
        QTest::keyClick(window_, Qt::Key_Down);
        QCOMPARE(ideas()->tried(), 1);
        QCOMPARE(ideas()->current(), 1);
        QCOMPARE(sorted(clip().notes), expected(1, 16.0));
        QTest::keyClick(window_, Qt::Key_Down);
        QTest::keyClick(window_, Qt::Key_Up);
        QCOMPARE(ideas()->tried(), 1);
        QCOMPARE(sorted(clip().notes), expected(1, 16.0));
        // Clicking a row tries it too.
        test::click(window_, test::at(row(3), QPointF(row(3)->width() - 90, row(3)->height() - 2)));
        QCOMPARE(ideas()->tried(), 3);
        QCOMPARE(sorted(clip().notes), expected(3, 16.0));
        QCOMPARE(undo().count(), steps);  // one step for the run

        undo().undo();
        QVERIFY(clip().notes.empty());
        QCOMPARE(clip().durationBeats, 4.0);
        QTRY_COMPARE(ideas()->tried(), -1);  // the clip no longer plays it
    }

    void theClipsOwnNotesStay() {
        const sub::app::ClipRef ref{trackId_, clipId_};
        const Note melody = note(84, 0.0, 1.0);
        session().editor()->setClipNotes(ref, {melody}, QStringLiteral("Notes"));
        openPanel();
        clickIn(0, "tryIdea");
        clickIn(1, "tryIdea");
        std::vector<Note> want = expected(1, 16.0);
        want.push_back(melody);
        QCOMPARE(sorted(clip().notes), sorted(want));

        // An edit between tries: the next try is a step of its own, and takes out what the last one wrote.
        const int steps = undo().count();
        std::vector<Note> edited = clip().notes;
        const Note added = note(86, 2.0, 1.0);
        edited.push_back(added);
        session().editor()->setClipNotes(ref, edited, QStringLiteral("Notes"));
        clickIn(2, "tryIdea");
        want = expected(2, 16.0);
        want.push_back(melody);
        want.push_back(added);
        QCOMPARE(sorted(clip().notes), sorted(want));
        QCOMPARE(undo().count(), steps + 2);
        undo().undo();
        QCOMPARE(sorted(clip().notes), sorted(edited));
    }

    void bassAndSeventhsPlayAsTheyShow() {
        openPanel();
        clickIn(0, "tryIdea");
        test::click(window_, test::centerOf(item("progressionBass", panel())));
        QVERIFY(ideas()->bass());
        QCOMPARE(sorted(clip().notes), expected(0, 16.0, true));
        QCOMPARE(std::count_if(clip().notes.begin(), clip().notes.end(), [](const Note& n) { return n.pitch < 48; }), 4);

        test::click(window_, test::centerOf(item("progressionSevenths", panel())));
        QVERIFY(ideas()->sevenths());
        for (const harmony::Chord& c : ideas()->played(0)) {
            QVERIFY2(c.size() == 4 || c.quality == harmony::Quality::Sus2 || c.quality == harmony::Quality::Sus4 ||
                         c.quality == harmony::Quality::Augmented,
                     c.name().c_str());
        }
        QCOMPARE(chipText(0, 0).split(QLatin1Char(' ')).first(), QString::fromStdString(ideas()->played(0)[0].name()));
        QCOMPARE(sorted(clip().notes), expected(0, 16.0, true));
        QCOMPARE(ideas()->tried(), 0);
        clickIn(1, "chord0");
        clickIn(2, "keepIdea");
        test::screenshot(window_, QStringLiteral("progressions-tried"));
    }

    void lockedChordsAndKeptIdeasStayForNewIdeas() {
        openPanel();
        clickIn(2, "chord1");
        QVERIFY(ideas()->isLocked(2, 1));
        clickIn(3, "keepIdea");
        QVERIFY(ideas()->isKept(3));
        QVERIFY(inRow(3, "keepIdea")->property("checked").toBool());
        const auto before = [this] {
            std::vector<std::vector<harmony::Chord>> all;
            for (int i = 0; i < ideas()->count(); ++i) all.push_back(ideas()->chords(i));
            return all;
        }();

        test::click(window_, test::centerOf(item("newIdeas", panel())));
        QCOMPARE(ideas()->chords(3), before[3]);                // kept as it is
        QVERIFY(ideas()->chords(2) != before[2]);               // drawn again...
        QCOMPARE(ideas()->chords(2)[1], before[2][1]);          // ... but the locked chord
        QVERIFY(ideas()->isLocked(2, 1));
        for (int i : {0, 1, 4, 5}) QVERIFY(ideas()->chords(i) != before[static_cast<size_t>(i)]);
        // Unlocked again.
        clickIn(2, "chord1");
        QVERIFY(!ideas()->isLocked(2, 1));
    }

    void aChordSwappedForAnother() {
        openPanel();
        clickIn(0, "tryIdea");
        const std::vector<harmony::Chord> before = ideas()->chords(0);
        // (A lock changes its row's chords: each click finds the chord again.)
        const auto chip = [this] { return test::centerOf(inRow(0, "chord2")); };
        clickIn(0, "chord2");  // locked...
        QVERIFY(ideas()->isLocked(0, 2));
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        clickIn(0, "chord2");  // ... and unlocked
        QVERIFY(!ideas()->isLocked(0, 2));
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(window_, Qt::RightButton, {}, chip());
        QObject* popup = panel()->findChild<QObject*>(QStringLiteral("swapPopup"));
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        // Its colours first (its seventh, sus4, sus2...), then the chords around it would take.
        const QVariantList choices = ideas()->alternatives(0, 2);
        QVERIFY(choices.size() >= 6);
        QVERIFY(choices.first().toMap().value(QStringLiteral("colour")).toBool());
        QQuickItem* instead = findIn(popup->property("contentItem").value<QQuickItem*>(), QStringLiteral("instead0"));
        QVERIFY(instead && instead->isVisible());
        test::screenshot(window_, QStringLiteral("progressions-swap"));
        test::click(window_, test::centerOf(instead));
        QTRY_VERIFY(!popup->property("visible").toBool());
        const std::vector<harmony::Chord> after = ideas()->chords(0);
        QVERIFY(after[2] != before[2]);
        QCOMPARE(after[0], before[0]);
        QCOMPARE(after[1], before[1]);
        QCOMPARE(after[3], before[3]);
        // In the clip already: written again.
        QCOMPARE(ideas()->tried(), 0);
        QCOMPARE(sorted(clip().notes), expected(0, 16.0));
    }

    void moreLikeThisPutsItsNeighboursInPlaceOfTheOthers() {
        openPanel();
        clickIn(4, "keepIdea");
        const std::vector<harmony::Chord> kept = ideas()->chords(4), source = ideas()->chords(1);
        clickIn(1, "moreLikeIdea");
        QCOMPARE(ideas()->chords(1), source);
        QCOMPARE(ideas()->chords(4), kept);
        for (int i : {0, 2, 3, 5}) {
            const std::vector<harmony::Chord> v = ideas()->chords(i);
            QCOMPARE(v.size(), source.size());
            int changed = 0;
            for (size_t k = 0; k < v.size(); ++k) changed += v[k] != source[k];
            QVERIFY(changed >= 1 && changed <= 2);
        }
    }

    void theMoodTheLengthAndTheKey() {
        openPanel();
        // Eight bars: eight chords, two lines of them; tried, the clip plays all eight.
        QMetaObject::invokeMethod(item("progressionLength", panel()), "activated", Q_ARG(int, 1));
        QCOMPARE(ideas()->length(), 8);
        for (int i = 0; i < ideas()->count(); ++i) QCOMPARE(ideas()->chords(i).size(), size_t(8));
        QTRY_VERIFY(inRow(0, "chord7") && inRow(0, "chord7")->y() > inRow(0, "chord0")->y());
        clickIn(0, "tryIdea");
        QCOMPARE(clip().durationBeats, 32.0);
        QCOMPARE(sorted(clip().notes), expected(0, 32.0));

        // Another mood: new ideas in it.
        const std::vector<harmony::Chord> sad = ideas()->chords(1);
        QMetaObject::invokeMethod(item("progressionMood", panel()), "activated", Q_ARG(int, 1));
        QCOMPARE(ideas()->mood(), 1);
        QVERIFY(ideas()->chords(1) != sad);

        // The song's key: ideas in it.
        session().editor()->setKeyByName(QStringLiteral("Em"));  // (as the key chooser has it)
        test::click(window_, test::centerOf(item("newIdeas", panel())));
        QCOMPARE(item("progressionKey", panel())->property("text").toString(), QStringLiteral("In E minor"));
        const harmony::Key eMinor{4, true};
        for (int i = 0; i < ideas()->count(); ++i) {
            if (i == 0) continue;  // (tried: in the clip, stays until another is)
            const auto chords = ideas()->chords(i);
            QVERIFY(std::any_of(chords.begin(), chords.end(),
                                [&](const harmony::Chord& c) { return eMinor.isDiatonic(c); }));
        }
    }
};

namespace {

const char* const kWindow = R"(
import QtQuick
import SUBstation

Window {
    width: 1200
    height: 600

    ClipView {
        objectName: "clipView"
        width: parent.width
        height: parent.height
    }
}
)";

}  // namespace

QTEST_MAIN(TestUiProgressions)
#include "test_ui_progressions.moc"
