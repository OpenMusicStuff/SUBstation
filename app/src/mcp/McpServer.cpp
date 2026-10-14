#include "mcp/McpServer.h"

#include "AppInfo.h"
#include "mcp/McpHttpServer.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpTools.h"

#include <QSettings>

#include <algorithm>

namespace sub::app {

namespace {

const QString kInstructions = QStringLiteral(
    "SUBstation is a digital audio workstation (DAW). These tools read and edit the project open in it right now, as "
    "the user works on it: its settings, its tracks and what each one is (a label worked out from its name, "
    "instrument presets, audio files, notes, effects and mixer), its devices, their parameters and automation, its "
    "clips, the notes, and the song's chords and key. Times are in beats (quarter notes) from the start of the song, "
    "and in bars: startBar and endBar are places (bar 17 starts at 17, halfway through it is 17.5), so a stretch "
    "over bars 17 to 24 is startBar 17, endBar 25, and the tools take start_bar and end_bar so. Levels are in dB; "
    "pan from -1 (left) to 1 (right). Call get_project first (the user's selection and their last edits), then "
    "get_arrangement_map (where each track plays, a character a bar, and the song's sections) and list_tracks; ids "
    "from those go to get_track. For what the user selected (\"this part\", \"the selection\"), "
    "get_selection_context gives it all in one call: its tracks, their notes there and the chords. Notes come as "
    "rows [startBeat, length, pitch, velocity], and write_midi_clip takes them so; notes with pitch bends or "
    "vibrato are listed in `bent`, and kept only when written as objects (get_notes format \"objects\"). To change "
    "notes that are there (transpose, quantize, velocities...), edit_notes; to write a new part, write_midi_clip. "
    "Devices to add (built-in ones, plug-ins, presets) come from list_devices. The edit tools "
    "(set_device_parameters, edit_notes, write_automation, add_track, add_device...) change the project as the "
    "user watches: each call is one "
    "change in the agent's history, which the user sees apart from their own undo and can rewind (as can "
    "rewind_agent_changes). Change what was asked for, and say what you changed.");

}  // namespace

McpServer::McpServer(Session& session, QObject* parent)
    : QObject(parent),
      protocol_(std::make_unique<mcp::Protocol>(QString::fromLatin1(kAppName), version(), kInstructions)),
      http_(std::make_unique<mcp::HttpServer>(*protocol_)) {
    mcp::addSessionTools(*protocol_, session);
    QSettings settings;
    enabled_ = settings.value(kEnabledKey, false).toBool();
    port_ = std::clamp(settings.value(kPortKey, kDefaultPort).toInt(), 1, 65535);
    editsAllowed_ = settings.value(kEditsKey, true).toBool();
    apply(false);
}

McpServer::~McpServer() = default;

bool McpServer::running() const { return http_->isListening(); }

QString McpServer::url() const {
    return QStringLiteral("http://127.0.0.1:%1%2").arg(port_).arg(mcp::HttpServer::kPath);
}

QString McpServer::claudeCodeCommand() const {
    return QStringLiteral("claude mcp add --transport http substation %1").arg(url());
}

void McpServer::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    QSettings().setValue(kEnabledKey, enabled);
    apply(true);
}

void McpServer::setEditsAllowed(bool allowed) {
    if (allowed == editsAllowed_) return;
    editsAllowed_ = allowed;
    QSettings().setValue(kEditsKey, allowed);
    Q_EMIT changed();
}

void McpServer::setPort(int port) {
    port = std::clamp(port, 1, 65535);
    if (port == port_) return;
    port_ = port;
    QSettings().setValue(kPortKey, port);
    apply(enabled_);
}

void McpServer::apply(bool announce) {
    http_->close();
    error_.clear();
    if (enabled_ && !http_->listen(static_cast<quint16>(port_))) {
        error_ = http_->error();
        Q_EMIT changed();
        Q_EMIT statusMessage(tr("The MCP server couldn't start on port %1: %2").arg(port_).arg(error_));
        return;
    }
    Q_EMIT changed();
    if (!announce) return;
    Q_EMIT statusMessage(enabled_ ? tr("MCP server on at %1").arg(url()) : tr("MCP server off"));
}

}  // namespace sub::app
