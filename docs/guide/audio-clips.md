# Audio clips

Double-click an audio clip to open it in the clip view, which covers the arrangement:
clip controls on the left, large waveforms on the right. Here you warp clips, change
their pitch, gain and pan, and give them a groove. Audio added from a file is set up from its name. MIDI clips
open in the piano roll instead (see [midi.md](midi.md)).

## The clip view

- It edits one clip or many at once: select several audio clips and open them, and
  each gets its own waveform, laid out on the song's bars and beats where it plays (see
  [Several clips](#several-clips)).
- Knobs move all the open clips by the same amount (transposing up 2 semitones
  transposes each clip by 2, whatever it was at); switches and the warp mode set the
  same value on all of them. Where clips differ, the value's tooltip says so.
- **Shift+Tab** shows or hides the clip view; **Esc** (or the × button) goes back to
  the arrangement.
- Selected a stretch with audio and MIDI clips in it? **Shift+Tab** opens the kind of the
  first one (top track first), and the **Audio** and **MIDI** buttons at the top left
  (each with how many there are) switch between them: the audio clips' settings and
  waveforms, or the MIDI clips in the piano roll. The choice stays while you reopen the
  same clips.
- Click a knob and type a value, as in Ableton (Enter sets it). **Up** and **Down** then
  turn it a step: Transpose a semitone (**Shift**: an octave), Detune a cent (ten), Gain
  half a dB (6 dB). Pan takes "25L", "C" or "40R".

Its controls come in four sections:

| Section | Controls |
|---|---|
| Warp | **Warp** switch, warp mode, **Seg. BPM**, :2 and ×2 |
| Pitch | **Transpose** (±48 semitones), **Detune** (±50 cents) |
| Mix | Clip **Gain** (−70 to +24 dB), clip **Pan** |
| Groove | **Groove**, **Swing** and **Step**, **Timing**, **Random**, **Quantize**; **Apply**, **Extract** (see [Groove](#groove)) |

A clip's gain changes how loud it plays, and its waveform with it (in the arrangement
and in the clip view): louder clips draw taller, cut off at the lane's edges. Its fades
show in the clip view too, as curves over the part it plays (they are made in the
arrangement: [arrangement.md](arrangement.md#clips)).

### Waveforms, the playhead and zooming

Each clip's waveform shows its whole file at first, the part it plays lit (between the
**S** and **E** flags): an unwarped clip's in seconds, with a ruler of seconds, a warped
clip's in beats (see [Warp markers](#warp-markers)).

- While the song plays inside a clip, the **playhead** moves along its waveform, at the
  moment of the file playing; the **start marker** (where playback starts) shows as a
  line too when it is inside the clip.
- **Click the ruler** (one clip open) to play from that moment of the file (before or after
  the part the clip plays: from its start or its end).
- **Ctrl+wheel** zooms in and out around the mouse; the **wheel** (or **Shift+wheel**)
  scrolls sideways; **Ctrl+Alt drag** moves it; dragging in the ruler scrolls (sideways)
  or zooms (up and down), as the arrangement's ruler does. A scroll bar shows under the
  waveforms while zoomed in. **+** and **−** zoom in and out, and **Z** shows the whole
  file again.

### Several clips

Opened together, clips share one axis, the song's bars and beats, each in a band of its
own where it plays in the arrangement (the parts of its file before and after it dimmed),
under a ruler of bars and beats.

- Zoom and scroll as with one clip: **Ctrl+wheel**, **Shift+wheel**, **Ctrl+Alt drag**, the
  ruler and the scroll bar under them. **Z** shows the stretch they play.
- When the bands don't fit, the **wheel** scrolls them up and down (as in the arrangement),
  with a scroll bar beside them; **Shift+wheel** still scrolls sideways.
- **Click the ruler** to play from that beat of the song.
- **Click a clip** to select it and its track, and show it alone.

## Warping

**Warp** locks a clip to the beat grid. Its audio is taken to be at the *Seg. BPM*
(segment BPM) and is stretched in real time to follow the project tempo: a warped clip
plays at project tempo ÷ segment BPM speed. Turning Warp on sets Seg. BPM to the
current tempo, so nothing moves until the tempo changes.

The **:2** and **×2** buttons halve or double each clip's segment BPM (so warped clips
play twice as fast, or half as fast). **Alt**-dragging a clip's edge in the arrangement
stretches it to any length, warping it if it isn't (see
[arrangement.md](arrangement.md#clips)).

### Warp markers

Warp markers pin moments of a warped clip's audio to beats, so a recording that drifts,
or a loop played with feel, can be lined up with the grid. They stand on the waveform as
small tabs. With one warped clip open:

- **Double-click** the waveform to add a marker there. It goes on the beat that moment
  plays on now, so nothing moves yet. The first one also adds markers at the clip's start
  and end.
- **Drag** a marker sideways to move it to another beat, as in Live: its moment of the
  audio comes with it, and the waveform squeezes on one side and stretches on the other.
  It snaps to the grid's lines near it; hold **Alt** to place it freely. **Shift-drag** a
  marker to move the waveform under it instead: the marker stays on its beat and the audio
  slides beneath it. Either way a marker stops where a stretch would play slower than
  20 BPM or faster than 999 BPM, and the clip stays where it is and as long.
- **Double-click** a marker to remove it.
- A warped clip is shown **in beats**, as Live shows it: the ruler counts the song's bars
  and beats, the grid under the waveform is even, and each stretch of the waveform is drawn
  as fast or slow as it plays, so you see the warp as you make it. In the arrangement the
  clip's waveform follows its markers too.
- **Mark Transients** (under :2 and ×2) puts a marker on every transient the clip plays:
  the attacks of its hits and notes, shown as short ticks along the waveform's top. Drag
  the ones that are early or late onto the beat. A marker added by double-clicking close
  to a tick goes exactly on it.

Between two markers the audio plays at the tempo that joins them; *Seg. BPM* shows the
first stretch's tempo, and changing it, :2, ×2 or Alt-stretching the clip speed up or slow
down every stretch alike. A frozen track's clip follows the tempo it was rendered under
and takes no markers. Ableton Live sets keep their warp markers when imported, unless a
stretch between two would play slower than 20 BPM or faster than 999 BPM: such a clip
is cut into a clip per stretch instead.

## Groove

An audio clip can play in a groove ([grooves.md](grooves.md)): its hits (the transients *Mark
Transients* finds) move to where the groove puts them, and the audio between them stretches to
fit, through the clip's warp. A loop played straight can swing; with **Quantize** at 100 %, a
loosely played one is put on the grid first, then takes the groove.

- **Groove**, in the clip view's Groove section: *None*, *Swing*, *Project* (the project's
  default groove), or one of the project's grooves. With Swing, **Swing** (50 to 75 %; 58 % to
  start with) and **Step** (1/8 to 1/32).
- **Timing**, **Random** and **Quantize**: how much of it plays
  ([grooves.md](grooves.md#how-a-groove-moves-notes)). A groove's velocities aren't applied to
  audio.
- The clip must be warped: giving an unwarped clip a groove warps it (at the project's tempo,
  as turning Warp on does). A frozen track's clips can't take one.
- The clip's own warp markers stay as they are: the groove plays on top of them, and the
  clip's start stays where it is. A hit the groove would move so far that a stretch would play
  slower than 20 BPM or faster than 999 BPM isn't moved on its own: it goes along with the audio
  around it.
- Until the clip's file is loaded (and its transients found), it plays as it is.
- **Apply** writes the groove into the clip's warp markers (a marker on each hit, where the
  groove put it) and clears it; drag them then as any marker. **Extract** makes a groove of
  the clip's feel (its hits against the 16th grid, over a bar, as loud as they are) in the
  project's grooves.

With several clips open, every one takes it (the controls show the first one's). *Apply
Groove* and *Extract Groove* are in a clip's right-click menu in the arrangement too.

## Fades

Hold **F** in the arrangement to drag an audio clip's fades by the squares at its top
corners, and bend them by the dots on their curves; see [arrangement.md](arrangement.md#clips).
A fade is measured in the clip's audio, so it stretches with the clip. Edges without a
fade of their own get a 4 ms fade against clicks only where they cut into the file, never
at its own start or end: a drum hit plays with its full attack.

### Warp modes

| Mode | For |
|---|---|
| *Transients* | Short stretch blocks, tight attacks: drums and other percussive material |
| *Standard* | All-round: melodies, bass lines and full mixes |
| *Smooth* | Long blocks, for pads, ambience and noisy sounds; softens attacks |
| *Formants* | Standard, keeping formants (vocal character) when transposing |
| *Re-Pitch* | No stretching: speed and pitch change together, like a turntable |

Projects saved with the earlier Ableton-style names load into the equivalent mode.

## Reversing

**R** (or *Reverse* in a clip's right-click menu) reverses the selected audio clips: each
plays its stretch of audio backwards, in the same place. If the selection covers just
part of a clip, that part is split off and reversed. As in Ableton, a reversed clip
plays a reversed copy of its file (`name R.wav`), made the first time and kept in the
project's `Reversed` folder (for a project not saved yet,
`Music\SUBstation\Recordings\Reversed`); reopening the project uses it again. Reversing
it again plays its own file again. Reversing long recordings (more than half a minute of
audio) shows its progress, with **Cancel**. Reversing is one undo step; MIDI clips aren't
reversed.

## Transpose and detune

**Transpose/Detune** shift the pitch without changing the speed, warped or not (except
in Re-Pitch, where the pitch follows the speed, so they have no effect).

## Tempo and key from file names

Audio you drag in (or double-click in the browser) is set up from its name, as sample
packs name loops (`Bass_Loop_128_Am.wav`, `Keys 92bpm F# minor`, `Vox_Ebmaj_140BPM`).

### Tempo

A tempo in the name (`128`, `128bpm`, `bpm128`) warps the clip with that Seg. BPM, so
the loop plays in time at any project tempo. Files 6 seconds or longer are warped even
without one (at the project tempo, as when you turn Warp on), until their tempo is
detected (below).

### Key

A key in the name (`Am`, `F#m`, `Eb`, `G minor`, `Cmaj`) transposes the clip to the
**project key**, chosen in the transport bar (*Key*, after the swing).

- A minor key and its relative major count as the same (an A minor loop stays put in C
  major), and the clip takes the shortest shift (at most 6 semitones).
- With *No Key*, nothing is transposed.
- A bare letter (`_D_`) counts only next to a tempo, so names like "A Day" are left
  alone.

## Tempo and chords detected

SUBstation also listens to the audio itself: a moment after a file is added (a loop in a
few hundredths of a second, a whole song in a few tenths, in the background), its tempo,
key and chords are known.

- **Tempo.** A file whose name gives no tempo is warped at the tempo it is heard to have,
  if it is 6 seconds or longer, or a loop: whole bars long at that tempo (a 2-bar loop at
  128 BPM, 3.75 s). One-shots stay as they are. Several files dropped together stay end to
  end. This is an undo step of its own, *Detect Tempo*: undo it to have the clips as they
  were added. A clip you have changed in the meantime is left alone. Detection follows
  one steady tempo (60 to 200 BPM, from the first minute); a song that speeds up, or one
  very slow or very fast, can come out off (often at double or half): set Seg. BPM, or :2
  and ×2, by hand.
- **Chords.** Audio clips name the chords they play along the bottom of their waveform
  (once the track is tall enough), as they play them: transposing a clip transposes its
  chords. **C** (*View › Chords and Key*) hides or shows them, with the piano roll's chord
  lane. They count in the song's chords too: the piano roll's chord lane (and Generate)
  hear your audio loops and recordings along with your MIDI, but for tracks named as
  drums.
- **The clip view** shows what a clip's file was heard as, next to its length: *heard:
  128.00 BPM, A Minor*.

The key heard isn't used to transpose: only a key in the name is (above).

## Audio files

The browser lists WAV, FLAC and MP3 files. To put another file in a clip's place (in
every clip playing it), or to find files that went missing, see
[file-manager.md](file-manager.md). Files are decoded into memory when added (see
[limitations.md](limitations.md)).

---

For developers: [../ui/piano-roll.md](../ui/piano-roll.md) (the clip view),
[../engine/warp.md](../engine/warp.md), [../app/model.md](../app/model.md),
[../groove.md](../groove.md) (grooves).
