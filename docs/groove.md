# Groove (plan)

**Status: a plan, not implemented.** Swing and feel for drums and anything else, in three ways that share one piece
of maths:

1. **The Groove device**, a MIDI effect: grooves the notes reaching it, live, per track or per Drum Rack pad,
   automated and modulated like any device.
2. **A clip's groove**, on audio clips: the audio itself moved to the groove, through the clip's warp markers.
3. **Apply Groove**, in the piano roll: the groove written into the notes, as Quantize writes the grid.

All three play **grooves** from the project's groove library (shared: edit one, everything using it follows) or a
groove of their own. This page is the design and the order to build it in. Read [music.md](music.md),
[engine/midi.md](engine/midi.md), [engine/devices.md](engine/devices.md#midi-effects) and
[engine/warp.md](engine/warp.md) first.

## What Ableton's groove pool does, and what this does differently

| | Ableton's groove pool | Here |
|---|---|---|
| Shared grooves, edited in one place | yes | yes: the project's groove library |
| Where a groove applies | a whole clip | a track, a rack chain, one Drum Rack pad (the device), an audio clip, or notes written once |
| Hats swung, kick straight, in one clip | no (split the clip) | a Groove device on the hats' pad |
| Live input, the Arpeggiator's notes | no | yes: the device grooves whatever reaches it |
| Amounts automated or modulated | no (one global amount) | yes: the device's parameters |
| Feel in milliseconds (a snare 8 ms late at any tempo) | no | yes: a groove's offsets can be in ms |
| Seeing what it did | only by committing | the device's display, and Apply Groove |

## The groove (`music/src`, no Qt)

A groove is a value, defined over one **cycle** of whole bars, in beats, so the tempo map moves it with the song:

```cpp
namespace sub::groove {

struct Step {
    double timing = 0.0;      // fraction of a step, -0.5..0.5 (+ later)
    double timingMs = 0.0;    // and milliseconds, -50..50, the same at any tempo
    double velocity = 0.0;    // added, -1..1 of full scale
    double length = 1.0;      // a note's length times this, 0.25..2
};

struct Lane {                 // an optional lane for some keys only (the hats, the snare)
    std::vector<int> keys;    // empty: the default lane, for every key no other lane takes
    std::vector<Step> steps;  // one per step of the cycle
};

struct Groove {
    double stepBeats = 0.25;  // 1/16 (1/8, 1/8T, 1/16T, 1/32...)
    int bars = 1;             // the cycle: 1, 2 or 4 bars
    std::vector<Lane> lanes;  // at least the default lane
    double base = 0.25;       // the grid a note is found on (Ableton's Base)
};

// Where a note at `beat` on `key` plays, and how: the step it falls on (nearest within half a step on the base
// grid; between steps, the offsets are interpolated), its offsets scaled by `amounts`. Pure and deterministic.
struct Amounts { double timing = 1.0, velocity = 1.0, random = 0.0, quantize = 0.0; uint32_t seed = 0; };
struct Placed { double beat; double velocity; double lengthBeats; };
Placed place(const Groove&, double beat, int key, double velocity, double lengthBeats, const TempoMap&, Amounts);

// Generators and extraction.
Groove swing(double percent, double stepBeats);   // MPC swing: 50 straight, 66.7 a triplet feel, up to 75
Groove fromNotes(std::span<const NoteTime> notes, double stepBeats, int bars, bool perKeyLanes);
Groove fromOnsets(std::span<const double> onsetBeats, std::span<const float> levels, double stepBeats, int bars);

}  // namespace sub::groove
```

- **Timing is a curve that only rises.** Offsets are held so that no step lands at or before the one before it
  (`place()` can't reorder notes on one key). Random adds a triangular jitter (as `notes::humanizedTiming` does), from
  a hash of the seed, the cycle and the step, so a render is the same every time and each cycle varies.
- **Quantize** first pulls the note toward the base grid (Ableton's Quantize amount), then the groove moves it; with
  quantize 0 the groove's offset is added to where the note is (a played part keeps its feel and gets the groove's).
- **Milliseconds** go through the tempo map at the note's beat, so they stay the same under any tempo.
- It lives in `music/` because every layer needs it: the engine (the device), the application (Apply Groove, the
  clip's groove, extraction) and the tests, and `music/` is under all of them.

**Where grooves come from:**

- **Swing**: `swing(percent, step)` makes the classic grooves (the offbeat steps late; at 66.7 % a triplet feel).
  The device and the clip have a plain Swing knob that is this, with no groove chosen.
- **From a MIDI clip** (Extract Groove, on a clip): its notes' offsets from the grid, their velocities from the
  clip's average, one lane, or a lane a key (`perKeyLanes`: a drummer's hats and snare keep their own feel).
- **From an audio clip**: its transients (`sampleSlices::transients`, [SampleSlices.h](../app/src/audio/SampleSlices.h),
  what Mark Transients uses) and their levels, in the clip's beats.
- **Ableton's grooves**: `.agr` files (XML: a MIDI clip's notes, with Base, Quantize, Timing, Random and Velocity),
  read as a MIDI clip is. A Live Set's groove pool comes across too, and its clips' grooves become clip grooves or a
  Groove device ([app/live-import.md](app/live-import.md)): the import leaves them out today.
- **A library**: a few built in (MPC 50–75 %, triplet shuffles, a few played drum grooves), files in the user's
  library, shown in the browser as presets are.

## The groove library (the project)

- `Project::grooves()`: id, name, `Groove`. Saved in the `.gilproj` ([app/serialization.md](app/serialization.md)),
  edited with undo (`ProjectEditor::addGroove`, `setGroove`, `renameGroove`, `removeGroove`: one removed while used
  leaves its users with their own copy, so nothing changes sound).
- A user is the Groove device (a state value: the groove's id, or its own groove), an audio clip (`Clip::groove`), or
  the project's default.
- **The project's default groove** (an Edit menu entry and the transport's Swing: a groove or a swing amount). Devices
  that play on the beat get a **Follow Groove** switch: the Arpeggiator's steps, the Delay's swing, synced LFOs. The
  engine gets the default as it gets the song's key (`Engine::setGroove`, into `ProcessContext::groove`), so a device
  reads it in `render()`.
- **The groove editor**: a lane a row, a bar a column, each step a handle dragged late or early (and its velocity
  below), like the piano roll's velocity lane; the notes it came from drawn faintly under it.

## 1. The Groove device (a MIDI effect)

A built-in MIDI effect ([engine/devices.md](engine/devices.md#midi-effects)), `builtin:groove`, listed with the
Arpeggiator. Its notes in, the same notes out, moved.

| id | Name | Unit | Range | Default |
|---|---|---|---|---|
| `source` | Groove | | Swing, Project, a library groove (a state value) | Swing |
| `swing` | Swing | % | 50..75 | 50 |
| `step` | Step | | 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 |
| `timing` | Timing | % | 0..200 | 100 |
| `velocity` | Velocity | % | -100..100 | 100 |
| `length` | Length | % | 0..100 | 0 |
| `random` | Random | % | 0..100 | 0 |
| `quantize` | Quantize | % | 0..100 | 0 |
| `early` | Allow Early | | Off, On | On |
| `latency` | Latency | | Off, On | Off |

Every parameter automates and modulates as any device's: a note takes the values at the moment it comes in.
The display streams the steps played (the editor lights them, as the Arpeggiator's does).

### Moving notes later

A note that should play later is held and played at its time; its note-off is moved by as much (and by Length), and
its per-note bends follow it (its `noteId` is kept). Raw MIDI passes at once. Up to 64 notes held. Switched off,
`endMidiNotes()` plays the held note-ons' note-offs (the held notes are dropped: they haven't sounded). This needs no
engine change, and the classic swing only moves notes later.

### Moving notes earlier: early notes

A note can't come out before it comes in. For **notes from clips** the engine knows them ahead, so it can hand them
over early:

- A MIDI effect says how early it may move a note: `Processor::earlyBeats()` (the Groove device: half a step while
  Allow Early is on; 0 otherwise). The snapshot takes, for each chain, the largest of its MIDI effects' (a chain in
  a rack, its own).
- **The prologue's early window**: for a chain with early beats `e`, `buildNoteEvents` also sends the clip notes that
  start in `[chunk end, chunk end + e)`, and their note-offs, as **early events**: `ProcessContext::earlyEvents`,
  each with the sample where it would have played. Only the first MIDI effect in the chain gets them; it plays
  them (at their grooved time, or held to their own) into `outEvents` as it plays any note. An early note is
  not sent again when its time comes (the renderer marks it sent, as it marks notes started).
- **Pads**: `Renderer::padNotes()` filters the early events by the pad's key as it filters the others, so a pad's
  Groove device gets its pad's notes early.
- **The loop and locating**: an early window that runs past the loop's end takes the notes after the loop's start
  (where the playhead will be). Playing from a beat, a note whose grooved time would be before it plays at once.
  Stopping drops the early notes held, never sounded.
- **Live input** can't come early: those notes are only moved later (the groove's early steps play at their time).
  With Latency on, the device reports its early beats as latency, and delay compensation
  ([engine/routing.md](engine/routing.md)) lets live input be moved both ways too, the track sounding that much
  later.
- **The Arpeggiator before it**: notes a MIDI effect plays aren't known early. The Groove device after an
  Arpeggiator moves its notes later only (or the Arpeggiator follows the groove itself: Follow Groove).

### Offline renders

The same code path: the device sees the same notes, early or not, in an export as in playback, and Random comes from
the seed, so exports are the same every time and the same as what was heard.

## 2. A clip's groove (audio clips)

Audio can't be moved after it plays, so for audio the groove goes into where the clip's audio sits in its beats:
**its warp markers** (`Clip::warpMarkers`, `sourceMap`). The clip plays from its grooved map; the clip's own markers
stay as they are.

- **What it moves**: the transients of the clip's audio (`sampleSlices::transients`, the ones Mark Transients uses). Each
  transient near a step of the base grid becomes a point that goes where `groove::place()` puts that step; the
  audio between them stretches. With quantize 100 % the hits first go onto the grid, so a loosely played loop can
  take any groove.
- **Where it is done**: in the application layer, where the engine bridge sends a clip
  ([app/engine-bridge.md](app/engine-bridge.md)). It gives the engine a `ClipDesc` whose source map is the grooved
  one. The engine plays it as a clip with warp markers: stretched live, then from the warp cache's render. The engine
  doesn't change.
- **What the clip needs**: to be warped (a groove on an unwarped clip warps it, as adding a warp marker does). A
  frozen track's clip takes no groove, as it takes no markers.
- **The settings**, in the clip view: Groove (none, Swing, Project, a library groove), Swing, Step, Timing, Quantize,
  Random. Velocity grooves the hits' level (a short gain around each transient, the clip's gain envelope); later.
- **A MIDI clip's groove** too, the same settings: its notes are grooved where the bridge flattens them
  (`Clip::heardNotes()`), so the engine gets them where they play. This is Ableton's groove pool, for those who like
  it; the device is the one that automates.

## 3. Apply Groove (the piano roll)

- `notes::grooved(notes, groove, amounts, tempo)` in [Notes.h](../app/src/model/Notes.h), next to `quantized` and
  `humanizedTiming`: pure, tested like them. It places each note with `groove::place()` and `untangle`s the result.
- In the piano roll, **Groove ▾** next to Quantize: the project's grooves and Swing, with the amounts at its right
  (as Humanize's menu has); Ctrl+Shift+U applies the last one. Selected notes, or every note with none selected.
- **Apply Groove on a clip** (the clip's menu): writes a MIDI clip's groove into its notes and clears it; on an audio
  clip, writes its grooved map as its warp markers.
- **Apply Groove on the device**: writes what a Groove device does into the clips of its track (or its pad's notes)
  and switches it off.
- The MCP server's note tools get `groove` next to `quantize` and `humanize` ([app/mcp.md](app/mcp.md)).

## Files (planned)

| File | What it would hold |
|---|---|
| `music/src/music/Groove.h` / `.cpp` | `Groove`, `Lane`, `Step`, `place()`, `swing()`, `fromNotes()`, `fromOnsets()` |
| `engine/src/builtin/devices/Groove.cpp` | the Groove device |
| `engine/src/Processor.h`, `Snapshot.h`, `Renderer.cpp` | `earlyBeats()`; `ProcessContext::earlyEvents`, `groove`; the early window in `buildNoteEvents` and `padNotes` |
| `engine/src/Engine.h` | `setGroove()` (the project's default) |
| `app/src/model/Project.h`, `Clip.h` | the groove library; `Clip::groove` (a groove id or its own, and its amounts) |
| `app/src/model/Notes.h` | `notes::grooved()` |
| `app/src/model/Grooves.h` / `.cpp` | a clip's grooved map (transients to points), the built-in grooves |
| `app/src/io/Groove*.cpp` | `.agr` files; the library on disk |
| `app/src/io/LiveImport.cpp` | a Live Set's groove pool and its clips' grooves |
| `ui/qml/...`, `ui/src/pianoroll/...` | the device's editor, the clip view's Groove, the piano roll's Groove menu, the groove editor |

## Steps

Each can be merged by itself.

1. **The maths**: `music/Groove`, `swing()`, `place()`, `fromNotes()`, with their tests. Nothing to hear yet.
2. **The Groove device, later only**: Swing and Timing, Velocity, Length, Random, Quantize, moving notes later; its
   editor. The classic swing works on every track and pad from here.
3. **Apply Groove** in the piano roll and its MCP tool: the same maths, written into notes.
4. **The groove library**: the project's grooves, saved and undone; Extract Groove from MIDI clips; `.agr` files; the
   built-in grooves; the groove editor. The device and Apply Groove pick from it.
5. **Early notes**: `earlyBeats()`, the prologue's early window (pads, the loop, locating), Allow Early; Latency for
   live input.
6. **Clip grooves**: MIDI clips at flattening, audio clips through their grooved map (extraction from audio clips
   comes with it); Live Sets' grooves.
7. **The project's default groove** and Follow Groove on the Arpeggiator, the Delay and synced LFOs.

## Testing

- **`place()`** (music's tests): swing at 50 % moves nothing; at 66.7 % an offbeat 16th lands on the triplet; offsets
  never reorder notes on a key; milliseconds stay milliseconds under a tempo ramp; Random repeats with its seed and
  varies by cycle; Quantize 100 % then Timing 0 is plain quantizing.
- **The device** (`engine_tests groove`): notes moved later by the groove's offsets, note-offs with them, bends
  following their note; on a Drum Rack pad, only that pad's notes; automation of Timing taking effect note by note;
  switched off mid-note, no hanging notes.
- **Early notes**: a note grooved early plays early from a clip, sample-exact, across a chunk's end, across the loop's
  wrap, from a pad; never twice; an export identical to playback.
- **Apply Groove** (app tests): the piano roll's notes the same as the device would play them.
- **Clip grooves**: an audio loop's transients on the grooved beats after the bridge (the `ClipDesc` the engine
  gets), and back on its own when the groove is removed.
- **Live import**: a Live Set's groove pool and a clip's groove arrive, as `test_live_import` checks the rest of a set.

## Open questions

- Should a groove's cycle follow time signature changes (a 7/8 bar has its own steps), or always be counted in beats
  from the song's start?
- Velocity on audio clips: a gain envelope around each hit, or leave it to the Transient Shaper.
- Whether Follow Groove should be on by default for the Arpeggiator once the project has a default groove.
