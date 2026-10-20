# MIDI in the engine

How MIDI tracks play: the notes of their clips turned into note events for the track's devices (each
note on its own, as MIDI 2.0 addresses notes, with its bend: MIDI 2.0's per-note pitch bend), the notes
the piano roll plays, MIDI input from controllers (WinMM on Windows, ALSA's sequencer on Linux; MIDI 2.0's packets too) played live
and recorded, and devices taking another track's notes. Playback is in the renderer
([Renderer.cpp](../../engine/src/Renderer.cpp): `buildNoteEvents`, `startNote`, `bendNotes`,
`releaseNotes`, `syncTempo`, and the notes handed early, `buildEarlyEvents`); a bend's rules are in
[NoteBend.h](../../engine/src/NoteBend.h); MIDI input
devices and the audio clock are in [MidiInput.h](../../engine/src/MidiInput.h) /
[MidiInput.cpp](../../engine/src/MidiInput.cpp), MIDI 2.0's packets in [Ump.h](../../engine/src/Ump.h);
the engine's MIDI input API is in [EngineInput.cpp](../../engine/src/EngineInput.cpp); MIDI takes are in
[Recorder.h](../../engine/src/Recorder.h).

For what the user sees (MIDI clips, the piano roll, MIDI input, the computer MIDI keyboard) see
[guide/midi.md](../guide/midi.md). How instruments take the events is in [devices.md](devices.md) (built-in)
and [plugins.md](plugins.md) (VST3); chunks, segments and the prologue in [rendering.md](rendering.md);
audio takes and placement in [recording.md](recording.md).

On Linux MIDI inputs are ALSA's sequencer's ports ([backends/MidiAlsa.cpp](../../engine/src/backends/MidiAlsa.cpp),
[below](#alsas-sequencer-linux)). Where neither it nor WinMM is built (no ALSA headers, other systems) the engine
has no MIDI input devices ([backends/MidiNone.cpp](../../engine/src/backends/MidiNone.cpp): none is ever listed or
opened). Everything else on this page works the same there, including input sent with `Engine::sendMidiInput()`
(the computer MIDI keyboard, the tests).

## Overview

- The engine bridge flattens a MIDI track's clips into the notes they play, in beats (`setTrackNotes`), keeping
  only what the clips play (notes cut at their clip's end), each with its bend (points and vibratos). The
  snapshot converts them to samples by the tempo map.
- Every note-on carries an id of its own; the note's note-off and bends carry it too (MIDI 2.0's per-note
  addressing; VST3's note ids). While a note sounds the renderer sends its bend along its curve: where it
  starts, then every 32 samples where it moved, and on its last sample (`NoteBend` events).
- Each block, the renderer turns notes starting in it into note-on events for the track's devices, and
  remembers when each ends. Note-offs come from that record, not from the snapshot, so a note edited or
  deleted while it sounds still ends.
- Stopping, locating and wrapping around the loop release every sounding note. A tempo change moves the
  recorded note ends along with the playhead.
- A chain's first MIDI effect that may play notes early (`Processor::earlyBeats()`: the Groove device) is
  handed, after each chunk, the clips' notes due within that many beats, with the ids they will come with
  in their time ([below](#early-notes)).
- Notes the piano roll plays go through a lock-free queue to the next audio block, straight to the
  track's instrument.
- Offline renders share devices with live playback, so the engine resets them before and after. A device
  that is switched off resets when it comes back on, so it can't keep notes whose note-offs it missed.
- MIDI input: each message is stamped with the host clock as it arrives and turned, right there, into a
  sample on the audio device's clock, plus one device buffer, so the delay is constant. The renderer
  routes the messages due in each chunk to the tracks whose `MidiInputRoute` accepts them, and remembers
  the live notes it started so it can release them.
- MIDI 2.0: Universal MIDI Packets (`Engine::sendUmp`) are stamped and queued as MIDI 1.0's bytes are;
  their channel voice messages play as MIDI 1.0's, and a per-note pitch bend bends the live note it names.
- A MIDI track being recorded gets a `MidiRecordingTake`; its note-ons, note-offs and per-note bends go
  through a queue and the edit side pairs them into notes (with their bends).
- A device may take another track's notes instead of its own track's (`setProcessorMidiInput`): the
  prologue copies that track's events for the chunk into the device's `MidiFeed`, and the device plays
  its copy.

## Files

| File | What it holds |
|---|---|
| [Engine.h](../../engine/src/Engine.h) | `NoteDesc` (a note in timeline beats, with its bend), `AudioClockStatus`, the MIDI API (`setTrackNotes`, `previewNote`, `midiInputDevices`, `openMidiInput`, `closeMidiInput`, `openMidiInputs`, `setTrackMidiInput`, `sendMidiInput`, `sendUmp`, `audioClock`, `setProcessorMidiInput`, `processorMidiInput`) |
| [NoteBend.h](../../engine/src/NoteBend.h) | `BendPoint`, `VibratoSpan`, `kMaxBendSemitones`, and a bend's rules (`sub::bend::curveAt`, `vibratoAt`, `at`), header-only, for any unit of time (the application layer draws with them in beats) |
| [Ump.h](../../engine/src/Ump.h) | MIDI 2.0's packets: `packetWords`, `decode` (into a `MidiInputEvent`), `bendSemitones`/`bendValue`, and packets to send (`noteOn`, `perNoteBend`, `perNoteManagement`...) |
| [EngineTracks.cpp](../../engine/src/EngineTracks.cpp) | `setTrackNotes`, `previewNote` |
| [EngineSnapshot.cpp](../../engine/src/EngineSnapshot.cpp) | Notes into `NoteRender`s in samples; `MidiInputRoute`, monitoring and arming into `TrackRender` |
| [Snapshot.h](../../engine/src/Snapshot.h) | `NoteRender` (and its `NoteBendRender`), `MidiInputRoute`, `TrackBuffers::events` (a track's note events per chunk, at most 1024), `TrackBuffers::early` (its clips' notes handed early per chunk, at most 256), `MidiFeed` and `MidiFeedRender` (devices taking other tracks' notes; `StripRender::midiFeeds`, `RenderSnapshot::midiFeeds`) |
| [Transport.h](../../engine/src/Transport.h) | `PreviewNote`; in `SharedState`: `previewNotes` queue, `midiInput` queue and `midiInputMutex`, `midiInputDelay`, `midiSampleRate`, `clock` |
| [Processor.h](../../engine/src/Processor.h) | `ProcessEvent` (`NoteOn`, `NoteOff`, `Midi`, `NoteBend`; `noteId`, `bend`), `acceptsMidi`, `setEnabled`, `requestReset`, `takeResetRequest`, `reset`, `resetOffline`; notes handed early: `EarlyEvent`, `EarlyList`, `ProcessContext::earlyEvents`, `earlyBeats` |
| [Renderer.h](../../engine/src/Renderer.h), [Renderer.cpp](../../engine/src/Renderer.cpp) | Note events, note ids, active notes and their bends, notes handed early, preview notes, MIDI input gathering and routing, live notes, MIDI recording, devices' MIDI feeds |
| [MidiInput.h](../../engine/src/MidiInput.h), [MidiInput.cpp](../../engine/src/MidiInput.cpp) | `MidiInputEvent`, `AudioClock`, `MidiInputDevices` (the same on every system: the names users see, which inputs are open, closing them) |
| [backends/MidiDriver.h](../../engine/src/backends/MidiDriver.h) | What a system's MIDI API gives `MidiInputDevices`: `systemInputNames()`, `openSystemInput()` (an `InputConnection`, closed when it goes), `shortMessageSize()` |
| [backends/MidiWinMM.cpp](../../engine/src/backends/MidiWinMM.cpp), [backends/MidiAlsa.cpp](../../engine/src/backends/MidiAlsa.cpp), [backends/MidiNone.cpp](../../engine/src/backends/MidiNone.cpp) | Those on Windows (WinMM), Linux (ALSA's sequencer), and elsewhere (no devices) |
| [backends/MidiAlsa.h](../../engine/src/backends/MidiAlsa.h) | ALSA's parts that need no sequencer, for the tests: `alsa::inputName`, `alsa::hostTime`, `alsa::Decoder` |
| [EngineInput.cpp](../../engine/src/EngineInput.cpp) | MIDI ports, `Engine::midiInput` and `umpInput` (decoding), `queueMidiInput` (stamping), `sendMidiInput`, `sendUmp`, `discardMidiInputLocked`, MIDI targets in `startRecording` |
| [EngineChains.cpp](../../engine/src/EngineChains.cpp) | `setProcessorMidiInput`, `processorMidiInput`, `processorInfo` (`acceptsMidi`) |
| [Recorder.h](../../engine/src/Recorder.h), [Recorder.cpp](../../engine/src/Recorder.cpp) | `RecordedNote` (with its `RecordedBend`s), `MidiRecordingTake`, `RecordingSession::midiNotes`, MIDI results in `finish()` |
| [EngineOffline.cpp](../../engine/src/EngineOffline.cpp) | `resetProcessorsLocked` around offline renders and exports |

## Key types

**`NoteDesc`** (Engine.h): `startBeat`, `lengthBeats`, `key` (60 = C3), `velocity`, in timeline beats, and
its bend: `bend` (`BendPoint`s: time in beats from the note's start, semitones, curve) and `vibrato`
(`VibratoSpan`s: start and length in beats from the note's start, depth in semitones, rate in Hz, fade).

**`NoteRender`** (Snapshot.h): a note already cut to its clip, in timeline samples: `start`, `end` (always
`> start`), `key` (clamped to 0..127), `velocity` (clamped to 1..127), and `bend` (a shared
`NoteBendRender`, null if it doesn't bend: its points' times in samples from its start, its vibratos'
starts and lengths in samples and rates in cycles a sample). A track's are sorted by start, then key.
`EngineSnapshot.cpp` rounds where the tempo map puts both ends (`RenderSnapshot::sampleAt`), keeping them
in beats too (`startBeat`, `endBeat`), and a bend's times are the samples from the note's start where it is; `noteBend()` leaves out vibratos that
can't swing and a bend whose points are all 0.

**`ProcessEvent`** (Processor.h): what devices get. `NoteOn`/`NoteOff` with `data[0]` the key, `data[1]`
the velocity (0 for note-offs), `data[2]` the MIDI channel; `NoteBend` with `data[0]` the key and `bend`
the note's pitch in semitones from its key; `Midi` with the raw bytes (pressure, controllers, program
changes, channel pitch bend from MIDI input). `noteId` names the note an event is about (a note-on's id,
which its note-off and bends repeat), -1 for none (a note the piano roll plays): such a note is known by
its key and channel, as in MIDI 1.0. `sampleOffset` is in the block (the renderer rebases it to each
stretch a device processes).

**`PreviewNote`** (Transport.h): `trackId`, `key`, `velocity` (0: a note-off), from the piano roll.

**`MidiInputEvent`** (MidiInput.h): `time` (the device sample at which it plays), `port` (the engine's id
of the input), `status`, `data1`, `data2`; `kind` `Message` (a MIDI 1.0 message, or a MIDI 2.0 one put in
its form), or `NoteBend` (a MIDI 2.0 per-note pitch bend: `status` 0x6n, `data1` the key, `value` its 32
bits, 0x80000000 none).

**`AudioClock`**: the device's sample clock against the host clock. The audio thread writes it at each
callback (`update(hostTimeNs, sampleTime)`) as a seqlock (wait-free for it; readers retry); `stop()` is
called by the edit side while no callback runs; `read()` returns `running`, `hostTimeNs`, `sampleTime`.
Callbacks that share a host time (WASAPI splitting a device buffer into engine callbacks) keep the first
one's anchor.

**`MidiInputRoute`** (Snapshot.h): which MIDI input a track hears and records: `enabled`, `port`
(`kAllPorts` or an engine port id), `channel` (`kAllChannels` or 0-15). `accepts(port, status)` checks
both.

**`MidiInputDevices`**: the system's MIDI inputs (WinMM on Windows, ALSA's sequencer on Linux). `available()` lists them by name (a second
device of the same name gets " #2"); `open(name, port)` (throws `std::runtime_error` for the user: not
connected, or "another application may be using it"), `close`, `closeAll`, `openNames`. Its handler is
called on the driver's thread for every short message (channel and system messages; System Exclusive is
ignored: no long buffers are given).

**`MidiRecordingTake`**: one MIDI track's take: `events` (`SpscQueue<Event, 1 << 14>`, audio thread →
edit side: `time` in timeline samples, channel, key, velocity, 0 for a note-off), `start`, `frames`,
`dropped` (events lost because nobody took them in time), and on the edit side the `notes` paired so far.

**`RecordedNote`**: `start`, `end` (-1: still held), `key`, `velocity`, `channel`, in timeline samples, and
`bend`: the per-note bends played on it (`RecordedBend`: samples from its start, semitones), in time order.
The API hands them over as a `std::vector<RecordedNote>` (`recordingProgress()`, `RecordedTake`).

**`MidiFeed`** (Snapshot.h): a device's copy of another track's events for the chunk (`events`, as many as
a track's, and `numEvents`), kept with the device (`ProcessorEntry::midiFeed`) across snapshots;
`MidiFeedRender` lists each in the snapshot with the snapshot track it copies (`source`, -1: none in this
snapshot).

## How it works: playback

### Notes into events (`Renderer::buildNoteEvents`)

In the chunk's prologue (serial: sounding notes are shared state), for each track that has notes, or
while anything is sounding, queued or arriving, the renderer builds the track's events into its
`TrackBuffers`:

1. **Preview notes** for this track, all at offset 0 and in the order they were played (dragging a note
   across keys releases one key and plays the next several times within a block; reordering those would
   leave notes playing whose note-off came first). Each note-on gets an id, and its note-off the same
   (`heldPreviews_`: the oldest held of that key on the track; 256 at most, the oldest forgotten). They
   have no bend.
2. **MIDI input** (`routeMidiInput`, below).
3. If stopped, or the clips' notes don't play (monitoring `In` with MIDI input), every active note of
   the track is released at offset 0 (`releaseNotes`), those handed early too.
4. For each segment of the chunk (a continuous stretch of the timeline; see [rendering.md](rendering.md)):
   - a segment that `jump`s (a locate, a loop wrap) releases the track's sounding notes at its start (at
     a wrap, not those handed early for after it);
   - a segment that `chase`s (playback starts here) sounds the notes already underway from there;
   - note-ons for the notes starting in it (binary search on `start`; `startNote`), each with a fresh id
     (`newNoteId()`) and recorded as an `ActiveNote` (`trackId`, `key`, `end`, `noteId`, `start`, the
     note's index as a hint, the bend sent); a note that starts bent (or is joined bent: chased) gets its
     bend there too, right after its note-on. A note handed early goes out again with the id it was
     handed with (`startHanded`, [below](#early-notes));
   - the sounding notes' bends (`bendNotes`, below), those ending in the segment included;
   - note-offs for active notes ending in it, those that just started included, with their ids.
5. The arrangement's notes and the input are sorted by offset; at the same offset a note-off comes
   first, then note-ons and raw messages, then bends, so a note that ends where the next one on its key
   starts doesn't cut the new one short, and a bend comes after the note-on it bends. The preview notes
   stay first, in their order.

Active notes are capped at 512 across all tracks (`kMaxActiveNotes`); a note that can't be recorded
there isn't started. A track's events are capped at `TrackBuffers::kMaxEvents` (1024); events that don't
fit (note-offs from `releaseNotes`) wait for the next block. `forgetNotesOfRemovedTracks()` drops the
active and live notes of tracks no longer in the snapshot: their instruments are gone with them.

Because note-offs come from `activeNotes_`, not the snapshot, a note edited or deleted while it sounds
still ends when it was going to. Every chain of a rack hears the track's notes, so an Instrument Rack
layers its instruments, but a Drum Rack's pad (a chain with a key: `setChainReceiveNote`) hears only
its key's, as C3 ([routing.md](routing.md#racks)). Constrained, a track played live plays its own
clips' notes `TrackRender::clipDelay` late, so only its input comes early
([routing.md](routing.md#delay-compensation)). A device then processes each stretch with the events
that fall in it (`processInserts` splits them by slice).

### Early notes

A MIDI effect that moves notes earlier (the Groove device: [devices.md](devices.md#groove-builtingroove-midieffect),
[groove.md](../groove.md)) can't play a note before it comes in, but a clip's notes are known ahead. A processor says
how early it may play one: `Processor::earlyBeats()` (beats; 0, the default: never). After a track's events are
built, for a track whose clips' notes play (not recorded, not monitoring `In`), the prologue hands them over
(`buildEarlyEvents`):

1. **How early**: `earlyBeatsOf()`: the `earlyBeats()` of the first MIDI effect on the notes' way (0 while it is
   switched off): the strip's first, if it has one; else the largest of its racks' chains' (inner racks too). Only
   the first: the notes the others hear (a rack's after the strip's MIDI effect too) are played by the effects
   before them, which aren't known ahead. None more than 0: nothing to do.
2. **Only while the playhead goes on from here**: playing, past any count-in, and the track's clips not played late
   (a constrained live track's `clipDelay`: it gets none).
3. **The window**: from where the next chunk starts, that many beats by the tempo map, round the loop once at most
   (a window running past the loop's end goes on from its start). For each stretch of it, `handEarly()`:
   - each note starting there that isn't an active note of the track already (handed before, or sounding from an
     earlier pass round the loop with the same start and key, which keeps this one to its time): a note-on with a
     fresh id (`newNoteId()`: the id is the note's from here on), its offset from the chunk's start (at or past its
     end: where it plays) and its `beat`; and an `ActiveNote` for it marked `early` and `pending`;
   - the note-offs of the notes handed early that end there, each once (`earlyOff`), but for a note handed for after
     a wrap, which ends in its own pass.

   Up to `TrackBuffers::kMaxEarly` (256) a chunk, within the 512 active notes.
4. They are sorted by offset, a note-off first at one offset, into the track's `TrackBuffers::early`.

During the graph, `renderTrack()` hands them to its strip (`WorkerScratch::early`), and `processMidiEffects()` gives
them to a chain's first MIDI effect if its `earlyBeats()` is more than 0: `ProcessContext::earlyEvents`, an
`EarlyList` of `EarlyEvent`s (the event, and `beat`, where it plays on the timeline), in the chain's first slice only
(their offsets are from the chunk's start; a `BuiltinProcessor` passes them to its first stretch only). A Drum
Rack's pad gets those of its key, as C3, as it gets its notes (`padEarly()`, into `WorkerScratch::padEarly`, by
depth); a rack's other chains get the strip's. Past a chain's MIDI effects (`processChain`), its racks get none:
their notes are those effects', played as they come.

When a handed note's time comes, `buildNoteEvents` sends its note-on again with the id it was handed with, and its
bend where it starts (`startHanded()`: the pending active note on its key whose start is within two samples of the
note's, as a tempo change may round it). It is pending no more, and goes on as any active note: its bends, and its
note-off from its record. So the devices that didn't take it early (a rack's chain without such an effect) hear it in
its time, and the effect that took it knows it by its id and ignores it. A handed note gone from the clips since (an
edit) is taken as started all the same where its start comes: its note-off comes from its record. While pending, a
note is sent no bends and no note-off by the segments.

- **The loop**: a wrap releases the track's sounding notes but keeps those handed for after it
  (`releaseNotes(..., keepHanded)`).
- **Locating and stopping** release them with the others: their note-offs go out with their ids, and the effect that
  took them, never having played them, drops them. The window starts again from where the playhead goes.
- **A tempo change** moves them as any active note (`syncTempo`, by their beats).
- Offline renders hand them over the same way, so an export is what playback plays.

### Bends: MIDI 2.0's per-note pitch bend

A note's bend ([NoteBend.h](../../engine/src/NoteBend.h)) is a curve of points from its start plus
vibratos on top: the curve starts at 0 at the note's start, goes to the first point and through the
others (a point's curve bends the segment starting at it, as `automationShape()` bends automation),
then holds the last point's value; a point at (or before) the start sets where it starts. A vibrato adds
`depth * swell * release * sin(2π rate (t - start))` over its stretch: it swells in (smoothstep) over its
first `fade` of its length and dies away over its last tenth (`kVibratoRelease`), so it begins and ends
on the curve. All is held to ±48 semitones (`kMaxBendSemitones`, MIDI 2.0's default per-note range). The
rules are written once, as templates on any types with the fields' names, for any unit of time: the
snapshot's `NoteBendRender` plays them in samples, and the application layer's `Note::bendAt()` draws
them in beats (a vibrato's rate turned from Hz into cycles a beat).

`bendNotes(track, segment)` sends each of the track's active notes' bends over a segment: it finds the
note's `NoteRender` again (`findNote`: at its hinted index, else by start and key in the current
snapshot's notes, to within two samples, as a tempo change may round its start), then for each point of
the note's own grid (every `kBendStep` = 32 samples from its start, after it, before its end) in the
segment, a `NoteBend` event where the value moved by `kBendEpsilon` (0.001 semitones) or more since the
last one sent, and where the note ends in the segment, one more on its last sample (if it moved), so its
release holds the value the curve ends on: a slide whose last point is on the note's end lands on it rather
than up to 32 samples short. The grid is the note's, so the values don't depend on where blocks start; a
flat stretch sends nothing. Because the note is looked up again in every snapshot, a bend drawn (or changed) while a
note sounds is heard at once; a note moved or deleted while it sounds keeps the bend it reached. Bends
stop while fewer than `kBendHeadroom` (64) of the track's events are free, so note-offs always fit.

Devices follow bends as they come: the Synth and the Sampler glide each voice to its note's bend over
about a millisecond (`dsp::kBendGlideSeconds`, so the 32-sample steps don't zipper); a VST3 plug-in gets
them as note expression "tuning" for the note's id ([plugins.md](plugins.md#events-notes-and-midi)), and
every note's tuning from its start, unbent unless its own bend follows (a synth that reuses a voice, as
Serum 2 can, may otherwise play the next note with the last note's tuning). A voice keeps its bend through
its release.

### Tempo changes (`Renderer::syncTempo`)

When the tempo map (`tempoVersion`) or the sample rate changes, the playhead, the expected position and
every active note's start and end go back to their beats by the new map (`ActiveNote::startBeat`,
`endBeat`), so they stay on the same beat. The new snapshot already has the notes themselves at the new
tempo.

### Preview notes

`Engine::previewNote(track, key, velocity)` pushes a `PreviewNote` onto
`SharedState::previewNotes` (256 slots, one producer: it is pushed under the engine's lock). At the
start of each live callback `drainPreviewNotes()` moves them into the renderer's list (256), played in
the callback's first chunk and then cleared. They are only heard while a device runs: without one,
`serviceTransportIfIdleLocked()` discards them (nobody would hear them, and a stale note-on could hang).
Offline renders never play them.

### Resets: offline renders and devices switched off

- Offline renders (and exports) share the processors with live playback. `resetProcessorsLocked()` runs
  before and after: `resetOffline()` on every processor (clearing what a real-time reset can't, such as a
  plug-in's reverb tail, while it isn't being processed), and `requestReset()`. Before keeps live notes
  out of the render; after keeps the render's last notes from hanging in live playback. Live output is
  suspended meanwhile ([rendering.md](rendering.md)).
- `requestReset()` sets a flag; whichever renderer processes the device next calls `reset()` first
  (`Renderer::takeResets`, from the rendering thread). A rack asked to reset passes it on to everything in
  it.
- `Processor::setEnabled(false)` requests a reset, so a device switched off resets when it comes back on
  and can't keep notes whose note-offs it missed.
- A device moved to another track resets too (`moveProcessor`): what it holds of the old strip's signal,
  and notes that strip will release elsewhere, stop.

## How it works: MIDI input

### Devices and ports

`Engine` owns a `MidiInputDevices` whose handler is `Engine::midiInput`. Inputs are opened, listed and
closed on the main thread, like audio devices.
Port ids are indices into `midiPorts_`, a list of names kept for the engine's life, so a track can name an
input that isn't open (yet). The engine bridge opens every connected input but those turned off in the
preferences (`EngineBridge::openMidiInputs()`), and the application adds the computer keyboard as one more
input, `Computer Keyboard` (`kComputerKeyboard`), which plays through `sendMidiInput`
([app/engine-bridge.md](../app/engine-bridge.md)). Without MIDI devices (`MidiNone.cpp`) it is the only input.

`setTrackMidiInput(track, enabled, device, channel)`: none (`enabled` false), every input (`""`) or one
by name, on every channel (0) or one (1-16; anything else is `std::invalid_argument`).

### ALSA's sequencer (Linux)

[backends/MidiAlsa.cpp](../../engine/src/backends/MidiAlsa.cpp): one sequencer client for the process,
"SUBstation", made the first time an input is listed or opened (not tried without `/dev/snd/seq`: no inputs
then, and again the next time). It has one port of its own, "Input" (written to only by the inputs it
connects: `NO_EXPORT`), and a thread polling it (and an eventfd that ends it).

- **Listed**: every port others can read from (`READ | SUBS_READ`, not `NO_EXPORT`), but the system's (client
  0) and its own, by client then port. Named by `alsa::inputName`: the port's name where it starts with its
  client's (a USB device's "Launchkey MK3 MIDI 1", "Midi Through Port-0"), else "client: port" ("VMPK Output:
  out"). PipeWire bridges ALSA's MIDI both ways, so this lists what it has too.
- **Opened**: the input connected to the port (`snd_seq_connect_from`). Sequencer connections are never
  exclusive, so another application reading it is no error. Its events come in with their source; each is
  decoded by `alsa::Decoder` (`snd_midi_event_decode`, each message with its status: a 14-bit controller is two
  controllers; System Exclusive and the sequencer's own events give none) and handed to the handler with the
  input's port. Closing disconnects it under the mutex the thread calls the handler under, so once it
  returns the handler isn't called.
- **Timestamps**: the port is stamped by a queue of the client's own (real time), so each event carries
  when the sequencer took it, not when the thread got to it. The thread reads the queue's time and
  `hostTimeNs()` together before it reads what came in, and `alsa::hostTime` places each event as long before
  that reading as it was before the queue's (read again for an event later than the reading; never later
  than it).
- **Hot-plug**: the client hears the system's announcements. A port or client gone (unplugged, the program
  ended) marks the inputs connected to it lost; a port come connects each lost input again to a port of the
  same name that none is connected to (another number after being plugged in again). Inputs plugged in
  for the first time are listed the next time the inputs are (*Refresh* in the MIDI preferences).
- The client speaks MIDI 1.0: the kernel turns a MIDI 2.0 device's packets (ALSA 1.2.10's UMP) into its
  messages for it.

### Stamping (`Engine::midiInput`)

On the driver's thread (WinMM calls back for each `MIM_DATA`, and the callback takes `hostTimeNs()` first; ALSA's
thread passes the time the sequencer stamped, [above](#alsas-sequencer-linux)) or
the caller's (`sendMidiInput`, which checks the message is a status byte and up to two data bytes, and
uses "now" when given host time 0):

```
clock   = shared_.clock.read()                 // the last callback's (hostTimeNs, sampleTime)
if !clock.running: drop                        // no device runs
time    = clock.sampleTime
        + round((hostTime - clock.hostTimeNs) * midiSampleRate)
        + midiInputDelay                       // one device buffer
push MidiInputEvent{time, port, bytes} onto shared_.midiInput   (holding midiInputMutex; full: dropped)
```

A callback renders a buffer ahead of what is heard, so a message that came in during one buffer is placed
at the same distance into the next, and the delay stays constant instead of jittering with the
callbacks. Converting on arrival also makes the timing testable: a message stamped against a known clock
reading lands on a known sample. `midiInputDelay` is the device's buffer size (set when the device opens;
512 if it reports none); `audioClock()` reports it with the clock. `Engine::setMidiImmediate()` makes it 0:
a message is then due before the next block begins, so it plays at that block's start (sooner by up to a
buffer, but as early as the block falls), and `startRecording()` places MIDI half a buffer earlier than it
is met, the delay on average.

The queue (`SpscQueue<MidiInputEvent, 4096>`) has one consumer, the audio thread; the producers (driver
threads, `sendMidiInput()`) share a mutex on their side of it, so the audio thread never waits. The engine
never takes its own `mutex_` on the driver's thread. While no device runs, the edit side empties the queue
(`discardMidiInputLocked`, in `serviceTransportIfIdleLocked`), and closing a device stops the clock, so
input is dropped from then on.

### Due in a chunk (`drainMidiInput`, `gatherMidiInput`)

At the start of each live callback, `drainMidiInput()` moves what arrived into `pendingInput_` (at most
2048 not yet due). Before each chunk, `gatherMidiInput()` takes the messages due in it, in the order they
came, with their offsets (`time - deviceTime_`, clamped into the chunk):

- A message more than a second off the chunk wasn't stamped against this run of the clock (it was reset,
  or live output was suspended meanwhile): it plays now, but a note-on that late is dropped rather than
  played out of time (its note-off is harmless).
- A note-off never plays before its note-on (they may be stamped out of order, or both late): just after
  it, or in the next chunk.
- Once one message is put off, those after it are too, keeping their order. At most 512 are due per
  chunk (`kMaxInputEvents`).

### Routing and live notes (`Renderer::routeMidiInput`)

A track hears its MIDI input (`hearsMidiInput`) only live, with a route enabled, and while monitored:
`In` (it plays its input, not its clips), `Auto` while armed (its clips play as well), never `Off`. For
each track, in the prologue:

1. Live notes it started that it no longer hears (its input or monitoring changed, or the transport
   stopped: `releaseLiveNotes_`) are released at the chunk's start (and recorded as note-offs).
2. Each due message its route `accepts`:
   - a note-on: recorded; if heard, a key it is already holding is released first (played again, it starts
     over), then the note-on goes to the track with a fresh id, and a `LiveNote` (`trackId`, `port`,
     `channel`, `key`, `noteId`) is remembered (at most 512 across all tracks; one that couldn't be
     released isn't played);
   - a note-off (or note-on with velocity 0): recorded; it goes to the track (with the live note's id) only
     if the track started that note;
   - a MIDI 2.0 per-note pitch bend: recorded; if heard, it goes to the live note the track holds on that
     port, channel and key as a `NoteBend` with that note's id (none held: nothing bends);
   - pressure, controllers, program changes and channel pitch bend go to the track as raw `Midi` events if
     heard (played, not recorded).

So each live note gets its note-off even if the track stops hearing its input, stops existing
(`forgetNotesOfRemovedTracks`), or the transport stops while the key is held.

### MIDI 2.0 input

MIDI 2.0 devices hand over Universal MIDI Packets ([Ump.h](../../engine/src/Ump.h)): one to four 32-bit
words, the first word's top four bits the message type. `Engine::sendUmp(device, words, hostTimeNs)` (what a
MIDI 2.0 backend will call, as `sendMidiInput` takes MIDI 1.0's bytes) decodes each whole packet with
`ump::decode` on the caller's thread and stamps it as `midiInput` stamps bytes (`queueMidiInput`):

| Packet | Becomes |
|---|---|
| MIDI 1.0 channel voice (type 2) | its bytes as they are |
| MIDI 2.0 note on / off (type 4) | a note-on / note-off; the 16-bit velocity scaled to 7 bits (a note-on at velocity 0 is a note in MIDI 2.0: velocity 1) |
| MIDI 2.0 poly pressure, control change, channel pressure | the MIDI 1.0 message, its 32-bit value scaled to 7 bits |
| MIDI 2.0 program change | the MIDI 1.0 message (its program; bank fields ignored) |
| MIDI 2.0 pitch bend | the MIDI 1.0 pitch bend, 32 bits into 14 |
| MIDI 2.0 per-note pitch bend | a `NoteBend` event: 0x80000000 none, either end ±48 semitones (`ump::bendSemitones`) |
| MIDI 2.0 per-note management with Reset | a `NoteBend` to none (the note's controllers reset) |
| anything else (utility, system, data, stream, registered and assignable controllers, per-note controllers) | nothing |

Groups are merged: a track's route takes the 16 channels of every group alike. Nothing on the audio thread
changes for MIDI 2.0 but the per-note bend (`routeMidiInput`).

## How it works: MIDI takes

`startRecording()` makes a `MidiRecordingTake` for each target without a file (the track must have a MIDI
input). The audio thread:

- stamps the take's `start` with the first segment's position and counts its `frames`, in `recordInput()`,
  alongside the audio takes; a jump interrupts the whole session ([recording.md](recording.md));
- in `routeMidiInput()`, sends what the track's route accepts, monitored or not, through `recordMidi()`:
  the timeline position where it played (the segment containing the event's offset). Nothing is recorded
  while the playhead stands (a count-in), or once the session is interrupted.

On the edit side, `MidiRecordingTake::collect()` pairs the events: a note-on starts a held note; a note-off
ends the earliest held note on its key and channel (one whose note-on came before the take began has none),
at least one sample long; a per-note bend (an event with `bend` set) adds a point (`RecordedBend`, from the
note's start) to the latest note held on its key and channel (none held: it is lost). Placement moves a
note, not its bend's times (they are the note's own). `RecordingSession::midiNotes()` moves them back by the *MIDI placement*:

```
midiPlacement = lag (output lag: delay compensation and the master's devices)
              + the device's output latency
              + midiInputDelay (the buffer MIDI waits for)
```

A MIDI message was played in response to what was heard when it arrived; the renderer meets it one MIDI
delay later, a block ahead of the output. So notes land where they were heard against the timeline. No note
starts before the take. The take itself spans the timeline where it recorded (its start isn't moved).

`recordingProgress()` returns the notes so far (held ones end at -1) for the live take. `finish()` returns a
`RecordedTake` with `midi` set: notes starting after the take's end are dropped, and a key still held when
recording stops (or a note running past the end) ends with the take. Lost events become an error message
("... MIDI events were lost while recording"). Nothing is written to disk: the bridge turns the notes into
a MIDI clip of what was recorded (replacing what was under it), in the same undo step as the audio takes,
and the session applies *Record Quantization* (`recordQuantize()` in
[app/src/audio/AudioSettings.h](../../app/src/audio/AudioSettings.h)) to their starts when it adds the takes
(`ProjectEditor::addRecordings`).

## A device's MIDI input from another track

`setProcessorMidiInput(processor, track)` gives a device that plays notes (`Processor::acceptsMidi()`:
the Synth, the Sampler, a VST3 plug-in with an event input) another track's notes instead of its own
track's; 0 gives it its own track's again. The engine keeps it with the device (`ProcessorEntry::
midiSource`, and its `MidiFeed`, made the first time), so it moves with it; `removeTrack` takes away
those from the track removed. Changing it resets the device (`requestReset()`), so no note it heard
hangs.

- It is no routing edge: notes are built in the prologue, before the graph, so a feed orders nothing and
  closes no cycle (two tracks may take each other's notes), and solo and mute don't stop it.
- `buildChainLocked()` puts each fed device's feed in its chain's `StripRender::midiFeeds` (by insert) and
  lists it in `RenderSnapshot::midiFeeds` with its source; the source is found in the snapshot's tracks by
  id at the end. A track left out of the snapshot (frozen, or in a frozen group) keeps its notes while
  something takes them (`fedTracks`).
- In the prologue, once every track's events are built, each feed gets a copy of its source's events
  (absolute offsets in the chunk). It must be a copy: the source's own devices rebase their events to
  their stretches in place, on whichever thread renders the source.
- `processDevice()` gives a fed device its feed's events instead of the strip's, split by the same
  slices (rebased in place: the copy is the device's own).
- The notes come on time, not delayed by latency before the device (as its own track's notes aren't
  either). A device switched off for a chunk doesn't consume its copy; it resets as it comes back on.

- Every note-on the renderer sends has a note-off: clips' notes through `activeNotes_`, live notes
  through `liveNotes_`, both released on stop, jump, track removal (forgotten with the instrument) and
  device resets.
- Every note-on from clips or MIDI input has an id of its own (counting up, never negative); its
  note-off and bends carry it. A bend never comes before its note-on, nor after its note-off.
- A note handed early has its id from when it is handed: its early note-on, its note-on in its time and
  its note-off all carry it. Only a chain's first MIDI effect asking for them (`earlyBeats()`) gets notes
  early.
- The audio thread never waits on MIDI producers: one mutex serialises the producers only.
- Offline renders never hear MIDI input or preview notes, and never record (`flags.live` is false); they
  start clean (`prepare()` empties every note list) and reset the shared devices before and after.
- All note and input buffers are sized in `Renderer::prepare()`; their counts say how much is in use.
- Opening a device prepares the renderer again, which forgets sounding, pending and live notes; the
  processors are prepared (silenced) along with it.

## Extending it

- **Another MIDI backend** (Windows MIDI Services, CoreMIDI on macOS): a file in
  `backends/` implementing [MidiDriver.h](../../engine/src/backends/MidiDriver.h), chosen in
  [engine/CMakeLists.txt](../../engine/CMakeLists.txt): list the inputs by their names, open one as an
  `InputConnection` that calls the handler with its port, the bytes (`shortMessageSize()` for APIs that hand over
  a byte stream) and `hostTimeNs()` taken on arrival, and stops calling once destroyed. `MidiInputDevices` does
  the rest.
- **A MIDI 2.0 backend** (Windows MIDI Services): a file in `backends/` that opens UMP endpoints and hands
  their words to the engine as `sendUmp()` does (`Engine::umpInput`, on the driver's thread). Decoding
  and everything after it are done.
- **Recording controllers** (sustain, channel pitch bend): `routeMidiInput()` would send them to the take as
  well (per-note bends go there already), and `RecordedNote` would need a form for them.
- **More per-note expressions** (MIDI 2.0's per-note controllers, VST3's other note expressions): a type
  of `ProcessEvent` beside `NoteBend`, sent along the note's curve the same way (`bendNotes`).
- **MPE for plug-ins without note expression**: notes spread over channels with channel pitch bend; it
  needs the plug-in's bend range.
- **More MIDI effects**: a built-in device of the `MidiEffect` category, as the Arpeggiator and the Groove
  device are: the renderer runs a chain's MIDI effects on its notes before its devices hear them
  ([devices.md](devices.md#midi-effects)); one that plays clips' notes before their time says how far with
  `earlyBeats()` ([Early notes](#early-notes)). MIDI effect plug-ins would need an event output in
  `Vst3Processor` and `isMidiEffect()` from the plug-in's category.
- **MIDI overdub** (recording into existing clips) is a model-side change: the engine already returns
  the notes.

## Gotchas

- A track with `In` monitoring and a MIDI input doesn't play its clips' notes; with `Auto` and armed it
  plays both.
- `previewNote` without a running device does nothing audible, and the note is discarded, not deferred.
- Messages sent with a host time far from the clock (over a second) are treated as stale; tests stamp
  against `audioClock()` to place them exactly.
- The live-note and active-note tables are shared by all tracks (512 each); a dense MIDI file on many
  tracks can fill them, and new notes are then not played rather than left hanging.
- `MidiRecordingTake` notes are paired by key and channel, not by port: two inputs on one track playing
  the same key and channel pair up with each other.
- A note's bend events are sent only while it sounds; bend points past its end shape the curve before it
  but are never reached. A note moved (or its key changed) while it sounds is no longer found: it keeps
  the bend it reached until its note-off.
- A device fed by another track hears that track's preview notes too (the piano roll's auditions).
- A note handed early that a locate or stop releases before its time gets its note-off though its note-on
  never came in its time: a device that didn't take it early gets a note-off for a note it never started.

## Tests

- [tests/engine/test_midi_alsa.cpp](../../tests/engine/test_midi_alsa.cpp): ALSA's sequencer (Linux): input names,
  events into messages, the sequencer's times into host times; then, where the kernel has a sequencer, a
  client of the test's own listed, its messages in (port, bytes, times in order), unplugged and plugged in
  again, and closed.

- [tests/engine/test_note_bends_engine.cpp](../../tests/engine/test_note_bends_engine.cpp): a bend's curve and
  vibratos (NoteBend.h), MIDI 2.0's packets decoded, the renderer's note ids and bends (sent along a note's
  own grid where it moves, from its note-on, chased, edited while it sounds, a vibrato as it swings, a slide
  landing on its last value) with a device of the test's own logging what it gets in snapshots made by hand,
  notes played by hand having ids, MIDI 2.0 per-note bends live (`Renderer::processLive` on a hand-made
  snapshot), recorded bends, and the Synth, the Sampler and SUB Test Synth (note expression) playing bent notes
  at their pitch, and the note after a bent one at its own (SUB Test Synth keeps a voice's last tuning, as
  Serum 2 can).
- [tests/engine/test_groove_engine.cpp](../../tests/engine/test_groove_engine.cpp): notes handed early to a Groove
  device, playing early to the sample across a chunk's end in chunks of several sizes, never twice, round the loop, to a
  Drum Rack's pad (its other chains hearing every note in its time, once), and dropped when stopping or locating
  before they sound.
- [tests/engine/test_midi_routing_engine.cpp](../../tests/engine/test_midi_routing_engine.cpp): devices taking
  another track's notes: an instrument, an effect with a MIDI input (SUB Test Note Effect, to the sample),
  in racks and after moves, from frozen tracks, the source going, what is refused, bit-identical on any
  number of threads.
- [tests/engine/test_midi_engine.cpp](../../tests/engine/test_midi_engine.cpp): notes start on their sample and
  follow the tempo, velocity sets the level, chords and repeated keys, offline renders leave no hanging notes,
  loop wraps release and retrigger notes, the instrument's output through the chain, notes without an
  instrument silent, the Synth's pitch, cutoff and parameters, preview notes needing a running device, and
  quick preview notes all ending.
- [tests/engine/test_midi_input.cpp](../../tests/engine/test_midi_input.cpp) (fake ASIO driver; skipped without
  the ASIO SDK): live notes reaching a test instrument at their offsets in a block, a note released as it is
  played still sounding, which tracks hear which input and channel, monitoring, held keys released when the
  transport stops or a track stops hearing them, input dropped without a running device, the device list,
  recorded notes landing on the beats a player heard them on (with latent plug-ins on a track or the master),
  MIDI and audio recorded together, and recording MIDI needing a MIDI input.
- [tests/engine/test_parallel_live.cpp](../../tests/engine/test_parallel_live.cpp): the MIDI input tests again live
  with workers (held, preview and recorded notes).
- In the application's tests: [test_ui_arrangement_tracks.cpp](../../tests/app/test_ui_arrangement_tracks.cpp)
  (the MIDI header controls), [test_editor_midi.cpp](../../tests/app/test_editor_midi.cpp) (a recorded MIDI take
  becoming a clip, record quantization), [test_ui_arrangement.cpp](../../tests/app/test_ui_arrangement.cpp) (a take
  drawn as it records), [test_ui_dialogs.cpp](../../tests/app/test_ui_dialogs.cpp) (the MIDI preferences),
  [test_ui_pianoroll.cpp](../../tests/app/test_ui_pianoroll.cpp) (the piano roll's notes being heard),
  [test_session_keyboard.cpp](../../tests/app/test_session_keyboard.cpp) (the computer keyboard playing) and
  [test_midi_model.cpp](../../tests/app/test_midi_model.cpp) (MIDI clips and notes in the model).
