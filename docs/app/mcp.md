# MCP server

Agents (Claude Code, and any other client of the [Model Context Protocol](https://modelcontextprotocol.io)) read
the project open in SUBstation through an MCP server inside the application: `Session.mcp`, code in
[app/src/mcp](../../app/src/mcp). It is off until the user turns it on (*Options › MCP Server*, or *Preferences ›
Agents*), and then listens at `http://127.0.0.1:<port>/mcp` (port 7381 unless changed) for programs on this computer
only. Its tools read the project, its arrangement at a glance (where each track plays, bar by bar, and the song's
sections guessed from it), its tracks and what each one is (the [track
labels](../intelligence.md#track-labels-what-each-track-is-in-words)), one track in full, a device's parameters, a
track's automation and notes, finding tracks as the find bar does, the devices that can be added, and the song's
chords and key; and they edit it (parameters, the mixer, names, tempo and key, MIDI clips, the notes there, automation,
tracks added, deleted, moved, grouped and colored, devices added, removed and moved) unless the user turned edits
off. Its
prompts are requests the user picks in the agent (write a part, describe the song, review the mix), and its resources
the project and its arrangement as documents. Agents' edits are kept
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
                                                  server/discover, initialize, tools/list,
                                                  prompts/*, resources/*
                                                  tools/call ──► McpTools (Session: Project,
 200 application/json  ◄──────────────────────                    TrackLabels, TrackSearch, Harmony,
 (202 for a notification)                                          Selection, EngineBridge;
                                                                   edits: AgentHistory's editor)
```

## Files

| File | What it holds |
|---|---|
| [McpServer.h](../../app/src/mcp/McpServer.h) | `McpServer` (`Session.mcp`): on or off (`enabled`, saved as `mcp/enabled`), the `port` (`mcp/port`), whether agents may edit (`editsAllowed`, `mcp/edits`, on at first), `running`, `url`, `error`, `claudeCodeCommand`; `statusMessage` (on, off, or why it couldn't start), forwarded by the session |
| [McpProtocol.h](../../app/src/mcp/McpProtocol.h) | `mcp::Protocol` (the messages, no transport), `Tool`, `ToolResult`, `Prompt`, `Resource`, `Headers`, `Reply`; the versions (`kModernVersion`, `kLegacyVersions`, `supportedVersions()`) and error codes (`mcp::code`) |
| [McpHttpServer.h](../../app/src/mcp/McpHttpServer.h) | `mcp::HttpServer`: Streamable HTTP over a `QTcpServer` on the loopback address; `isLocalHost()`, `isLocalOrigin()` |
| [McpTools.h](../../app/src/mcp/McpTools.h) | `mcp::addSessionTools()`: the tools, prompts and resources, over the session |
| [McpSupport.h](../../app/src/mcp/McpSupport.h) | what the tools share: JSON as the model reads it (`schema()`, `number()`, `beat()`, `rounded()`, `pitchOf()`), and `ToolContext`: bars (`bar()`, `stretch()`), stretches the arguments name (`span()`), a device's parameters and the values asked for them (`params()`, `wanted()`), and edits as changes (`change()`) |
| [McpEdits.h](../../app/src/mcp/McpEdits.h) | `mcp::addEditTools()`: editing notes where they are, automation, tracks, devices and modulation (McpEdits.cpp) |
| [ModulatorShapes.h](../../app/src/audio/ModulatorShapes.h) | the LFO's and the Envelope's shapes by name and the parameters a curve is written as, shared by the device view's modulator graph and `set_modulation` |
| [McpArrangement.h](../../app/src/mcp/McpArrangement.h) | `mcp::arrangementMap()`: the arrangement at a glance (a line of cells a track, the sections guessed from it), on the model alone |
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
  `resultType: "complete"` and the server's identity in `_meta`; `tools/list`, `prompts/list`, `resources/list` and
  `server/discover` say how long they may be cached (`ttlMs`: 10 minutes, `cacheScope: "private"`). An unknown method is 404.
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
(`content`, one text block: the JSON, compact, unless the tool says otherwise, as `get_arrangement_map` does with its
grid; indenting would cost the model a third more tokens and tell it nothing); a tool's failure in use (a track that
isn't there, an argument missing) is `ToolResult::failure()`, a result with `isError` the model sees and can correct.
Tools list in the order they were added (the spec asks for a deterministic order), each with `readOnlyHint` and
`openWorldHint: false`; an edit also with `destructiveHint: false` (each is a change the user can rewind) and
`idempotentHint` (`Tool::idempotent`: setting values is; writing a clip or copying a track isn't).

**Prompts** (`Prompt`, `addPrompt()`; offered in `capabilities` once there is one): `prompts/list` lists them with
their arguments (strings, some required); `prompts/get` answers one user message, the prompt's `text()` with the
arguments in it. An unknown prompt, or one without an argument it requires, is -32602. **Resources** (`Resource`,
`addResource()`; offered likewise, without subscriptions): `resources/list`, `resources/templates/list` (none),
and `resources/read`, the resource's `read()` as it is now (not cacheable: the project changes as the user works). An
unknown URI is -32002 (`kResourceNotFound`). In the modern era `Mcp-Name` says the prompt's name or the resource's
URI, as it says a tool's.

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

[McpTools.cpp](../../app/src/mcp/McpTools.cpp). Times are beats (quarter notes) from the song's start, and bars:
`startBar` and `endBar` are places (bar 17 starts at 17, halfway through it is 17.5: `1 + beats / beatsPerBar`; where the time signature changes, each bar
as long as its own: `barNumberAt()`, `beatOfBarNumber()`), so a
stretch over bars 17 to 24 is 17 to 25. The tools that take a stretch take `start_bar` and `end_bar` (instead of
`start_beat` and `end_beat`) the same way: one meaning in and out, so a selection's `endBar` can be handed straight
back (an `end_bar` "to the end of this bar" would write a bar too many). The arrangement map's whole bars are
`firstBar` and `lastBar`, both in it. Beats are given to a ten-thousandth; levels, pans and values to a hundredth.
Levels are dB, pan -1 to 1; ids are the project's (`"master"` for the master). What is as it usually is (no pan, not
muted, in no group, a device on, a clip active) is left out of a track's, a device's or a clip's JSON.

| Tool | Arguments | What it gives |
|---|---|---|
| `get_project` | none | name (the file's, else Untitled), path, tempo, time signature, `beatsPerBar` (at the song's start; where they change, `tempoChanges` and `timeSignatureChanges`: each with its beat and bar), key (the project's, else inferred: `keyInferred`), its `style` (`SoundSuggestions::summary`: "Techno, Punchy · A Minor", once its sounds are tagged), loop, where the last clip ends (`endBeat`, `endBar`), how many tracks and returns, the selected tracks, the insert marker, the time selection (beats and bars, tracks, the clips in it; null without one), and `recentEdits`: the user's last 5 steps on the undo stack, the latest first (what "that" and "this" are) |
| `get_arrangement_map` | `first_bar`, `last_bar`, `bars_per_cell` (optional) | the arrangement at a glance ([McpArrangement.h](../../app/src/mcp/McpArrangement.h)): a line a track with clips there (a group's line is what its tracks play), a cell a bar (or several: as many as keep a line to 128 cells; at most 512), `#` where something is heard (an active audio clip, a note `Clip::heardNotes` gives), `-` where a clip is but nothing of it is heard, `.` nothing; the tracks with nothing there counted; and the **sections** (below). Its text is the grid itself, with a ruler, each line's id, and the sections by bars and tracks; its structured content the same as data |
| `list_tracks` | `query` (optional) | every track in the arrangement's order (groups before what they hold, `parent`), the returns, the master: id, name, kind, label, family, role, traits, volume, device names, clip count; pan, mute, solo, frozen and parent only when they aren't as usual. With `query`, only the tracks the find bar would find, the best first ([TrackSearch](../intelligence.md#finding-tracks)), the same way |
| `get_track` | `track_id` | what `list_tracks` gives, plus the label's details, the devices (plug-ins' name, vendor, whether loaded, preset; racks' chains and macros' mappings; a sidechain's source and tap; `midiFrom`; what a modulator moves), the clips (kind, beats and bars; an audio clip's file, what it sounds like as the browser tags it (`sound`: `tagging::label`), and its gain, pan, transpose, detune, warp, reversal and fades where they aren't as placed; a MIDI clip's note count), the sends, the output, and what is automated (`automation`: key, target, how many points). Its output: `master`, a group (its id), a track, a sidechain or none; the model's "its group, else the master" said as the one it is |
| `get_harmony` | a stretch, `include_tracks` (optional) | the key, whether inferred, the chords (beats, the bar each starts in, names) in that part; with `include_tracks`, the tracks heard (id, name) |
| `get_device_parameters` | `track_id`, `device_id`; `query`, `offset`, `limit` (optional) | the device's parameters as `EngineBridge::deviceParamSpecs` describes them (its on/off first; a rack's macros and chains' faders), those whose name has every word of `query`, a page at a time (100; at most 500; `nextOffset`): id (the automation key's parameter), name, value (the number the device shows, in its unit: `ownValue` through `ParamSpec::shown`, the engine's `ParamDisplay`, where the value shows otherwise, as the Vital's level squared, its envelopes' times to the fourth, its cutoffs' notes in hertz; to 4 significant digits), text (`ParamSpec::format`: a plug-in's own), normalized, min, max, default (shown so), unit (the one it shows in), steps, choices (up to 64), automated and overridden, the macro moving it; a plug-in not loaded (yet) says so |
| `get_automation` | `track_id`; `key`, a stretch (optional) | a track's envelopes (one, with `key`), each with what it moves ("Volume", "Delay: Feedback": `EngineBridge::paramSpec`), its range and unit, where it is at the stretch's start as the device shows it (`startText`), whether overridden, and its points there as rows `[beat, value, curve]`: the value in the target's units (normalized, saying so, without a spec), the curve left out when straight; at most 2000 points |
| `get_notes` | `track_id`; a stretch, `format` (optional) | a MIDI track's notes as played (`Clip::playedNotes`, on the timeline; at most 5000) and its clips there (beats and bars). As rows by default, `[startBeat, length, pitch, velocity]` (`fields` names them; `muted` lists the rows of notes that don't sound; `bent` those with a bend or vibrato, which rows leave out, with `bendNote` saying so): a fifth of the size of objects. `format: "names"` gives rows with pitches as names ("C3": models reason about harmony better with them); `"objects"` gives objects with each note's name, its `bend` (`[time, semitones, curve]` rows, time from the note's start) and `vibrato` |
| `get_selection_context` | `format` (optional: rows or names) | the time selection (beats, bars, the selected clips) and each track in it but groups (at most 16: `summary()`, the label's details, a MIDI track's notes there as rows), the key and chords there, the tempo, time signature and beats a bar; a failure without a time selection. One call for "the selection" instead of `get_project`, `get_track`, `get_notes` and `get_harmony` |
| `set_device_parameters` | `track_id`, `device_id`, `parameters` (`[{id, value or normalized}]`) | edit: values as the device shows them (`ToolContext::wanted()`: back through `ParamSpec::fromShown`; a choice's name, refused when two choices have it, saying their values; true or false for a switch) or normalized, checked against the range first; `device:on` sets the switch, `macroN` the macro (`setMacro`), a chain's fader `setChainParam`, the others `setDeviceParam` with the value before (`ownValue`) |
| `set_track` | `track_id`; `name`, `color`, `volume_db`, `pan`, `mute`, `sends` (`[{return_id, level_db}]`) | edit: what is given, all of it one change: a name template (`# Keys`; not the master's: `renameTrack`), a color ("#rrggbb": `setTrackColor`), the mixer (`setTrackParam`, `setSend`) |
| `set_project` | `tempo`, `key` | edit: `setTempo`, `setKey` (`keyFromName`; "": none) |
| `write_midi_clip` | `track_id`, a stretch (required), `notes` (rows `[start_beat, length, pitch, velocity]`, velocity optional, as `get_notes` gives them; or `{pitch, start_beat, length, velocity, muted, bend, vibrato}`, as its objects are; a pitch a number or a name, "Bb2"), `muted` (optional: the notes, by their place, to write deactivated, as `get_notes`' `muted`) | edit: a new MIDI clip there playing the notes (their beats on the timeline, inside it), winning over what it overlaps (`edits::resolveOverlaps`), one clip edit. `replacedNotes`: how many notes played there before; a `warning` when more of them bent than it writes (rows would drop the bends: read and written as objects, a part comes back the same) |
| `duplicate_track` | `track_id` | edit: `duplicateTracks` (plug-ins' states stored first); the copy's id |
| `edit_notes` | `track_id`, `operations`; a stretch, `pitches` (numbers or names), `clip_ids` (optional) | edit: the notes of the track's MIDI clips starting in the stretch (`playedNotes`, on the timeline), of those pitches and clips, changed by each operation in turn, as the piano roll's tools change them: `transpose` {semitones}, `shift` {beats}, `quantize` {grid, amount} (`notes::quantized`, on the song's grid), `humanize_timing` {amount, seed} (`notes::humanizedTiming`), `groove` {groove: "swing" (default), "project" (the default groove; a failure without one) or a library groove by id or name (a failure listing the library's names); swing (50..75, default 66.7) and step (beats, default 0.25) for Swing; timing (0..2, default 1), velocity (-1..1, default 1), random and quantize (0..1, default 0), seed} (`notes::grooved`, on the timeline: the groove's cycle counted from the song's start; Apply Groove, as the Groove device plays it), `scale_time` {factor} (`notes::timeScaled`), `legato` (`notes::legato`, in each clip), `length` {beats or factor}, `velocity` {set, change or factor; held to 1..127}, `humanize_velocity` {amount} (`Humanizer::velocities`; a failure without its model), `mute`, `unmute`, `delete`; each clip's notes then `notes::place`d (the changed ones win where they land on others), one `setClipsNotes`. Bends and vibrato go with their notes; the others stay. How many notes it took and changed |
| `write_automation` | `track_id`; what (`device_id` and `parameter_id`, `mixer`: volume, pan or activator, `send`: a return's id, or `key`); `points` (rows `[beat, value, curve]` in the target's units, as `get_automation` gives them: `wanted()` reads each value), a `shape` (`ramp` {from, to, curve} over the stretch; `lfo` {wave: sine, triangle, saw or square, low, high, cycle_beats: a bar by default}: points a cycle, 16 for a sine, steps for a square, ending where the wave is at the stretch's end; at most 2000), or `clear`; a stretch (points: from the first to the last if not given; a shape: required) | edit: the envelope with that stretch replaced (`automation::pasteRange`; one point goes in among the others: `addPoint`), the rest kept; cleared in the stretch (`removeRange`), or all of it (`clearEnvelope`). Says when the user's hand overrides it |
| `add_track` | `kind` (midi, audio, return); `name`, `instrument` (an id from `list_devices`), `after_track_id` or `group_id` (optional) | edit: `addMidiTrack` (a built-in instrument or a plug-in, as its default preset has it), `addMidiTrackWith` (a preset, then named), `addAudioTrack`, `addReturnTrack`; after a track (`insertionPoint`), last in a group, or last. Its trackId |
| `delete_tracks` | `track_ids` (returns too) | edit: `deleteTracks` (plug-ins' states stored first, so rewound they sound as they did); one in a frozen group is refused |
| `move_tracks` | `track_ids`; `before_track_id`, `after_track_id` or `group_id` (none: last, in no group) | edit: `moveTracks` where `canMoveTracks` says they can go |
| `group_tracks` | `track_ids`, `name`; or `ungroup` | edit: `groupTracks` (then named), the group's id; or, with `ungroup` (groups' ids) instead, `ungroup` |
| `list_devices` | `query`, `type` (builtin, plugin, preset), `role` (instrument, effect), `offset`, `limit` (all optional) | the catalog: the built-in devices (`builtinDevices()`; id their kind) and an empty rack (`rack`), the plug-ins scanned (`PluginIndex`; id `plugin:<uid>`, vendor and VST3 category) and the library's presets (`PresetIndex`; id `preset:<path>`, the device it is `for`), whether each is an instrument, those having every word of `query` in their name, category or vendor, a page at a time (100; at most 500; `nextOffset`); a note while plug-ins are still being scanned |
| `add_device` | `track_id`, `device` (an id from `list_devices`); `before_device_id` or `chain_id` (optional) | edit: `addDevice` (a built-in device, as its default preset has it, or a plug-in) or `insertDevices` (a preset, read with `loadPreset`; no editor shown), before that device in its chain (a rack's too), last in that rack chain, or last on the track. An instrument only on a MIDI track, where it replaces the one there (`replaced` says which). Its deviceId |
| `remove_devices` | `track_id`, `device_ids` | edit: `removeDevices` (plug-ins' states stored first) |
| `set_modulation` | `track_id`, `modulator_id` (an LFO's or an Envelope's); `targets` (`[{device_id, parameter_id, from, to, invert}]`: from and to as the parameter shows, `wanted()`), `remove` (`[{device_id, parameter_id}]`), `clear`, `shape` (a name from `modulatorShapes()`: the device view's list) or `points` (rows `[x, y, curve]`) | edit, one change: `clearModulations`, `unmapModulation` each, `mapModulation` each (from and to normalized: the mapping's low and high; a parameter mapped to another modulator moves over), and the curve written whole (`modulatorPointValues()`, Random off) with `setDeviceParams`. What it moves now (`modulates`: as `ToolContext::modulationsJson()` gives it, and `get_track` too: each parameter's name, from and to as it shows, text, invert) |
| `move_devices` | `track_id`, `device_ids`; `before_device_id`, `chain_id` or `to_track_id` | edit: `moveDevices` on the track, or `moveDevicesToTrack` (effects keep their state and automation); a failure saying why where they can't go |
| `list_agent_changes` | none | the changes: id, what it did, the tool, when, the tracks it touched, applied or rewound |
| `rewind_agent_changes` | `change_ids` or `all`; `restore` | the changes rewound, the last first; or, with `restore`, the rewound changes named put back, the first first (`all` isn't taken there); where it `stopped`, and why |

The descriptions (in the code) are what the model reads to choose a tool: they say what each gives and when to use it.
An edit tool checks its arguments before it edits (a value out of range, a parameter or note that isn't there: a
failure the model sees, nothing changed), and says what it did (`change`) and the change's id (`changeId`; null when
nothing changed: it was so already). With edits off (`editsAllowed`) every edit, restoring too, is a failure saying
so; reading and rewinding still work.

### Sections

The project keeps no markers, so the arrangement map's sections are all an agent has of "the chorus" or "the drop":
a guess from where tracks come in and drop out ([McpArrangement.cpp](../../app/src/mcp/McpArrangement.cpp)). Each
cell has the set of tracks heard in it (not muted, nor in a muted group). Two neighbouring cells start a new run when
their sets are under 0.6 alike (the Jaccard index: what both have over what either has; two empty sets are alike). A
run one cell long (a fill, a break) goes with the run before it, or the one after if it is first. What a run plays is
the tracks heard in at least half its cells; neighbouring runs whose tracks are 0.75 alike or more are one. Each is
named by letter, A first, and one whose tracks are 0.75 alike or more to an earlier one's by that one's letter again
(the most alike), so a song reads "A B A C"; one with nothing heard is "silent". It errs towards more sections: a
build-up adding a track every few bars is a section each. It is a guess, and says so.

### Prompts and resources

| | Name | What it is |
|---|---|---|
| Prompt | `write_part` (`request`, required) | write that over the bars selected: `get_selection_context` first, on a copy of the track unless asked to change it, in the key and over the chords, then say what was written |
| Prompt | `describe_song` | the song's form by bars, what plays in each part, its key and progressions, what stands out (a thin or crowded section, tracks that never play), changing nothing |
| Prompt | `mix_review` (`focus`) | suggestions for the mix, each with why, made only once the user says which: from levels, pans, effects and what overlaps (an agent can't hear it) |
| Resource | `substation://project` | `get_project`'s JSON (`application/json`) |
| Resource | `substation://arrangement` | `get_arrangement_map`'s text (`text/plain`) |

Claude Code shows a server's prompts as slash commands (`/mcp__substation__write_part`), and its resources as
`@substation:substation://arrangement` mentions.

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
  done; down, those undone; the same, the top one merged, or a push past `Session::kUndoLimit` dropping the
  oldest; commands a push dropped are forgotten).
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

- **A tool**: a method returning a `ToolResult` on `Tools` (McpTools.cpp: reading, the first edits) or `EditTools`
  (McpEdits.cpp: notes where they are, automation, tracks, devices), both `ToolContext`s (McpSupport.h), and a
  `protocol.addTool({...})` in `addSessionTools()` or `addEditTools()` with its name, title, description (for the
  model) and input schema (`schema()`, `property()`). Read what the session already works out (labels, harmony, the
  search) rather than working it out again, and take places as the others do (`span()`: beats or bars). A tool that
  changes the project checks its arguments, then edits through `change()` (the agents' editor: one change; an
  `EditError` thrown in it takes the change back and is the failure the model sees), sets `readOnly` false (and
  `idempotent` if it is), and ends its description with the sentence about the history the other edits have. Both
  register through `addTool()` (McpSupport.h), which makes every tool that isn't read-only fail while edits are off,
  so no handler checks for itself; `rewind_agent_changes` alone is added straight to the protocol, since rewinding
  works with edits off (its `restore` checks for itself).
- **A new kind of undo command** (model/Commands.h): its regions in `ownFootprint()` (AgentHistory.cpp); without
  them it touches everything, and refuses every step of the other history across it.
- **A prompt or a resource**: a `protocol.addPrompt({...})` (its name, title, description, arguments, and the
  message it gives) or `protocol.addResource({...})` (its URI, name, title, description, MIME type, and what it
  reads) at the end of `addSessionTools()`. A resource reads what a tool gives (`substation://project` is
  `get_project`'s JSON, `substation://arrangement` `get_arrangement_map`'s text), so the two never differ.
- **Change notifications** (a modern client's `subscriptions/listen`, as the track list changes): an SSE response
  kept open by the transport, which now answers each request at once.
- **stdio** (clients that only launch servers): a small executable bridging stdin and stdout to this endpoint, or to
  `Protocol::handle()` over a local socket; `Headers::http` false skips the header checks, as stdio has none.
- **A newer protocol revision**: its version in `supportedVersions()`, what changed in `handle()`, and the tests.

## Tests

| Test file | Covers |
|---|---|
| [test_mcp.cpp](../../tests/app/test_mcp.cpp) | Legacy clients' `initialize`, notifications, lists and calls; modern requests' versions, headers (mismatches, Base64 names), `server/discover`, unsupported versions, unknown methods; compact text; the tools on a real session (the project, its time selection and its last edits, in beats and bars; tracks with their labels, what is as usual left out; one track in full; finding tracks; the harmony over bars; automation in the target's units; a device's parameters found and paged; notes as rows, names and objects, with their bends; errors the model sees); the arrangement map (cells, a group's line, two bars a cell, part of the song, the sections and a muted track left out of them, the text); prompts and resources (listed, a prompt's arguments, unknown ones, `Mcp-Name`); edits (parameters, the mixer, a MIDI clip on a copy of a track, in bars, by note names, muted notes and bends written, read back and written again unchanged, a warning when bends would go; notes edited where they are, by pitch, clip and stretch, each operation, several in order, the wrong ones refused, all rewound; automation as points in the target's units, a stretch replaced, one point among others, a ramp, triangle and square LFOs, cleared in part and in all, the wrong ones refused; tracks added with an instrument, after a track, as a return, grouped, moved before, after, into and out of a group, ungrouped, colored, deleted, all rewound; devices listed, found and paged, added last, before another, into a rack's chain, an instrument replacing the one there, moved on a track and to another, removed, a rewind stopping at the user's edit until they undo it) kept off the undo stack, refused when wrong or turned off, listed, rewound out of order, restored, all rewound; the two histories refusing to undo each other's work (Undo over an agent's change, a rewind over the user's, a later change first, a redo onto a track that went) and going on where they don't touch the same; footprints; HTTP over a real socket (keep-alive, chunked bodies, 405, 415, 404, parse errors, batches, DNS rebinding refused); turning it on and off, its port kept, a taken port's error |
| [test_ui_dialogs.cpp](../../tests/app/test_ui_dialogs.cpp) | Preferences › Agents: on and off, the port changed at once (a wrong one undone), the address and the command |
| [test_ui_mainwindow.cpp](../../tests/app/test_ui_mainwindow.cpp) | Options › MCP Server checked as it is, its address in the status line |

Checked by hand against Claude Code (`claude mcp add --transport http substation http://127.0.0.1:7381/mcp`, then
`claude mcp list` connecting and a tool called), and with curl for modern requests.
