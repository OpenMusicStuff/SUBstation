#pragma once
// The MCP server's tools: what an agent can learn of the project open in
// SUBstation. Read only, for now: the project and its settings, its tracks
// with what each is (their labels: TrackLabels), one track in full (its
// devices, clips, sends, routing), the song's chords and key (Harmony), and
// tracks found by name or label (TrackSearch, as the find bar finds them).
//
// Times are in beats (quarter notes) from the song's start, levels in dB, pan
// from -1 (left) to 1 (right). Track ids are what the project calls them; a
// return's and the master's ("master") too.

namespace sub::app {
class Session;
}

namespace sub::app::mcp {

class Protocol;

// Adds the session's tools to `protocol` (the session outlives it).
void addSessionTools(Protocol& protocol, Session& session);

}  // namespace sub::app::mcp
