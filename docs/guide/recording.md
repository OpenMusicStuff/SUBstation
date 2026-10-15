# Recording

Audio recording works as Ableton's Arrangement recording, from an ASIO driver's inputs;
MIDI tracks record along with audio ones, and any track can record another track's (or
the master's) output by resampling. This page covers arming, inputs, monitoring, the
count-in, takes, take lanes and comping, and resampling. On Linux there are no device inputs: audio records by
resampling, and MIDI from the computer MIDI keyboard.

## Arming

- An audio or MIDI track's header has an **arm** button (●). Arming one disarms the
  others unless Ctrl is held, and clicking a selected track's arms all the selected
  tracks.
- **Ctrl+Shift+A** (*Edit › Arm Selected Tracks*) arms the selected tracks and disarms the
  others; with all of them armed already, it disarms them.
- Arming a track with no input says so in the status line: choose one to record.
- Group tracks have no arm button: they record nothing.

## Inputs

A track's input is chosen in its header's In/Out column, as in Ableton: **Audio From**
(a MIDI track's **MIDI From**) and, under it, the channel (or where the source is taken).
An audio track's Audio From lists:

- *Ext. In*: the audio device's inputs, and under it the channel or pair (*1*, *2*, ...,
  then *1/2*, *3/4*, ...; their names show as tooltips). Choosing Ext. In takes the
  first pair;
- *Configure…*: the audio preferences, where the driver and its inputs are chosen;
- *Resampling* (the master's output) and every other track, group and return (see
  [Resampling](#resampling));
- *No Input*.

Choosing an input the driver hasn't open opens it (and keeps it open next time). With
WASAPI there are no device inputs: the channel menu says to choose an ASIO driver (see
[audio-setup.md](audio-setup.md)). On Linux (the *System* driver) there are none either,
and the menu just says so.

A MIDI track's MIDI From is *All Ins*, the *Computer Keyboard* or one MIDI input, or *No
Input*, and under it the channel (*All Channels*, *Ch. 1* to *Ch. 16*); see
[midi.md](midi.md#midi-input).

## Monitoring

Each track's **In**, **Auto** and **Off** buttons (under its input, the one chosen lit) choose:

| Mode | Audio track | MIDI track |
|---|---|---|
| *In* | Always hears its input, not its clips | Plays its input, not its clips |
| *Auto* | Hears its input while armed, unless playing back without recording | Plays its input while armed, its clips as well |
| *Off* | Never hears its input | Never plays its input |

A monitored track's input goes through its devices and mixer like any audio; it isn't
delayed to line up with latent plug-ins on other tracks, so you hear yourself with only
the latency of the track's own devices.

While a track records it plays none of its clips (nor a MIDI track its clips' notes):
its take replaces them. With monitoring *Off* it is silent until recording stops.

## Recording

- **Record** (the button next to Stop, or **F9**) records every armed track that has an
  input:
  - from the insert marker, after the **count-in** chosen in the metronome's drop-down ▾ (none, 1, 2 or 4
    bars: the metronome counts in even if it is off) if stopped;
  - or from the playhead while playing (punch in).
- Record again to stop recording and keep playing; Stop or Space stop both.
- With the loop on, recording goes round it as playback does: each time round is a pass,
  kept as a take of its own (see [Recording round the loop](#recording-round-the-loop)).

## Takes

- A take grows on its track as it records, right up to the playhead, with its waveform
  (which comes in a little behind, by the input's latency).
- When recording stops, the takes become clips in one undo step, replacing what was
  under them (overdub); undo takes them away again.
- They are WAV files (32-bit float) in the project's `Recordings` folder (for a project
  not saved yet, `Music\SUBstation\Recordings`).
- Takes land where they were played: the engine moves them back by the driver's input
  and output latency and the delay compensation's lag, so what you played along to
  lines up.
- Changing the audio device (or its sample rate, or a reset by the driver) ends a
  recording; what was recorded so far is kept. So does moving the playhead.

### Take lanes and comping

Every take is also kept on a **take lane** of its own below its track ("Take 1", "Take 2",
...), as in Ableton, so recording a part several times keeps every pass, and the track
plays the *comp*: the best parts of each, chosen from the lanes. Audio and MIDI tracks
alike.

- Right-click a track's header › **Show Take Lanes** to show them (it is
  greyed out until the track has a take). They show below the track, under its automation
  lanes; *Hide Take Lanes* (on a take lane's menu too) hides them again.
- On a take lane, what the comp plays shows lit; the rest is shaded.
- **Click** a take's clip on its lane to put all of it in the comp; **drag** across a lane
  to put just that stretch in (on the grid; Alt: off it), replacing whatever the track
  played there. Where the lane has nothing, that stretch of the track is left silent.
- A take lane's menu: *Comp Whole Take*, *Delete Take Lane*, *Hide Take Lanes*. The track
  header's *Delete Take Lanes* deletes them all; the track's clips stay as they are.
- Comping and deleting lanes are undo steps; recording makes the takes and their lanes in
  one. Lanes are saved with the project; duplicating a track duplicates them.
- The comp's clips are ordinary clips: move, trim, fade and edit them as any other.
- The File Manager lists the files the track's clips play, not those only a take lane
  plays.

### Recording round the loop

With the loop on, recording wraps at the loop's end and goes on from its start, as
playback does, until you stop it: play a part over and over, then choose the best.

- Each time round is a take of its own on its own take lane ("Take 1", "Take 2", ...;
  the first one from wherever recording began). Stopping just after the loop wrapped
  makes no take of that moment.
- The track plays the last time round (where you stopped part-way, the one before for the
  rest of the loop): show the take lanes to comp from the others.
- While recording, the take draws only the time round under way.
- Audio takes are one WAV file, each take a stretch of it; MIDI takes the notes that
  start in that time round (one held over the loop's end ends there).
- Moving the playhead still ends the recording; the loop's wrap doesn't.

### Punch In and Out

The **Punch In** and **Punch Out** buttons either side of Loop in the transport bar
restrict recording to the loop brace, as in Ableton, whether the loop is on or not:

- **Punch In**: start recording before the loop's start to play along to what comes
  before it: the armed tracks play their clips until the playhead reaches the loop's
  start, and only what is played from there is kept.
- **Punch Out**: the armed tracks play their clips again from the loop's end, and what
  is played after it isn't kept. With the loop on, recording goes round the loop before
  it gets there: Punch Out matters with the loop off.
- Both together: drop into the loop's stretch and out of it again, the rest of the
  track left as it was. With the loop on, every time round is a take (above), each just
  the loop's stretch.
- While recording, the take is drawn only inside the punch region.
- They are settings of the project (saved with it); turning them on or off is an undo
  step, as the loop's is. The punch region is the one when recording began.
- The audio file still holds everything recorded, the run-in too: the takes are the
  stretches of it inside the punch region.

### MIDI takes

- Recording records armed MIDI tracks along with audio ones: the notes show on the take
  as they are played, and become a MIDI clip of what was recorded (replacing what was
  under it), in the same undo step as the audio takes.
- Notes land where they were heard against the timeline: moved back by the audio output
  latency, the delay compensation's lag and the buffer MIDI waits for.
- A key still held when recording stops ends with the take.
- *Edit › Record Quantization* (none, 1/4 to 1/32, triplets) puts recorded notes' starts
  on that grid (lengths as played). With *Quantize Note Ends* checked in the same menu (as
  Ableton's *Quantize Note End*), their ends go on the grid too (a note whose end would land
  where it starts lasts a grid step). Both are kept with the preferences.
- A MIDI 2.0 controller's per-note pitch bends are recorded with their notes: each
  note's bend as it was played, drawn with as few points as keep its shape (to edit in
  the piano roll's bend mode: [midi.md](midi.md#pitch-bends-vibrato-and-slides)). MIDI 1.0's
  channel pitch bend is played, not recorded.

## Resampling

An audio track's Audio From also lists *Resampling* (the master's output) and every
other track, group and return: the track then takes that one's output as its input.
Under it, where it is taken, as in Ableton: *Post Mixer* (after its fader and pan, the
default), *Post FX* (after its devices, before its fader) or *Pre FX* (before its
devices; a MIDI track's after its instrument). The master's is taken as it is heard.

- Recording records it (in stereo) and the take lands exactly where it was heard, so it
  lines up with what it was recorded from; latent plug-ins on the source (or the
  master) change nothing.
- Monitored, the track hears its source instead of its clips, as it would a microphone
  (not delayed to line up with other tracks); never the master's, which would feed back
  into it.
- A source the track feeds (its own group, a return it sends to) is greyed out.
- Moving a track into the group it takes its input from, or deleting its source, leaves
  it with no input (in the same undo step).
- Resampling needs no audio inputs, so it records with WASAPI devices (and on Linux)
  too.

---

For developers: [../engine/recording.md](../engine/recording.md),
[../engine/midi.md](../engine/midi.md),
[../ui/arrangement.md](../ui/arrangement.md#track-headers) (the track headers).
