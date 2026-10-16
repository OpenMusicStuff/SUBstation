#pragma once
// What the project sounds like: Session.suggestions (the agents read it).
//
// The project's own sounds (the files its audio clips and samplers play, as
// the sound tagger hears them: tagging::ProjectFit) say what it is ("Techno,
// Punchy"), a sound fits the closer its description is to the nearest of them
// and the more it is what they are; and the key (the project's, or the one its
// notes are in: Session.harmony) says which notes fit. A change to the
// project's clips or devices, to the key, or to the tags (more files analysed)
// makes them again, kSettleMs after the last of a burst, then `changed`.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>

#include "tagging/ProjectFit.h"
#include "tagging/SoundTagTable.h"

namespace sub::app {

class Project;
class Harmony;
class SoundSimilarity;

class SoundSuggestions : public QObject {
    Q_OBJECT
    // What the project sounds like, as the browser says it: "Techno, Punchy · A Minor"; "" for nothing yet.
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    static constexpr int kSettleMs = 300;

    SoundSuggestions(Project* project, Harmony* harmony, SoundSimilarity* similarity, QObject* parent = nullptr);

    QString summary() const { return summary_; }
    // How well sounds fit the project (ProjectFit::fit).
    const intelligence::tagging::ProjectFit& projectFit() const { return *fit_; }
    // Makes them again now (tests; otherwise kSettleMs after a change).
    void update();

Q_SIGNALS:
    void changed();

private:
    void invalidate();

    QPointer<Project> project_;
    QPointer<Harmony> harmony_;
    QPointer<SoundSimilarity> similarity_;
    QTimer settle_;
    std::shared_ptr<const intelligence::tagging::ProjectFit> fit_;
    std::shared_ptr<const intelligence::tagging::SoundTagTable> tags_;
    QString summary_;
};

}  // namespace sub::app
