# Rendering

The `Renderer` turns a `RenderSnapshot` into audio, a chunk (at most 1024 frames) at a time. It
lives in [Renderer.h](../../engine/src/Renderer.h) and [Renderer.cpp](../../engine/src/Renderer.cpp);
the snapshot it reads is defined in [Snapshot.h](../../engine/src/Snapshot.h) and built by
[EngineSnapshot.cpp](../../engine/src/EngineSnapshot.cpp). One `Renderer` is driven by the device
callback (live); offline renders and exports make one of their own.

The header comment of `Renderer.h` is the authoritative summary; this page explains it with the
code. How edges and delay compensation are worked out is in [routing.md](routing.md), how the
graph is spread over threads in [scheduler.md](scheduler.md).

## Files

| File | What it holds |
|---|---|
| [Snapshot.h](../../engine/src/Snapshot.h) | The immutable render graph: `RenderSnapshot`, `TrackRender`, `StripRender`, `ChainRender`, `RackRender`, `EdgeRender`, `ClipRender`, `NoteRender`, `AutomationRender`, `InputEdge`; and the state kept outside snapshots: `TrackParams`, `TrackBuffers`, `EdgeState`, `DelayLine`. |
| [EngineSnapshot.cpp](../../engine/src/EngineSnapshot.cpp) | `Engine::rebuildSnapshotLocked()`, `buildChainLocked()`, `buildAutomationLocked()`. |
| [Renderer.h](../../engine/src/Renderer.h) / [.cpp](../../engine/src/Renderer.cpp) | `Renderer`: transport, chunks, tracks, strips, racks, faders, solo, notes, clips, metronome, preview, MIDI input, recording hand-off. |
| [Metronome.h](../../engine/src/Metronome.h) / [.cpp](../../engine/src/Metronome.cpp) | The click generator. |
| [EngineOffline.cpp](../../engine/src/EngineOffline.cpp) | Offline renders and exports (WAV; MP3 through [Mp3Writer.cpp](../../engine/src/Mp3Writer.cpp)). |

## The snapshot

`RenderSnapshot` holds everything a chunk needs, with every position in samples:

- `sampleRate`, the tempo map and time signatures (`tempoMap`, `meter`, never null; `tempoVersion`, a new
  number with each new map), the song's key (`keyTonic`, `keyMinor`: `Engine::setSongKey`) and the project's
  default groove (`groove`: `Engine::setGroove`, normalized; null for none). The tempo map, the key and the groove
  are handed to every device in its `ProcessContext` (`tempoMap`, `keyTonic`, `keyMinor`, `groove`).
  `sampleAt(beat)`, `beatAt(sample)`, `tempoAt(sample)` and
  `samplesBetween(beat, beats)` convert by the map; one tempo throughout converts as it always has (beats
  times samples a beat), to the bit.
- The loop (`loopEnabled`, `loopStart`, `loopEnd`): enabled only if the loop is at least 256
  samples long.
- `tracks`: `TrackRender`s in routing order, every track after those that feed it.
- `edges`: the routing graph's edges (`EdgeRender`), by source in snapshot order;
  `masterInputs`: the edges the master sums, in order.
- `graph`: the `TaskGraph` for the scheduler; `parallelWork`: how many tracks are worth a
  thread of their own (an enabled device, or clips to stretch or resample).
- `master`: the master's `StripRender`.
- `maxLatency` (how late the tracks reach the master) and `outputLatency()` (that plus the
  master's devices: how far the output lags the timeline).
- `warpVoices` (stretchers for the live renderer) and `chainDelays` (each rack chain's
  compensation, so offline renders can make lines of their own).

**Strips.** `StripRender` is what every strip has: `inserts` (its devices), their `automation`,
its mixer's `volume` and `pan` envelopes, `latency` (what its enabled devices add, plus the
delays before sidechained ones), per insert the sidechain edge into it (`sidechains`) and the
rack it is (`racks`), and the edges leaving after a device (`deviceTaps`). `TrackRender`
adds a track's id, its edges (`incoming`, `outgoing`, `inputCount`), `inputLatency`, its
`TrackBuffers`, input, MIDI route, monitor mode and arm state, its clips (`ClipRender`, sorted by
start, with `maxClipLength` to bound the search) and notes (a `NoteList` of `NoteRender`s, sorted by start then
key, shared by the snapshots built while they don't change). The master is a plain `StripRender`. A rack's chain is a `ChainRender`: a strip inside a
strip, with its id and its line-up delay.

**State outside the snapshot.** The edit side allocates these with their track or edge and
keeps them across snapshots, so a snapshot swap loses nothing:

| Type | Holds | Written by |
|---|---|---|
| `TrackParams` | gain, pan, mute, solo (atomics the API writes); peaks (the audio thread writes, the API reads and resets); the fader's `SmoothedValue`s | API / rendering thread |
| `TrackBuffers` | a track's signal for the chunk, its note events (up to 1024), its clips' notes handed early to its MIDI effects (`early`, up to 256), the stretch voices its clips play through (up to 32), monitoring, solo flags, `audible`, and its measured `cost` | the prologue, then the thread rendering the track |
| `EdgeState` | a send's level (atomic), the edge's own signal buffer, whether solo lets it through this chunk, the level and audible ramps | API / rendering threads |
| `DelayLine` | a stereo delay for one edge (or a sidechained device, or a rack chain); starts again from silence when the delay changes | rendering threads |

## A chunk

`processLive()` drains transport commands, preview notes and MIDI input, then splits the device
buffer into chunks of at most `kMaxBlock` (1024) frames and calls `renderChunk()` for each. After
each chunk it mixes the preview in, copies the master to the first two outputs (mixed to mono if
the device has one output; any others are silent) and feeds the scope ring. Last it publishes
the transport (`publishTransport()`).

`renderChunk()` has three parts:

```
prologue (serial, the rendering thread)
  1. segments: the chunk split where the loop wraps; count-in; metronome ticks scheduled
  2. ProcessContext for the chunk; the recording's device input (recordInput)
  3. per track, in snapshot order: monitored? recorded?, note events (preview, MIDI input, clips),
     the clips' notes due early (buildEarlyEvents), stretch voices for its clips -> its TrackBuffers
     then each device fed another track's notes: a copy of that track's events -> its MidiFeed
  4. workOutSolo: which edges and tracks are heard
graph (Scheduler: the rendering thread and the workers)
  renderTrack(t) for every track, each once all its incoming edges are done
epilogue (serial)
  5. master: sum masterInputs in order -> its strip (devices, fader)
  6. recordRendered: tracks' and master's outputs into the recording
  7. metronome clicks, after the master fader
```

Everything in the prologue touches state the tracks share (sounding notes, MIDI input, the
recording, the stretch voice pools), so it runs serially. After it, a track's render depends only
on its own `TrackBuffers`, its incoming edges and its own devices, which is what lets the graph
run on any thread with bit-identical results (see [scheduler.md](scheduler.md)).

### Segments and the loop

While playing, the chunk is split into `Segment`s: continuous stretches of the timeline. A
segment ends where the playhead reaches `loopEnd` (it then goes back to `loopStart`), up to 16
segments per chunk. Each segment records:

- `jump`: the playhead didn't continue from where the last one ended (a locate, a loop wrap):
  sounding notes stop;
- `chase`: playback starts here, so notes already underway sound from here.

- `wrap`: the jump is the loop's wrap (from `from`, the loop's end): recording goes on through it
  (in recording time, `linear`: [recording.md](recording.md#recording-audio-thread)).

Stopped, there are no segments: devices see one stretch at the playhead.

Processors see continuous stretches of the timeline: `processInserts()` turns the segments into
`Slices` (plus one for the end of a count-in) and calls each device once per slice, with the
`ProcessContext` moved to the slice's position and its events. So a block the loop wraps in is
processed in two parts, and tempo-synced plug-ins stay in time. A device processes the whole
chunk (slice by slice) before the next device starts, so a rack can run its chains over the
whole chunk too.

`ProcessContext` carries the sample rate, sample and beat position, the tempo and time signature
there and where its bar starts (`barStartBeat`: bars needn't be alike from beat 0; `barStart()`,
`gridOffset()` for devices with a bar grid), set for each slice by `placeContext()`, playing,
looping (recording too), the loop in beats, whether the render is offline, the song's tempo map, key
and default groove, the slice's events, and for a chain's first MIDI effect that asks for them the
clips' notes handed early (`earlyEvents`, in the first slice only). See [devices.md](devices.md) for
how processors use it.

### Transport in the renderer

Only one thread drives a renderer at a time: the audio thread while a device runs, otherwise the
engine under its mutex (`serviceTransportIfIdleLocked()`). `applyCommand()` handles Play (a
count-in only if stopped: at the tempo and in the time signature where playback starts), Stop
(cancels a count-in) and Locate (in beats, by the tempo map). `syncTempo()` notices a new tempo
map (`tempoVersion`) or sample rate and moves the playhead, the expected position and the ends of
sounding notes back to their beats, so they stay on the same beat; a tempo change keeps clips
playing without a re-seek ([warp.md](warp.md)). Their beats are taken by the map before
(`rememberBeats()`, after every chunk and locate): the renderer never reads a map but the
snapshot's it renders, as the one before may be gone.

**Pre-roll.** Play while stopped arms `preRollLeft_` (150 ms, `kPreRollSeconds`). Live, once a
count-in is over, the playhead waits chunk by chunk (`waitsForStreams()`) until every page the
snapshot's streamed clips play from over the next half second is in memory (`streamsReady()`), or
the 150 ms are up: playback started where the disk hasn't been read yet comes in whole when it
can, and late (counted) when it can't ([streaming.md](streaming.md#moving-the-playhead)). Without
streamed sources nothing waits.

## Rendering a track

`renderTrack()` (on whichever thread the scheduler picks, with that thread's `WorkerScratch`):

1. Clears the track's buffer, then sums its incoming edges in `incoming` order, each at its
   level (`sumEdge()`). Only edges that sum (outputs and sends) are added: a bus (a group, a
   return) hears what goes into it in a fixed order, whichever thread finished first.
2. Its own audio: if monitored, its input (the device's channels, or another track's output on
   its input edge); otherwise its clips over each segment (`renderClips()`), unless it is being
   recorded (`TrackBuffers::recorded`: its take replaces them, so it plays none).
3. Its devices (`processInserts()`), with their note events (a device fed another track's notes
   gets its `MidiFeed` instead), sidechains and automation. Taps
   after a device copy the signal into their edge's buffer as the chain goes along.
4. Pre-fader taps copy the signal into their edges' own buffers.
5. The fader (`applyFader()`): volume and pan, mute and solo, metering.
6. The edges that need a signal of their own (`EdgeRender::ownSignal()`: a tap before the
   fader, or a signal delayed for this edge alone): a pre-fader send is multiplied by the fader's
   mute ramp (a sidechain isn't: it isn't heard); a post-fader edge is a copy. Each is delayed by
   its compensation (`compensationFor()`: none while the track is monitored).
7. Its cost: the time from step 1 to here per frame, smoothed (`kCostSmoothing` = 0.1, roughly
   the last ten chunks), into `TrackBuffers::cost`.

An edge without a signal of its own (a post-fader output not delayed) is read straight from the
source's buffer by its destination (`edgeSignal()`).

### Clips

`renderClips()` finds the clips playing in a segment with a binary search bounded by
`maxClipLength`. Each clip plays one of three ways (`ClipRender::Playback`):

- `Direct`: source frames one to one (unwarped, or warped at its own tempo without
  transposing): bit-exact.
- `Resample`: Re-Pitch, speed and pitch together (`renderResampled()`).
- `Stretch`: live, from its whole render (`ClipRender::rendered`, made in the background by the
  `WarpCache`) once that is ready, read in place like `Direct`, except while the voice already
  playing it carries on (a render takes over where the voice would start again: a start, a locate,
  a loop's jump; a voice taking over from a render mid-way, after a tempo change, crossfades from it).
  Otherwise, and in offline renders, through the stretch voice the prologue gave it
  (`assignVoices()`, `acquireVoice()`): the voice that played the clip last if it is still free,
  else the one idle longest. More stretched clips at once than voices: the extra ones stay silent.

Then gain, the clip's fades (`ClipRender::fadeIn` / `fadeOut`, samples from each end, each
shaped by its curve with `automationShape()`), and the clip's pan as balance gains. Details of
warping and the voices are in [warp.md](warp.md).

A resident source (or render) is read in place. A streamed one ([streaming.md](streaming.md)) is
fetched through its `SourceCursor` into the worker's scratch (`warpLeft`/`warpRight`; Re-Pitch's
span into `span`), as the chunk's `StreamReads` say: live, never waiting (a page not in memory
plays as silence, faded at its edges, and is counted; for `kJumpGrace` after the playhead jumps
while playing, as expected); offline, waiting for the disk.

The snapshot works out each clip's fades: its own (`ClipDesc::fadeInSec` / `fadeOutSec`, in source
seconds, so on the timeline they follow the clip's speed; shortened in proportion where together
they would be longer than it), or, at an edge without one, a short linear fade against clicks
(`setClipFadeMs`, 4 ms by default, 0 to 100 ms, at most half the clip, giving way to the other
edge's own fade), but only where the edge cuts into the file: none where the clip starts at the
file's first frame or plays to its last (to a frame or two), so a one-shot's attack is heard as it
is.

### Notes

`buildNoteEvents()` (in the prologue) builds a track's events: preview notes first (at the block
start, in the order they were played), then its MIDI input (`routeMidiInput()`), then its clips'
notes: note-ons for notes starting in each segment, chased notes where playback starts, and
note-offs from the renderer's own record of sounding notes (`activeNotes_`, up to 512), not from
the snapshot, so a note edited or deleted while it sounds still ends. Stopping, a jump and a
removed track release notes. A bent note's bends (MIDI 2.0's per-note pitch bend) follow its
note-on, one every 32 samples at most while its pitch moves. Events are sorted by offset, a
note-off first at the same offset and a note's bends after its note-on. See [midi.md](midi.md).

A chain's first MIDI effect that may play notes early (`Processor::earlyBeats()`: the Groove device)
gets the clips' notes due within that many beats after the chunk, early: `buildEarlyEvents()`, after
the track's events, puts them in `TrackBuffers::early` (round the loop once at most), each with the
id it will come with in its time, and `processMidiEffects()` hands them over
(`ProcessContext::earlyEvents`; a Drum Rack's pad, those of its key: `padEarly()`). See
[midi.md](midi.md#early-notes).

A device can take another track's notes instead of its own track's
(`Engine::setProcessorMidiInput()`): once every track's events are built, the prologue copies
the source track's into the device's own `MidiFeed` (`RenderSnapshot::midiFeeds`, allocated
when the input is set), so the graph never reads another track's `TrackBuffers`. It is not an
edge: it orders nothing and can't close a cycle. See
[midi.md](midi.md#a-devices-midi-input-from-another-track).

## Strips, chains and racks

`processInserts()` first takes the devices' reset requests (`takeResets()`; a rack asked to reset
passes it on to its devices). If no device is switched on, only the device taps are filled.
Otherwise it builds the slices and calls `processChain()`:

```
MIDI effects of the chain, in order: each plays the notes the last one played (processMidiEffects);
    the first, if it asks (earlyBeats()), also gets the clips' notes handed early
for each insert i of the chain:
    MIDI effect or modulator?  run before (the audio goes past it)
    rack?    processRack(...)
    device?  sidechain edge into it?  -> setSidechainConnected; its own signal waits (deviceDelay)
             for each slice: automate its lanes, process(), clearAutomation()
    taps after device i -> their edges' buffers
```

A device switched off is skipped (and adds no latency: the snapshot leaves it out of delay
compensation). A sidechain's key signal is handed over with `setSidechain()` before each
`process()` call, and only if solo lets the sidechain edge through. While a track is monitored
its sidechained devices don't wait for their sidechains: a player hears only the devices' own
latency.

`processRack()` runs each chain from a copy of the rack's input, in the scratch of its depth
(`WorkerScratch::racks`, one per depth up to `kMaxRackDepth` = 8): the chain's devices (never as
monitored: a chain skipping its waits would play early against chains lined up to it), its
fader (muted, or left out when another chain of the rack is soloed; solo is read once per rack
so its chains agree), then its line-up delay (`ChainRender::compensation`). The rack puts out the
sum. An empty rack passes its input on. Every chain's devices get the strip's note events (as the
strip's MIDI effects played them; a chain's own MIDI effects then play them for that chain), which
is what layers instruments in an Instrument Rack; a Drum Rack's pad gets its key's, as C3 (`padNotes()`), and
of the notes handed early, its key's too (`padEarly()`). MIDI effects are in
[devices.md](devices.md#midi-effects). Rack routing and latency are in
[routing.md](routing.md#racks).

## Faders

`applyFader()` is shared by tracks, the master and rack chains:

- Volume and pan come from `TrackParams` (gain, and pan as balance gains), or, where automated,
  from the envelope sample by sample (`fillLane()`, then `automationVolumeGain()` and
  `automationPan()`). See [automation.md](automation.md).
- `audible` (not muted, and solo lets the strip through) is a 0/1 target.
- Live renders ramp all four (audible, volume, left and right pan gains) with `SmoothedValue`
  over 20 ms. The smoothers live in `TrackParams`, so they survive snapshot swaps, and are reset
  when the sample rate changes. Where automation stops, or is overridden, the ramp carries on from
  the envelope's last value.
- Live renders meter: the peak after the fader goes into `peakLeft`/`peakRight` with
  `atomicStoreMax`; `takeMeters()` reads and clears them (master, tracks, then rack chains).
- Offline renders hold the engine lock, so nothing can change mid-render: values are applied
  directly, without ramps or meters.
- `audibleOut` hands the mute ramp to the caller, for muting pre-fader sends with the track.

`sumEdge()` does the same for a send's level where its destination sums it: the `EdgeState`'s
level and audible ramps (20 ms), or the send's automation sample by sample.

## Solo and mute

`workOutSolo()` runs once per chunk in the prologue, reading each solo button once so the chunk
sees one consistent state. Mute and solo follow the routing:

- A **muted** track is silent on every edge, its pre-fader sends too.
- **Solo works on edges.** An edge plays if what it leaves is downstream of a solo (soloed, or
  fed by something soloed: `soloDown`), or if where it goes is upstream of one (soloed, or
  feeding something soloed: `soloUp`). So a soloed track is heard through its groups and the
  returns it sends to ("solo in place"), and what goes into a soloed group or return keeps going
  into it. A track none of whose edges play is silent (`TrackBuffers::audible`).
- `soloDown` is worked out forwards over the snapshot order (sources come first), `soloUp`
  backwards.
- An **input edge** counts only while its track is monitored (otherwise it only feeds a
  recording).
- A **sidechain** isn't heard: solo doesn't go down it (soloing what keys a strip doesn't make
  the strip heard) but goes up it (what keys a heard strip keeps keying it, even unheard), and one
  into the master's devices always plays. Mute and solo silence it only after the source's fader,
  so a muted source still keys from a pre-fader or after-device tap.
- The master never mutes or solos.

How this looks to the user is in [guide/mixing.md](../guide/mixing.md).

## Metronome and count-in

`Metronome` ([Metronome.cpp](../../engine/src/Metronome.cpp)) synthesises two clicks in
`prepare()`: 35 ms sine bursts with a 1 ms attack and an exponential decay, 1568 Hz at 0.6 for
the accented one and 1046.5 Hz at 0.45 for the others. `start()` replaces the click sounding;
`renderUntil()` adds it into a range of the buffer.

Ticks fall on every beat of the time signature's denominator (a quarter-note tick at 4/4, an
eighth at 6/8), the first of each bar accented: on each bar of each time signature where the
tempo map puts it (`scheduleTicks()`; one grid from sample 0 when tempo and time signature are
the same throughout, and for the count-in: `scheduleGridTicks()`). `scheduleTicks()` (while playing with the
metronome on) and `scheduleCountIn()` put them into a ring of pending ticks (256), stamped in
output samples, **delayed by the snapshot's `outputLatency()`**: the click is heard when the
tracks' audio for that position is, after delay compensation and the master's devices.
`renderTicks()` takes those due in the chunk, and the epilogue renders them after the master
fader (so the master's volume doesn't change the click, and a click may ring on into the next
chunk).

**Count-in.** `play(countInBeats)` while stopped sets `countIn_`: for that many samples the
playhead waits and only count-in ticks are scheduled, even if the metronome is off; the rest of
the chunk then plays from the playhead (the first slice of the chunk stands still at the start
position). `isCountingIn()` reads `SharedState::countingIn`. Recording starts after the count-in
([recording.md](recording.md)).

## Browser preview

`Engine::preview(path)` plays a source that is already loaded at the engine's rate straight into
the master output. The handover is lock-free: the engine stores the source pointer and bumps
`previewSerial`; `mixPreview()`, after each chunk, notices the new serial, starts the source from
its beginning, and adds it at `previewGain` (0.8) after the master fader and the metronome (a
streamed source fetched through its cursor; its position published in `previewPosition` for the
disk threads to read ahead of).
When it reaches the end it clears `previewActive` (unless a newer preview started). The engine
keeps the source alive in `previewHold_` and retires the old one through the epoch pool. Offline
renders never play the preview. Opening a device drops it.

## Input and recording hand-off

A monitored track (`isMonitored()`: live only, an input it can hear, and `In`, or `Auto` while
armed and not playing back without recording) plays its input through its strip like any audio,
without delay compensation on its edges. While recording, `recordInput()` (prologue) starts the
takes, ends them if the playhead jumped, and pushes the device's input over the chunk's segments;
`recordRendered()` (epilogue) pushes the tracks' outputs (after the fader, before any edge's
delay) and the master's (after its fader, before the metronome) over the same segments. See
[recording.md](recording.md) and [midi.md](midi.md) for MIDI input.

## Offline renders

`Engine::renderOffline()` and `exportWav()` use a separate `Renderer` (see
[README.md](README.md#offline-renders)); exports and freezes run on a thread of their own
(`RenderJob`, see [README.md](README.md#in-the-background)), rendering the snapshot they
started with. Differences from live:

- `ChunkFlags::live` is false: no smoothing, no meters, no monitoring, no MIDI input, no preview.
- Looping and the metronome are opt-in (`renderOffline(startBeat, frames, loop, metronome)`); exports leave
  them off.
- `setWarpVoices()` and `setDelayLines()` give it fresh stretch voices and delay lines (one per
  snapshot edge, per sidechained device, per rack chain), sized from the snapshot.
- It renders `outputLatency()` samples first and drops them.
- Processors are shared with live playback, so they are reset before and after.
- The scheduler is the live one (live output is suspended meanwhile), so offline renders use the
  same threads, and are bit-identical whatever their number.

**One track** (freezing it): `Engine::renderTrackOffline()` and `renderTrackToWav()` render the
graph the same way, but put out one track's signal after its devices, before its fader
(`Renderer::setCaptures()`, then `renderCaptures()`: the track is copied out of `renderTrack()` as it
leaves `processInserts()`). Solo is ignored (`ignoreSolo_`), so a group's bus is heard as if nothing
were soloed.

Only what the track needs renders: the track and everything upstream of it (`Renderer::upstream()`:
through every edge into it, however far back: what goes into a group or a return, sidechains, input
edges), in a graph of their own for the scheduler (`prunedGraph_`, whose nodes are those tracks in
snapshot order, `prunedTracks_`; `prunedWork_` counts those worth a thread as `parallelWork` does),
and not the master's strip, nor the metronome after it. The rest of the chunk is as it was: the
prologue still works out every track's notes, stretch voices and solo, so the state the tracks
share (sounding notes, which voice plays which clip) is the same, and a track's render depends only
on its own state and what comes in on its edges. A device taking another track's notes
(`MidiFeed`) needs nothing rendered: the prologue copies them. So the track comes out the same,
to the bit, as rendering everything makes; `Engine::setPruneTrackRenders(false)` renders
everything instead, for the tests that compare the two. The signal lags the timeline by the track's `inputLatency` plus its devices'
latency, so that much is rendered first and dropped. `renderTrackToWav()` writes 32-bit float
(nothing above full scale is clipped), and renders on past the end for up to `tailSeconds`,
cut where it falls below -100 dB. `startTrackRender()` is the same in the background (freezing
from the UI).

**Several tracks** (freezing them at once: `startTracksRender()`, `renderTracksToWav()`; a track or a
file named twice is refused) render in one pass: `setCaptures()` takes them all, renders the union of what feeds each, and
`renderCaptures()` puts out each track's signal into a buffer of its own. Each has its own
`TrackCapture`: its own lag dropped (the job renders the longest first), its own range, tail and
silence; a track whose tail is done is taken out (`setCaptures()` again with those left, between two
pieces), so what only it needed renders no more. Each file is the one rendering its track alone
writes, to the bit: what a track's render takes depends only on what feeds it.

Each `TrackCapture` keeps what it writes, planar, and once the file is finished makes the
`AudioSource` decoding the file would (`AudioSource::fromSamples()`: the same samples, the 32-bit
float file holds them exactly, and the same peaks) on the job's thread: `RenderJob::sources()`. The
application plays a frozen track from it at once, without reading the file back.

**The tail** (`TrackCapture`, [EngineOffline.cpp](../../engine/src/EngineOffline.cpp)) is kept back
as it renders and written up to its last frame above -100 dB. The render stops before the end of
`tailSeconds` once the track has been silent (both channels under -100 dB) for long enough
(`quietWindow()`), and nothing feeding it sounds later: no clip or note on it or upstream of it plays
on past that point, nor one of a track whose notes a device on them takes (a `MidiFeed`, which has no
edge) (`lastSounding()`, the tail's end at most). Long enough is a second
(`kQuietSeconds`) plus, added up, the latency and tail (`Processor::tailSamples()`) of every
device on the track and on the tracks feeding it, rack chains' too: a device rings on no longer than
its tail after its input falls silent, so a silence that long within it (between a delay's echoes,
which `Delay` counts in its tail) can't be followed by more of it, and a chain of them by no longer
than their sum. Where a device could sound again after any silence the whole tail is rendered: a MIDI
effect (an arpeggiator holding its notes plays on past the arrangement), or a plug-in declaring no
tail (many do, delays among them). So the file is the same as rendering the whole tail and cutting
it would make, to the bit, whenever the tail really dies away; it differs only for a device that
sounds again after falling silent for longer than it declares (a generator gated by a slow LFO), whose
later sound is left out.

## Invariants and real-time rules

- The renderer never allocates after `prepare()`/`setScheduler()`: all buffers (scratch per
  thread, notes, MIDI input, ticks) are sized there; every count is checked against its capacity
  and the excess dropped (a note not played, a click not scheduled) rather than grown.
- A snapshot is read-only for the renderer. What it writes lives in `TrackBuffers`, `EdgeState`,
  `TrackParams`, `DelayLine`, the voices and the processors.
- During the graph a track writes only its own `TrackBuffers`, its outgoing edges' `EdgeState`s
  and its own devices, and reads only its incoming edges' sources, which are done.
- Anything shared between tracks happens in the prologue or epilogue.

## Extending it

- **A per-chunk thing every track needs** (a new kind of event source): work it out in the
  prologue, into `TrackBuffers`, never during the graph.
- **A new strip feature** (a meter tap, an insert slot): add it to `StripRender`, fill it in
  `buildChainLocked()`/`rebuildSnapshotLocked()`, and handle it in `processChain()` or
  `renderTrack()`. Keep its state outside the snapshot if it must survive swaps.
- **More master processing** belongs in the epilogue, before `recordRendered()` if resampling the
  master should hear it.

## Gotchas

- `processStrip()` (the master) calls `processInserts()` with `monitored` false and no events;
  tracks go through `renderTrack()` instead.
- `takeResets()` returning false means "no device switched on": the chain is skipped entirely,
  but its device taps are still filled.
- The loop needs at least 256 samples, and at most 15 wraps fit in a chunk.
- The metronome's delay is the snapshot's `outputLatency()` when the tick is scheduled; a latency
  change moves later ticks only.

## Tests

In the engine's tests, [tests/engine](../../tests/engine):

- [test_engine_render.cpp](../../tests/engine/test_engine_render.cpp): sample-exact clip placement, tempo,
  offsets, gain and pan, mute and solo, loop wraps, tempo changes keeping the playhead on the
  beat, transport state without a device, metronome clicks, clip fades, export matching a render.
- [test_render_jobs.cpp](../../tests/engine/test_render_jobs.cpp): renders in the background.
- [test_freeze_engine.cpp](../../tests/engine/test_freeze_engine.cpp): a track's render, lined up, its
  tail (the same as cutting the whole tail), the same rendering only what feeds it as rendering
  everything (on a graph with groups, returns, sidechains, MIDI from another track, an input edge,
  racks, stretched clips); frozen tracks.
- [test_groups_engine.cpp](../../tests/engine/test_groups_engine.cpp),
  [test_sends_engine.cpp](../../tests/engine/test_sends_engine.cpp): solo and mute across levels and sends,
  meters of groups and returns.
- [test_racks_engine.cpp](../../tests/engine/test_racks_engine.cpp): chains summing, chain faders, mute and
  solo, empty racks, chain meters.
- [test_parallel_engine.cpp](../../tests/engine/test_parallel_engine.cpp),
  [test_parallel_live.cpp](../../tests/engine/test_parallel_live.cpp): the same renders on any number of
  threads, live too.
- [test_midi_engine.cpp](../../tests/engine/test_midi_engine.cpp), [test_warp.cpp](../../tests/engine/test_warp.cpp),
  [test_recording.cpp](../../tests/engine/test_recording.cpp): notes, warped clips, monitoring and the
  count-in.
- [test_groove_engine.cpp](../../tests/engine/test_groove_engine.cpp): notes handed early to a chain's first MIDI
  effect, across chunks, round the loop and to a Drum Rack's pad.
