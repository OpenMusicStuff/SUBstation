# Project model and undo

The project model is the single source of truth for the UI, undo and saving. It lives in
[app/src/model](../../app/src/model): value types for tracks, clips, notes, devices and envelopes, a `Project`
QObject that holds them and signals every change, pure functions that do the editing maths, and the undo
commands. The `ProjectEditor` ([app/src/editor](../../app/src/editor)) turns each user action into those
commands. The audio engine only mirrors this model (see [engine-bridge.md](engine-bridge.md)); saving and loading
are in [serialization.md](serialization.md); what acts on the selection (the clipboard, the Edit menu) is the
session's ([session.md](session.md)).

## Overview

```
 QML control / scene-graph item ──calls──► ProjectEditor (editor/)
                                              │  works out the new state with the pure functions:
                                              │  Edits.h (clips), Notes.h (notes), Automation.h (envelopes)
                                              ▼
                                         QUndoCommand (Commands.h) ──pushed onto──► QUndoStack (the session's)
                                              │ redo() / undo()
                                              ▼
                                         Project mutators (Project.h) ──emit──► Qt signals
                                                                                   │
                              ┌────────────────────────────┬───────────────────────┼──────────────────────┐
                              ▼                            ▼                       ▼                      ▼
                       the UI repaints        EngineBridge pushes the      the session prunes      the title's "*"
                                              change into the engine       the selection           (undo stack not clean)
```

- **Undoable state** changes only through commands. `Project`'s mutating methods are meant to be called by a
  command's `redo()`/`undo()` and nothing else, so every edit is undoable and every change is signalled.
- **View state** (track heights, folding, which automation shows, arming, solo, Lock Envelopes, folded devices) is
  saved with the project but changes directly, without an undo step, as in Ableton.
- Everything runs on the Qt GUI thread. Nothing in `model/` calls the engine; `Devices.h` reads the engine's list
  of built-in devices (`sub::BuiltinRegistry`), and `ParamSpec` is made from the engine's `ParamInfo`.
- It is `sub_app`'s: Qt Core and Gui (`QUndoCommand` and `QUndoStack` are in Qt Gui), never QML. QML reads the
  `Project`'s properties and calls the editor's `Q_INVOKABLE`s through the `Session` singleton.

## Files

| File | What it holds |
|---|---|
| [Project.h](../../app/src/model/Project.h) | `Project` (QObject: queries, mutators, signals), `ProjectContents` (everything replaced at once), `ChainField`/`ChainValue` and `SettingsField`/`SettingsValue` (what `updateChain` and `updateSettings` change), `DeviceParam`, `LaneRef` |
| [Track.h](../../app/src/model/Track.h) | `Track`, `Freeze`, `MidiInput`, `Send`, `SendMap`, `EnvelopeMap`; `TrackField`/`TrackValue` (what `updateTrack` changes) and `trackFieldName`; the kinds (`kAudioKind`, `kMidiKind`, `kGroupKind`, `kReturnKind`, `kMasterKind`), `kMonitorModes`, `kTrackColors`, track heights; `newMaster`, `returnLetter` |
| [TrackNames.h](../../app/src/model/TrackNames.h) | Track names: `#` for its number (`numberedName`), audio and MIDI tracks named by what they hold (`contentsLabel`, `contentsName`, `namedByContents`), names older projects had (`hasPlainName`, `plainNameTemplate`), `takeName` |
| [Clip.h](../../app/src/model/Clip.h) | `Clip` (audio and MIDI alike), `Note`, `BendPoint`, `Vibrato`, `PlayedNote`; `kWarpModes`, `legacyWarpMode`, `kMinSegmentBpm`, `kMaxSegmentBpm` |
| [Device.h](../../app/src/model/Device.h) | `Device`, `Chain`, `PluginRef`, `Sidechain`, `MacroMapping`; the device-tree helpers (`iterDevices`, `iterChains`, `devicePath`, `deviceAt`, `findDevice`, `findChain`, `chainIndex`, `chainDevices`, `containerOf`, `rackDepth`, `rackHeight`, `refreshIds`); `kPluginKind`, `kRackKind`, `kMaxRackDepth`, `kDefaultMacroCount`, `kMaxMacroCount`, `macroParam`, `macroIndex`, `macroCount`, `macroName`, the taps `kPostFader`, `kPreFader`, `kPreFx` |
| [Devices.h](../../app/src/model/Devices.h) | Kinds of devices: `builtinDevices()`, `builtinDevice()`, `builtinCategories()` (from the engine); `isInstrument`, `deviceIsInstrument`, `loadsInto`, `deviceName`, `kindName`; `newDevice`, `newRack`, `newChain`; `builtinParamInfo`, `deviceIdsOf`, `deviceIdsOfList`, `addDeviceIds` (into a set of many lists' ids), `innerDeviceIds` (what is in a rack) |
| [Vital.h](../../app/src/model/Vital.h) | A built-in Vital as the application sees it besides its parameters (see [Vital](#vital)): `isVital`, `vitalModulations`, `vitalModulationSlots`, `vitalSources`, `vitalDestinations`, `vitalSourceName`, `vitalDestinationName`, `vitalSlotParam`, `vitalWavetableNames`, `vitalLfoNames`, `vitalShapeNames`, `vitalWavetableFrames`, `vitalLfoShape`, `vitalStateValue` |
| [Routing.h](../../app/src/model/Routing.h) | The group tree (`TreeEntry`, `TrackTree`, `treeProblem`, `repairTree`) and the routing graph (`routingGraph`, `feeds`, `wouldCycle`, `inputWouldCycle`, `sidechainWouldCycle`); `isDefaultOutput` (an output that goes where the default does: its own group, or the master from outside a group) |
| [Automation.h](../../app/src/model/Automation.h) | `AutomationPoint`, `Envelope`, `AutomationView`, `kMaster`; in `sub::app::automation`: target keys, the mixer's normalized mappings, evaluation (`valueAt`, `leftValue`, `shape`), every envelope edit |
| [ParamSpec.h](../../app/src/model/ParamSpec.h) | `ParamSpec`: any automatable parameter described alike, with the engine's normalized mapping; `mixerSpecs`, `sendSpec`, `chainSpecs`, `formatValue` |
| [Edits.h](../../app/src/model/Edits.h) | `sub::app::edits`: pure clip maths for audio and MIDI clips alike: overlaps, cuts, trims, splits, ranges, tempo fitting, consolidating, reversing, stretching, slipping, fades; `sortByStart`, how clip lists are kept |
| [Notes.h](../../app/src/model/Notes.h) | `sub::app::notes`: pure note maths for the piano roll: overlaps on a key, moves, resizes, velocity, legato, ×2/÷2, quantize, humanize timing, Apply Groove (`grooved`), bends and vibratos; note names |
| [GrooveTypes.h](../../app/src/model/GrooveTypes.h), [Grooves.h](../../app/src/model/Grooves.h) | The project's grooves as values (`LibraryGroove`, `GrooveSource`, `ClipGroove`; apart so a `Clip` can hold one), and in `sub::app::grooves` what works on them: sources resolved and described, a clip's grooved notes and warp markers, Apply Groove, Extract Groove, the Groove device's state, the built-in grooves ([Grooves](#grooves)) |
| [Commands.h](../../app/src/model/Commands.h) | The `QUndoCommand` subclasses and what they share: `ValueCommand` (a target, its old and new value), `MergeableCommand` (one that merges continuous gestures: `kMergeId`, `mergeId`), `InsertCommand`/`RemoveCommand` (a track into or out of the tracks or the returns) |
| [Timebase.h](../../app/src/model/Timebase.h) | `TimeSignature`, beats and seconds, bar.beat.sixteenth formatting and parsing, dB and pan text |
| [Keys.h](../../app/src/model/Keys.h) | Musical keys (`Key`), tempo and key from file names (`parseFilename`), what a dropped clip starts with (`clipSettings`) |
| [SamplerState.h](../../app/src/model/SamplerState.h) | `sub::app::samplerState`: a Sampler's sample and its similar sounds' reference in its state, and stepping through similar sounds (`step`) |
| [DeviceState.h](../../app/src/model/DeviceState.h) | `sub::app::deviceState`: a built-in device's state besides its parameters (a sampler's sample), in the engine's text format, base64 in `Device::state` |
| [RecordedTake.h](../../app/src/model/RecordedTake.h) | `RecordedTake`, `RecordedTakeNote`: what a recording hands the editor |
| [Errors.h](../../app/src/model/Errors.h) | `EditError` (an edit the user can't make), `ProjectFileError` (a file that can't be read or written); both are `UserError`s, carrying a `QString` message for the user (one catch takes either) |
| [Paths.h](../../app/src/model/Paths.h) | Paths as the application compares them: `absoluteCleanPath`, `pathIdentity` (absolute and clean, case folded where the system ignores case: `sub::platform::kCaseSensitivePaths`), `samePath`. Decoded sources, the File Manager's files and the recent projects are keyed by it. `withSafeCharacters`: the characters Windows forbids in file names made `_` (takes' and presets' file names) |
| [Ids.h](../../app/src/model/Ids.h), [OrderedMap.h](../../app/src/model/OrderedMap.h), [Numbers.h](../../app/src/model/Numbers.h) | `newId()`, `optionalId()` (`""`: none); a map that keeps its keys in insertion order; rounding half to even, floor division that stays exact (`floorDiv`), Python's whole-number `%` (`floorMod`), fixed-point text |
| [editor/](../../app/src/editor) | `ProjectEditor` ([ProjectEditor.h](../../app/src/editor/ProjectEditor.h)), one source file per area: `EditorTracks.cpp` (tracks, returns and sends, groups, inputs, recordings), `EditorSettings.cpp` (tempo, time signature, key, loop), `EditorClips.cpp` (clips and time selections), `EditorDeviceChains.cpp` (devices in chains), `EditorRacks.cpp` (racks, their chains and macros), `EditorDeviceSettings.cpp` (a device's parameters, state, presets, switch, sidechain), `EditorAutomation.cpp` (envelopes and the lanes shown), `EditorFreezing.cpp` (freezing, unfreezing, flattening, and what frozen tracks refuse), `EditorFiles.cpp` (the files clips and devices play: one put in another's place, files found somewhere else), `EditorGrooves.cpp` (the project's grooves, clips' grooves, Apply and Extract Groove, the Groove device's source, the default groove). Its value types: `ClipRef`/`ClipRefs`, `TimeRange`, `MovedRange`, `ClipboardContent`/`CopiedTrack`/`CopiedFreeze`, `CopiedTracks`, `CopiedAutomation`, `TrackParent`/`InsertionPoint` |
| [io/Serialization.h](../../app/src/io/Serialization.h), [io/Presets.h](../../app/src/io/Presets.h) | Project and preset files, the preset library: see [serialization.md](serialization.md) |

## Key types

All of them are values: copying a `Track` copies its clips, devices and automation. Containers are `std::vector`
(the device tree is recursive), keyed maps `QMap` (sorted) unless an order shows somewhere, then `OrderedMap`
(insertion order). Ids, names and paths are `QString`; an optional field is a `std::optional`.

### Time

Timeline positions are quarter-note **beats** ([Timebase.h](../../app/src/model/Timebase.h)). `TimeSignature` gives
`beatsPerBar()` and `beatLength()` in quarter notes; `formatPosition`/`parsePosition` use Ableton's one-based
`bars.beats.sixteenths`. `dbToGain` treats -70 dB and below as silence (the faders' floor,
`automation::kMinVolumeDb`). Pan is -1..1 in the model and shows as `25L`/`C`/`30R` (±50: `formatPan`,
`parsePan`). Texts shown to the user round half to even (`formatFixed`), as they always have.

### Clips and notes

`Clip` ([Clip.h](../../app/src/model/Clip.h)) is one struct for both kinds (`kind`: `Clip::Kind::Audio` or `Midi`;
`isAudio()`, `isMidi()`); an edit makes a new clip (copy it, set what changes). Each is a *window* onto its content,
and editing moves the window's edges while the content stays put on the timeline. Its methods behave per kind, so
both kinds are edited alike.

- **Audio**: `path`, `startBeat`, and in **source seconds** `offsetSec`, `durationSec` and `sourceDurationSec`;
  clip-view settings `gainDb`, `warp`, `warpMode`, `segmentBpm`, `transpose` (semitones), `detune` (cents), `pan`.
  Unwarped audio plays at its own speed, so the clip's length in beats follows the tempo, where it is (under a tempo
  that changes, two seconds may be four beats at one place and two at another); warped (`isWarped()`: `warp` and
  `segmentBpm > 0`), the audio is taken to be at `segmentBpm`, so its length in beats is fixed. A warped clip whose
  source has a tempo that changes along it (a track frozen under a tempo map) has `sourceMap`: where the file's
  seconds fall in its beats; the clip's offset plays at its start beat and its beats follow the map's from there
  (the engine's `ClipDesc::sourceMap`, sent only when it isn't one tempo). Its warp markers (`warpMarkers`,
  `WarpMarker{sec, beat}`: moments of the file pinned to beats of its own) are none or two and more, rising in both;
  `setWarpMarkers()` keeps those that go on and makes `sourceMap` from them (a steady tempo between two, the end
  stretches' beyond) and `segmentBpm` their first stretch's tempo. `warpTempos()` gives each stretch's,
  `scaleWarp()` speeds them all up as much, `warpBeatAt()` and `warpSecondsAt()` convert a file's second to the
  clip's own beat and back (without markers, by `segmentBpm`). `sourceTempo()`,
  `beatsToSource()` and `sourceToBeats()` convert between the two, from the clip's start (`beatsToSourceFrom()`,
  `sourceToBeatsFrom()`: from another beat; a fade-out's beats count back from its end). They take a `Tempo`
  ([Timebase.h](../../app/src/model/Timebase.h)): the song's tempo map (music/TempoMap.h, [music.md](../music.md)),
  made from a BPM implicitly where one tempo is all there is; one tempo throughout converts as a single BPM always
  did, to the bit. Every clip edit (`edits::`) takes one too. `segmentBpm` 0 means "not set"
  (shown as the project tempo). A reversed clip plays a reversed copy of a file (its `path`, written by the
  bridge); `reversedFrom` is the file it was made from (reversing again goes back to it), empty for a clip that
  isn't reversed. Its fades, `fadeInSec` and `fadeOutSec`, are measured in its audio too (so they stretch with it;
  `fadeInBeats()` / `fadeOutBeats()` on the timeline), each bent by `fadeInCurve` / `fadeOutCurve` (-1..1, 0 a
  straight line, positive bulging up: `fadeGain()`, which is `automation::shape()`, as the engine's
  `automationShape()`). `fitFades()` holds them to the clip: where together they are longer, both shorten in
  proportion so they meet (a MIDI clip has none). Without a fade of its own, the engine fades an edge that cuts
  into the file for a few milliseconds against clicks, but not one at the file's own start or end, so a one-shot's
  attack plays as it is ([engine/rendering.md](../engine/rendering.md)).
- **MIDI**: `startBeat`, `durationBeats`, `offsetBeats` (the content beat at the clip's start) and `notes`, sorted by
  start then pitch. Its length is in beats, so it doesn't change with the tempo. Notes outside the window are kept
  but not played, so trimming or splitting never deletes them. `playedNotes()` gives the notes that start inside the
  window, cut at its end, in timeline beats (`PlayedNote`), as in Ableton.
- Both: `id`, `startBeat`, `name` (an audio clip's: its file's name as it was placed; a MIDI clip has none: one
  in an earlier file is dropped on load), and `muted`: deactivated (Ableton's clip activator off, 0), the clip is
  kept, edited, copied and saved as any other but isn't heard (`plays()`; `heardNotes()`: none of its notes, and
  never a deactivated note), so the
  bridge leaves it out and the song's chords don't count it; what is split, trimmed or copied from it is deactivated
  too.
- Either kind's `groove` (`std::optional<ClipGroove>`, [Grooves](#grooves)): what it plays moved in time with a
  groove where the bridge hands it to the engine (a MIDI clip's notes, an audio clip's audio through its warp
  markers); its own notes and markers stay as they are. None: as it is. Its pieces keep it.
- `Note`: `pitch` (60 = C3, Ableton's octave numbering), `start` and `length` in beats from the clip's content start,
  `velocity` 1..127, and `muted`: deactivated (0 in the piano roll), kept and edited as any other but not heard.
  `notes::withActive()` deactivates (or activates) some of a clip's notes where they are.
- A note's **bend** (MIDI 2.0's per-note pitch bend): `bend`, a list of `BendPoint`s (`time` in beats from the
  note's start, `semitones` from its pitch, `curve` -1..1 bending the segment from it to the next as an automation
  breakpoint's does), sorted by time, and `vibrato`, `Vibrato`s on top of it (`start` and `length` in beats from the
  note's start, `depth` semitones either way, `rate` cycles a second, `fade` the share of its length it swells in
  over), in time order and not overlapping. The curve starts at the note's pitch at its start, goes through the
  points, then holds the last one's value; a point at (or before) the start sets where it starts. A vibrato swells
  in and dies away over its last tenth, so it begins and ends on the curve: drawn over a bend, it follows it.
  `bent()` says whether it has either; `bendAt(beat, tempo)` is what it sounds like there (the vibratos' rates are in
  seconds, so it takes the tempo) and `curveAt(beat)` the points' curve alone. Both are the engine's own rules,
  `sub::bend` in [engine/src/NoteBend.h](../../engine/src/NoteBend.h) (header-only, no engine types), so what the
  piano roll draws is what plays. The bend's times are the note's own, so it moves, copies and transposes with the
  note; points or vibratos past its end are kept, not heard. Bends are held to ±48 semitones
  (`notes::kMaxBendSemitones`, MIDI 2.0's per-note range).
- `kWarpModes` is in the engine's `WarpMode` order: Transients, Standard, Smooth, Formants, Re-Pitch.
  `legacyWarpMode()` maps the earlier Ableton-style names (Beats, Tones, Complex, Texture, Complex Pro) to the mode
  that plays the same way; loading applies it.

### Tracks

`Track` ([Track.h](../../app/src/model/Track.h)). Its `kind` is fixed for its life (flattening replaces the track):

| kind | Where it lives | Clips | Notes |
|---|---|---|---|
| `kAudioKind` `"audio"`, `kMidiKind` `"midi"` | `Project::tracks()` | yes | `hasClips()` |
| `kGroupKind` `"group"` | `Project::tracks()` | no | holds other tracks |
| `kReturnKind` `"return"` | `Project::returns()` | no | fed by sends; named by letter (`returnLetter`: A..Z, AA...) |
| `kMasterKind` `"master"` | `Project::master()` | no | id `kMaster` (`"master"`), effects only |

Fields: `name` (as shown: its `nameTemplate` with each `#` its number, its place in `Project::tracks()` from 1;
`Project` keeps it so, see [TrackNames.h](../../app/src/model/TrackNames.h)), `nameTemplate` (what renaming changes
and the project file saves: `"# Kick"`; `TrackField::Name`; empty: `name` as it is; `nameSource()`), `color`, mixer (`volumeDb`, `pan`, `mute`, `solo`), `height` (its lane's; a new track's is
`kDefaultTrackHeight`, 96 px, a new group's `kDefaultGroupHeight`, 104), `clips` (sorted by `startBeat`),
`devices`, `automation` (target key → envelope, never empty; an `EnvelopeMap`, which keeps the order targets were
first automated in), `automationView`, audio input (`input`: none, `{c}` or `{l, r}` device channels, or
`inputTrack`: another track's id or `kMaster` for resampling; `inputTap`: where that is taken, `kPostFader`,
`kPreFader` or `kPreFx`, as a sidechain's tap), `output` (an `Output`: where its output goes; see below), `midiInput` (a `MidiInput`, or none for *No Input*;
new MIDI tracks hear every input), `monitor` (`kMonitorModes`: `off`, `in`, `auto`), `armed`, `parent` (the group it
is in), `folded`, `sends` (return id → `Send`), `frozen`, `takes` and `takesShown`.

**Take lanes** (`TakeLane`: `id`, `name`, `clips`). Recording keeps what each take played on a lane of its own below
its track (`ProjectEditor::addRecordings`: "Take 1", "Take 2"..., `nextTakeName`), as copies with ids of their own,
in the same undo step that puts the takes on the track; the track's own clips are then the *comp*. A take
recorded round the loop (`RecordedTake::wraps`: the engine's recording time) is cut into the passes it made
(`loopPasses`, [RecordedTake.h](../../app/src/model/RecordedTake.h)): a clip and a lane each, the later passes
winning on the track. A take lane's
clips don't play, aren't selected, and nothing but the take-lane edits touches them: `compTake` (what a lane holds
between two beats, its silence too, replaces what the track plays there: `edits::removeRange` then
`edits::sliceRange`, copies again), `deleteTakeLane`, `deleteTakeLanes` (`SetTakeLanesCommand`, the track's whole
list; `Project::setTakeLanes` emits `takesChanged`). `edits::compedSpans` says where the track plays what a lane
holds (`edits::sameContent`: a clip that is a window onto the same file or notes, at the same place), so a lane can
draw the parts its comp is made of. Whether they show is view state (`takesShown`, `TrackField::TakesShown`, not
undone). Copies of a track get lanes (and clips on them) with ids of their own; flattening drops them. The File
Manager lists take lanes' files too (`FileUses::takeClips`), and replacing or relinking one changes the lanes in the
same `ReplaceFilesCommand`.

The master and the returns are `Track`s too, so whatever works on a track's devices, mixer or automation works on
theirs: `Project::track(id)` finds an arrangement track, a return, or the master (`kMaster`). `hasTrack()` is true
only for arrangement tracks; `hasOwner()` for anything with devices, a mixer and automation. `allTracks()` is the
tracks, the returns, then the master; `owners()` the same ids; `senders()` the tracks then the returns.

### Devices, racks, chains

`Device` ([Device.h](../../app/src/model/Device.h)): `id`, `kind`, `enabled`, `params` (param id → plain value),
`plugin`, `state`, `sidechain`, `midiFrom`, and for a rack `chains`, `macros` and `name`.

`midiFrom` is a MIDI track whose notes the device plays instead of its own track's (`""`: its own track's), for a
device that plays notes: an instrument, or an effect with a MIDI input (a vocoder, a pitch corrector: whether it has
one is its processor's, `EngineBridge::acceptsMidi`). It is no edge of the routing graph: notes carry no audio, so
no MIDI input closes a cycle, and a device on any track (a return, the master) may take any MIDI track's but its own.
`Project::midiSources(track)` lists those, in order.

- **Built-in** (`kind` `"synth"`, `"utility"`, ...): its state is its `params`, and `state` for what isn't a
  parameter (see `DeviceState.h`). The engine follows the model. The list of built-in devices, their names,
  categories, default parameters and which are instruments come from the engine (`builtinDevices()` reads
  `sub::BuiltinRegistry`), so a new device needs nothing here. New MIDI tracks have none: the user adds the
  instrument.
- **Plug-in** (`kind` `kPluginKind`, `"plugin"`): `plugin` is a `PluginRef` (`format` `"VST3"`, `uid` the class id,
  `name`, `vendor`, `path` where it was last loaded, `instrument`). The plug-in keeps its own state; `state` holds it
  (base64 `.vstpreset`) as last stored, for saving and copying. Its `params` only record values changed from the
  host, for undo.
- **Rack** (`kind` `kRackKind`, `"rack"`): `chains` is a list of `Chain` (`id`, `name`, `devices`, and a mixer:
  `volumeDb`, `pan`, `mute`, `solo`; while any chain of a rack is soloed, only those are heard). It has 1 to
  `kMaxMacroCount` (16) macros, a new one `kDefaultMacroCount` (4): `macroNames` holds one name per macro (`""`: named
  by its number, "Macro N"; `macroName()`), so its size is how many it has (`macroCount()`). Its `params` are its
  macros' values, `"macro1"`.. (`macroParam(i)`; `macroIndex()` the other way), normalized 0..1, one per macro.
  `macros` is a list of `MacroMapping{macro, deviceId, paramId, low, high}`: the macro's 0..1 maps to the parameter's
  normalized `low`..`high` (`low > high` turns it the other way, 0.5..1 is its upper half; `target()`). Racks nest at most `kMaxRackDepth` = 8 deep (the
  engine's limit). A rack with an instrument in it is an *Instrument Rack* (`deviceIsInstrument`), otherwise an
  *Audio Effect Rack*. `name` is the preset it was saved as or loaded from (none: named by its kind; `deviceName`).
- **Drum Rack** (a rack with `drum` set: `isDrumRack()`; asked for by the kind `kDrumRackKind`, `"drum_rack"`, which
  no device has: `newDevice` makes `newDrumRack()`, `isDeviceKind` and `isInstrument` know it): an instrument even
  while empty, named *Drum Rack*, its presets loading only into Drum Racks. Its chains are **pads**: each has its key
  (`Chain::receiveNote`, 0..127, one pad a key; -1 for every other rack's chains), whose notes alone its devices hear,
  as C3 (`kDrumPadNote`), and `locked` (similar sounds swapped in leave its sample be). Pads are kept in key order as
  they are added (`newDrumPad`, named after their key, `drumNoteName()`, until a sample names them); `drumPad(rack,
  key)` finds one, `padSampler(pad)` the Sampler playing its sample (its first).
- **Modulator** (a built-in device whose engine category is `Modulator`: `isModulator(kind)`, the `"lfo"` and the
  `"envelope"`): `modulations` is a list of `ModulationMapping{deviceId, paramId, low, high, invert, bipolar}`: the
  parameter of another device on its track (anywhere: in racks too) that it moves, over its normalized `low`..`high`
  (`low > high` the other way), upside down with `invert` (`target()`). Its *amount*, as a modulated knob's handle
  drags it (`amount()`, `setAmount(base, amount)`): unipolar, `high - low` up from `low`; `bipolar`, half of that
  either side of the middle (only the handle reads `bipolar`: the engine plays `low`..`high` either way). Set from
  the handle, the ends may reach past 0..1 (to -1..2), so the amount survives a range cut off at the parameter's
  ends; what plays is held to 0..1 (`target()`, `playedLow()`, `playedHigh()`). A parameter follows one modulator at most; while it
  does, its own value and its automation wait (the engine plays the modulation instead). The engine plays it: see
  [engine/automation.md](../engine/automation.md#modulation).

Device ids are unique in the whole project, so a device is found wherever it sits: `Project::device(track, id)` and
`findDevice()` search racks too, `Project::deviceOwner(id)` finds its track. The tree helpers:

- `iterDevices(devices)`: every device depth first (each, then what is in its chains).
- `iterChains(devices)`: every (rack, chain) depth first.
- `devicePath` / `deviceAt`: a device's place as (index, chain, index, chain, ...).
- `findChain(devices, chain)`: a rack chain anywhere in a chain, and its rack (`Project::chain` and `chainRack` are
  it on a track's devices); `chainIndex(rack, chain)`: where one of a rack's own chains is.
- `chainDevices(devices, chain)`: the vector of a chain (none = the track's own), which edits change in place on a
  copy of the track's devices.
- `containerOf`: the chain a device is in (none = the track's own).
- `rackDepth(devices, chain)`: how many racks a chain is in; `rackHeight(device)`: how deep racks nest inside a
  device. `rackDepth + rackHeight ≤ kMaxRackDepth` is checked before any insert or move.
- `renamedModulations(mappings, renamed)`: modulation mappings to devices given new ids, renamed (the others
  dropped): a duplicated track's modulators, and devices pasted together, keep moving the copies.
- `pruneModulations(devices, isModulator)`: drops the modulation mappings a track's devices can't have (to a device
  not among them, to the modulator itself, of a device that isn't a modulator, a parameter mapped already: the
  first stays); the editor runs it on every change of a track's devices, and loading on every track.
- `refreshIds(device)`: new ids for a device, everything in it and its chains, in place; macro (and modulation) mappings follow their
  devices and mappings to devices not in it go. Used for presets, pasted devices, duplicated chains and duplicated
  tracks.

### Freezing

A track, group or return can be frozen: `Track::frozen` is a `Freeze` (its frozen audio's `path`, `durationSec`, and
the `tempo` it was rendered at; `Freeze::clip()` is the warped clip that plays all of it from beat 0). Rendered under a
tempo that changes along the song, it keeps that tempo map (`Freeze::map`), and its clip and segments follow it
(`Clip::sourceMap`): they keep the beats the render was made on whatever the song's tempo becomes, as any warped
clip does.
`Project::setFrozen()` emits `freezeChanged`; `updateTrack()` can't set it (there is no `TrackField` for it).

What of the render plays are its **segments**: `Freeze::segments`, audio clips into the render file (warped from its
`tempo`, as `clip()` is; `Freeze::segment(id, start, offset, length)` makes one). None (as frozen, and as projects saved
before segments load) means all of it, from beat 0; `Freeze::playing(trackId)` is what plays either way (the bridge
plays it, flattening turns it into clips). Edits of a time selection over a frozen track change them as they change
its clips (below), so the arrangement and what plays stay in step. `Project::setFrozenSegments()` sets them (sorted)
and emits `clipsChanged`: what the track plays changed, not whether it is frozen. Unfreezing drops the freeze, segments
and all, and leaves the clips as edited.

- `Project::frozenBy(track)`: the frozen track holding a track's audio (itself, or the outermost frozen group it is
  in); `isFrozen(track)`: it has one.
- `freezeProblem(track)`: why it can't be frozen (the master, frozen already, in a frozen group, or a sidechain taps
  its signal Pre FX or after one of its devices). `flattenProblem(track)`: only frozen audio and MIDI tracks flatten.
- The editor's `freezeTracks(OrderedMap<id, Freeze>)` (one step; disarms them; a track in a group frozen with it is
  left out), `unfreezeTracks()` and `flattenTracks()` (a `ReplaceTrackCommand` per track: an audio track of the same
  id playing `Freeze::playing()` as clips named after it, no devices, no device automation). The render itself is
  the bridge's
  (`renderFreeze()`, or `startFreeze()` and `finishFreezes()` in the background: several tracks in one render); the session does both, its progress
  shown ([session.md](session.md#renders-in-the-background)).
- What frozen audio holds can't change. `ProjectEditor::push()` refuses (and says why on `refused`; the checks are in
  [EditorFreezing.cpp](../../app/src/editor/EditorFreezing.cpp)) commands changing a frozen track's (or a track in a
  frozen group's) clips, devices or device automation, and the mixer automation of a track in a frozen group
  (`laneFrozen()`); a frozen track's own mixer and every send stay live, and a sidechain may still go when its source
  does. Rearranging tracks, deleting, grouping and ungrouping refuse moving tracks into or out of a frozen group (a
  frozen group being ungrouped holds nothing any more); new tracks and duplicates meant for a frozen group go after it;
  `armTracks()` leaves frozen tracks unarmed.
- **Except time selections, which take the frozen audio along.** `deleteRange`, `moveRange` (and Ctrl-drag copies),
  `duplicateRange`, `cutRange` and `paste` over a frozen track (or what is in a frozen group: the group holds the
  render) do to the segments of each frozen render they touch (`frozenRenders()`: frozen tracks among them and frozen
  groups they are in) what they do to the clips, and move the automation baked into it (its devices', and the mixer's
  of what is in a frozen group) with it. One `SetClipsCommand` carries the clips and the segments (`frozenBefore`,
  `frozenAfter`), so it is one undo step restoring both; `frozenProblem()` lets a clip change on a frozen track through
  only if it carries all the frozen renders holding it (`carriesFrozen()`), and `commitMoved()` lets automation baked
  into frozen audio change only with that audio. The rules, kept simple so clips and audio can't drift apart:
  - On a frozen track a stretch that is moved, copied, duplicated or pasted **replaces everything where it lands**,
    empty parts too, as its audio does (on other tracks only the clips it brings replace what they land on).
  - A stretch of only some of a frozen group is refused (`frozenAreaProblem()`: *select the whole group*): its audio is
    one render of all of it. A selection over the group's row takes in all of it.
  - Clips don't move between a frozen track (or one in a frozen group) and another track: a move with a track delta
    is refused; in time only, it moves.
  - `copyRange` copies the frozen audio there of each frozen render the selection takes in whole
    (`ClipboardContent::frozen`, `CopiedFreeze{trackId, path, segments}`; a frozen track with audio there and no
    clips is a row of its own, to paste back onto). Pasted onto tracks that aren't frozen, the clips go as they are.
    Into a frozen track (or a frozen group's tracks) only content carrying its frozen audio, the same render (`path`),
    pastes, onto the tracks it came from; anything else is refused (*only what was copied from it can be pasted into
    it*).
  - Everything else that changes a frozen track's clips (trimming, splitting, consolidating, reversing, clip settings,
    `moveClips`, adding clips) stays refused, as do its devices.

### Groups

Groups hold other tracks. `Project::tracks()` stays **flat**; the hierarchy is each track's `parent`. The invariant
(`treeProblem`, [Routing.h](../../app/src/model/Routing.h)) is that a track's parent is a group listed before it, and
everything between a group and its last descendant is a descendant of it: a group's tracks follow it, together, and
no group is in itself. `repairTree` takes tracks out of groups they can't be in (an edited file). Queries on
`Project`: `subtreeEnd`, `descendants`, `withContents(ids)` (those tracks and what is in the groups among them, in
order), `children`, `ancestors`, `isDescendant`, `depth`, `isHidden` (a group it is in is folded), `parentAt(index)`
(a track inserted there goes into the group of the track it goes before), `tree()` (every track's id and parent: a
`TrackTree`). The engine sees only where each track's output goes.

### Outputs, returns, sends, inputs, sidechains: the routing graph

`Output{to, id}`: where a track's (group's, return's) output goes, Ableton's *Audio To*. `Group` (the default):
into its group, or the master outside one; `Master`: the master, past its group; `Track` (`id`: an audio
track's): into that track's input (*Track In*: the bridge has the engine take what goes into an audio track as its
input, heard while it monitors); `Sidechain` (`id`: a device's, on a track, a return or the master): into that
device's sidechain input, wherever the device is (it moves with it); `None`: nowhere (*Sends Only*).
`outputTarget(track)` is the track it goes into (its group, a track, a device's track), none for the master or
nowhere.

`Send{levelDb, preFader}`: a track's (group's, return's) send to a return, after its fader and pan, or before it.
Silent at `kMinVolumeDb` (the default for a new send). A muted track sends nothing either way. `Track::sends` is
replaced whole, never changed in place.

`Sidechain{trackId, tap, chain}`: what a device's aux input hears. `tap` is `kPostFader` (`"post"`, Ableton's *Post
Mixer*), `kPreFader` (`"pre"`, *Post FX*), `kPreFx` (`"pre-fx"`: before all the source's devices; on a MIDI track,
after its instrument), or a device id (after that device; before the fader while that device isn't on the source).
`tapDevice()` is that id, or none. `chain`, when set, is a rack's chain on the source (a Drum Rack's pad) whose own
tap it is: `kPostFader` after the pad's fader (its pan, mute and solo among the rack's chains), `kPreFader` after its
devices, `kPreFx` before them (after its instrument, if it starts with one); while that chain isn't on the source,
before the source's fader. Never with a device id as `tap` (`setDeviceSidechain` refuses it, and a chain that isn't
the source's). A source copied with the device (`insertCopies`) gives the copy its copied device or chain.

`routingGraph(tracks, returns)` maps each track and return to what its signal goes into: where its output goes
(its group by default), the returns it sends to, the tracks taking their input from it, and the tracks whose
devices take it as their sidechain. The master isn't in it (everything reaches it). `feeds(graph, a, b)` is
reachability. The cycle checks:

- `outputWouldCycle(track, output)`: its output would go into itself (a device on itself) or a track it feeds.
- `wouldCycle(track, return)`: a send would close a cycle (the return is the track or feeds it).
- `inputWouldCycle(track, source)`: never for `kMaster` (resampling; a track recording it never plays it back into
  it, since it can't monitor it).
- `sidechainWouldCycle(track, source)`: always for `kMaster` as source; never for a device on the master.

`Project` wraps them and adds the menus' lists: `sendTargets`, `inputSources`, `sidechainSources`, `inputName`
(`"Resampling"` for the master), `outputTarget`, `outputWouldCycle`.

The editor keeps outputs valid ([EditorTracks.cpp](../../app/src/editor/EditorTracks.cpp)): `setTrackOutput`
refuses one into a track that isn't an audio track, a device that isn't there, or a cycle, and stores *Main* outside
a group, or its own group, as the default (`isDefaultOutput`; loading does the same); a track going into another group goes into it, as in Ableton (`arrange`:
the explicit outputs come back one by one, those that would close a cycle going into their groups); deleting the
track or device an output goes into (`dropOutputs`, `setDevices`), flattening a track, or moving a device where an
output into it would close a cycle sends those outputs into their groups, in the same undo step; copies of tracks
go into the copies of what they went into (`insertCopies`).

### Automation

See [Automation.h](../../app/src/model/Automation.h) and, for the engine side,
[engine/automation.md](../engine/automation.md).

- Every target is automated in **normalized** values (0..1). An envelope is a vector of
  `AutomationPoint{beat, value, curve}`; it belongs to an owner (a track id or `kMaster`) and is keyed by target:
  - `mixer:volume`, `mixer:pan`, `mixer:on` (`automation::kMixerVolume`, `kMixerPan`, `kMixerOn`: a track's
    activator; the master has none)
  - `send:<return id>` (`sendKey`): the level of the owner's send to a return
  - `device:<device id>:<param id>` (`deviceKey`)
  - `device:<device id>:device:on` (`deviceOnKey`, `kDeviceOn`): a device's (or a rack's) on/off
  - `device:<rack id>:chain:<chain id>:volume` / `:pan` (`chainKey`): a rack chain's fader, a target of its rack
  - `song:tempo` (`kSongTempo`), on the master only: the Song Tempo lane. It is never stored as an envelope:
    `Project::envelope(kMaster, kSongTempo)` is `tempoEnvelope()`, a straight point for each of the tempo map's, its
    BPM mapped linearly over `tempoRange()` (60 to 200 BPM, widened, never narrowed, to fit every tempo the map
    has had, so nothing moves under a drag); `tempoOfEnvelope()` turns a lane's points back into a map (BPMs to
    0.01, curves kept: the map's ramps bend as an envelope's segments do). The editor's envelope edits on it change the tempo map (`setTempoMap`: with the clip
    trims, one undo step per drag); a range moved over it and other lanes is one `SetEnvelopesCommand` carrying
    the tempo change. Its spec is `tempoSpec(low, high)` (group *Song*), the master's second `paramGroups` group.
- `parseKey` splits a key (a `Target`); `keyDevice`, `keySend`, `keyChain`, `keyChainControl`, `isMixerKey`,
  `isSwitchKey` (an activator or an on/off), `isKey` classify it.
- Switches are 0 (off) or 1 (on): `switchSpec` describes them (*Track Activator* in `mixerSpecs`, *Device On*:
  `deviceOnSpec`, every device's first parameter).
- Before the first breakpoint an envelope holds the first one's value, after the last the last one's; two points at
  one beat make a step (from that beat on, the later one counts: `valueAt` vs `leftValue`).
- `curve` (-1..1) bends the segment starting at that point; segments are exponential with `kCurvature` = 6 (the
  engine's `kAutomationCurvature`), so `splitAt` can split one into two that follow it exactly. `shape()` must
  match the engine.
- Mixer mappings: volume is a cube-law fader, 1 = `kMaxVolumeDb` (+6 dB), 0 dB at about 0.79, 0 = silence
  (`volumeToNormalized`, `normalizedToVolume`); pan maps -1..1 to 0..1. Sends and chain volumes map as volume.
- Edits (each returns a new envelope): `addPoint`, `movePointsMapped` (one point stays between its neighbours;
  several override what they land on), `deletePoints`, `setCurve`, `removeRange`, `copyRange`, `pasteRange`,
  `moveRange`, `dropRedundant`, `shift`, `normalize` (sort, clamp).
- `AutomationView{shown, key, lanes}`: what the arrangement shows of an owner's automation (view state). A lane with
  nothing chosen shows the first target automated (`ProjectEditor::defaultAutomationKey`: the envelope map's first),
  else the volume.

### Parameters

`ParamSpec` ([ParamSpec.h](../../app/src/model/ParamSpec.h)) describes a parameter whatever it belongs to: `key` (its
automation key), `name`, `group`, range (`minimum`, `maximum`), `defaultValue`, `unit`, `scale` (`Linear`, `Log`, or
`Fader` for a mixer volume), `steps` (discrete), `labels` and an optional `text` formatter. `toNormalized` /
`fromNormalized` are the same mapping as the engine's `ParamInfo` (evenly, in log(value), or in whole steps as VST3
maps stepped parameters), and [test_automation_model.cpp](../../tests/app/test_automation_model.cpp) holds the two
together. `ParamSpec::fromInfo()` makes one from an engine `ParamInfo`; `mixerSpecs`, `sendSpec` and `chainSpecs`
describe the mixer, sends and rack chain faders. The bridge hands them to the UI (`EngineBridge::paramGroups`).

### Keys and file names

`Key{tonic, minor}` ([Keys.h](../../app/src/model/Keys.h)); `Key::name()` (`"F#m"`, `"Bb"`) is what projects save and
`keyFromName` reads. `transposeTo(source, target)` is the smallest shift (a minor key and its relative major count as
the same; at a tritone it goes down), 0 if either is unknown. `parseFilename` finds a tempo (`128bpm`, `bpm128`, or a
lone number in 50..250) and a key (spelled `A minor`, `Ebmaj`; short `Am`, `F#m`; a bare capital only next to a tempo
or after "key", never as the first word). `clipSettings(name, duration, tempo, projectKey)` gives a new clip `warp`
and `segmentBpm` when the name has a tempo or the file is at least `kAutoWarpMinSec` (6 s) long (then at the project
tempo), and a `transpose` to the project key. Used by `ProjectEditor::addClips`. User-facing behaviour:
[guide/audio-clips.md](../guide/audio-clips.md).

### Built-in device state

[DeviceState.h](../../app/src/model/DeviceState.h) encodes a built-in device's non-parameter values as the engine
does (`BuiltinProcessor::encodeState`): `name=value` lines, a backslash escaping a backslash or a newline.
`deviceState::toModel`/`fromModel` wrap it in base64 for `Device::state`. The Sampler's are `"sample"`, its file path
(see [ui/device-view.md](../ui/device-view.md)), and the reference its similar sounds come from
([SamplerState.h](../../app/src/model/SamplerState.h): `"similar_reference"`, and `"similar_reference_saved"` when the
user saved it; `samplerState::loaded()`, `stepped()`, `referenceSet()` and `step()` are the rules, see
[intelligence.md](../intelligence.md#swapping-in-similar-samples)); a Vital's are its patch's ([Vital](#vital)); the
Groove device's are the groove it plays ([Grooves](#grooves)).

### Vital

A built-in Vital (`kind` `"vital"`, [engine/devices.md](../engine/devices.md#vital-builtinvital-instrument)) keeps
Vital's parameters as its `params` (Vital's ids) and the rest of its patch as its state values (`modulations`,
`wavetables`, `lfos`, `sample`, `tuning`: JSON, as a `.vital` file has them; `preset_name`, `author`, `comments`,
`preset_style`, `macro1`..`macro4`), so it saves, copies, undoes and makes presets as any built-in device does.
[Vital.h](../../app/src/model/Vital.h) reads them (through the engine's `VitalPatch.h`, which has no Vital headers):
the modulations slot by slot (`vitalModulationSlots`: all 64, "" in a free one; `vitalModulations`: those in use),
every source and destination Vital's engine has and their names, a slot's parameters (`vitalSlotParam(slot,
"amount")` is `modulation_<slot + 1>_amount`), the wavetables' and LFO shapes' names, and what the editor draws:
`vitalWavetableFrames(device, oscillator, points)` (the wavetable's frames as Vital's engine renders them, each
`points` values) and `vitalLfoShape(device, lfo, samples)` (an LFO's values across a cycle and its points). The editor's edits
([EditorVital.cpp](../../app/src/editor/EditorVital.cpp)), one undo step each:

- `setVitalModulation(track, device, slot, source, destination)` [Q]: a slot's source and destination (both "": the
  slot cleared, its amount, Bipolar, Power, Stereo and Bypass back to their defaults, as Vital leaves a slot it
  frees); a new source sets Bipolar as Vital does for it (`sourceBipolar`: LFOs, random LFOs, the pitch wheel...).
  Refused for a source or destination Vital's engine hasn't, past 64 slots, or another kind of device.
- `addVitalModulation(track, device, source, destination)` [Q]: in the first free slot, or the slot already joining
  them; -1 if none is free.
- `setVitalWavetable(track, device, oscillator, wavetable)` (a wavetable as the state keeps one:
  [io/VitalPresets.h](../../app/src/io/VitalPresets.h) `vitalWavetableFromFile` makes one from a `.vitaltable` or audio
  file), `setVitalWavetableShape` [Q], `setVitalLfoShape` [Q] (a basic shape: `vitalShapeNames()`).

Vital's own presets are [io/VitalPresets.h](../../app/src/io/VitalPresets.h)'s: see
[serialization.md](serialization.md#vitals-presets).

### Grooves

A groove (`groove::Groove`: steps over a cycle of whole four-beat bars, counted from the song's start, with lanes for
some keys) and where a note plays in it (`groove::place()`) are music/'s
([music/Groove.h](../../music/src/music/Groove.h), [music.md](../music.md)); the design is [groove.md](../groove.md).
The project's side is [GrooveTypes.h](../../app/src/model/GrooveTypes.h) (the values) and
[Grooves.h](../../app/src/model/Grooves.h) (`sub::app::grooves`: pure functions, tested on their own):

- `LibraryGroove{id, name, groove, notes}`: a groove of the project's library (`Project::grooves()`), and the notes
  it was made from (an audio clip's hits: key 60), folded into its cycle's beats (`folded()`), which the groove editor
  draws under it (none for one made otherwise).
- `GrooveSource`: what plays a groove. `Kind::Swing` (`swing` percent, 50..75, on steps of `stepBeats`:
  `groove::swing()`), `Project` (the project's default groove), `Library` (`libraryId`: edit that groove, and
  everything playing it follows) or `Own` (`own`: what a library groove removed while it is used leaves its users, so
  nothing changes sound). `kindName()` / `kindFromName()` ("swing", "project", "library", "own": as files save them
  and QML and agents give them), `swingSource()`, `librarySource()`. `resolve(source, library, default)` is the groove
  it plays, `groove::normalized()`; none for Project without a default, or a library groove that isn't there (the
  default is never Project itself). `describe()` is how it shows ("Swing 66.7 % (1/16)", "Project", the groove's name,
  "(missing groove)", "(own groove)"); `detached(source, id, groove)` turns a source playing library groove `id` into
  its own copy and leaves the others as they are.
- `ClipGroove{source, amounts}`: a clip's groove and how much of it plays (`groove::Amounts`: Timing, Velocity,
  Random, Quantize and Random's seed; Length stays 0 for clips).
- **A MIDI clip's groove** moves its notes where the bridge flattens them: `groovedNotes(clip, groove, amounts,
  tempo)` is `heardNotes()`, each placed by `groove::place()` on the timeline (milliseconds where the tempo map puts
  them), its length and velocity as the groove has them, and a note running into the next on its key ending where
  that one starts.
- **An audio clip's groove** moves its audio through its warp markers: `groovedMarkers(clip, transientSeconds, groove,
  amounts, tempo)` is a marker at each of its own markers, its window's ends and each transient in its window, each on
  the beat `place()` puts it on (Quantize first pulling it to the base grid); the clip's start stays where it is. A
  point that would take a stretch's tempo out of `kMinSegmentBpm`..`kMaxSegmentBpm` is left out; a clip that isn't
  warped gets none. `takesGroove()`: a MIDI clip, or an audio clip but a frozen track's (a source map without
  markers); `warpedForGroove(clip, tempo)` warps an unwarped audio clip for it as warping it by hand does (at the
  tempo at its start, if it had no segment BPM).
- **Apply Groove**: `applied(clip, groove, transientSeconds, tempo)` is the clip as its groove plays it, written into
  it (a MIDI clip's notes through `notes::grooved`, an audio clip's warp markers), its groove cleared; without a
  groove resolved, only cleared.
- **Extract Groove**: `fromMidiClip(clip, stepBeats, bars, perKeyLanes, &notes)` (its heard notes' offsets from the
  grid, their velocities from the clip's average, on the timeline's beats: `groove::fromNotes`; a lane a key with
  `perKeyLanes`) and `fromAudioClip(clip, transients, tempo, stepBeats, bars, &notes)` (its transients in its window
  and their strengths, where they play on the timeline: through its warp, or unwarped at `tempo` from its start;
  `groove::fromOnsets`), each handing over what it was made from, folded. Both count the cycle from the song's start,
  as a groove plays.
- **The Groove device** (`kDeviceKind`, the engine's `builtin:groove`) keeps its source in its state
  ([DeviceState.h](../../app/src/model/DeviceState.h)): `source` (`swing`, `project` or `groove`), `groove_id` (the
  library groove it plays), `groove` (what it plays as text, `groove::toText()`: a library groove's copy, which the
  editor keeps in step with the library, or its own; the engine plays it and knows nothing of the library) and
  `seed` (Random's). Swing's percent and step are its parameters, not its state. `deviceSource()`,
  `deviceState(state, source, library, seedFrom)` (the rest of the state kept; a seed made from `seedFrom` if it has
  none yet), `deviceSeed()`.
- **The built-in grooves** (`builtins()`, `builtin(id)`; ids `builtin:...`): MPC swing at 50 to 75 % on 16ths and
  8ths, triplet and lazy shuffles, and played drum grooves (Laid Back Backbeat, Boom Bap 16, Funk 16 Ghosts, Drunk 16,
  Pushed Hats 16) with lanes for the kick (35, 36), the snare (37..40) or the hats (42, 44, 46). A project plays one
  once it is added to its library (`ProjectEditor::addBuiltinGroove`: a copy).

`notes::grooved(notes, groove, amounts, tempo)` ([Notes.h](../../app/src/model/Notes.h)) is Apply Groove on notes
(the piano roll's, the MCP server's): each note placed on the timeline, untangled. A MIDI clip's groove plays notes
where it would write them, and so does the Groove device while it may move them early.

## The Project object and its signals

`Project` ([Project.h](../../app/src/model/Project.h)) holds the tempo (`tempoMap()`, a `Tempo`: one throughout, or
changing along the song; `tempo()` is the one at its start), the time signature (`meter()`, a `MeterMap` of changes,
each starting a bar; `timeSignature()` the first), `key`, the loop
(`loopEnabled`, `loopStart`, `loopEnd`, beats; `punchIn`, `punchOut`: recording kept only inside the loop brace), `automationLocked`, `master`, `tracks`, `returns`, `foldedDevices`,
`shownChainLists` (the racks whose chain list shows: hidden by default), `hiddenRackDevices` (the racks whose
chain's devices don't show beside them: shown by default) (ids, view state), the project's grooves (`grooves()`, its
library in order: `LibraryGroove`s; `findGroove(id)`) and its default groove (`defaultGroove()`: a `GrooveSource`, never
`Project`; none: straight), and `path`. QML reads the settings as properties (`tempo`, `loopEnabled`, `loopStart`, `loopEnd`,
`automationLocked`, `timeSignatureText`, `keyName`, `path`).

| Signal | When |
|---|---|
| `trackInserted(id, index)`, `trackRemoved(id, index)` | an arrangement track came or went |
| `returnInserted(id, index)`, `returnRemoved(id, index)` | a return came or went |
| `trackChanged(id)` | name (renamed; renumbered by a track coming, going or moving above it, after that track's signal; renamed by what it holds, after `clipsChanged` or `devicesChanged`), colour, mixer, sends, input, monitoring, arming, height, folding (`kMaster`: the master's mixer) |
| `tracksArranged()` | the order or groups changed (not which tracks there are) |
| `clipsChanged(track id)` | a track's clips were replaced, or what of its frozen audio plays |
| `devicesChanged(track id)` | devices added, removed, moved, toggled (in racks too), a sidechain, a MIDI input, macros or a rack's name changed |
| `chainChanged(track id, chain id)` | a rack chain's name or mixer |
| `deviceParamChanged(track id, device id, param id)` | one parameter |
| `deviceStateChanged(track id, device id)` | a device's state was set (a preset, a sample) |
| `devicesFolded(track id)` | devices on it folded or unfolded |
| `rackViewChanged(track id)` | a rack on it showed or hid its chain list, or its chain's devices |
| `freezeChanged(track id)` | it was frozen or unfrozen |
| `settingsChanged()` | tempo, time signature, key, loop, automation lock |
| `groovesChanged()` | the groove library: a groove added, removed, renamed or edited (after `reset` too) |
| `defaultGrooveChanged()` | the default groove (after `reset` too) |
| `automationChanged(owner, key)` | an envelope was set or removed |
| `automationViewChanged(owner)` | what its automation shows |
| `reset()` | everything replaced (new, open) |
| `pathChanged()` | the file it is saved to changed |

Mutators, called only from commands (or directly for view state): `insertTrack`, `removeTrack`, `insertReturn`,
`removeReturn`, `arrangeTracks(tree)` (validates with `treeProblem` and throws `EditError`, changing nothing),
`updateTrack(id, field, value)` and `updateTrack(id, values)` (the settings a `TrackField` names; one `trackChanged`),
`replaceTrack` (a flattened track), `setFrozen`, `setFrozenSegments` (sorts; `clipsChanged`), `setClips` (sorts),
`setDevices`, `setChains` (several tracks' devices at once, all changed before any signal: a device moving between
tracks), `updateChain`, `setDeviceMacros`,
`setDeviceName`, `setDeviceParam(s)`, `setDeviceEnabled`, `setDeviceSidechain`, `setDeviceState`,
`setDevicesFolded`, `addFoldedDevices`, `setChainListShown`, `setRackDevicesShown`, `updateSettings`, `setGrooves`
(the whole library), `setDefaultGroove` (a `Project` source is none), `setEnvelope` (an empty envelope removes the target's
automation), `setAutomationView`, `replaceContents` (loading; emits `reset`), `clear`, `setPath`.
`storePluginState()` keeps a plug-in's current state (and where it was found) in the model without a signal or an
undo step: it isn't an edit, only what saving or copying the device needs (the bridge's `storePluginStates()`).

### Values held, references handed out

The project holds its tracks **by value**, in vectors. Its queries hand out references (`const Track&`,
`const Device&`) and pointers (`findTrack`, `findDevice`, `findClip`) into those vectors, valid **until the project
next changes**: a track inserted or removed moves the others, and any command replaces what it changes. Code that
keeps something across a change keeps its id and looks it up again. This matters most in slots reacting to the
project's signals (the id may be gone by then) and around edits that run while a reference is held.

Asking for a track, device, chain or clip that isn't there is a programming error: `track()`, `device()`, `chain()`,
`clip()` and `trackIndex()` throw `std::out_of_range`. Where an id may be gone, use `findTrack`, `findDevice`,
`findClip` (null) or `hasTrack`/`hasOwner`/`hasDevice`.

## The undo design

The session owns one `QUndoStack` and one `ProjectEditor(project, undoStack)`. The editor pushes commands from
[Commands.h](../../app/src/model/Commands.h):

| Command | Changes |
|---|---|
| `SetClipsCommand` | clip lists of one or more tracks (before and after by track id), and for a time selection over frozen tracks their frozen audio's segments |
| `InsertTrackCommand`, `RemoveTrackCommand` | an arrangement track |
| `ReplaceTrackCommand` | a track replaced by another of the same id (flattening) |
| `SetFreezeCommand` | a track frozen (its `Freeze`) or unfrozen |
| `InsertReturnCommand`, `RemoveReturnCommand` | a return track |
| `ArrangeTracksCommand` | order and groups (`TrackTree` before and after) |
| `UpdateTrackCommand` | one track setting (also `Sends`, `InputTrack`, `MidiInput`, `Monitor`) |
| `UpdateTrackFieldsCommand` | several settings of one track as one change (input channels and input track) |
| `UpdateTracksCommand` | one mixer setting on several tracks |
| `SetTempoCommand` | tempo (one, or a tempo map) plus the clip trims that keep unwarped clips from overlapping (`TempoState`) |
| `UpdateSettingsCommand` | time signature, key, loop |
| `SetDevicesCommand` | a track's whole device tree |
| `SetChainsCommand` | several tracks' devices at once (a device moving between tracks stays the same device) |
| `SetDeviceParamCommand`, `SetDeviceParamsCommand` | one parameter; several at once (a macro and what it moves) |
| `UpdateChainCommand`, `SetMacrosCommand`, `SetDeviceNameCommand` | a rack chain's name or mixer; a rack's macro mappings, with the values of the parameters a range change moves (merging per gesture); a rack's name |
| `SetModulationsCommand` | a modulator's mappings (`Device::modulations`): a range or an amount dragged merges per gesture; Invert, Bipolar, unmapping one or all |
| `SetDeviceStateCommand` | a device's state (a plug-in preset, a sampler's sample) |
| `ReplaceFilesCommand` | other files in some files' place: the clip lists of the tracks playing them and the states of the built-in devices naming them (`DeviceStates`), together; merging per hot swap (the same tracks and devices); `relink` for files only found somewhere else (frozen tracks take those) |
| `SetDeviceEnabledCommand`, `SetDeviceSidechainCommand`, `SetDeviceMidiFromCommand` | on/off; sidechain; MIDI input (`Device::midiFrom`, "Change MIDI Input" / "Remove MIDI Input") |
| `SetEnvelopeCommand`, `SetEnvelopesCommand` | one envelope; several (a range moved on several lanes; with the Song Tempo lane among them, its tempo change as `SetTempoCommand`'s, merged alike) |
| `SetGroovesCommand` | the groove library and what plays its grooves, together (`GroovesState`): the library, the states of the Groove devices playing a groove (their copies kept in step), and, a groove removed, the clip lists and the default groove left with their own copies; target the groove edited, so the groove editor's drags merge per gesture |
| `SetDefaultGrooveCommand` | the project's default groove (merging per gesture: the transport's Swing) |

How it fits together:

- **Snapshots, not deltas.** Commands store whole before/after values (a clip list, a device tree, an envelope, a
  track) and put one or the other in place. They are values, so the project gets copies and the stack's are never
  the live model's. Most hold one such value: they are `ValueCommand`s (what they change, its `oldValue()` and
  `newValue()`), and each has only its `redo()` and `undo()`. `RemoveTrackCommand` keeps the track it removed and puts
  it back; it and `InsertTrackCommand` are the same classes as the returns' (`RemoveCommand`, `InsertCommand`) over
  the other list's `Project` methods.
- **Compound steps** are undo macros (`QUndoStack::beginMacro`/`endMacro`, opened by a scoped `Macro` in
  [EditorSupport.h](../../app/src/editor/EditorSupport.h), so it closes even when an edit throws): deleting tracks
  (inputs and sidechains from them dropped, the tracks, the sends into deleted returns and their automation),
  grouping, ungrouping, adding clips (with a new track), moving clips with their automation, deleting devices with
  their automation, recording.
- **Gestures merge.** A merge key is a `QString`; empty never merges. A command given one returns id `kMergeId`
  (0x6E1, `mergeId()`), and `mergeWith` accepts the next command of the same class with the same merge key and the same target,
  keeping the first's old value and the latest's new one (`MergeableCommand`). `SetClipsCommand` needs the same set
  of tracks; `ReplaceFilesCommand` the same tracks and devices, and turns obsolete (leaving the stack) when it is back
  where it began. The UI makes one key per gesture (`QUuid::createUuid().toString()`; the shared `Knob` and `ValueBox`
  controls hand one out with each `moved(value, gestureKey)`), so a fader drag, a knob drag or a breakpoint drag is
  one undo step. A plug-in editor's knob drag is one too: the session merges its edits by
  `"plugin edit|<device id>|<param id>|<gesture>"`.
- **Baselines for drags that trim.** `setTempo`, `updateClips` (segment BPM, warp) and `replaceFile` (a hot swap)
  trim clips that would run into the next one (`edits::fitToTempo`). While a drag (a hot swap) merges, they fit from
  the clips as they were when it began (the previous merged command's old value), so dragging up and back down (or
  trying a long sample, then a short one) doesn't leave clips trimmed.
- **Not undone** (changed directly on the project; saved with it): track `height`, `folded`, folded devices, `armed`,
  track and chain `solo` (`soloTracks`, `setChainParam(..., Solo, ...)`), the automation view, and
  `automationLocked`. These push nothing, so they don't mark the project as changed either.
- **Clean state.** The session uses `QUndoStack::isClean()` for the title's `*` and its `clean` property (the UI asks
  about unsaved changes before New, Open and Quit), `setClean()` after saving, and `resetClean()` when a plug-in reports a change no parameter shows
  (`EngineBridge::pluginStateDirty`).

### ProjectEditor

[ProjectEditor.h](../../app/src/editor/ProjectEditor.h) is the API the UI calls: one class, its operations grouped by
area in `Editor*.cpp`, all pushing their commands through `push()`. A new operation goes in the file of what it
edits. Its conventions:

- Ids are `QString`s; an empty one means none (no chain: a track's own; no group; no track: a new one). An index below
  0 means last.
- Operations that make something return its id (`""` if nothing was made). Results are copies, never references into
  the project.
- Continuous gestures pass a merge key.

Its signals: `pluginAdded(track id, device id)` when the user adds a plug-in (the session opens its editor; not on
undo or redo), `parameterTouched(owner, key)` when the user changes or takes hold of a parameter that can be
automated (the session shows its lane), and `refused(message)` (below).

Main operations, by area (`[Q]`: `Q_INVOKABLE`, callable from QML):

- **Tracks**: `addAudioTrack` [Q], `addMidiTrack` [Q] (with no devices; C++ forms take a built-in instrument (as its
  default preset has it) or a plug-in and a parent), `addMidiTrackWith` (a new MIDI track with an instrument
  preset, one step), `insertionPoint`, `deleteTracks` [Q] (a group with what is in it, a return with the sends into
  it and their automation; inputs, sidechains and MIDI inputs from them go: one step), `duplicateTracks` [Q] (new ids for tracks,
  clips, devices and rack chains; automation keys, automation view, sidechains, inputs and MIDI inputs among the
  copied tracks renamed to the copies; not armed), `copyTracks`, `cutTracks`, `pasteTracks` (`CopiedTracks`; a copy's name without a `#` is made unique),
  `renameTrack` [Q] (its name template: `"# Lead"`),
  `setTrackColor` [Q], `setTracksColor` [Q] (one colour on several tracks, one undo step), `setTrackParam` / `setTracksParam` (mixer; the master only volume and pan), `soloTracks` [Q],
  `armTracks` [Q], `setTrackHeight` [Q], `setFolded` [Q].
- **Inputs**: `setTrackInput` (device channels), `setTrackInputTrack` (`EditError` for a cycle or a non-audio track),
  `setTrackMonitor`, `setTrackMidiInput`.
- **Groups**: `groupTracks` [Q] (Ctrl+G), `ungroup` [Q] (Ctrl+Shift+G), `moveTracks` [Q] / `canMoveTracks` [Q]. They
  go through one arranging step, which first drops any input or sidechain the new tree would turn into a cycle, in
  the same undo step.
- **Returns and sends**: `addReturnTrack` [Q], `setSend` (`EditError` for a cycle), `removeSend` [Q].
- **Settings**: `setTempo` [Q] (20..999, rounded to 0.01), `setTimeSignature` [Q], `setKey`, `setKeyByName` [Q],
  `setLoop` [Q], `setLoopEnabled` [Q], `setPunchIn` [Q], `setPunchOut` [Q], `setAutomationLocked` [Q].
- **Clips**: `commitClips`, `addClips` (audio files one after another, set up by `clipSettings`; a new audio track if
  needed), `addMidiClip`, `addMidiClipsOver`, `midiClipSpan`, `setClipNotes` (and `setClipsNotes`: several clips' notes in one
  undo step, for the piano roll editing them together, bends too), `moveClips` (with `clampTrackDelta`:
  only onto tracks of the same kind), `replaceClip`, `updateClips`, `deleteClips`, `splitClips`, `duplicateClips`,
  `consolidateClips` (Ctrl+J), time selections (`deleteRange` [Q], `duplicateRange`, `copyRange`, `cutRange`,
  `paste`, `pasteTargets`, `moveRange`, `movedRange` (what `moveRange` would make of the clips, and of frozen
  tracks' segments, without making it: a drag's preview), `reverseRange` (the audio clips in a range play reversed
  copies of their files, given by the
  caller; split at the range's edges), `setRangeActive` (0: the clips in a range deactivated, or activated again;
  split at the range's edges), `clipsArea`, `clipsInRange`, `clipsAt`), and `addRecordings` (takes become
  clips in one step; MIDI takes quantized to the record grid; the per-note bends played on a note, MIDI 2.0's, become
  its bend, drawn with as few points as keep its shape: `notes::simplifiedBend`).
- **A time selection is everything in it**: unless `automationLocked`, deleting, moving, copying, duplicating,
  cutting and pasting a range acts on the automation of every track in it as on its clips, whether the track has
  clips there or not (a group's too): only envelopes with breakpoints in the range; across tracks only the mixer's (a
  device's automation belongs to its track). `copyRange` copies a track with automation and no clips too. Over
  frozen tracks they take the frozen audio along ([Freezing](#freezing)); refused, `deleteRange` returns false,
  `duplicateRange`, `cutRange` and `paste` none, and `moveRange` where the range still is.
- **Devices**: `addDevice` [Q], `insertDevice(s)` (an instrument only on a MIDI track, first, replacing the one there;
  effects never before it), `copyDevices`, `pasteDevices` (sidechains kept unless the source is gone or would close a
  cycle; MIDI inputs kept unless the source is gone or is the track pasted onto; folded copies stay folded), `moveDevice(s)` [Q], `moveDevicesToTrack` [Q] (the same devices, so plug-ins
  keep their state; their automation moves with them in the same step), `removeDevice(s)` [Q], `setDevicesFolded`
  [Q], `setChainListShown` [Q], `setRackDevicesShown` [Q] (view state), `setDeviceDefaults` (where new devices come
  from: default presets). Changing a track's devices deletes the automation of devices (and rack chains) that left
  the track, and macro and modulation mappings to them, in the same step; and the mappings and automation of macros
  a rack no longer has. Devices pasted together keep their modulators' mappings among them (`renamedModulations`).
- **A device's settings**: `setDeviceParam` [Q] (a C++ form takes `old` for plug-ins, whose values the model doesn't
  hold), `setDeviceParams` [Q] (several of a built-in device's at once: one step, merging per gesture while the same
  parameters change), `touchParameter` [Q], `setDeviceState`, `loadPresetInto` (a preset into a device of its kind:
  `loadsInto`), `renameRack` [Q], `setDeviceEnabled` [Q], `setDeviceSidechain`, `setDeviceMidiFrom` (`EditError`
  for a source that isn't a MIDI track; its own track, or `""`, is its own track's notes), `dropMidiInputs` (devices
  taking these tracks' notes take their own track's again: deleting tracks, and flattening them, which makes them
  audio tracks, do it in the same step).
- **Racks**: `groupDevices` [Q] (Ctrl+G: devices of one chain into a new rack), `ungroupRack` [Q] (refused if several
  instruments would come out), `addRackChain`, `removeRackChains` [Q], `duplicateRackChain` [Q], `moveRackChain` [Q],
  `renameChain` [Q], `setChainParam` [Q], `mapMacro`, `unmapMacro` [Q], `macroOf`, `macroTargets`, `setMacro` [Q]
  (the macro and every mapped parameter in one `SetDeviceParamsCommand`), `setMacroCount` [Q] (1..16: new macros
  last, at 0; those taken away with their values, mappings and automation, one step), `renameMacro` [Q] (`""` or
  "Macro N": by its number), `setMacroRange` [Q] (a mapping's `low`..`high`, held to 0..1; the parameter goes where
  its macro puts it in the new range, in the same `SetMacrosCommand`, merging per gesture). A Drum Rack's
  `addRackChain` and `duplicateRackChain` put the pad on a free key (`freePadNote`); `setChainParam`'s
  `receive_note` moves a pad to another key (the pad there takes this one's: "Move Pad"), `locked` locks it.
- **Samplers and Drum Racks** (EditorDrumRacks.cpp): `loadSample` [Q] (a sample, or a part of a file as Start and
  End, loaded by hand: one step, the reference rules of `samplerState::loaded`), `loadDrumPad` [Q] (onto a pad's key:
  into its Sampler, or a new pad with one in 1-Shot (`kSamplerOneShot`), named after the sample; -1: the first free key), `swapSamples` (Samplers
  stepped to similar sounds, one step for them all), `setSimilarReference` [Q]; a pad named after the sample its
  Sampler played follows it (`renamePadsFollowing`). Pads as tracks: `separatePads` [Q] (a track's own Drum Rack:
  the track becomes a group of the same id by `ReplaceTrackCommand`, keeping its mixer, sends, output and the devices
  after the rack but its MIDI effects, with their automation; a MIDI track in it for each pad by `padTrack()`: the
  pad's devices, copies of the track's MIDI effects, its fader, its automation (its chain's faders as the track's
  volume and pan) and its key's notes at `kDrumPadNote`; devices taking the track's notes take their own again) and
  `padToTrack` [Q] (one pad so, after a track (`insertionPoint`), the pad and its notes leaving the Drum Rack's
  track); one step each. Sidechains taken from those pads are taken from their tracks, as they were in the pads
  (`padSidechainsToTracks`; not on frozen tracks).
- **Modulation** (EditorModulation.cpp): `mapModulation` (a parameter of another device on the modulator's track,
  over `low`..`high`, which way up; it moves over from another modulator in the same step, "Map Modulation" through
  `setDevices`; `EditError` for a device that isn't a modulator, the modulator itself, another track's device, a
  rack, or a parameter that can't be automated: hidden, read-only), `tryMapModulation` [Q], `canModulate` [Q],
  `setModulationRange` [Q] (held to 0..1, merging per gesture), `setModulationInverted` [Q], `setModulationAmount`
  [Q] (the amount from a base, the parameter's own value: a knob's handle, merging per gesture),
  `setModulationBipolar` [Q] (unipolar or bipolar from a base, the amount kept), `unmapModulation` [Q]
  (from whichever modulator moves it), `clearModulations` [Q], `modulatorOf` [Q], `modulators` [Q] (a track's, in
  chain order, racks' depth first). `tryMapModulation` (the menu, Map) maps unipolar, from the parameter's own value
  (the base) up to 1; `mapModulation`'s range is as given (0..1 by default). A parameter set by hand
  (`setDeviceParam`, `setDeviceParams`: a knob, a graph's handle, a plug-in's own editor) takes its mapping along
  (`pushModulatedParams`): the amount is kept from the new value (`ModulationMapping::setAmount`), in one
  `SetModulationsCommand`, which carries the parameters' values besides the mappings (as `SetMacrosCommand` does) so
  a drag is one undo step.
- **Grooves** (EditorGrooves.cpp; [Grooves](#grooves)). The library, one `SetGroovesCommand` each, which also gives
  the Groove devices playing a groove its new copy: `addGroove(name, groove, notes)` (its name made unique: "Groove
  2"; its id), `addBuiltinGroove` [Q] (a copy of a built-in one), `duplicateGroove` [Q], `setGroove(id, groove,
  mergeKey)` (the groove editor: a drag with a merge key is one step, worked out from the library and the devices as
  they were when it began), `renameGroove` [Q], `removeGroove` [Q] (what played it, the tracks' clips, Groove devices and
  the default groove, keeps its own copy, `grooves::detached`, in the same step). `extractGroove(track,
  clip, stepBeats, bars, perKeyLanes)` [Q]: a clip's feel into the library, named "<track> Groove" ("" for a MIDI clip
  with no notes heard, or an audio file not decoded yet or without transients). `setClipGroove(refs, groove,
  mergeKey)` ("Set Clip Groove", "Remove Clip Groove"; clips that can't take one stay as they are, an unwarped audio
  clip is warped for it) and `applyClipGrooves(refs)` ("Apply Groove": `grooves::applied`). A Groove device:
  `setDeviceGrooveSource(track, device, kind, grooveId)` [Q] ("swing", "project", "library" with the groove's id, or
  "own": what it plays now, apart from the library; "Change Groove") and `applyDeviceGroove(track, device)` [Q] (what
  it does written into the notes of its track's MIDI clips, on a Drum Rack's pad that pad's key's, which it hears as
  C3, no earlier than Allow Early or Latency lets it move them; and it switched off: one step; false if it isn't one or plays
  none). The default groove: `setDefaultGroove(source, mergeKey)` (a swing of 50 % is none; a library groove that
  isn't there changes nothing), `setDefaultSwing(percent, mergeKey)` [Q] (on the default swing's step, else 16ths),
  `setDefaultGrooveId(id)` [Q] ("": none).
- **Envelopes**: `setEnvelope`, `addAutomationPoint` [Q], `moveAutomationPoints`, `deleteAutomationPoints` [Q],
  `setAutomationCurve`, `clearEnvelope` [Q], `deleteAutomationRange`, `moveAutomationRange`,
  `duplicateAutomationRange`, `copyAutomationRange`, `cutAutomationRange`, `pasteAutomation`,
  `automationPasteTargets`.
- **Automation view** (not undone): `showAutomation` [Q], `hideAutomation` [Q], `toggleAllAutomation` [Q] (A),
  `addAutomationLane` [Q], `setAutomationLane` [Q], `removeAutomationLane` [Q], `resetAutomationView` [Q],
  `defaultAutomationKey` [Q].
- **Freezing**: `freezeTracks`, `unfreezeTracks` [Q], `flattenTracks` [Q], `laneFrozen` [Q].
- **Files**: `replaceFile(uses, path, seconds, text, mergeKey)` (a file in the place of whatever these clips,
  take lanes' clips and devices play, one `ReplaceFilesCommand`: each clip as `edits::replaceFile` has it, trimmed at
  the next one; refused on frozen tracks, though not on their take lanes; with a merge key, from the clips as they
  were before the hot swap began), `relinkFiles({old: new})` (files found somewhere else: every clip playing one or
  reversed from it, on take lanes too, every device naming it, frozen tracks too; "Locate Missing Files"). What plays a file (`FileUses`) is
  [files/ProjectFiles.h](../../app/src/files/ProjectFiles.h)'s ([session.md](session.md#the-file-manager-and-hot-swaps-filemanager-hotswap)).

Four hooks connect it to the rest without it knowing the bridge or the preset library; the session sets them
([session.md](session.md#wiring)): `setParamInfo` (how `paramInfo()` learns a plug-in's or a rack's parameter
mapping, for macros: the bridge's `deviceParamInfo`, made a `ParamSpec`), `setOwnValue` (a plug-in parameter's
current value, set in its own editor, for a macro's undo: `EngineBridge::ownValue`), `setDeviceDefaults` (new
devices as the user's default preset has them: `defaultDevice` in [io/Presets.h](../../app/src/io/Presets.h)), and
`setTransients` (an audio file's transients, seconds and strengths as Mark Transients finds them, none while it isn't
decoded: `EngineBridge::transients`; `transientsOf(path)` reads it for Apply and Extract Groove on audio clips).

### Errors: EditError and the try forms

An edit the user can't make (a send that would close a cycle, an input of three channels, a channel out of range)
throws `EditError` ([Errors.h](../../app/src/model/Errors.h)) with a message for the user, as the C++ API. Nothing
callable from QML throws: the throwing operations have `try*` forms, `Q_INVOKABLE`, which report the message on
`refused` and return `false` (or `""`): `trySetTrackParam(track, field, value, key)` (field by its file name:
`"volume_db"`, `"pan"`, `"mute"`, `"solo"`), `trySetSendLevel`, `trySetSendPreFader`, `trySetTrackInput`,
`trySetTrackInputTrack`, `trySetTrackMonitor`, `trySetTrackMidiInput`, `trySetDeviceSidechain`, `trySetDeviceMidiFrom`, `tryAddRackChain`,
`tryMapMacro`. The `Q_INVOKABLE` operations also do nothing for ids that are gone (QML may hold stale ones). Edits
refused because of frozen audio are said on `refused` too. The session shows `refused` in the status line.

## Pure editing modules

[Edits.h](../../app/src/model/Edits.h) (`sub::app::edits`) and [Notes.h](../../app/src/model/Notes.h)
(`sub::app::notes`) take and return values and never touch Qt's objects or undo, so they are tested directly.

- `resolveOverlaps(clips, winners, tempo)`: Ableton's rule. Winners keep their place; every other clip on the track
  is trimmed, split or removed where a winner covers it (`cutClip`, `subtractIntervals`). The first piece keeps the
  clip's id.
- `removeRange`, `sliceRange` (new clips holding just a range; `keepIds` for clips wholly inside), `splitClip`,
  `trimStart` (a MIDI clip revealing time before its content shifts its notes so they stay put), `trimEnd` (audio
  limited by the source), `fitToTempo` (the same clips if nothing changes; `changed` says whether anything did),
  `consolidateMidi` (what the clips play: a deactivated clip's notes come deactivated, unless every clip is; over an
  `area`, the time selection's empty space too), `reverseClip` (a clip playing a reversed copy of its file: the same stretch of audio, its offset
  mirrored), `selectionSpan`.
- `replaceFile(clip, path, totalSec)` (another file in its place: where it is, with its settings; a clip playing all
  of its file, `playsWholeFile`, plays all of the new one, one playing a stretch plays the same stretch as far as the
  new file goes; named after the file, forwards, its fades held to its length), `relinkFile(clip, from, to)` (a file
  found somewhere else, also what it was reversed from; nothing else changes; paths compared by `samePath`,
  [Paths.h](../../app/src/model/Paths.h)).
- `stretchClip(clip, edgeBeat, left, tempo)` (Alt-dragging an edge): that edge moves, the other stays, and the
  content plays faster or slower to fill it: an audio clip is warped to the segment BPM that makes it that long
  (within `kMinSegmentBpm`..`kMaxSegmentBpm`, 20..999; a clip with warp markers has every stretch's tempo scaled
  as much, all held to that range), a MIDI clip's notes and offset are scaled; never before beat 0.
  `setWarpTempo(clip, bpm)` sets an audio clip's segment BPM, its markers' stretches scaled with it.
  `addWarpMarker(clip, sec)` marks a moment on the beat it plays now (the first also marks the clip's start and end;
  `addWarpMarkers(clip, seconds)` several, a clip's transients),
  `moveWarpMarker(clip, index, sec)` moves one keeping its beat (held where its stretches' tempos stay in range, and
  to the file), `removeWarpMarker(clip, index)` removes one; moving and removing keep the clip on the beats of its own
  it played, so it stays where it is and as long, the audio moving under it. Unwarped clips, and a frozen track's
  clip (a source map without markers), take none. `slipClip(clip, deltaBeats, tempo)` (Ctrl+Shift-dragging the body): the clip stays, its content moves by
  the delta (audio within its file; a MIDI clip's notes anywhere, shifted as `trimStart` does when the offset would
  go below 0). `fadeClip(clip, out, beats, tempo)` and `curveFade(clip, out, curve)` set a fade (held to what the
  other leaves) and its curve.
- Fades stay at a clip's ends: `piece()` (so `cutClip`, `removeRange`, `sliceRange`, `fitToTempo`) gives a piece
  from inside a clip no fade at that side, `splitClip` gives the left half the fade in and the right the fade out,
  and the trims keep both, held to the new length.
- Minimum sizes: `kMinClipSec` (5 ms), `kMinMidiClipBeats` and `notes::kMinNoteBeats` (1/64).
- `notes::place(notes, removed, added)` is how the piano roll commits: added notes win where they overlap others on
  their key (`resolveOverlaps`); notes changed together are made consistent by `untangle`. `normalize` sorts and
  removes exact duplicates. `legato`, `timeScaled`, `quantized` (`kQuantizeGrids`), `humanizedTiming`
  (starts only, by up to `kHumanizeBeats` = a 32nd at 100 %; a triangular random, more often a little than a lot,
  from the `QRandomGenerator` given), `grooved` (Apply Groove: each note where `groove::place()` puts it on the
  timeline, its velocity and length as the groove has them; [Grooves](#grooves)). Humanized velocities are the velocity model's
  ([intelligence.md](../intelligence.md#humanizing-velocities-by-machine-learning)).
- Bends, for the piano roll's bend mode: `withBendPoint` (a point added, kept sorted, after any other at its time;
  says where it went), `withBendPointsMoved` (points moved together stay in order between those not moved, and
  within the note), `withoutBendPoints`, `withBendCurve`, `withVibrato` (a new vibrato takes the stretch it covers
  from those already there: they are shortened, split, or go; held to the note, at least `kMinVibratoBeats`),
  `withoutVibrato`, `nextNote` (the note a slide goes to: the next to start after it, of a chord the nearest in
  pitch), `withSlide` (a slide over a stretch as two points, from the curve's value at its start to an interval at
  its end, replacing the points it covers), and `simplifiedBend` (Ramer-Douglas-Peucker: a recorded bend's many
  points drawn with few, within 0.05 semitones). Note edits carry bends: `withStart` (a trimmed start keeps the bend where it was in time:
  `resized` and `resolveOverlaps` use it), `timeScaled` scales them, and `lessFull` orders notes alike but for
  their bends (the piano roll's note sets are keyed by it).

## Invariants

- `Project::tracks()` satisfies `treeProblem(...)` being none at all times; `arrangeTracks` refuses anything else.
- The routing graph has no cycles: every place that adds an edge (`setSend`, `setTrackInputTrack`,
  `setDeviceSidechain`, arranging tracks, `moveDevicesToTrack`, `pasteDevices`) checks it first, and loading repairs
  edited files (`repairRouting`, [serialization.md](serialization.md#repairs-on-load)).
- Device ids, chain ids and track ids are unique in the project (`newId()`: 12 hex digits of a random UUID);
  copied devices always go through `refreshIds`, copied tracks and clips get new ids.
- A track's `automation` never holds an empty envelope; a device's automation lives on the track the device is on,
  keyed by its id.
- On a MIDI track an instrument (or instrument rack) is first in its chain, and there is at most one; audio tracks,
  groups, returns and the master take no instrument.
- Racks never nest deeper than `kMaxRackDepth`.
- Macro mappings only name devices inside their rack, and macros the rack has.
- Modulation mappings are only a modulator's, name other devices on its track, and each parameter once.
- A rack has 1 to 16 macros, and a value for each.
- A device's `midiFrom` names a MIDI track other than its own, or is empty: deleting or flattening the source takes
  it away in the same step, and loading repairs edited files.
- A note's bend points are sorted by time and its vibratos by start; edits keep vibratos from overlapping (each at
  least `kMinVibratoBeats` long) and every value within ±48 semitones, and loading sorts both and holds them to
  the range.
- Library grooves' ids are unique. The default groove is never `Project`. Removing a library groove leaves its
  users their own copy in the same step: the tracks' clips, Groove devices and the default (a source naming a
  groove that isn't there, from an edited file, plays none). A Groove device playing a library groove holds its copy
  as the library has it.

## Extending it

- **A new undoable edit**: a `ProjectEditor` method in the `Editor*.cpp` of its area that computes the new value with
  the pure functions and pushes an existing command; only add a command when no existing one replaces the right
  thing. If it needs a new kind of change on `Project`, add a mutator that emits a signal, and handle that signal in
  the engine bridge and the UI. If QML calls it, make it `Q_INVOKABLE` and make sure it can't throw (a `try*` form if
  it can refuse).
- **A new track field**: add it to `Track` (with a default, so older files load), and to `TrackField`,
  `trackFieldName` and `Track::value`/`setValue` if `updateTrack` sets it; decide whether it is undoable (a command)
  or view state (set directly); and add it to [Serialization.cpp](../../app/src/io/Serialization.cpp) (bump
  `kProjectVersion`; see [serialization.md](serialization.md)).
- **A new automation target kind**: a key form in `Automation.h` (`parseKey` and the classifiers), a `ParamSpec`, its
  own value in `EngineBridge::ownValue`, and the engine lane in `EngineBridge::engineLane`
  ([BridgeParameters.cpp](../../app/src/audio/BridgeParameters.cpp)).
- **A new built-in device**: nothing here. It comes from the engine ([engine/devices.md](../engine/devices.md)).

## Gotchas

- **References go stale.** A `const Track&` or `const Device*` from the project is invalid after the next change (see
  [above](#values-held-references-handed-out)). Keep ids, not references, across edits.
- `Project`'s mutators don't check that a change is undoable; calling them from the UI skips undo. Only view state may
  do that.
- The model links the engine: `Devices.h` includes the engine's `builtin/BuiltinRegistry.h` (the built-in devices come
  from there). That header is the engine's; the UI may not include it directly.
- A plug-in's `Device::state` is only as fresh as the last `EngineBridge::storePluginStates()`. Copying devices,
  duplicating tracks or chains and saving presets must store states first (the session does).
- A plug-in's `params` only hold values the host changed; its real values live in the plug-in. Use
  `EngineBridge::ownValue` to read them.
- `Clip::segmentBpm` 0 means "not set": a clip with `warp` on but no BPM isn't warped.
- MIDI clips accept a `tempo` they ignore, so both kinds of clip can be edited alike.
- `Track::sends` must be replaced whole, never changed in place: the commands compare old and new maps.
- `Track::automation` keeps the order targets were first automated in (an `OrderedMap`): the
  first one is what a lane shows by default.
- `sub_app` is built with `QT_NO_KEYWORDS`: write `Q_SIGNALS`, `Q_SLOTS` and `Q_EMIT`.

## Tests

- [test_project.cpp](../../tests/app/test_project.cpp), [test_commands.cpp](../../tests/app/test_commands.cpp): the
  project's queries, the group tree and routing graph, each command and its signals, merging.
- [test_edits.cpp](../../tests/app/test_edits.cpp), [test_midi_model.cpp](../../tests/app/test_midi_model.cpp),
  [test_automation_model.cpp](../../tests/app/test_automation_model.cpp),
  [test_timebase.cpp](../../tests/app/test_timebase.cpp), [test_keys.cpp](../../tests/app/test_keys.cpp): the pure
  functions, the parameter mappings against the engine's.
- [test_warp_markers.cpp](../../tests/app/test_warp_markers.cpp): warp markers: a clip's beats and seconds through
  them, those left out, split, stretched, retimed and reversed with them, saved, and the engine's source map.
- [test_editor_edits.cpp](../../tests/app/test_editor_edits.cpp): overlaps, trims, splits, ranges, tempo fitting,
  moves across tracks, gesture merging, inputs undoable and arming not, recorded takes, copy and paste and automation
  carried with clips, time selections over groups and tracks without clips, `movedRange`, reversing.
- [test_editor_midi.cpp](../../tests/app/test_editor_midi.cpp),
  [test_editor_automation.cpp](../../tests/app/test_editor_automation.cpp),
  [test_editor_duplicate.cpp](../../tests/app/test_editor_duplicate.cpp),
  [test_editor_groups.cpp](../../tests/app/test_editor_groups.cpp),
  [test_editor_sends.cpp](../../tests/app/test_editor_sends.cpp),
  [test_editor_resampling.cpp](../../tests/app/test_editor_resampling.cpp),
  [test_editor_sidechain.cpp](../../tests/app/test_editor_sidechain.cpp),
  [test_editor_midi_from.cpp](../../tests/app/test_editor_midi_from.cpp),
  [test_editor_racks.cpp](../../tests/app/test_editor_racks.cpp),
  [test_editor_presets.cpp](../../tests/app/test_editor_presets.cpp),
  [test_editor_freeze.cpp](../../tests/app/test_editor_freeze.cpp): each feature's rules, undo, cycles refused.
- [test_editor_frozen_areas.cpp](../../tests/app/test_editor_frozen_areas.cpp): time selections over frozen tracks
  and groups (delete, move, copy, duplicate, cut, paste) with their frozen audio, in one undo step; what is refused;
  unfreezing and flattening afterwards.
- [test_note_bends.cpp](../../tests/app/test_note_bends.cpp): bends and vibratos, the pure functions (order, moves,
  vibratos taking their stretch, slides into the next note, note edits carrying bends), the model bending as the
  engine does, recorded bends
  drawn with few points and becoming the notes' bends, project files (and damaged bends refused), and bent notes
  playing at their bent pitch.
- [test_file_manager.cpp](../../tests/app/test_file_manager.cpp): files replaced and relinked (the pure functions,
  the editor's one step, a hot swap's merging and its baseline, frozen tracks), the File Manager and hot swaps.
- [test_grooves.cpp](../../tests/app/test_grooves.cpp): a MIDI clip's notes and an audio clip's warp markers as its
  groove plays them, notes grooved as the device would play them, Apply Groove (clips, the device, a Drum Rack pad's
  notes only), Extract Groove, the built-in grooves, sources resolved, the library with undo (a groove removed leaving
  its users their own copy, one edited reaching the devices playing it), clips' grooves warping audio, the default
  groove; grooves in project files, `.gilgroove` and `.agr` files.
- [test_session_engine.cpp](../../tests/app/test_session_engine.cpp) and the `test_bridge_*` tests: the engine
  hearing the editor's edits.

See [testing.md](../testing.md) for the whole suite.
