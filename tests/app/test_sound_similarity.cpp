// Find Similar: the sound similarity (SoundSimilarity, the intelligence
// module's index) analysing the files the browser lists, and the browser's
// list of the sounds most like one (BrowserController::findSimilar): its sort,
// filtering it, ending it, a clip's part of a file, a file outside the
// library, files found later, and a sound that can't be analysed. And what the
// sound tagger hears in the files: searches by how they sound, their rows'
// labels, Samples › Kicks, and the project's sounds in the Best sort. And the
// Sampler's and the Drum Rack's arrows stepping through similar sounds
// (SimilarSamples), from a reference.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "EditorFixture.h"
#include "TestSupport.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

#include "Model.h"
#include "browser/BrowserController.h"
#include "browser/ItemListModel.h"
#include "browser/PathKeys.h"
#include "intelligence/Harmony.h"
#include "intelligence/SimilarSamples.h"
#include "model/DeviceState.h"
#include "model/Devices.h"
#include "model/SamplerState.h"
#include "intelligence/SoundSimilarity.h"
#include "intelligence/SoundSuggestions.h"
#include "model/Project.h"

using namespace sub::app;

namespace {

constexpr int kRate = test::kSampleRate;

// The same noise everywhere (std's distributions aren't).
struct Noise {
    uint32_t state;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.f - 1.f;
    }
};

std::vector<float> kick(double startHz, double endHz, double decay) {
    std::vector<float> s(size_t(0.6 * kRate));
    double phase = 0.0;
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = double(i) / kRate;
        phase += 2.0 * std::numbers::pi * (endHz + (startHz - endHz) * std::exp(-t / 0.03)) / kRate;
        s[i] = float(0.9 * std::sin(phase) * std::exp(-t / decay));
    }
    return s;
}

std::vector<float> hat(double decay, uint32_t seed) {
    Noise noise{seed};
    std::vector<float> s(size_t(0.4 * kRate));
    float a = 0.f, b = 0.f;
    for (size_t i = 0; i < s.size(); ++i) {
        const float n = noise.next();
        s[i] = float(0.3 * (n - 2.f * a + b) * std::exp(-double(i) / kRate / decay));
        b = a;
        a = n;
    }
    return s;
}

std::vector<float> snare(double toneHz, double decay, uint32_t seed) {
    Noise noise{seed};
    std::vector<float> s(size_t(0.5 * kRate));
    float previous = 0.f;
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = double(i) / kRate;
        const float n = noise.next();
        const double tone = std::sin(2.0 * std::numbers::pi * toneHz * t) * std::exp(-t / 0.05);
        s[i] = float(0.6 * (0.3 * tone + 0.7 * (n - 0.6 * previous) * std::exp(-t / decay)));
        previous = n;
    }
    return s;
}

std::vector<float> join(std::vector<float> a, const std::vector<float>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<float> padded(std::vector<float> s, double seconds) {
    s.resize(size_t(seconds * kRate), 0.f);
    return s;
}

QStringList names(const ItemListModel& model) {
    QStringList out;
    for (int row = 0; row < model.rowCount(); ++row) out << model.item(row)->name;
    return out;
}

QString kindOf(const QString& name) {
    for (const char* kind : {"Kick", "Hat", "Snare", "Loop"})
        if (name.startsWith(QLatin1String(kind))) return QString::fromLatin1(kind);
    return {};
}

}  // namespace

class TestSoundSimilarity : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { test::prepareApplication(); }

    void init() {
        tmp_ = std::make_unique<QTemporaryDir>();
        QVERIFY(tmp_->isValid());
        QSettings().remove(QStringLiteral("browser"));
        qputenv("SUBSTATION_LIBRARY", path(QStringLiteral("library.json")).toUtf8());
        qputenv("SUBSTATION_PLUGIN_CACHE", path(QStringLiteral("vst3-cache.json")).toUtf8());
        qputenv("SUBSTATION_VST3_PATH", path(QStringLiteral("VST3")).toUtf8());
        lib_ = path(QStringLiteral("lib"));
        const double kicks[][3] = {{150, 50, 0.25}, {170, 45, 0.3}, {130, 55, 0.2}};
        for (int i = 0; i < 3; ++i) {
            write(QStringLiteral("Kicks/Kick %1.wav").arg(i + 1), kick(kicks[i][0], kicks[i][1], kicks[i][2]));
            write(QStringLiteral("Hats/Hat %1.wav").arg(i + 1), hat(0.02 + 0.01 * i, 10 + i));
            write(QStringLiteral("Snares/Snare %1.wav").arg(i + 1), snare(180 + 20 * i, 0.1 + 0.02 * i, 20 + i));
        }
        // A hat, then a kick, a second apart.
        write(QStringLiteral("Loops/Loop.wav"), join(padded(hat(0.03, 30), 1.0), kick(150, 50, 0.25)));
    }

    void cleanup() {
        browser_.reset();
        similarity_.reset();
        QSettings().remove(QStringLiteral("browser"));
        tmp_.reset();
    }

    void theBrowsersFilesAreAnalysedInTheBackground() {
        make();
        QVERIFY(settle());
        QCOMPARE(similarity_->libraryFiles(), 10);
        QCOMPARE(similarity_->analysedFiles(), 10);
        QCOMPARE(similarity_->failedFiles(), 0);
        QVERIFY(!similarity_->analysing());
        QVERIFY(browser_->canFindSimilar());
    }

    void findSimilarListsTheMostSimilarFirst() {
        make();
        QVERIFY(settle());
        browser_->setSort(QStringLiteral("name"));
        browser_->setSearchText(QStringLiteral("kick"));
        QVERIFY(settle());
        QSignalSpy sortChanged(browser_.get(), &BrowserController::sortChanged);
        QSignalSpy similarChanged(browser_.get(), &BrowserController::similarChanged);
        const QString from = lib_ + QStringLiteral("/Kicks/Kick 1.wav");
        browser_->findSimilar(from);
        QCOMPARE(browser_->similarTo(), from);
        QCOMPARE(browser_->similarName(), QStringLiteral("Kick 1.wav"));
        QCOMPARE(browser_->sort(), kSimilarSort);
        QCOMPARE(browser_->sorts().first().toMap().value(QStringLiteral("value")).toString(), kSimilarSort);
        QCOMPARE(browser_->sorts().first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Similarity"));
        QCOMPARE(browser_->searchText(), QString());  // (cleared)
        QVERIFY(sortChanged.count() >= 1);
        QCOMPARE(similarChanged.count(), 1);
        QVERIFY(browser_->searching());
        QVERIFY(settle());
        // Every sound, the sound itself first, then the other kicks.
        const QStringList shown = names(*browser_->results());
        QCOMPARE(shown.size(), 10);
        QCOMPARE(shown.first(), QStringLiteral("Kick 1.wav"));
        QCOMPARE(kindOf(shown[1]), QStringLiteral("Kick"));
        QCOMPARE(kindOf(shown[2]), QStringLiteral("Kick"));
        QCOMPARE(browser_->statusText(), QStringLiteral("10 sounds like Kick 1.wav"));
        // The settings keep the sort chosen before.
        QCOMPARE(QSettings().value(QStringLiteral("browser/sort")).toString(), QStringLiteral("name"));
    }

    void searchTextAndPlacesFilterIt() {
        make();
        QVERIFY(settle());
        browser_->findSimilar(lib_ + QStringLiteral("/Hats/Hat 1.wav"));
        QVERIFY(settle());
        browser_->setSearchText(QStringLiteral("snare"));
        QVERIFY(settle());
        QCOMPARE(names(*browser_->results()).size(), 3);
        for (const QString& name : names(*browser_->results())) QCOMPARE(kindOf(name), QStringLiteral("Snare"));
        QCOMPARE(browser_->sort(), kSimilarSort);
        // A place lists its files (not its folder tree) by similarity.
        browser_->setSearchText(QString());
        browser_->setScope({QStringLiteral("place"), lib_});
        QVERIFY(settle());
        QVERIFY(!browser_->showingTree());
        QCOMPARE(browser_->sort(), kSimilarSort);
        QCOMPARE(names(*browser_->results()).first(), QStringLiteral("Hat 1.wav"));
        QCOMPARE(kindOf(names(*browser_->results())[1]), QStringLiteral("Hat"));
    }

    void anotherSortOrListOrClearingEndsIt() {
        make();
        QVERIFY(settle());
        const QString from = lib_ + QStringLiteral("/Kicks/Kick 1.wav");
        // Another sort.
        browser_->findSimilar(from);
        QVERIFY(settle());
        browser_->setSort(QStringLiteral("name"));
        QVERIFY(settle());
        QCOMPARE(browser_->similarTo(), QString());
        QCOMPARE(browser_->sort(), QStringLiteral("name"));
        QCOMPARE(browser_->sorts().size(), 3);  // (Best, Rank, Name: no Similarity)
        QCOMPARE(names(*browser_->results()).first(), QStringLiteral("Hat 1.wav"));
        // A list that isn't files.
        browser_->findSimilar(from);
        QVERIFY(settle());
        browser_->setScope({QStringLiteral("plugins")});
        QCOMPARE(browser_->similarTo(), QString());
        QCOMPARE(browser_->sort(), QStringLiteral("name"));
        // Clearing: the sort before comes back.
        browser_->setScope({QStringLiteral("samples")});
        browser_->findSimilar(from);
        QVERIFY(settle());
        QSignalSpy similarChanged(browser_.get(), &BrowserController::similarChanged);
        browser_->clearSimilar();
        QCOMPARE(similarChanged.count(), 1);
        QCOMPARE(browser_->sort(), QStringLiteral("name"));
        QVERIFY(settle());
        QCOMPARE(names(*browser_->results()).first(), QStringLiteral("Hat 1.wav"));
        QCOMPARE(browser_->statusText(), QStringLiteral("10 items"));
        // Ctrl+F (all items) ends it too.
        browser_->findSimilar(from);
        browser_->focusSearch();
        QCOMPARE(browser_->similarTo(), QString());
    }

    void aClipsPartOfAFileFindsItsKind() {
        make();
        QVERIFY(settle());
        auto firstOtherThanLoop = [this] {
            for (const QString& name : names(*browser_->results()))
                if (kindOf(name) != QStringLiteral("Loop")) return kindOf(name);
            return QString();
        };
        const QString loop = lib_ + QStringLiteral("/Loops/Loop.wav");
        browser_->findSimilar(loop, 0.0, 0.5);  // the hat
        QVERIFY(settle());
        QCOMPARE(firstOtherThanLoop(), QStringLiteral("Hat"));
        browser_->findSimilar(loop, 1.0, 0.6);  // the kick
        QVERIFY(settle());
        QCOMPARE(firstOtherThanLoop(), QStringLiteral("Kick"));
    }

    void aSoundOutsideTheLibrary() {
        make();
        QVERIFY(settle());
        const QString outside = path(QStringLiteral("project/Recorded.wav"));
        QDir().mkpath(QFileInfo(outside).absolutePath());
        test::writeWav(outside, kick(160, 48, 0.28));
        QSignalSpy found(similarity_.get(), &SoundSimilarity::found);
        browser_->findSimilar(outside);
        QVERIFY(settle());
        QCOMPARE(names(*browser_->results()).size(), 10);  // (not itself: it isn't in the library)
        QCOMPARE(kindOf(names(*browser_->results()).first()), QStringLiteral("Kick"));
        // What the search found, for the sampler and the drum rack to step through.
        QCOMPARE(found.count(), 1);
        const SimilarSounds result = found.at(0).at(0).value<SimilarSounds>();
        QCOMPARE(result.path(), outside);
        QVERIFY(result.error().isEmpty());
        QCOMPARE(result.count(), 10);
        const auto best = result.best(3);
        QCOMPARE(best.size(), 3);
        for (const auto& [file, similarity] : best) {
            QCOMPARE(kindOf(QFileInfo(file).fileName()), QStringLiteral("Kick"));
            QVERIFY(similarity > 0.0 && similarity <= 1.0);
        }
        QVERIFY(best[0].second >= best[1].second);
        QVERIFY(result.similarity(best[0].first) > result.similarity(lib_ + QStringLiteral("/Hats/Hat 1.wav")));
        QVERIFY(std::isnan(result.similarity(outside)));
    }

    void theSamplersArrowsStepThroughSimilarSounds() {
        make();
        QVERIFY(settle());
        test::EditorFixture f;
        SimilarSamples similar(&f.project, &f.editor, similarity_.get());
        QSignalSpy messages(&similar, &SimilarSamples::statusMessage);
        const QString track = f.editor.addMidiTrack(-1, {}, kSamplerKind);
        const QString sampler = f.track(track).devices[0].id;
        const auto values = [&] { return deviceState::fromModel(f.project.device(track, sampler).state); };
        const auto sample = [&] { return samplerState::sample(values()); };
        const auto step = [&](int direction) {
            similar.step(track, sampler, direction);
            return QTest::qWaitFor([&] { return !similar.searching(); }, 20000);
        };
        QVERIFY(step(1));  // no sample: nothing to step from
        QCOMPARE(sample(), QString());
        QVERIFY(!messages.isEmpty());
        const QString kick = lib_ + QStringLiteral("/Kicks/Kick 1.wav");
        f.editor.loadSample(track, sampler, kick);
        QVERIFY(!similar.canReturn(track, sampler));
        // Forward: the other kicks first, most like it first; the reference stays Kick 1.
        QVERIFY(step(1));
        const QString first = sample();
        QCOMPARE(kindOf(QFileInfo(first).fileName()), QStringLiteral("Kick"));
        QVERIFY(first != kick);
        QCOMPARE(samplerState::reference(values()), kick);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Swap Similar Sample"));
        QVERIFY(similar.canReturn(track, sampler));
        QVERIFY(step(1));
        const QString second = sample();
        QCOMPARE(kindOf(QFileInfo(second).fileName()), QStringLiteral("Kick"));
        QVERIFY(second != kick && second != first);
        // Back: the one before, then the reference itself.
        QVERIFY(step(-1));
        QCOMPARE(sample(), first);
        QVERIFY(step(-1));
        QCOMPARE(sample(), kick);
        QVERIFY(!similar.canReturn(track, sampler));
        // Return to Reference.
        QVERIFY(step(1));
        QVERIFY(step(1));
        QCOMPARE(sample(), second);
        similar.returnToReference(track, sampler);
        QCOMPARE(sample(), kick);
        QCOMPARE(f.stack.undoText(), QStringLiteral("Return to Reference"));
        // Another reference saved: the steps come from it (the hats).
        f.editor.loadSample(track, sampler, lib_ + QStringLiteral("/Hats/Hat 1.wav"));
        similar.saveReference(track, sampler);
        QCOMPARE(samplerState::reference(values()), lib_ + QStringLiteral("/Hats/Hat 1.wav"));
        QVERIFY(step(1));
        QCOMPARE(kindOf(QFileInfo(sample()).fileName()), QStringLiteral("Hat"));
        QVERIFY(sample() != lib_ + QStringLiteral("/Hats/Hat 1.wav"));
    }

    void theDrumRacksArrowsStepItsUnlockedPads() {
        make();
        QVERIFY(settle());
        test::EditorFixture f;
        SimilarSamples similar(&f.project, &f.editor, similarity_.get());
        const QString track = f.editor.addMidiTrack(-1, {}, kDrumRackKind);
        const QString rack = f.track(track).devices[0].id;
        const QString kick = lib_ + QStringLiteral("/Kicks/Kick 1.wav");
        const QString hat = lib_ + QStringLiteral("/Hats/Hat 1.wav");
        const QString snare = lib_ + QStringLiteral("/Snares/Snare 1.wav");
        const QString kickPad = f.editor.loadDrumPad(track, rack, 36, kick);
        const QString hatPad = f.editor.loadDrumPad(track, rack, 42, hat);
        const QString snarePad = f.editor.loadDrumPad(track, rack, 38, snare);
        f.editor.setChainParam(track, hatPad, QStringLiteral("locked"), 1.0);
        QCOMPARE(similar.rackSamplers(track, rack).size(), 2);  // (not the locked one)
        const auto padSample = [&](const QString& pad) {
            return samplerState::sample(deviceState::fromModel(padSampler(f.project.chain(track, pad))->state));
        };
        const int steps = f.stack.count();
        similar.stepRack(track, rack, 1);
        QVERIFY(QTest::qWaitFor([&] { return !similar.searching(); }, 20000));
        QCOMPARE(f.stack.count(), steps + 1);  // one undo step for every pad
        QCOMPARE(f.stack.undoText(), QStringLiteral("Swap Similar Samples"));
        QVERIFY(padSample(kickPad) != kick);
        QCOMPARE(kindOf(QFileInfo(padSample(kickPad)).fileName()), QStringLiteral("Kick"));
        QVERIFY(padSample(snarePad) != snare);
        QCOMPARE(kindOf(QFileInfo(padSample(snarePad)).fileName()), QStringLiteral("Snare"));
        QCOMPARE(padSample(hatPad), hat);  // locked
        QCOMPARE(f.project.chain(track, kickPad).name, QFileInfo(padSample(kickPad)).completeBaseName());
        QVERIFY(similar.rackCanReturn(track, rack));
        similar.returnRackToReferences(track, rack);
        QCOMPARE(padSample(kickPad), kick);
        QCOMPARE(padSample(snarePad), snare);
        QVERIFY(!similar.rackCanReturn(track, rack));
        // Their samples saved as their references: one undo step for them all (none when they are already).
        const int before = f.stack.count();
        similar.saveRackReferences(track, rack);
        QCOMPARE(f.stack.count(), before + 1);
        QCOMPARE(samplerState::reference(deviceState::fromModel(padSampler(f.project.chain(track, kickPad))->state)), kick);
        similar.saveRackReferences(track, rack);
        QCOMPARE(f.stack.count(), before + 1);
        // Every pad locked: nothing to step.
        for (const QString& pad : {kickPad, snarePad}) f.editor.setChainParam(track, pad, QStringLiteral("locked"), 1.0);
        QSignalSpy messages(&similar, &SimilarSamples::statusMessage);
        similar.stepRack(track, rack, 1);
        QVERIFY(!similar.searching());
        QCOMPARE(messages.count(), 1);
    }

    void filesFoundLaterAreAnalysedToo() {
        make();
        QVERIFY(settle());
        write(QStringLiteral("Kicks/Kick 4.wav"), kick(140, 52, 0.26));
        browser_->rescan();
        QVERIFY(QTest::qWaitFor([this] { return similarity_->analysedFiles() == 11 && !similarity_->analysing(); }, 20000));
        browser_->findSimilar(lib_ + QStringLiteral("/Kicks/Kick 1.wav"));
        QVERIFY(settle());
        QVERIFY(names(*browser_->results()).mid(0, 4).contains(QStringLiteral("Kick 4.wav")));
    }

    void aSoundThatCantBeAnalysed() {
        make();
        QVERIFY(settle());
        const QString junk = path(QStringLiteral("junk.wav"));
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a sound");
        file.close();
        QSignalSpy messages(browser_.get(), &BrowserController::statusMessage);
        browser_->findSimilar(junk);
        QVERIFY(settle());
        QCOMPARE(browser_->results()->total(), 0);
        QCOMPARE(browser_->statusText(), QStringLiteral("Could not analyse junk.wav"));
        QCOMPARE(messages.count(), 1);
        QVERIFY(messages.at(0).at(0).toString().startsWith(QStringLiteral("Could not analyse junk.wav: ")));
    }

    // Samples are found by what they sound like, whatever their names: a kick
    // named "Untitled" is found by "kick" (after those named so) and listed
    // under Samples › Kicks; its row says what it is.
    void samplesAreFoundByHowTheySound() {
        write(QStringLiteral("Misc/Untitled 1.wav"), kick(160, 48, 0.28));
        write(QStringLiteral("Misc/Untitled 2.wav"), hat(0.025, 40));
        make();
        QVERIFY(settle());
        similarity_->takeTags();
        QVERIFY(similarity_->tags() && similarity_->tags()->size() == 12);
        QCOMPARE(browser_->sort(), kBestSort);
        browser_->setSearchText(QStringLiteral("kick"));
        QVERIFY(settle());
        const QStringList found = names(*browser_->results());
        QVERIFY2(found.contains(QStringLiteral("Untitled 1.wav")), qPrintable(found.join(QLatin1Char(','))));
        QVERIFY(!found.contains(QStringLiteral("Untitled 2.wav")));
        QVERIFY(!found.contains(QStringLiteral("Hat 1.wav")));
        QVERIFY(found.indexOf(QStringLiteral("Kick 1.wav")) < found.indexOf(QStringLiteral("Untitled 1.wav")));  // (named so first)
        // Its row says what it is.
        const int row = static_cast<int>(found.indexOf(QStringLiteral("Untitled 1.wav")));
        const QString label = browser_->results()->get(row).value(QStringLiteral("tags")).toString();
        QVERIFY2(label.startsWith(QStringLiteral("Kick")), qPrintable(label));
        QVERIFY(browser_->results()->get(row).value(QStringLiteral("toolTip")).toString().contains(QStringLiteral("Kick")));
        // Samples › Kicks: every kick, named so or not, and nothing else. (Samples starts
        // collapsed: showing one of its sounds expands it.)
        const QStringList kicksScope{QStringLiteral("sounds"), QStringLiteral("kick")};
        QVERIFY(!browser_->isExpanded(QStringLiteral("samples")));
        QCOMPARE(browser_->sidebar()->find(kicksScope), -1);
        browser_->setSearchText(QString());
        browser_->setScope(kicksScope);
        QVERIFY(browser_->isExpanded(QStringLiteral("samples")));
        QVERIFY(browser_->sidebar()->find(kicksScope) >= 0);
        QCOMPARE(browser_->scope(), kicksScope);
        QVERIFY(settle());
        QStringList kicks = names(*browser_->results());
        kicks.sort();
        QCOMPARE(kicks, (QStringList{QStringLiteral("Kick 1.wav"), QStringLiteral("Kick 2.wav"), QStringLiteral("Kick 3.wav"),
                                     QStringLiteral("Untitled 1.wav")}));
        // Words the tagger doesn't know still match names (and only names).
        browser_->setScope({QStringLiteral("samples")});
        browser_->setSearchText(QStringLiteral("untitled"));
        QVERIFY(settle());
        QCOMPARE(names(*browser_->results()).size(), 2);
    }

    // The Best sort: with nothing typed, what fits the project comes first: the
    // sounds most like the project's own.
    void bestPutsWhatFitsTheProjectFirst() {
        make();
        Project project;
        Harmony harmony(&project);
        SoundSuggestions suggestions(&project, &harmony, similarity_.get());
        QVERIFY(settle());
        similarity_->takeTags();
        suggestions.update();
        QVERIFY(!suggestions.fit());  // (no sounds in the project, no key: nothing to fit)
        project.insertTrack(test::makeTrack(QStringLiteral("t"), QStringLiteral("Drums")), 0);
        project.setClips(QStringLiteral("t"), {Clip::audio(QStringLiteral("c"), lib_ + QStringLiteral("/Hats/Hat 1.wav"),
                                                           QStringLiteral("Hat 1"), 0.0, 0.4)});
        suggestions.update();
        const auto fit = suggestions.fit();
        QVERIFY(fit);
        auto fitOf = [&](const QString& file) {
            const std::string path = toBackendPath(lib_ + QLatin1Char('/') + file);
            const size_t cut = path.find_last_of("/\\");
            return fit(sub::browser::fileHash(sub::browser::folderHash(path.substr(0, cut)), path.substr(cut + 1)));
        };
        QVERIFY(fitOf(QStringLiteral("Hats/Hat 2.wav")) > fitOf(QStringLiteral("Kicks/Kick 2.wav")));
        QCOMPARE(fitOf(QStringLiteral("Nowhere/None.wav")), 0.5f);
    }

    void audioFilesOfferFindSimilar() {
        make();
        QVERIFY(settle());
        auto actions = [this](int row) {
            QStringList ids;
            for (const QVariant& a : browser_->resultActions(row)) ids << a.toMap().value(QStringLiteral("action")).toString();
            return ids;
        };
        QCOMPARE(actions(0), QStringList({QStringLiteral("findSimilar"), QString(), QStringLiteral("showInFolder"), QString(),
                                          QStringLiteral("toggleTags")}));
        // Hide Tags: kept in the settings, the entry then Show Tags.
        QVERIFY(browser_->tagsShown());
        QCOMPARE(browser_->resultActions(0).last().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Hide Tags"));
        browser_->setTagsShown(false);
        QCOMPARE(QSettings().value(QStringLiteral("browser/tags_shown")).toBool(), false);
        QCOMPARE(browser_->resultActions(0).last().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Show Tags"));
        browser_->setTagsShown(true);
        // Without a sound similarity, not offered.
        browser_.reset();
        QSettings().setValue(QStringLiteral("browser/places"), QStringList{lib_});
        BrowserController::Options options;
        options.scanPlugins = false;
        options.indexPath = QString();
        browser_ = std::make_unique<BrowserController>(nullptr, options);
        QVERIFY(settle());
        QVERIFY(!browser_->canFindSimilar());
        QCOMPARE(actions(0), QStringList({QStringLiteral("showInFolder"), QString(), QStringLiteral("toggleTags")}));
        browser_->findSimilar(lib_ + QStringLiteral("/Kicks/Kick 1.wav"));
        QCOMPARE(browser_->similarTo(), QString());
    }

private:
    QString path(const QString& relative) const { return tmp_->filePath(relative); }

    void write(const QString& relative, const std::vector<float>& samples) {
        const QString file = lib_ + QLatin1Char('/') + relative;
        QDir().mkpath(QFileInfo(file).absolutePath());
        test::writeWav(file, samples);
    }

    void make() {
        QSettings().setValue(QStringLiteral("browser/places"), QStringList{lib_});
        SoundSimilarity::Options s;
        s.storePath = QString();
        s.threads = 2;
        s.background = false;
        s.refreshSeconds = 0.0;
        similarity_ = std::make_unique<SoundSimilarity>(s);
        BrowserController::Options options;
        options.scanPlugins = false;
        options.indexPath = QString();
        options.similarity = similarity_.get();
        browser_ = std::make_unique<BrowserController>(nullptr, options);
    }

    // Until nothing is on its way: indexing, analysing, searching.
    bool settle() {
        auto quiet = [this] {
            return !browser_->searching() && !browser_->indexing() && (!similarity_ || !similarity_->analysing());
        };
        for (int i = 0; i < 2; ++i) {
            if (!QTest::qWaitFor(quiet, 20000)) return false;
            QTest::qWait(50);  // (what the backends say next: their wakes are queued)
        }
        return quiet();
    }

    std::unique_ptr<QTemporaryDir> tmp_;
    QString lib_;
    std::unique_ptr<SoundSimilarity> similarity_;
    std::unique_ptr<BrowserController> browser_;
};

QTEST_GUILESS_MAIN(TestSoundSimilarity)
#include "test_sound_similarity.moc"
