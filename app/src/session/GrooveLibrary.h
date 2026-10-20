#pragma once
// The project's grooves for the UI (Session.grooves; model/Grooves.h): its
// library, the built-in grooves and the user's groove files to add from, the
// groove editor's edits, what a Groove device and a clip play, and the default
// groove (the Edit menu's, the transport's Swing). It reads the project and
// edits it through the editor (each edit undoable). The user's groove files are
// the preset library's Grooves folder (libraryDir()/Grooves: .gilgroove and
// .agr files), listed in the browser as presets are (a groove file is a Groove
// device's preset: io/Presets.h).
//
// A groove, for QML (groove(), and the `groove` of what plays one):
// {id, name, step (beats), stepIndex (of stepNames; -1: none of them), bars,
//  base (beats), steps (in a cycle), lanes: [{keys: [...], steps: [{timing, ms,
//  velocity, length}]}], notes: [{beat, key, velocity}] (what it was made from,
//  in its cycle's beats)}.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <vector>

#include "model/GrooveTypes.h"

namespace sub::app {

class Project;
class ProjectEditor;

// The user's groove files' folder: the preset library's Grooves folder.
QString grooveFilesDir(const QString& root = {});
// A groove (and the notes it came from) as QML has it (above).
QVariantMap grooveToMap(const groove::Groove& groove, const std::vector<groove::NoteTime>& notes = {});

class GrooveLibrary : public QObject {
    Q_OBJECT
    // The project's grooves, in order: [{id, name}].
    Q_PROPERTY(QVariantList grooves READ grooves NOTIFY groovesChanged)
    // The built-in grooves: [{id, name}].
    Q_PROPERTY(QVariantList builtins READ builtins CONSTANT)
    // The user's groove files: [{path, name}], by name.
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    // The steps a groove's grid offers ("1/8", "1/8T", "1/16", "1/16T", "1/32"; groove::kSteps).
    Q_PROPERTY(QStringList stepNames READ stepNames CONSTANT)
    // The default groove: its kind ("": none, "swing", "library", "own"), its Swing (50: none), its
    // library groove's id ("": none), and how it is shown ("None", "Swing 58.0 % (1/16)", its name).
    Q_PROPERTY(QString defaultKind READ defaultKind NOTIFY defaultChanged)
    Q_PROPERTY(double defaultSwing READ defaultSwing NOTIFY defaultChanged)
    Q_PROPERTY(QString defaultGrooveId READ defaultGrooveId NOTIFY defaultChanged)
    Q_PROPERTY(QString defaultName READ defaultName NOTIFY defaultChanged)
    // Counts the project's changes that change what plays a groove (devices, clips, the library, the
    // default): bindings on deviceGroove() and clipGroove() read it to be worked out again.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    // `root`: the preset library's folder ("": libraryDir()).
    GrooveLibrary(Project* project, ProjectEditor* editor, QObject* parent = nullptr, const QString& root = {});

    QVariantList grooves() const;
    static QVariantList builtins();
    QVariantList files() const { return files_; }
    static QStringList stepNames();
    QString defaultKind() const;
    double defaultSwing() const;
    QString defaultGrooveId() const;
    QString defaultName() const;
    int revision() const { return revision_; }

    // A groove of the library, a built-in one, or Swing's ({}: none).
    Q_INVOKABLE QVariantMap groove(const QString& grooveId) const;
    Q_INVOKABLE static QVariantMap builtinGroove(const QString& builtinId);
    Q_INVOKABLE static QVariantMap swingGroove(double percent, int stepIndex);
    // What a Groove device plays: {kind: "swing" | "project" | "library" | "own", grooveId, name, groove}.
    Q_INVOKABLE QVariantMap deviceGroove(const QString& trackId, const QString& deviceId) const;
    // A clip's groove: {kind ("": none; "swing", "project", "library", "own"), grooveId, name, swing,
    // stepIndex, timing, velocity, random, quantize (fractions), groove (what it plays; {}: none)}.
    Q_INVOKABLE QVariantMap clipGroove(const QString& trackId, const QString& clipId) const;
    // The library's groove a name is (by id or name, as agents name them); "": none.
    Q_INVOKABLE QString find(const QString& idOrName) const;

    // A Groove device's source ("swing", "project", "library" with `grooveId`, "own"), one undo step
    // (ProjectEditor::setDeviceGrooveSource); Apply Groove on it (ProjectEditor::applyDeviceGroove).
    Q_INVOKABLE void setDeviceSource(const QString& trackId, const QString& deviceId, const QString& kind,
                                     const QString& grooveId = {});
    Q_INVOKABLE bool applyDevice(const QString& trackId, const QString& deviceId);

    // Into the library; the new groove's id ("": it couldn't be: statusMessage says why).
    Q_INVOKABLE QString addBuiltin(const QString& builtinId);
    Q_INVOKABLE QString addFile(const QString& path);
    Q_INVOKABLE QString addSwing(double percent, int stepIndex);
    // Extract Groove from a clip (ProjectEditor::extractGroove), on steps of stepNames()[stepIndex].
    Q_INVOKABLE QString extract(const QString& trackId, const QString& clipId, int stepIndex, int bars,
                                bool perKeyLanes);
    // A Groove device's own groove (what it plays now) into the library, and the device then playing it.
    Q_INVOKABLE QString addFromDevice(const QString& trackId, const QString& deviceId);
    Q_INVOKABLE QString duplicate(const QString& grooveId);
    Q_INVOKABLE void rename(const QString& grooveId, const QString& name);
    Q_INVOKABLE void remove(const QString& grooveId);
    // A library groove saved among the user's groove files (named as it is); its path ("": failed).
    Q_INVOKABLE QString saveFile(const QString& grooveId);
    // The user's groove files listed again.
    Q_INVOKABLE void refreshFiles();

    // The groove editor: a step's `field` ("timing": of a step, -0.5..0.5; "ms"; "velocity": -1..1;
    // "length": 0.25..2) in lane `lane`, one drag one undo step (its merge key); a lane added for keys
    // (its steps as the default lane's), one's keys, one removed (not the default lane); a lane's steps
    // back to straight; the cycle's bars (1, 2, 4: the steps repeated or cut) and the base grid.
    Q_INVOKABLE void setStep(const QString& grooveId, int lane, int step, const QString& field, double value,
                             const QString& mergeKey = {});
    Q_INVOKABLE void addLane(const QString& grooveId, const QVariantList& keys);
    Q_INVOKABLE void setLaneKeys(const QString& grooveId, int lane, const QVariantList& keys);
    Q_INVOKABLE void removeLane(const QString& grooveId, int lane);
    Q_INVOKABLE void resetLane(const QString& grooveId, int lane);
    Q_INVOKABLE void setBars(const QString& grooveId, int bars);
    Q_INVOKABLE void setBase(const QString& grooveId, double baseBeats);

    // The default groove: Swing (percent; 50: none, straight), a library groove ("": none), none.
    Q_INVOKABLE void setDefaultSwing(double percent, const QString& mergeKey = {});
    Q_INVOKABLE void setDefaultGroove(const QString& grooveId);
    Q_INVOKABLE void clearDefault();

Q_SIGNALS:
    void groovesChanged();
    void filesChanged();
    void defaultChanged();
    void revisionChanged();
    void statusMessage(const QString& message);

private:
    void edit(const QString& grooveId, const std::function<void(groove::Groove&)>& change, const QString& mergeKey = {});
    // An edit made, or why it couldn't be (statusMessage).
    bool tried(const std::function<void()>& change);
    void bump();

    Project* project_;
    ProjectEditor* editor_;
    QString root_;
    QVariantList files_;
    int revision_ = 0;
};

}  // namespace sub::app
