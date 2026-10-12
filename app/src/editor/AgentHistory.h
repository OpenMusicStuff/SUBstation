#pragma once
// What agents changed (Session.agentChanges): the edits they make through the
// MCP server, kept apart from the user's undo stack, each one a change the user
// (or the agent) can rewind on its own and restore again.
//
// An agent edits through an editor of its own (editor(): a ProjectEditor on a
// stack of its own), so its edits are the same undo commands the user's are,
// but Ctrl+Z never takes one back. record() runs one tool call's edits as one
// change (a macro on that stack), listed here with what it did; one refused
// (an EditError, a frozen track) is taken back whole, and changes nothing.
//
// Two histories edit one project, and commands are snapshots (Commands.h): an
// undo puts back what was there before, whatever came after. So each command
// says what it touches (footprint(): regions such as "track/<id>/clips",
// "track/<id>/devices/<id>/param/<id>", "auto/<owner>/<key>"; a region holds
// those below it), and a step of either history is refused while the other has
// a step on the same region after it: the user can't undo (or redo) what an
// agent changed since, until that change is rewound, and an agent's change
// can't be rewound (or restored) over what the user, or a later change, did
// to the same since. Steps of the user's stack are timed as they happen (its
// indexChanged), the agents' as they record, rewind and restore. Within the
// user's stack, undo goes in order as ever; changes may go in any order.
//
// The project's state, and whether it has changes to save, is both
// histories': clean() is this one's (Session.clean asks both). Opening or
// starting a project clears it (clear()).

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

class QUndoCommand;
class QUndoStack;

namespace sub::app {

class Project;
class ProjectEditor;

class AgentHistory : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // How many of them are in effect (not rewound).
    Q_PROPERTY(int appliedCount READ appliedCount NOTIFY countChanged)

public:
    // One tool call's edits.
    struct Change {
        int id = 0;            // 1, 2...: in the order made
        QString text;          // what it did ("Set Cutoff of Serum 2 on 4 Bass to 1.2 kHz")
        QString tool;          // the tool that made it ("set_device_parameters")
        QDateTime time;        // when it was made
        bool applied = true;   // in effect (false: rewound)
        QStringList tracks;    // the tracks (returns, the master) it changed, by id
        quint64 step = 0;      // when it was made, rewound or restored last (the histories' clock)
        QUndoCommand* command = nullptr;  // its edits (the agents' stack holds it)
    };

    enum Role {
        IdRole = Qt::UserRole + 1,
        TextRole,
        ToolRole,
        TimeRole,     // "14:02:51"
        AppliedRole,
    };
    Q_ENUM(Role)

    // `userStack`: the user's undo stack, timed alongside.
    AgentHistory(Project* project, QUndoStack* userStack, QObject* parent = nullptr);
    ~AgentHistory() override;

    // The agents' editor: what record() runs edits with.
    ProjectEditor* editor() const { return editor_; }

    // Runs `edit` with the agents' editor as one change, named `text`. Its id;
    // none if nothing changed. An EditError it throws, or an edit refused
    // (frozen), takes back whatever it did and comes out as an EditError.
    std::optional<int> record(const QString& text, const QString& tool,
                              const std::function<void(ProjectEditor&)>& edit);

    // Every change, the first first.
    const std::vector<Change>& changes() const { return changes_; }
    const Change* find(int id) const;
    int count() const { return static_cast<int>(changes_.size()); }
    int appliedCount() const;

    // Rewind a change (take back its edits), or restore one rewound: "" when it
    // went, else why not (it isn't there; it is already so; the user, or a later
    // change, changed the same since).
    Q_INVOKABLE QString rewind(int id);
    Q_INVOKABLE QString restore(int id);
    // Rewinds the changes in effect from `id` on (every one: 0), the last
    // first; it stops at one that can't be (and says why: "" if all went). The
    // ids rewound are added to `rewound`, if given.
    QString rewindFrom(int id, QList<int>* rewound = nullptr);
    Q_INVOKABLE QString rewindAll() { return rewindFrom(0); }

    // Why the user's next undo (redo) can't be made now: an agent changed the
    // same since ("": it can).
    QString undoProblem() const;
    QString redoProblem() const;

    // Whether the project has no changes from agents to save; set when it is saved.
    bool clean() const { return clean_; }
    void setClean();
    // Forgets every change (another project): clean.
    void clear();

    // What a command touches (see above): its regions, "*" for everything.
    static QStringList footprint(const QUndoCommand& command, const Project& project);
    // Whether two footprints share a region (one in the other counts).
    static bool overlaps(const QStringList& a, const QStringList& b);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();
    void cleanChanged();
    // A change was made, rewound or restored (its id).
    void changed(int id);

private:
    // The user's stack moved to `index`: the steps it took timed.
    void userIndexChanged(int index);
    // Why `command` (a change's, or the user's) can't be taken back (`applying`
    // false) or put back (true) now, having last moved at `since`: a step of
    // the other history (or another change) on the same region after it.
    QString problem(const QUndoCommand& command, quint64 since, bool applying, const Change* self) const;
    QString transition(int id, bool apply);
    void markDirty();
    int rowOf(int id) const;

    Project* project_;
    QUndoStack* userStack_;
    QUndoStack* stack_;  // the agents'
    ProjectEditor* editor_;
    std::vector<Change> changes_;
    QHash<const QUndoCommand*, quint64> userSteps_;  // the user's commands: when each moved last
    int userIndex_ = 0;
    quint64 clock_ = 0;
    int nextId_ = 1;
    bool clean_ = true;
};

}  // namespace sub::app
