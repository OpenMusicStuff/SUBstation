# MCP server

Agents (Claude Code, and any other client of the [Model Context Protocol](https://modelcontextprotocol.io)) read
the project open in SUBstation through an MCP server inside the application: `Session.mcp`, code in
[app/src/mcp](../../app/src/mcp). It is off until the user turns it on (*Options › MCP Server*, or *Preferences ›
Agents*), and then listens at `http://127.0.0.1:<port>/mcp` (port 7381 unless changed) for programs on this computer
only. Its tools read the project, its tracks and what each one is (the [track
labels](../intelligence.md#track-labels-what-each-track-is-in-words)), one track in full, a device's parameters, a
track's notes, finding tracks as the find bar does, and the song's chords and key; and they edit it (parameters, the
mixer, names, tempo and key, MIDI clips, copies of tracks) unless the user turned edits off. Agents' edits are kept
apart from the user's undo stack, each a change the user can rewind on its own ([the agents'
history](#the-agents-history)). For the user's side see [guide/agents.md](../guide/agents.md).

```
 agent (Claude Code...)                         SUBstation, the application's thread
 ----------------------                         ------------------------------------------------
 POST /mcp  {"jsonrpc": "2.0", ...}  ─────────► mcp::HttpServer (QTcpServer on 127.0.0.1)
   MCP-Protocol-Version, Mcp-Method,              Host / Origin checks, HTTP/1.1, keep-alive
   Mcp-Name headers                               │
                                                  ▼
                                                mcp::Protocol::handle(message, headers)
                                                  era, version and header checks
                                                  server/discover, initialize, tools/list
                                                  tools/call ──► McpTools (Session: Project,
 200 application/json  ◄──────────────────────                    TrackLabels, TrackSearch, Harmony,
 (202 for a notification)                                          Selection, EngineBridge;
                                                                   edits: AgentHistory's editor)
```

## Files

| File | What it holds |
|---|---|
| [McpServer.h](../../app/src/mcp/McpServer.h) | `McpServer` (`Session.mcp`): on or off (`enabled`, saved as `mcp/enabled`), the `port` (`mcp/port`), whether agents may edit (`editsAllowed`, `mcp/edits`, on at first), `running`, `url`, `error`, `claudeCodeCommand`; `statusMessage` (on, off, or why it couldn't start), forwarded by the session |
| [McpProtocol.h](../../app/src/mcp/McpProtocol.h) | `mcp::Protocol` (the messages, no transport), `Tool`, `ToolResult`, `Headers`, `Reply`; the versions (`kModernVersion`, `kLegacyVersions`, `supportedVersions()`) and error codes (`mcp::code`) |
| [McpHttpServer.h](../../app/src/mcp/McpHttpServer.h) | `mcp::HttpServer`: Streamable HTTP over a `QTcpServer` on the loopback address; `isLocalHost()`, `isLocalOrigin()` |
| [McpTools.h](../../app/src/mcp/McpTools.h) | `mcp::addSessionTools()`: the tools, over the session |
| [AgentHistory.h](../../app/src/editor/AgentHistory.h) | `AgentHistory` (`Session.agentChanges`): the agents' editor and the changes it made, rewound and restored; what each command touches (`footprint()`); the user's Undo and Redo refused over an agent's change (`undoProblem()`, `redoProblem()`); the list *View › Agent Changes* shows |

## The protocol

`Protocol::handle()` takes one JSON-RPC message and the headers it came with, and answers with an HTTP status and a
JSON-RPC response (none for a notification: 202). It speaks both eras of MCP, as the 2026-07-28 revision allows a
*dual-era* server to:

- **Modern (2026-07-28).** No handshake and no sessions: every request says its version in
  `params._meta["io.modelcontextprotocol/protocolVersion"]` (and the client's capabilities). Over HTTP the
  `MCP-Protocol-Version`, `Mcp-Method` and (for `tools/call`) `Mcp-Name` headers must say what the body says, else
  400 with `HeaderMismatch` (-32020); `Mcp-Name` may come Base64-encoded (`=?base64?...?=`,
  `decodeHeaderValue()`). A version it doesn't speak is 400 with `UnsupportedProtocolVersion` (-32022) listing those
  it does. `server/discover` answers the versions, the capabilities (`tools`) and the instructions. Results carry
  `resultType: "complete"` and the server's identity in `_meta`; `tools/list` and `server/discover` say how long
  they may be cached (`ttlMs`: 10 minutes, `cacheScope: "private"`). An unknown method is 404.
- **Legacy (2025-03-26, 2025-06-18, 2025-11-25).** `initialize` answers the client's version if it is one of
  these, else the newest; `notifications/initialized` is taken; `ping` answers. No `Mcp-Session-Id` is ever given
  (those revisions let a server go without sessions), so every request stands alone and no state is kept between
  them. A request without `_meta` is legacy; its version is its `MCP-Protocol-Version` header, or 2025-03-26
  without one (as the transport says). An unknown method is a JSON-RPC error with 200: to a legacy client a 404 meant
  its session had expired. Structured content that isn't an object (a list) is wrapped in `{"result": ...}` there,
  since it had to be an object before 2026-07-28.

A request is modern when it has `_meta`'s version, or its header names 2026-07-28 (it then must have `_meta`).
Notifications (`notifications/initialized`, `notifications/cancelled`...) are taken and ignored: nothing here runs
long enough to cancel. Responses (a client answering a server's request) and batches are refused: the server sends
no requests, and batches went with 2025-06-18.

`tools/call` finds the tool by name (an unknown one is a protocol error, -32602), checks that its arguments are an
object, and calls it. A `ToolResult` is the structured content (`structuredContent`) and the text the model reads
(`content`, one text block: the JSON, indented, unless the tool says otherwise); a tool's failure in use (a track that
isn't there, an argument missing) is `ToolResult::failure()`, a result with `isError` the model sees and can correct.
Tools list in the order they were added (the spec asks for a deterministic order), each with `readOnlyHint` and
`openWorldHint: false`; an edit also with `destructiveHint: false` (each is a change the user can rewind) and
`idempotentHint` (`Tool::idempotent`: setting values is; writing a clip or copying a track isn't).

## The transport

`mcp::HttpServer` listens on `QHostAddress::LocalHost` only, on the application's thread (the tools read the
session there, as everything else does; each answers in well under a millisecond for a song). It speaks HTTP/1.1
itself, enough for MCP: one endpoint, `/mcp`; requests kept alive until `Connection: close`; bodies by
`Content-Length` or chunked, at most 4 MB; headers at most 64 KB. Another transfer coding is 501, and chunks with a
`Content-Length` too 400, the connection closed. (`Transfer-Encoding` overrides `Content-Length`, and RFC 9112 6.1 lets
a server refuse both: programs that read such a message differently are how requests are smuggled.)

- **POST /mcp** with `Content-Type: application/json`: the message, answered with `application/json` (never an SSE
  stream: no progress to report), or 202 with no body for a notification.
- **GET** (a legacy client's stream of server messages) and **DELETE** (ending a legacy session): 405,
  `Allow: POST`. Another path: 404. Another content type: 415. A body that isn't JSON: 400 with a parse error
  (-32700); a batch (an array): 400.
- **DNS rebinding.** A web page can't reach it: a request whose `Host` isn't `localhost`, `127.0.0.1` or `[::1]`
  (any port), or that has an `Origin` that isn't one of those (`null` included), is 403. Agents send no `Origin`.

There is no authentication: anything running on this computer as any user can read the project while the server is
on. That is why it is off until the user turns it on.

## The tools

[McpTools.cpp](../../app/src/mcp/McpTools.cpp). Times are beats (quarter notes) from the song's start, levels dB,
pan -1 to 1; ids are the project's (`"master"` for the master).

| Tool | Arguments | What it gives |
|---|---|---|
| `get_project` | none | name (the file's, else Untitled), path, tempo, time signature, key (the project's, else inferred: `keyInferred`), loop, where the last clip ends (`endBeat`), how many tracks and returns, the selected tracks, the insert marker, the time selection (beats, tracks, the clips in it; null without one) |
| `list_tracks` | none | every track in the arrangement's order (groups before what they hold, `parent`), the returns, the master: id, name, kind, label, family, role, traits, volume, pan, mute, solo, frozen, device names, clip count |
| `get_track` | `track_id` | what `list_tracks` gives, plus the label's details, the devices (plug-ins' name, vendor, whether loaded, preset; racks' chains), the clips (kind, beats, active, file, note count), the sends and the output |
| `find_tracks` | `query` | the tracks the find bar would find, the best first ([TrackSearch](../intelligence.md#finding-tracks)): id, name, label |
| `get_harmony` | `start_beat`, `end_beat`, `include_tracks` (optional) | the key, whether inferred, the chords (beats and names) in that part; with `include_tracks`, the tracks heard (id, name) |
| `get_device_parameters` | `track_id`, `device_id`; `query`, `offset`, `limit` (optional) | the device's parameters as `EngineBridge::deviceParamSpecs` describes them (its on/off first; a rack's macros and chains' faders), those whose name has every word of `query`, a page at a time (100; at most 500; `nextOffset`): id (the automation key's parameter), name, value (plain: `ownValue`, a plug-in's read from it), text (`ParamSpec::format`: a plug-in's own), normalized, min, max, default, unit, steps, choices (up to 64), automated and overridden, the macro moving it; a plug-in not loaded (yet) says so |
| `get_notes` | `track_id`; `start_beat`, `end_beat`, `format` (optional) | a MIDI track's notes as played (`Clip::playedNotes`, on the timeline; at most 5000) and its clips there. As rows by default, `[startBeat, length, pitch, velocity]` (`fields` names them; `muted` lists the rows of notes that don't sound): a fifth of the size of objects; `format: "objects"` gives objects with each note's name |
| `get_selection_context` | none | the time selection (beats, the selected clips) and each track in it but groups (at most 16: `summary()`, the label's details, a MIDI track's notes there as rows), the key and chords there, the tempo and time signature; a failure without a time selection. One call for "the selection" instead of `get_project`, `get_track`, `get_notes` and `get_harmony` |
| `set_device_parameters` | `track_id`, `device_id`, `parameters` (`[{id, value or normalized}]`) | edit: values in their units (a choice's name, true or false for a switch) or normalized, checked against the range first; `device:on` sets the switch, `macroN` the macro (`setMacro`), a chain's fader `setChainParam`, the others `setDeviceParam` with the value before (`ownValue`) |
| `set_mixer` | `track_id`; `volume_db`, `pan`, `mute`, `sends` (`[{return_id, level_db}]`) | edit: what is given |
| `rename_track` | `track_id`, `name` | edit: a name template (`# Keys`) |
| `set_project` | `tempo`, `key` | edit: `setTempo`, `setKey` (`keyFromName`; "": none) |
| `write_midi_clip` | `track_id`, `start_beat`, `end_beat`, `notes` (rows `[start_beat, length, pitch, velocity]`, velocity optional, as `get_notes` gives them; or `{pitch, start_beat, length, velocity}`) | edit: a new MIDI clip there playing the notes (their beats on the timeline, inside it), winning over what it overlaps (`edits::resolveOverlaps`), one clip edit |
| `duplicate_track` | `track_id` | edit: `duplicateTracks` (plug-ins' states stored first); the copy's id |
| `list_agent_changes` | none | the changes: id, what it did, the tool, when, the tracks it touched, applied or rewound |
| `rewind_agent_changes` | `change_ids` or `all` | the changes rewound, the last first; where it `stopped`, and why |
| `restore_agent_changes` | `change_ids` | the changes restored, the first first; where it `stopped`, and why |

The descriptions (in the code) are what the model reads to choose a tool: they say what each gives and when to use it.
An edit tool checks its arguments before it edits (a value out of range, a parameter or note that isn't there: a
failure the model sees, nothing changed), and says what it did (`change`) and the change's id (`changeId`; null when
nothing changed: it was so already). With edits off (`editsAllowed`) every edit, restoring too, is a failure saying
so; reading and rewinding still work.

## The agents' history

[AgentHistory.h](../../app/src/editor/AgentHistory.h). An agent edits with an editor of its own: a second
`ProjectEditor` over the same project, on a `QUndoStack` of its own that never undoes. The session gives it the same
parameter, value and device-default sources as the user's, and its switches override automation as the user's do;
its `pluginAdded` and `parameterTouched` go nowhere (no plug-in editors popping up, no automation lanes changing).
`record()` runs one tool call's edits in a macro there: one *change*, listed with what it did. An edit that throws
(an `EditError`, or anything else) or is refused (a frozen track) takes back what it did, and the tool fails.

A change is rewound with its macro's `undo()` and restored with its `redo()`, in any order, not as a stack. But two
histories now edit one project with snapshot commands, whose undo puts back what was there before, whatever came
after: the user's Undo of a pan would put the pan back over an agent's later pan, and of a clip edit on a track an
agent since deleted, edit a track that isn't there. So:

- **Footprints.** `footprint()` says what a command touches, as regions: `track/<id>` (the whole track: inserted,
  removed, replaced, frozen with what is in it), `track/<id>/clips`, `track/<id>/frozen`, `track/<id>/field/<name>`,
  `track/<id>/devices` (the whole device tree), `track/<id>/devices/<device>` (a state), `.../<device>/param/<id>`,
  `.../<device>/on` (and `name`, `sidechain`, `midi`, `macros`), `track/<id>/devices/chains/<chain>/<field>`,
  `auto/<owner>/<key>`, `settings/<field>`, `order/<id>` (where a track is) and `order` (the whole arrangement:
  `ArrangeTracksCommand`). A macro's are its children's; a command of another kind (none are, now) is `*`,
  everything. Regions overlap when one is the other or holds it (`overlaps()`).
- **Time.** One clock counts the steps of both: a change's when it was made, rewound or restored last; a user
  command's when the user's stack did, undid or redid it last (followed on `QUndoStack::indexChanged`: up, those
  done; down, those undone; the same, the top one merged; commands a push dropped are forgotten).
- **The rule.** A step taking something back (Undo, Rewind) is refused while a step of the other history (or another
  change) on an overlapping region came after it and is in effect. A step putting something back (Redo, Restore) is
  refused while one came after it at all, taken back since or not: it was made over a state that is gone. Within the
  user's stack nothing changes: its steps go in order. `Session::undo()`/`redo()` (Edit › Undo and Redo) ask
  `undoProblem()`/`redoProblem()` first and say why in the status line; a refused rewind says why to whoever asked.

It errs towards refusing: the user's edit of one device stands in the way of rewinding an agent's change to another
device on the same track only when one of them replaced the whole device tree (adding or moving devices). Changes
make the project unsaved (`clean()`: Session.clean asks both histories); saving makes them clean; a new or opened
project clears them (`clear()`).

## Extending it

- **A tool**: a method on `Tools` in McpTools.cpp returning a `ToolResult`, and a `protocol.addTool({...})` in
  `addSessionTools()` with its name, title, description (for the model) and input schema (`schema()`,
  `property()`). Read what the session already works out (labels, harmony, the search) rather than working it out
  again. A tool that changes the project checks its arguments, then edits through `Tools::change()` (the agents'
  editor: one change), sets `readOnly` false (and `idempotent` if it is), and ends its description with the
  sentence about the history the other edits have.
- **A new kind of undo command** (model/Commands.h): its regions in `ownFootprint()` (AgentHistory.cpp); without
  them it touches everything, and refuses every step of the other history across it.
- **Resources** (the project as a document, say): `capabilities()` gains `resources`, and `handle()` answers
  `resources/list` and `resources/read` (both cacheable in the modern era: `ttlMs`, `cacheScope`), with `Mcp-Name`
  checked against the `uri`.
- **Change notifications** (a modern client's `subscriptions/listen`, as the track list changes): an SSE response
  kept open by the transport, which now answers each request at once.
- **stdio** (clients that only launch servers): a small executable bridging stdin and stdout to this endpoint, or to
  `Protocol::handle()` over a local socket; `Headers::http` false skips the header checks, as stdio has none.
- **A newer protocol revision**: its version in `supportedVersions()`, what changed in `handle()`, and the tests.

## Tests

| Test file | Covers |
|---|---|
| [test_mcp.cpp](../../tests/app/test_mcp.cpp) | Legacy clients' `initialize`, notifications, lists and calls; modern requests' versions, headers (mismatches, Base64 names), `server/discover`, unsupported versions, unknown methods; the tools on a real session (the project and its time selection, tracks with their labels, one track in full, finding tracks, the harmony, a device's parameters found and paged, notes, errors the model sees); edits (parameters, the mixer, a MIDI clip on a copy of a track) kept off the undo stack, refused when wrong or turned off, listed, rewound out of order, restored, all rewound; the two histories refusing to undo each other's work (Undo over an agent's change, a rewind over the user's, a later change first, a redo onto a track that went) and going on where they don't touch the same; footprints; HTTP over a real socket (keep-alive, chunked bodies, 405, 415, 404, parse errors, batches, DNS rebinding refused); turning it on and off, its port kept, a taken port's error |
| [test_ui_dialogs.cpp](../../tests/app/test_ui_dialogs.cpp) | Preferences › Agents: on and off, the port changed at once (a wrong one undone), the address and the command |
| [test_ui_mainwindow.cpp](../../tests/app/test_ui_mainwindow.cpp) | Options › MCP Server checked as it is, its address in the status line |

Checked by hand against Claude Code (`claude mcp add --transport http substation http://127.0.0.1:7381/mcp`, then
`claude mcp list` connecting and a tool called), and with curl for modern requests.
