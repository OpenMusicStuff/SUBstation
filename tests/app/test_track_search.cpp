// Finding tracks (Session.trackSearch, the arrangement's find bar): names
// first, then labels, then what the labels were made from (an instrument's
// preset), each in the arrangement's order; stepping through the matches,
// which follow the project.

#include "EditorFixture.h"
#include "TestSupport.h"

#include "intelligence/TrackLabels.h"
#include "intelligence/TrackSearch.h"
#include "model/Automation.h"
#include "model/Devices.h"

#include <QSignalSpy>
#include <QTest>

#include <utility>

using namespace sub::app;
using test::EditorFixture;

namespace {

void put(QByteArray& to, quint64 value, int bytes) {
    for (int i = 0; i < bytes; ++i) to.append(static_cast<char>((value >> (8 * i)) & 0xff));
}

// A .vstpreset (as a plug-in's Device::state holds one, base64) of a component's and a controller's state.
QString savedState(const QByteArray& component, const QByteArray& controller = {}) {
    QByteArray out("VST3");
    put(out, 1, 4);
    out += QByteArray(32, 'A');
    put(out, 0, 8);
    QByteArray list("List");
    put(list, 2, 4);
    for (const auto& [id, data] : {std::pair{QByteArray("Comp"), component}, std::pair{QByteArray("Cont"), controller}}) {
        list += id;
        put(list, static_cast<quint64>(out.size()), 8);
        put(list, static_cast<quint64>(data.size()), 8);
        out += data;
    }
    const auto at = static_cast<quint64>(out.size());
    for (int i = 0; i < 8; ++i) out[40 + i] = static_cast<char>((at >> (8 * i)) & 0xff);
    return QString::fromLatin1((out + list).toBase64());
}

// A MIDI track playing BBC Symphony Orchestra (not loaded: no engine here) with a preset.
QString orchestra(EditorFixture& f, const QString& name, const QByteArray& preset) {
    const QString track = f.editor.addMidiTrack(-1, {}, QString());
    const PluginRef plugin{QStringLiteral("VST3"), QString(32, u'0'), QStringLiteral("BBC Symphony Orchestra"),
                           QStringLiteral("Spitfire Audio"), QString(), true};
    const QString device = f.editor.addDevice(track, kPluginKind, -1, plugin);
    f.project.storePluginState(track, device, savedState({}, R"({"presetName":")" + preset + R"("})"), {});
    f.editor.renameTrack(track, name);
    return track;
}

}  // namespace

class TestTrackSearch : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void namesFirstThenLabelsThenWhatTheyAreMadeOf() {
        EditorFixture f;
        const QString pad = f.editor.addMidiTrack(-1, QStringLiteral("Pad"));
        const QString bbc = orchestra(f, QStringLiteral("BBC SO"), "Core - Horns");
        const QString thorn = f.editor.addAudioTrack(-1, QStringLiteral("Thorn"));
        const QString horn = f.editor.addMidiTrack(-1, QStringLiteral("Horn Stab"));
        TrackLabels labels(&f.project, nullptr, nullptr);
        QVERIFY2(labels.toolTip(bbc).contains(QStringLiteral("Core - Horns")), qPrintable(labels.toolTip(bbc)));

        // A name that has the word, then one that has it inside a word, then the
        // track whose instrument plays horns: names come first, whatever their order.
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("horn")), (QStringList{horn, thorn, bbc}));
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("HORN")), (QStringList{horn, thorn, bbc}));
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("horns")), QStringList{bbc});
        // Every word, wherever it is: the name has one, the preset the other.
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("bbc horn")), QStringList{bbc});
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("orchestra")), QStringList{bbc});
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("pad")), QStringList{pad});
        QVERIFY(TrackSearch::find(f.project, labels, QStringLiteral("cello")).isEmpty());
        QVERIFY(TrackSearch::find(f.project, labels, QStringLiteral("  ")).isEmpty());
        QVERIFY(TrackSearch::find(f.project, labels, QString()).isEmpty());

        // Within each rank, the arrangement's order: its tracks, in groups too, then the returns, the master.
        const QString horn2 = f.editor.addMidiTrack(0, QStringLiteral("Horn 2"));
        const QString group = f.editor.groupTracks({horn2});
        f.editor.setFolded(group, true);
        const QString ret = f.editor.addReturnTrack();
        f.editor.renameTrack(ret, QStringLiteral("Horn Verb"));
        // (The group last: what it holds, its details say, is a horn.)
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("horn")),
                 (QStringList{horn2, horn, ret, thorn, bbc, group}));
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("master")), QStringList{kMaster});
        // Words split as names are written: "HornStab", "Kick808".
        f.editor.renameTrack(horn, QStringLiteral("BrassHornStab808"));
        QCOMPARE(TrackSearch::find(f.project, labels, QStringLiteral("stab 808")), QStringList{horn});
    }

    void steppingThroughTheMatches() {
        EditorFixture f;
        const QString bbc = orchestra(f, QStringLiteral("BBC SO"), "Core - Horns");
        const QString thorn = f.editor.addAudioTrack(-1, QStringLiteral("Thorn"));
        const QString horn = f.editor.addMidiTrack(-1, QStringLiteral("Horn Stab"));
        TrackLabels labels(&f.project, nullptr, nullptr);
        TrackSearch search(&f.project, &labels);
        QSignalSpy changed(&search, &TrackSearch::changed);
        QCOMPARE(search.current(), -1);
        QCOMPARE(search.next(), QString());  // (nothing typed: nothing to go to)

        search.setQuery(QStringLiteral("horn"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(search.count(), 3);
        QCOMPARE(search.current(), 0);
        QCOMPARE(search.currentTrackId(), horn);  // the best, at once
        QCOMPARE(search.next(), thorn);
        QCOMPARE(search.next(), bbc);
        QCOMPARE(search.current(), 2);
        QCOMPARE(search.next(), horn);  // after the last, the first again
        QCOMPARE(search.previous(), bbc);
        QCOMPARE(search.previous(), thorn);

        // The project changes: the matches follow once the labels settle, staying on their track.
        f.editor.renameTrack(horn, QStringLiteral("Lead"));
        QTRY_COMPARE(search.count(), 2);
        QCOMPARE(search.currentTrackId(), thorn);
        QCOMPARE(search.current(), 0);
        // Its track no longer matches: the next is the best one.
        f.editor.renameTrack(thorn, QStringLiteral("Pad"));
        QCOMPARE(search.next(), bbc);
        QCOMPARE(search.count(), 1);

        search.setQuery(QString());
        QCOMPARE(search.count(), 0);
        QCOMPARE(search.current(), -1);
        QCOMPARE(search.currentTrackId(), QString());
        QCOMPARE(search.previous(), QString());
    }
};

QTEST_GUILESS_MAIN(TestTrackSearch)
#include "test_track_search.moc"
