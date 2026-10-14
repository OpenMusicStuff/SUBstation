#pragma once
// The MCP server's edits of notes where they are, automation, tracks and
// devices (McpEdits.cpp), added to the session's tools by addSessionTools().

#include <QString>

namespace sub::app {
class Session;
}

namespace sub::app::mcp {

class Protocol;

// The tools: edit_notes, write_automation, add_track, delete_tracks,
// move_tracks, group_tracks (and ungrouping), list_devices, add_device,
// remove_devices, move_devices. `edits`: the sentence every edit's
// description ends with (the agents' history).
void addEditTools(Protocol& protocol, Session& session, const QString& edits);

}  // namespace sub::app::mcp
