#pragma once
// What the MCP server's tools share (McpTools.cpp: reading, and the first
// edits; McpEdits.cpp: editing notes, automation, tracks and devices): JSON
// as the model reads it, beats and bars, stretches of the song, device
// parameters and the values asked for them, and edits as changes in the
// agents' history. Not used outside mcp/.

#include "mcp/McpProtocol.h"
#include "model/ParamSpec.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sub::app {
struct Device;
class Project;
class ProjectEditor;
class Session;
}  // namespace sub::app

namespace sub::app::mcp {

// --- JSON as the model reads it ---

QJsonArray strings(const std::vector<std::string>& values);
// An input schema: an object of these properties, nothing else.
QJsonObject schema(const QJsonObject& properties = {}, const QStringList& required = {});
QJsonObject property(const QString& type, const QString& description);
QJsonValue orNull(const QString& value);
// A number as the model reads it: no more digits than it means.
QString number(double value, int decimals = 2);
// Whether every word of `query` is in `text` (any case).
bool matches(const QString& text, const QStringList& words);
// A beat as the model reads it: to a ten-thousandth (a 1/2560th note), not 579.7826893194218.
double beat(double value);
// A level, a pan, a value as the model reads it: to a hundredth.
double rounded(double value);
// A pitch as given: a MIDI note number, or a name as notes::noteName() writes
// them ("C3" is 60; "F#2", "Bb4", "C-1"). None: neither, or out of 0..127.
std::optional<int> pitchOf(const QJsonValue& value);

// What an edit says when the user turned edits off.
extern const QString kNoEdits;

// A stretch of the song (beats); none and why not.
struct Span {
    double from = 0.0;
    double to = std::numeric_limits<double>::infinity();
    QString error;
};

// A device's parameter: what the engine says of it, and its id on the
// device ("cutoff", a plug-in's number, "device:on", "macro1", "chain:<id>:volume").
struct Param {
    ParamSpec spec;
    QString id;
};

// The session as the tools see it: the project, its bars, and edits.
class ToolContext {
public:
    explicit ToolContext(Session& session) : session_(session) {}

    const Project& project() const;

    double beatsPerBar() const;
    // A beat as a bar: 1 + beats / beats a bar, so bar 17 is 17 and halfway through it 17.5.
    double bar(double beatValue) const;
    // A stretch as the user reads it: "bars 17 to 24" (whole bars), "bar 5", else "beats 3.5 to 6".
    QString stretch(double from, double to) const;
    // The stretch the arguments name: start_beat or start_bar (default: the
    // start), end_beat or end_bar (default: the end). Bars are places, as the
    // tools give them: bars 17 to 24 are start_bar 17, end_bar 25.
    // `required`: both ends must be given.
    Span span(const QJsonObject& arguments, bool required = false) const;

    // A device's parameters, as the engine describes them.
    std::vector<Param> params(const QString& trackId, const Device& device) const;
    // What a parameter is to be set to: `value` in its units (a choice's name,
    // true or false for a switch), or `normalized` (0..1). Plain; none and why not.
    static std::pair<std::optional<double>, QString> wanted(const Param& param, const QJsonObject& item);

    // Whether the user lets agents edit.
    bool editsAllowed() const;
    // Runs `edit` as one change in the agents' history (an EditError it throws:
    // nothing changed, the tool fails saying why); the result (`out`) says
    // what it did, and its id.
    ToolResult change(const QString& text, const QString& tool, QJsonObject out,
                      const std::function<void(ProjectEditor&)>& edit);

protected:
    Session& session_;
};

}  // namespace sub::app::mcp
