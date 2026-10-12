# MCP server

Agents (Claude Code, and any other client of the [Model Context Protocol](https://modelcontextprotocol.io)) read
the project open in SUBstation through an MCP server inside the application: `Session.mcp`, code in
[app/src/mcp](../../app/src/mcp). It is off until the user turns it on (*Options › MCP Server*, or *Preferences ›
Agents*), and then listens at `http://127.0.0.1:<port>/mcp` (port 7381 unless changed) for programs on this computer
only. Its tools only read, for now: the project, its tracks and what each one is (the [track
labels](../intelligence.md#track-labels-what-each-track-is-in-words)), one track in full, finding tracks as the find
bar does, and the song's chords and key. For the user's side see [guide/agents.md](../guide/agents.md).

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
 (202 for a notification)                                          Selection, EngineBridge)
```

## Files

| File | What it holds |
|---|---|
| [McpServer.h](../../app/src/mcp/McpServer.h) | `McpServer` (`Session.mcp`): on or off (`enabled`, saved as `mcp/enabled`), the `port` (`mcp/port`), `running`, `url`, `error`, `claudeCodeCommand`; `statusMessage` (on, off, or why it couldn't start), forwarded by the session |
| [McpProtocol.h](../../app/src/mcp/McpProtocol.h) | `mcp::Protocol` (the messages, no transport), `Tool`, `ToolResult`, `Headers`, `Reply`; the versions (`kModernVersion`, `kLegacyVersions`, `supportedVersions()`) and error codes (`mcp::code`) |
| [McpHttpServer.h](../../app/src/mcp/McpHttpServer.h) | `mcp::HttpServer`: Streamable HTTP over a `QTcpServer` on the loopback address; `isLocalHost()`, `isLocalOrigin()` |
| [McpTools.h](../../app/src/mcp/McpTools.h) | `mcp::addSessionTools()`: the tools, over the session |

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
`openWorldHint: false`.

## The transport

`mcp::HttpServer` listens on `QHostAddress::LocalHost` only, on the application's thread (the tools read the
session there, as everything else does; each answers in well under a millisecond for a song). It speaks HTTP/1.1
itself, enough for MCP: one endpoint, `/mcp`; requests kept alive until `Connection: close`; bodies by
`Content-Length` or chunked, at most 4 MB; headers at most 64 KB. Another transfer coding is 501, and chunks with a
`Content-Length` too 400 (RFC 9112 6.1), the connection closed: where the body ends can't be told.

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
| `get_project` | none | name (the file's, else Untitled), path, tempo, time signature, key (the project's, else inferred: `keyInferred`), loop, where the last clip ends (`endBeat`), how many tracks and returns, the selected tracks |
| `list_tracks` | none | every track in the arrangement's order (groups before what they hold, `parent`), the returns, the master: id, name, kind, label, family, role, traits, volume, pan, mute, solo, frozen, device names, clip count |
| `get_track` | `track_id` | what `list_tracks` gives, plus the label's details, the devices (plug-ins' name, vendor, whether loaded, preset; racks' chains), the clips (kind, beats, active, file, note count), the sends and the output |
| `find_tracks` | `query` | the tracks the find bar would find, the best first ([TrackSearch](../intelligence.md#finding-tracks)): id, name, label |
| `get_harmony` | `start_beat`, `end_beat` (optional) | the key, whether inferred, the tracks heard, the chords (beats and names) in that part |

The descriptions (in the code) are what the model reads to choose a tool: they say what each gives and when to use it.

## Extending it

- **A tool**: a method on `Tools` in McpTools.cpp returning a `ToolResult`, and a `protocol.addTool({...})` in
  `addSessionTools()` with its name, title, description (for the model) and input schema (`schema()`,
  `property()`). Read what the session already works out (labels, harmony, the search) rather than working it out
  again. A tool that changes the project goes through `ProjectEditor` (one undo step each), sets `readOnly` false,
  and says so in its description; the server's instructions (McpServer.cpp) say the tools change nothing, which
  changes with it.
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
| [test_mcp.cpp](../../tests/app/test_mcp.cpp) | Legacy clients' `initialize`, notifications, lists and calls; modern requests' versions, headers (mismatches, Base64 names), `server/discover`, unsupported versions, unknown methods; the tools on a real session (the project, tracks with their labels, one track in full, finding tracks, the harmony, errors the model sees); HTTP over a real socket (keep-alive, chunked bodies, 405, 415, 404, parse errors, batches, DNS rebinding refused); turning it on and off, its port kept, a taken port's error |
| [test_ui_dialogs.cpp](../../tests/app/test_ui_dialogs.cpp) | Preferences › Agents: on and off, the port changed at once (a wrong one undone), the address and the command |
| [test_ui_mainwindow.cpp](../../tests/app/test_ui_mainwindow.cpp) | Options › MCP Server checked as it is, its address in the status line |

Checked by hand against Claude Code (`claude mcp add --transport http substation http://127.0.0.1:7381/mcp`, then
`claude mcp list` connecting and a tool called), and with curl for modern requests.
