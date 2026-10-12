#pragma once
// Mapping a modulator by touching what it should move (as Ableton's Map
// button): start(track, modulator) arms it; the next parameter the user takes
// hold of (a knob pressed or turned, a plug-in's control, a handle in an
// editor's graph: ProjectEditor::parameterTouched) on the modulator's track is
// mapped to it over its whole range, and it stops. Touching the modulator's own
// controls doesn't count (adjusting it while it waits); a parameter on another
// track, the mixer's or one that can't be modulated says why (statusMessage)
// and it keeps waiting. stop() (Escape, the button again) gives up; so does the
// modulator going away.

#include <QObject>
#include <QString>

namespace sub::app {

class Project;
class ProjectEditor;

class ModulationLearn : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString trackId READ trackId NOTIFY changed)
    Q_PROPERTY(QString modulatorId READ modulatorId NOTIFY changed)

public:
    ModulationLearn(Project* project, ProjectEditor* editor, QObject* parent = nullptr);

    bool active() const { return !modulatorId_.isEmpty(); }
    QString trackId() const { return trackId_; }
    QString modulatorId() const { return modulatorId_; }

    Q_INVOKABLE void start(const QString& trackId, const QString& modulatorId);
    Q_INVOKABLE void stop();
    // Starts it for this modulator, or stops it if it waits for this one already.
    Q_INVOKABLE void toggle(const QString& trackId, const QString& modulatorId);
    // Whether it waits for this modulator.
    Q_INVOKABLE bool isLearning(const QString& modulatorId) const { return active() && modulatorId == modulatorId_; }

    // A parameter taken hold of (owner, automation key): mapped if it waits and it can be.
    void touched(const QString& owner, const QString& key);

Q_SIGNALS:
    void changed();
    void statusMessage(const QString& message);

private:
    void followDevices(const QString& trackId);

    Project* project_;
    ProjectEditor* editor_;
    QString trackId_;
    QString modulatorId_;
};

}  // namespace sub::app
