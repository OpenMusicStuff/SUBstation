# Groove

**Status: implemented.** Swing and feel for drums and anything else, in three ways that share one piece of maths:

1. **The Groove device**, a MIDI effect: grooves the notes reaching it, live, per track or per Drum Rack pad,
   automated and modulated like any device.
2. **A clip's groove**: a MIDI clip's notes moved where the engine bridge hands them to the engine, an audio clip's
   audio moved through its warp markers. The clip's own notes and markers stay as they are.
3. **Apply Groove**: the groove written into notes (the piano roll's Groove menu, a clip's, the device's), as
   Quantize writes the grid.

All three play **grooves**: MPC swing, the project's default groove, a groove of the project's groove library
(shared: edit one, and everything playing it follows) or a groove of their own. Devices that play on the beat (the
Arpeggiator, the Delay, the LFO) can follow the default groove too (**Follow Groove**). This page is the map of all
of it; the parts have their own pages: the maths in [music.md](music.md#groove), the device in
[engine/devices.md](engine/devices.md#groove-builtingroove-midieffect), early notes in
[engine/midi.md](engine/midi.md#early-notes). How the user works with grooves is in the guide
([devices](guide/devices.md), [MIDI](guide/midi.md), [audio clips](guide/audio-clips.md)).

## What Ableton's groove pool does, and what this does differently

| | Ableton's groove pool | Here |
|---|---|---|
| Shared grooves, edited in one place | yes | yes: the project's groove library (Edit › Grooves…) |
| Where a groove applies | a whole clip | a track, a rack chain, one Drum Rack pad (the device), a MIDI or audio clip, or notes written once |
| Hats swung, kick straight, in one clip | no (split the clip) | a Groove device on the hats' pad, or a groove with a lane for the hats |
| Live input, the Arpeggiator's notes | no | yes: the device grooves whatever reaches it (later only; both ways with Latency) |
| Amounts automated or modulated | no (one global amount) | yes: the device's parameters |
| Feel in milliseconds (a snare 8 ms late at any tempo) | no | yes: a step's offset can be in ms |
| The Arpeggiator, a delay, a synced LFO in time with it | no | Follow Groove |
| Seeing what it did | only by committing | the device's editor (its steps, the one playing lit), and Apply Groove |

## The groove (`music/src`, no Qt)

A groove is a value ([Groove.h](../music/src/music/Groove.h)), defined over one **cycle** of whole bars, in beats, so
the tempo map moves it with the song:

```cpp
namespace sub::groove {

struct Step {
    double timing = 0.0;    // fraction of a step, -0.5..0.5 (+ later)
    double timingMs = 0.0;  // and milliseconds, -50..50, the same at any tempo
    double velocity = 0.0;  // added, -1..1 of full scale (127)
    double length = 1.0;    // a note's length times this, 0.25..2
};

struct Lane {                 // an optional lane for some keys only (the hats, the snare)
    std::vector<int> keys;    // empty: the default lane, for every key no other lane takes
    std::vector<Step> steps;  // one per step of the cycle
};

struct Groove {
    double stepBeats = 0.25;  // 1/16 (1/8, 1/8T, 1/16T, 1/32...)
    int bars = 1;             // the cycle: 1, 2 or 4 bars of four beats (kBarBeats)
    std::vector<Lane> lanes;  // at least the default lane (normalized(): first)
    double base = 0.25;       // the grid a note is found on (Ableton's Base)
};

// How much of it plays: Timing 0..2, Velocity -1..1 (negative: turned round), Random 0..1, Quantize 0..1,
// Length 0..1 (how far a note's length goes to its step's), and Random's seed.
struct Amounts { double timing = 1.0, velocity = 1.0, random = 0.0, quantize = 0.0, length = 0.0; uint32_t seed = 0; };
// Where a note plays: its beat, velocity (1..127), length in beats, and the step of the cycle it fell on.
struct Placed { double beat = 0.0; double velocity = 100.0; double lengthBeats = 0.0; int step = 0; };
// A note a groove is made from.
struct NoteTime { double beat = 0.0; int key = 60; double velocity = 100.0; double lengthBeats = 0.25; };

// The steps the device, the clip view and the piano roll offer: 1/8, 1/8T, 1/16 (kDefaultStep), 1/16T, 1/32.
inline constexpr std::array<StepChoice, 5> kSteps{...};
int stepIndex(double beats);  // the choice nearest

Groove normalized(Groove groove);                // held to what a groove may be
const Lane* laneFor(const Groove& groove, int key);
Placed place(const Groove& groove, double beat, int key, double velocity, double lengthBeats, const TempoMap& tempo,
             const Amounts& amounts);

// Generators and extraction.
Groove swing(double percent, double stepBeats = 0.25);  // MPC swing: 50 straight, 66.7 a triplet feel, up to 75
std::optional<double> swingOf(const Groove& groove);    // the swing a groove is, if it is one
Groove fromNotes(std::span<const NoteTime> notes, double stepBeats, int bars, bool perKeyLanes);
Groove fromOnsets(std::span<const double> onsetBeats, std::span<const float> levels, double stepBeats, int bars);

// As text (devices keep their groove in their state so), and back.
std::string toText(const Groove& groove);
std::optional<Groove> fromText(std::string_view text);

}  // namespace sub::groove
```

- **Timing is a warp that only rises.** Each step's place is moved by its offsets, and between two steps a note
  moves in proportion of their time; a step that would land at or before the one before it is held just after it.
  So `place()` can't reorder the notes of a lane. Random adds a triangular jitter (up to a quarter of a step either
  way at 100 %), from a hash of the seed, the cycle and the step, so a render is the same every time and each cycle
  varies.
- **Quantize** first pulls the note toward the base grid, then the groove moves it; with Quantize 0 the groove's
  offset is added to where the note is (a played part keeps its feel and gets the groove's).
- **Velocity and length** come from the step the note falls on (the nearest point of the base grid; between two
  steps, the two interpolated).
- **Milliseconds** are added in seconds where the tempo map puts each step, so they stay the same under any tempo.
- **The cycle** is counted in bars of four beats from the song's start (see [Decisions](#decisions)).
- `place()` neither allocates nor throws: the Groove device calls it on the audio thread.
- It lives in `music/` because every layer needs it: the engine (the device, Follow Groove), the application (clip
  grooves, Apply Groove, extraction) and the tests, and `music/` is under all of them. Its details are in
  [music.md](music.md#groove).

**Where grooves come from:**

- **Swing**: `swing(percent, step)` makes the classic grooves (every second step late; at 66.7 % a triplet feel).
  The device, a clip and the piano roll have a plain Swing that is this, with no groove chosen.
- **The built-in grooves** (`grooves::builtins()`, [Grooves.h](../app/src/model/Grooves.h)): MPC swing on 16ths and
  8ths at 50, 54, 58, 62, 66, 71 and 75 % (`builtin:mpc-16-50` to `builtin:mpc-8-75`), triplet shuffles
  (`builtin:shuffle-8t`, `builtin:shuffle-16t`, `builtin:shuffle-lazy-8` at 70 %) and a few played drum grooves with
  lanes for kicks, snares or hats (`builtin:laid-back`, `builtin:boom-bap`, `builtin:funk-ghosts`, `builtin:drunk`,
  `builtin:pushed-hats`). Added to the library as copies (`ProjectEditor::addBuiltinGroove`).
- **Extract Groove**, from a clip (`ProjectEditor::extractGroove`): a MIDI clip's heard notes (`grooves::fromMidiClip`:
  their offsets from the grid, velocities from the clip's average, one lane or a lane a key with `perKeyLanes`), or
  an audio clip's transients (`grooves::fromAudioClip`: the ones Mark Transients marks, from the bridge's
  `EngineBridge::transients()`, and their strengths as velocities). The groove goes into the library, named after its
  track, with the notes it came from (`LibraryGroove::notes`, folded into its cycle), which the groove editor draws
  under it. An audio file not decoded yet gives none.
- **Groove files**: SUBstation's own `.gilgroove` (JSON: format `gilstudio-groove`, version 1, the name, the groove,
  the notes) and Ableton's `.agr` (read only: its clip's notes made into a groove on its Base grid over its clip's 1,
  2 or 4 bars, its Timing and Velocity worked into the groove, since a library groove keeps no amounts)
  ([GrooveFiles.h](../app/src/io/GrooveFiles.h)). The user's groove files are the preset library's Grooves folder
  (`libraryDir()/Grooves`, [Presets.h](../app/src/io/Presets.h)); the Grooves dialog adds from them and saves into
  them. They are listed in the browser as presets are: a groove file is a Groove device's preset, which loads as a
  Groove device playing it as its own (`grooveDevice()`).
- **A Live Set's groove pool** comes across ([LiveImport.cpp](../app/src/io/LiveImport.cpp),
  [app/live-import.md](app/live-import.md)): each groove into the library (as an `.agr` is read, its amounts kept
  apart), and each clip that plays one (`GrooveSettings/GrooveId`) gets a clip groove playing it at the groove's
  Timing, Velocity, Random and Quantize, scaled by the set's Global Amount; audio clips only if warped, as Live
  grooves them.

## The groove library (the project)

- `Project::grooves()`: the library, in order, each a `LibraryGroove` (id, name, `Groove`, the notes it was made
  from: [GrooveTypes.h](../app/src/model/GrooveTypes.h)). Saved in the `.gilproj` (version 28: `"grooves"`,
  `"default_groove"` and a clip's `"groove"`, only when there are some; [app/serialization.md](app/serialization.md)),
  edited with undo in [EditorGrooves.cpp](../app/src/editor/EditorGrooves.cpp): `ProjectEditor::addGroove`,
  `addBuiltinGroove`, `duplicateGroove`, `setGroove` (the groove editor: a drag with a merge key is one step),
  `renameGroove` (names made unique), `removeGroove`. One removed while it is used leaves its users their own copy
  (`grooves::detached()`), so nothing changes sound; all in one step (`SetGroovesCommand`).
- **What plays a groove** is a source (`GrooveSource`): `Swing` (a percent and a step), `Project` (the default),
  `Library` (a groove's id) or `Own` (a groove of its own). `grooves::resolve()` gives the groove a source plays
  (normalized; none for Project without a default, or a library groove that isn't there); `describe()` how it is
  shown.
- **Its users**: the Groove device (its source in its state), a clip (`Clip::groove`: a `ClipGroove`, its source and
  its amounts), and the project's default. The engine has no library: a Groove device playing a library groove holds
  a copy of it in its state (`"groove"`), and an edit of the library changes those copies in the same undo step
  (`commitGrooves`). Clips' grooves are resolved where the bridge flattens them.
- **The project's default groove** (`Project::defaultGroove()`; never Project itself): Edit › Default Groove (None,
  Swing at 54, 58, 62, 66.7 or 71 %, or one of the project's grooves) and the transport's Swing box (50 to 75 %, Off
  at 50; dragging it makes the default a swing) set it (`setDefaultGroove`, `setDefaultSwing`,
  `setDefaultGrooveId`, undoable). The bridge hands it to the engine resolved (`Engine::setGroove`, into the
  snapshot and `ProcessContext::groove`), as it hands it the song's key: the Groove device's Project source and
  Follow Groove play it. Clips set to Project play it too.
- **The groove editor** (Edit › Grooves…, [GrooveDialog.qml](../ui/qml/dialogs/GrooveDialog.qml)): the library on the
  left (added from the built-in grooves, the user's groove files or a swing; duplicated, renamed, removed, saved as a
  file, made the default); on the right the groove chosen: a lane a row (a lane's keys typed as note numbers), a bar a
  column, each step a handle dragged late or early and its velocity below, double-click putting it back, the notes it
  came from drawn faintly under it; its cycle's bars and base grid above. Not modal: the song plays on, and what plays
  the groove follows each drag.
- For QML, `Session.grooves` ([GrooveLibrary.h](../app/src/session/GrooveLibrary.h)): the library, the built-in
  grooves and the user's files, a groove as a map, what a device or a clip plays, the editor's edits, the default.
- The MCP server's `edit_notes` has a `groove` operation next to `quantize` and `humanize_timing`: Swing (a percent
  and a step), the project's default, or a library groove by id or name, at Timing, Velocity, Random, Quantize and a
  seed ([app/mcp.md](app/mcp.md)).

## 1. The Groove device (a MIDI effect)

A built-in MIDI effect, `builtin:groove` ([Groove.cpp](../engine/src/builtin/devices/Groove.cpp)), listed with the
Arpeggiator. Its notes in, the same notes out, moved, with their velocities and lengths as the groove has them. The
full reference is in [engine/devices.md](engine/devices.md#groove-builtingroove-midieffect).

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `swing` | Swing | % | 50..75 | 50 |
| `step` | Step | | 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 |
| `timing` | Timing | % | 0..200 | 100 |
| `velocity` | Velocity | % | -100..100 | 100 |
| `length` | Length | % | 0..100 | 0 |
| `random` | Random | % | 0..100 | 0 |
| `quantize` | Quantize | % | 0..100 | 0 |
| `early` | Allow Early | | Off, On | On |
| `latency` | Latency | | Off, On | Off |

The groove it plays is its **source**, a state value, not a parameter: `"source"` is `swing` (Swing and Step:
`groove::swing()`), `project` (the default groove: `ProcessContext::groove`) or `groove` (`"groove"`, a groove's
text, `groove::toText()`, which it plays; `"groove_id"`, the library groove it is a copy of, which only the
application reads). `"seed"` is Random's seed. The application keeps the state (`grooves::deviceSource()`,
`deviceState()`, `ProjectEditor::setDeviceGrooveSource`). Every parameter automates and modulates as any device's: a
note takes the values at the moment it comes in.

- **Moving notes later**: a note-on is held and played at its time; its note-off is moved by as much (and by Length),
  and its per-note bends follow it (its note id is kept). Raw MIDI passes at once (with Latency, as late as the
  notes). Up to 64 notes held. Stopped, the
  notes pass as they come (there is no beat to groove them on), and stopping drops the held notes that haven't
  sounded. Switched off, `endMidiNotes()` plays the sounding notes' note-offs (the held ones are dropped: they haven't
  sounded).
- **The display** `step`: the step of the cycle the newest sounding note fell on (-1 while none sounds). Its editor
  ([GrooveEditor.qml](../ui/qml/devices/editors/GrooveEditor.qml)) draws the groove's steps and lights the one
  playing; its source is chosen above them; **Add to Grooves** puts the groove it plays in the library (it then
  plays that), **Apply** writes what it does into its track's notes (below).

### Moving notes earlier: early notes

A note can't come out before it comes in. For **notes from clips** the engine knows them ahead, so it hands them
over early ([engine/midi.md](engine/midi.md#early-notes) has the renderer's side):

- A MIDI effect says how early it may move a note: `Processor::earlyBeats()` (the Groove device: half a step while
  Allow Early is on and Latency off; 0 otherwise).
- After each chunk's notes, the renderer hands a chain's **first** MIDI effect the clips' notes due within its early
  beats after the chunk, and their note-offs, as early events (`ProcessContext::earlyEvents`, each with where it
  would play), round the loop once at most. Each handed note-on has the id the note will come with in its time; the
  device places it both ways (no earlier than half a step) and plays it into `outEvents` as it plays any note. The
  note still comes in its time, with that id, for the devices that didn't take it early (another chain of a rack):
  the device ignores it then, and the devices after it hear the note as the device played it.
- **Pads**: a Drum Rack's pad gets the early events of its key, as C3, as it gets its notes, so a pad's Groove device
  gets its pad's notes early.
- **Stopping and locating** end the notes handed early that haven't sounded: the device drops them.
- **Live input** can't come early: those notes are only moved later. With **Latency** on, the device reports half a
  step as its latency and plays every note that much later, so delay compensation
  ([engine/routing.md](engine/routing.md)) lets live input move both ways too, the track sounding that much later.
- **Notes a MIDI effect plays** aren't known early: a Groove device after an Arpeggiator moves its notes later only
  (or the Arpeggiator follows the groove itself: Follow Groove).

### Offline renders

The same code path: the device sees the same notes, early or not, in an export as in playback, and Random comes from
its seed, so exports are the same every time and the same as what was heard.

### Follow Groove

A switch, `follow_groove` (Off by default; a **Groove** button in their editors), on the devices that play on the
beat, following the project's default groove (`ProcessContext::groove`; without one, they play as they would):

- **The Arpeggiator**: synced while the song plays, its steps where the groove puts them (`groove::place()` of each
  step's beat), each step's notes as loud as the groove makes them, in place of Swing.
- **The Delay** (and the Vintage Delay, its engine with the Tape character): its Swing is the groove's: every other
  echo as late as the groove's offbeat steps are on average, as a share of that side's time.
- **The LFO**: synced and following the song, it plays its straight time where the groove puts it, so a 1/16 LFO
  swings with the hats.

See [engine/devices.md](engine/devices.md) for each.

## 2. A clip's groove

A clip of either kind can play a groove (`Clip::groove`, a `ClipGroove`: a source and its amounts; Length stays 0).
What it plays moves where the engine bridge hands the clip to the engine
([EngineDescs.h](../app/src/audio/EngineDescs.h), [app/engine-bridge.md](app/engine-bridge.md)); the clip's own notes
and warp markers stay as they are, and its pieces (split, copied) keep it.

- **A MIDI clip**: its notes are grooved where the bridge flattens them (`clipNoteDescs`, `grooves::groovedNotes()`:
  each heard note placed by `groove::place()` on the timeline, its length and velocity as the groove has them, a note
  running into the next on its key ending where that one starts). The engine gets them where they play.
- **An audio clip**: audio can't be moved after it plays, so the groove goes into where its audio sits in its beats:
  its warp markers (`grooves::groovedMarkers()`). A point at each of its own markers, its window's ends and each
  transient in its window (the ones Mark Transients marks: `EngineBridge::transients()`), each where `place()` puts
  its beat; the audio between them stretches. The clip's start stays where it is. A point that would make a stretch
  too fast or slow is left out. The bridge gives the engine a `ClipDesc` with those markers (`groovedClip()`), which
  it plays as any warped clip: stretched live, then from the warp cache's render. Until its file is decoded (its
  transients found) the clip plays as it is, and it is handed over again once it is. With Quantize at 100 % the hits
  first go onto the grid, so a loosely played loop can take any groove.
- **What the clip needs**: an audio clip must be warped: a groove set on an unwarped one warps it, as warping it by
  hand does (`grooves::warpedForGroove()`). A clip playing a frozen track's tempo map takes none
  (`grooves::takesGroove()`).
- **Following changes**: the library, the default groove or the tempo map changing hands the grooved clips over again
  (`EngineBridge::pushGrooves`).
- **The settings**, in the clip view ([ClipGrooveControls.qml](../ui/qml/clipview/ClipGrooveControls.qml); the audio
  controls' Groove section, the MIDI header's **Groove ▾**): the groove (None, Swing, Project, one of the library's,
  or its own while it has one), Swing and Step (Swing's), Timing, Velocity (MIDI clips only), Random and Quantize;
  Apply and Extract. Every clip shown changes together, one undo step (`ProjectEditor::setClipGroove`).

## 3. Apply Groove

- `notes::grooved(notes, groove, amounts, tempo)` in [Notes.h](../app/src/model/Notes.h), next to `quantized` and
  `humanizedTiming`: pure, tested like them. It places each note (its start in timeline beats) with
  `groove::place()` and `untangle`s the result.
- **In the piano roll**, **Groove ▾** next to Quantize ([NoteTools.qml](../ui/qml/pianoroll/NoteTools.qml),
  `PianoRoll::applyGroove`): Swing (on 16ths, its percent beside it), Project (the default groove; none: the status
  line says so) and the project's grooves, with Timing, Velocity, Random and Quantize below. Ctrl+Shift+U applies
  the last one again. Selected notes, or every note with none selected; the cycle is the song's, wherever the clip
  starts; Random's seed is new each time.
- **On clips** (the clip view's Apply: `ProjectEditor::applyClipGrooves`, `grooves::applied()`): a MIDI clip's groove
  written into its notes, an audio clip's grooved warp markers written as its own; the groove cleared. One undo step.
- **On the device** (its editor's Apply: `ProjectEditor::applyDeviceGroove`): what a Groove device does (its source,
  parameters and seed; early moves held to half a step with Allow Early or Latency, none without) written into the notes of its
  track's MIDI clips (on a Drum Rack's pad, that pad's key's, placed as the pad hears them, as C3), and the device
  switched off; one undo step.
- **The MCP server**: `edit_notes`' `groove` operation (above).

## Files

| File | What it holds |
|---|---|
| [music/src/music/Groove.h](../music/src/music/Groove.h) / [.cpp](../music/src/music/Groove.cpp) | `Groove`, `Lane`, `Step`, `Amounts`, `Placed`, `NoteTime`, `kSteps`; `normalized()`, `laneFor()`, `place()`, `swing()`, `swingOf()`, `fromNotes()`, `fromOnsets()`, `toText()`, `fromText()` |
| [engine/src/builtin/devices/Groove.cpp](../engine/src/builtin/devices/Groove.cpp) | the Groove device |
| [engine/src/builtin/devices/Arpeggiator.cpp](../engine/src/builtin/devices/Arpeggiator.cpp), [Delay.cpp](../engine/src/builtin/devices/Delay.cpp), [Lfo.cpp](../engine/src/builtin/devices/Lfo.cpp) | Follow Groove |
| [engine/src/Processor.h](../engine/src/Processor.h) | `Processor::earlyBeats()`; `EarlyEvent`, `EarlyList`; `ProcessContext::earlyEvents`, `tempoMap`, `groove` |
| [engine/src/Snapshot.h](../engine/src/Snapshot.h) | `TrackBuffers::early` (`kMaxEarly`); `RenderSnapshot::groove` |
| [engine/src/Renderer.h](../engine/src/Renderer.h) / [.cpp](../engine/src/Renderer.cpp) | early notes: `buildEarlyEvents`, `handEarly`, `earlyBeatsOf`, `startHanded`, `padEarly`; `ActiveNote::early`, `pending`, `earlyOff` |
| [engine/src/Engine.h](../engine/src/Engine.h) / [.cpp](../engine/src/Engine.cpp), [EngineSnapshot.cpp](../engine/src/EngineSnapshot.cpp) | `Engine::setGroove()` (the project's default), into the snapshot |
| [app/src/model/GrooveTypes.h](../app/src/model/GrooveTypes.h) | `LibraryGroove`, `GrooveSource`, `ClipGroove` |
| [app/src/model/Grooves.h](../app/src/model/Grooves.h) / [.cpp](../app/src/model/Grooves.cpp) | sources (`resolve`, `describe`, `detached`), a MIDI clip's grooved notes, an audio clip's grooved markers (`groovedMarkers`, `takesGroove`, `warpedForGroove`), `applied`, extraction (`fromMidiClip`, `fromAudioClip`, `folded`), the Groove device's state (`deviceSource`, `deviceState`, `deviceSeed`), the built-in grooves |
| [app/src/model/Project.h](../app/src/model/Project.h), [Clip.h](../app/src/model/Clip.h), [Commands.h](../app/src/model/Commands.h) | the library and the default groove (`grooves()`, `defaultGroove()`, `groovesChanged`, `defaultGrooveChanged`); `Clip::groove`; `SetGroovesCommand`, `SetDefaultGrooveCommand` |
| [app/src/model/Notes.h](../app/src/model/Notes.h) | `notes::grooved()` |
| [app/src/editor/EditorGrooves.cpp](../app/src/editor/EditorGrooves.cpp) | the editor's groove edits: the library, Extract Groove, clips' grooves, Apply Groove on clips and on the device, the device's source, the default groove |
| [app/src/io/GrooveFiles.h](../app/src/io/GrooveFiles.h) / [.cpp](../app/src/io/GrooveFiles.cpp) | grooves as JSON (the project file's), `.gilgroove` files, `.agr` files (`agrGroove()`), a groove file as a Groove device |
| [app/src/io/Serialization.cpp](../app/src/io/Serialization.cpp), [Presets.cpp](../app/src/io/Presets.cpp) | the project's grooves (version 28); groove files as presets |
| [app/src/io/LiveImport.cpp](../app/src/io/LiveImport.cpp) | a Live Set's groove pool and its clips' grooves |
| [app/src/audio/EngineDescs.h](../app/src/audio/EngineDescs.h) / [.cpp](../app/src/audio/EngineDescs.cpp), [BridgeTracks.cpp](../app/src/audio/BridgeTracks.cpp), [BridgeSources.cpp](../app/src/audio/BridgeSources.cpp) | clips' grooves where the bridge flattens them (`GrooveContext`, `groovedClip`), `pushGrooves` (`Engine::setGroove`), files' transients |
| [app/src/mcp/McpEdits.cpp](../app/src/mcp/McpEdits.cpp) | `edit_notes`' `groove` operation |
| [app/src/session/GrooveLibrary.h](../app/src/session/GrooveLibrary.h) / [.cpp](../app/src/session/GrooveLibrary.cpp) | `Session.grooves`: the grooves for QML, the user's groove files |
| [ui/qml/devices/editors/GrooveEditor.qml](../ui/qml/devices/editors/GrooveEditor.qml), [GrooveSteps.qml](../ui/qml/devices/editors/GrooveSteps.qml) | the Groove device's editor; a groove's steps drawn |
| [ui/qml/clipview/ClipGrooveControls.qml](../ui/qml/clipview/ClipGrooveControls.qml) | a clip's groove in the clip view |
| [ui/qml/pianoroll/NoteTools.qml](../ui/qml/pianoroll/NoteTools.qml), [ui/src/pianoroll/PianoRoll.h](../ui/src/pianoroll/PianoRoll.h) | the piano roll's Groove menu, `PianoRoll::applyGroove` |
| [ui/qml/dialogs/GrooveDialog.qml](../ui/qml/dialogs/GrooveDialog.qml), [GrooveLaneEditor.qml](../ui/qml/dialogs/GrooveLaneEditor.qml) | Edit › Grooves…: the library and the groove editor |
| [ui/qml/Main.qml](../ui/qml/Main.qml), [ui/qml/transport/TransportBar.qml](../ui/qml/transport/TransportBar.qml) | Edit › Default Groove; the transport's Swing |

Every step of the plan this page started as is in; what it left open is settled under [Decisions](#decisions).

## Testing

- **The maths** ([test_groove.cpp](../tests/engine/test_groove.cpp), `engine_tests groove`): swing at 50 % moves
  nothing; at 66.7 % an offbeat 16th lands on the triplet; offsets never reorder notes on a key; milliseconds stay
  milliseconds under a tempo ramp; Random repeats with its seed and varies by cycle; Quantize 100 % then Timing 0 is
  plain quantizing; velocity and length from a note's step; lanes; `fromNotes()`, `fromOnsets()`; text and back.
- **The device, early notes and Follow Groove** ([test_groove_engine.cpp](../tests/engine/test_groove_engine.cpp)):
  its listing; swing moving the offbeats later, their note-offs with them; bends following their note; velocity and
  Length; on a Drum Rack's pad, only that pad's notes; Timing automated taking effect note by note; switched off
  mid-note, no note hanging; stopping dropping held notes. A note grooved early playing early from a clip, to the
  sample, across a chunk's end (in chunks of several sizes), never more than half a step, round the loop's wrap, from
  a pad, never twice; stopping and locating dropping a note handed early. The Arpeggiator's steps, the Delay's swing
  and a synced LFO following the project's groove.
- **The application** ([test_grooves.cpp](../tests/app/test_grooves.cpp)): a MIDI clip's grooved notes, Apply Groove
  placing notes as the device would, an audio clip's grooved markers, Apply Groove on clips and on the device (a pad's
  notes only), Extract Groove, the built-in grooves, sources, the library with undo (a groove removed leaving its
  users their own copy, an edit reaching the devices playing it), the default groove, `.gilgroove` and `.agr` files,
  project files.
- **Through the bridge** ([test_bridge_grooves.cpp](../tests/app/test_bridge_grooves.cpp)): a MIDI clip's groove
  moving the notes the engine plays, a library groove edited heard where it plays, the Groove device playing the
  project's default, an audio loop's transients heard on the grooved beats and back on its own when the groove is
  removed.
- **The UI**: the device's editor ([test_ui_device_editors_groove.cpp](../tests/app/test_ui_device_editors_groove.cpp)),
  the Grooves dialog ([test_ui_grooves.cpp](../tests/app/test_ui_grooves.cpp)), the piano roll's Groove menu and
  Ctrl+Shift+U ([test_ui_pianoroll.cpp](../tests/app/test_ui_pianoroll.cpp)), an audio clip's groove in the clip view
  ([test_ui_clipview.cpp](../tests/app/test_ui_clipview.cpp)).
- **Live import** ([test_live_import.cpp](../tests/app/test_live_import.cpp)): a Live Set's groove pool and a clip's
  groove arriving, at the groove's amounts scaled by the Global Amount.

## Decisions

- **A groove's cycle is counted in bars of four beats from the song's start** (`groove::kBarBeats`), not by the
  time signature: a groove doesn't follow time signature changes (a 7/8 bar has no steps of its own). `place()`
  needs only the tempo map, and the cycle is the same for the device, a clip and Apply Groove wherever a clip starts.
- **Velocity on audio clips is left out for now**: the clip view offers no Velocity for audio (a clip's amounts keep
  one, which only MIDI clips play), and no gain is put around the hits. The Transient Shaper can do it.
- **Follow Groove is off by default** on the Arpeggiator, the Delay and the LFO, whether the project has a default
  groove or not: choosing a default groove changes nothing those devices play until it is switched on.
