# Piano roll and clip view

The clip view is what a double-clicked clip opens: it covers the arrangement until Esc, × or Shift+Tab go back
([README.md](README.md#the-main-window)). Audio clips get their settings and large waveforms; MIDI clips get the
piano roll (one clip, or several edited together), laid out like Ableton's MIDI editor: a ruler on top, keys on the left, the notes in the middle and
velocities below. The clip view is [ui/qml/clipview](../../ui/qml/clipview) with its controller in
[ui/src/pianoroll](../../ui/src/pianoroll); the piano roll is
[PianoRollView.qml](../../ui/qml/pianoroll/PianoRollView.qml) and
[NoteTools.qml](../../ui/qml/pianoroll/NoteTools.qml) over the C++ items of
[ui/src/pianoroll](../../ui/src/pianoroll), which share one `PianoRoll`.

What the user does with them: [guide/midi.md](../guide/midi.md), [guide/audio-clips.md](../guide/audio-clips.md).
This page is about the code.

## Files

| File | What it holds |
|---|---|
| [ClipView.qml](../../ui/qml/clipview/ClipView.qml) | The clip view: its header (the track's colour, the Audio and MIDI switch when both kinds were opened, the name, a line about the clips; for a MIDI clip the key, Notes, Groove and Generate; ×), the audio clips' controls, waveforms, playhead and scroll bar, or the piano roll; `zoom(factor)` and `zoomToFit()` for the window's zoom keys |
| [ClipViewController](../../ui/src/pianoroll/ClipViewController.h) | What the clip view shows and does (the old `ClipView` without its widgets): which clips open, MIDI or audio (and the other kind, `showKind()`), editing every open audio clip at once, the clips' groove |
| [ClipGrooveControls.qml](../../ui/qml/clipview/ClipGrooveControls.qml) | A clip's groove: the audio controls' Groove section, and what the MIDI header's Groove menu shows ([below](#a-clips-groove)) |
| [ClipWaveform](../../ui/src/pianoroll/ClipWaveform.h) | The audio clips' waveforms, each source file across the width (all of it, or the share zoomed into), the start marker, and where the playhead is in each file; zooming and scrolling; the ruler's clicks |
| [ClipPlayhead](../../ui/src/pianoroll/ClipPlayhead.h) | The playhead over the waveforms, an item of its own |
| [ClipViewKnob.qml](../../ui/qml/clipview/ClipViewKnob.qml), [ClipViewSection.qml](../../ui/qml/clipview/ClipViewSection.qml) | A captioned knob with its readout; a titled box of controls |
| [PianoRollView.qml](../../ui/qml/pianoroll/PianoRollView.qml) | The piano roll's layout: the headphones and bend buttons, the ruler, the keys, the note grid with the chord lane and the note tools over it, the velocity lane, the scroll bars |
| [PianoRoll](../../ui/src/pianoroll/PianoRoll.h) | The piano roll's state (the old `PianoRoll` without its widgets): the clips it shows, its own time axis and row height, the selected notes (and the stretch a rubber band selected), the paste marker, the notes copied, the playhead, the keys sounding, the note tools' settings and actions, the song's chords and key over the clips, Generate; bend mode, its tool, the Shift view of the notes in it (`peeking`, `bendView`) and the vibrato's settings, the selected bend points, bend edits |
| [RollItem](../../ui/src/pianoroll/RollItem.h) | The base of the piano roll's items: `session` and `roll` properties |
| [NoteGrid](../../ui/src/pianoroll/NoteGrid.h) | The notes: painting, hit-testing, mouse, wheel, keys, and its gestures (`MoveNotesGesture`, `ResizeNotesGesture`, `SelectNotesGesture`, `PanGesture`) |
| [NoteGridBends.cpp](../../ui/src/pianoroll/NoteGridBends.cpp) | The note grid's bends: the curves painted, hit-testing (`bendHitAt`, `curveNear`), bend mode's presses, double-clicks, cursor and keys, and its gestures (`MoveBendPointsGesture`, `CurveBendGesture`, `SelectBendsGesture`, `VibratoGesture`, `GlideGesture`) |
| [NoteGridGesture.h](../../ui/src/pianoroll/NoteGridGesture.h) | `NoteGrid::Gesture`, the base of the grid's mouse drags (private to NoteGrid.cpp and NoteGridBends.cpp) |
| [BendTools.qml](../../ui/qml/pianoroll/BendTools.qml) | The bend bar, at the grid's top right in bend mode (not in the Shift view): Draw, Vibrato and Glide, a new vibrato's rate, depth and ramp, with the glide tool a new slide's curve, Clear |
| [PianoKeys](../../ui/src/pianoroll/PianoKeys.h), [PianoRuler](../../ui/src/pianoroll/PianoRuler.h), [VelocityLane](../../ui/src/pianoroll/VelocityLane.h), [RollPlayhead](../../ui/src/pianoroll/RollPlayhead.h) | The keyboard, the ruler, the velocities, and the playhead over each |
| [ChordLane](../../ui/src/pianoroll/ChordLane.h) | The song's chords along the top of the notes |
| [ProgressionPanel.qml](../../ui/qml/clipview/ProgressionPanel.qml) | Progression ideas beside the piano roll (Generate ▾ › Progressions…): the mood, the length, 7ths, Bass, New Ideas and the key; a row per idea (its chords, ▶, ★, ≈), the swap menu ([below](#progression-ideas)) |
| [ProgressionIdeas](../../ui/src/pianoroll/ProgressionIdeas.h) | The ideas the panel shows (a list model): made, locked, kept, swapped and varied by the intelligence module's [harmony/Progressions.h](../../intelligence/src/harmony/Progressions.h), and tried in the lead clip (`PianoRoll::writeProgression`) |
| [NoteTools.qml](../../ui/qml/pianoroll/NoteTools.qml) | The floating bar with Legato, ×2, ÷2, Quantize, Groove (Swing, the project's grooves, their amounts) and Humanize (Velocity, Timing) (the clip view's header has a Notes menu too: [below](#the-clip-view)) |
| [NoteSet.h](../../ui/src/pianoroll/NoteSet.h) | `ClipNote` (a note of one of the clips shown); sets of notes and of ClipNotes as sorted vectors (`noteSet`, `clipNoteSet`, `contains`, `united`, `without`, `byTime`, `notesOf`, `tagged`, `clipsOf`) |

The note maths is in the application layer's [model/Notes.h](../../app/src/model/Notes.h) (pure functions on
`Note`s, tested without Qt): `place`, `shifted`, `resized`, `clampMove`, `withVelocity`, `legato`, `timeScaled`,
`quantized`, `humanizedTiming`, `grooved`, `span`, `byTime`, `kMinNoteBeats`, `kQuantizeGrids`, `kHumanizeBeats`, and bends'
`withBendPoint`, `withBendPointsMoved`, `withoutBendPoints`, `withBendCurve`, `withVibrato`, `withoutVibrato`,
`nextNote`, `withSlide`. See
[app/model.md](../app/model.md). Humanize › Velocity's velocities are the session's humanizer's (`Session.humanizer`,
[intelligence.md](../intelligence.md#humanizing-velocities-by-machine-learning)).

## The clip view

`ClipView` is given clips (the main window sets them from `Session.arrangement.clipViewRequested`):

```qml
ClipView {
    trackId: t                      // the track plain ids in clipIds are on
    clipIds: [c]                    // or [{trackId: t, clipId: c}, ...] across tracks
    leadClipId: c                   // optional: the clip double-clicked (a MIDI lead opens the MIDI clips)
    onCloseRequested: ...           // Esc, ×, its clips all gone, the project reset
    onLocateRequested: beat => ...  // the piano roll's ruler was clicked: Session.locate(beat)
    onStatusMessage: message => ...
}
```

[ClipViewController](../../ui/src/pianoroll/ClipViewController.h) orders the clips top track first, then by time.
If the lead clip (the one double-clicked; by default the first) is a MIDI clip, every MIDI clip among them opens in
the piano roll, edited together, the lead first (`pianoRoll.setClips()`; `refs()` in that order); otherwise every
audio clip among them opens together. It shows them only while `active`
(bound to the view's `visible`): becoming active opens them afresh (the piano roll fitted to the clip again), going
inactive forgets them (a key sounding stops). Deleted clips or tracks are dropped; with none of the kind shown left,
the other kind's show, and with none left at all, or on a project reset, it emits `closeRequested` (the piano roll
skips a clip that is gone, and is given the rest when its lead goes). When clips open (`opened`) the notes (MIDI) or
the view (audio, for Esc) take the keyboard.

**Both kinds.** The clips asked for are kept (`requested_`, with the lead), the shown kind's in `refs()`;
`audioCount` and `midiCount` count those that exist of each, and `mixed` says there are both. Then the header shows
a switch before the name (`kindSwitch`: `showAudio` and `showMidi`, "Audio 3" and "MIDI 5", in the track headers'
monitor style), and `showKind(midi)` shows the other kind (its first clip leads: `show()`). The choice
(`chosenMidi_`) stays while the same clips and lead are asked for (shown again with Shift+Tab, it shows the same
kind), and goes with a new request. A clip deleted on any of their tracks updates the counts (`clipsChanged` for a
track of `requested_`).

The header shows the lead track's colour, `name` (an audio clip's; for a MIDI clip, which has none, its track's) and
`info` ("4.00 beats · 12 notes"; several MIDI clips: "3 MIDI Clips", "on 2 tracks · 12 notes · edited together";
"2.50 s · 5.00 beats"; "on 2 tracks · changes apply to every selected clip"; with
"· deactivated (0 activates)" when every clip shown is), and a × button. A MIDI clip's header also has the key,
*Notes ▾*, *Groove ▾* ([A clip's groove](#a-clips-groove)) and *Generate ▾* ([below](#chords-the-key-and-generate)).

*Notes ▾* (`notesMenu`) is the note tools for many clips at once, without selecting a group first: its first line
(`notesTarget`, disabled) says what it acts on ("The 3 selected notes", or "Every note of the 8 clips"), then
Transpose Up and Down, Octave Up and Down (`roll.transpose(±1)`, `transpose(±12)`), Legato, Quantize (at the note
tools' grid), Timing ×2 and ÷2, and Select All Notes (`roll.selectAll()`, as Ctrl+A). They are the roll's tools
([Note tools](#note-tools)): on the selected notes, or every note of every clip shown. Closing it gives the notes the
keyboard back.

### Audio clips

The left column (`kControlsWidth` 260 px, scrollable) has four `ClipViewSection`s:

- **Warp**: the Warp switch, the warp mode (`warpModes`, a tooltip each from `warpModeTips`; "Mixed" when the clips
  differ), *Seg. BPM* (`ValueBox`, 20 to 999), and :2 / ×2.
- **Pitch**: Transpose (±48 semitones, whole numbers) and Detune (±50 cents). Greyed out, with a tooltip saying
  why, when every clip is warped in Re-Pitch (`repitch`).
- **Mix**: clip gain (-70 to +24 dB; the waveforms are drawn scaled by it) and pan.
- **Groove**: the clips' groove ([A clip's groove](#a-clips-groove)).

Every control edits all open clips through `editor.updateClips(refs, change, text, mergeKey)` with a function from
clip to new clip:

- Switches, the mode and the BPM set the same value on all (`setWarp`, `setWarpMode`, `setSegmentBpm`).
- Turning Warp on sets a clip's segment BPM to the project tempo if it was never set, so nothing moves until the
  tempo changes.
- :2 and ×2 scale each clip's own segment BPM (`scaleBpm(0.5)`, `scaleBpm(2.0)`). The segment BPM goes through
  `edits::setWarpTempo`, so a clip with warp markers has every stretch scaled alike.
- Warp markers are the lead clip's alone (`warpMarkers`: {sec, beat} maps; `addWarpMarker(sec)`,
  `moveWarpMarker(index, sec, gestureKey)`, `removeWarpMarker(index)`, through `edits::addWarpMarker` and the rest,
  [model.md](../app/model.md)), while `warpMarkable` (one clip open, warped, not a frozen track's). *Mark Transients*
  (`markTransients()`) puts one on each transient between the clip's start and end (`edits::addWarpMarkers`). A
  file's transients (`transients(path)`: seconds, those at least `kMinTransientStrength` (`sampleSlices::kMarkStrength`, 0.25) of the strongest, as audio clips' grooves take them)
  are the Sampler's slicing's (`sampleSlices::transients`,
  [SampleSlicing.h](../../engine/src/builtin/SampleSlicing.h)), found once the file is decoded and kept while the
  view is open.
- The knobs (`knobs`: a map per knob of caption, range, default, value, text, differs) sit on the first clip's value
  and their readout shows the range when the clips differ. Turning one (`nudge(knob, value, gestureKey)`) moves
  every clip by the lead's change, measured from where the gesture started (the baseline keeps each clip's value at
  the gesture's first event), so a clip held at a limit keeps its offset to the others when the knob comes back.

The right side, [ClipWaveform](../../ui/src/pianoroll/ClipWaveform.h), draws each clip's source file across the width
(the whole of it at first), in its track's colour and scaled by its gain (rounded to 0.1 dB), the part the clip plays
tinted, the rest dimmed, and S and E flags at its start and end, as in Ableton's sample editor. Columns come from the
source's peaks (or its samples, zoomed in that far), blurred lightly when from peaks, as the arrangement's tiles are,
but drawn without a cache: what shows is one width. One clip gets a time ruler (steps from a fixed list, at least 70 px
apart: `formatTime()`); several are stacked in bands of at least `kMinBandHeight` (40 px), labelled, with "+N more"
when they don't fit. What it draws (each clip's colour, its source, why it couldn't be loaded) is worked out on the
GUI thread. `placed()` says where each band is drawn: its area, its file's length, and the part of it shown (`fromSec`,
`spanSec`); `xAt(placed, sec)` and `secAt(placed, x)` convert, and everything drawn or hit goes through them.

**Zooming and scrolling.** What shows is a share of each file, `viewFrom` to `viewTo` (0..1): one clip zooms as any
view does, and several, each across the width, zoom together. `zoomAt(x, factor)` keeps the moment under `x` in place,
held to `minSpan()` (the first clip's file at `kMaxPixelsPerFrame`, 8 px a frame) and to the whole file;
`scrollTo(from)` keeps the zoom; `zoomToFit()` shows the whole of each (on `opened`: other clips open whole). The
mouse is as in the arrangement and the piano roll: Ctrl+wheel zooms around the mouse (`kWheelZoom`, 1.2 a notch), the
wheel (Shift, or a horizontal one) scrolls sideways (`kWheelScroll`, 80 px a notch: there is nothing to scroll up and
down), Ctrl+Alt drags it (`ViewDrag`), and a press on the ruler (one clip) is a click or a drag decided on its first 3
px, as the piano roll's ruler decides: sideways scrolls, up and down zooms around the press (1.012 a pixel, down in),
and a click (on the release) plays from the moment clicked (`locateRequested(beat)`: `clip.startBeat` plus the
clip's beats to it, held to the part the clip plays; the clip view passes it on as its own `locateRequested`). Under
the waveforms a `ScrollBar` (`waveformBar`) shows while zoomed in. The window's Zoom In, Zoom Out and Zoom to
Arrangement (+, −, Z) call the clip view's `zoom(factor)` and `zoomToFit()` while it shows (Main.qml's
`window.zoom()` and `window.zoomToFit()`): the waveforms' (about their middle), or the piano roll's
(`PianoRoll::zoomAt()` about the grid's middle, `PianoRoll::zoomToFit()`: fitted again as when it opened).

**Where the song is.** As in the arrangement, the start marker (`Selection::insertBeat()`, while inside the part a
clip plays: `startSecs_`, worked out on `insertChanged`) is a line through its waveform in `Theme::insertMarker()`, and
a triangle at the top of the ruler; the playhead, while playing inside a clip (`playheadSecs()`, worked out on the
bridge's `positionChanged` and `transportChanged`, and on a tempo change), is a line in `Theme::playhead()` through its
waveform and a triangle at the ruler's foot. The second of a clip's file at a beat is `clip.offsetSec +
clip.beatsToSource(beat - clip.startBeat, tempo)` (`secAtBeat()`), so warping, warp markers and the tempo map count.
The playhead is drawn by [ClipPlayhead](../../ui/src/pianoroll/ClipPlayhead.h), an item over the waveforms
(`clipPlayhead`) that repaints alone on `playheadChanged`, as `RollPlayhead` does over the notes: the waveforms aren't
drawn again every frame.

A warped clip's warp markers stand on its waveform: a line under a `kMarkerTab` (8 px) tab in the accent colour. With
one clip open whose markers can be edited (warped, and not a frozen track's clip with a source map of its own:
`editableSec()`), double-clicking adds one where the mouse is, double-clicking within half a tab of one removes it,
and pressing one drags it (`moveWarpMarker` with one gesture key a drag: one undo step), the cursor a horizontal
resize over them. The file's transients are 5 px ticks along the top then; a double-click within half a tab of one
adds the marker on it (`snappedSecAt`).

Warping and its modes in the engine: [engine/warp.md](../engine/warp.md).

### A clip's groove

A clip's groove ([app/model.md](../app/model.md#grooves)) is set in the clip view: an audio clip's in the controls'
**Groove** section, a MIDI clip's in the popup the header's *Groove ▾* opens (reading "Groove: <name> ▾" while the
lead clip has one). Both are [ClipGrooveControls.qml](../../ui/qml/clipview/ClipGrooveControls.qml) over the
controller (`midi` set for the header's: only it shows Velocity, which audio clips don't take):

- `groove` is the lead clip's (`Session.grooves.clipGroove()`: kind "" for none, "swing", "project", "library" or
  "own"; its swing, step, amounts, the groove it plays), worked out on every refresh, the library or the default
  changing too; `grooveable` says whether any clip shown can take one (MIDI clips; audio clips but a frozen track's),
  the controls disabled otherwise.
- The chooser: None, Swing, Project (with the default's name), the project's grooves, and "Its own" while the lead
  plays its own copy (shown, not chosen). `setGroove(kind, grooveId)` gives every clip shown that source at the
  lead's amounts (from none: Timing and Velocity 100 %; Swing starts at 58 %), one undo step
  (`ProjectEditor::setClipGroove`, which warps an unwarped audio clip for it); None takes it away.
- Swing and Step (shown for Swing only), Timing (0 to 200 %), Velocity (MIDI; -100 to 100 %), Random and Quantize (0 to
  100 %), shown while there is a groove: `setGrooveValue(field, value, gestureKey)` sets one on every clip shown with a
  groove, one undo step a gesture ("Change Clip Groove").
- **Apply** (`applyGroove()`: `ProjectEditor::applyClipGrooves`, one undo step) writes their grooves into them and
  clears them; **Extract** (`extractGroove(stepIndex, bars, perKeyLanes)`: the controls ask for 16ths over a bar, one
  lane) makes a groove of the lead clip's feel in the project's grooves (`Session.grooves.extract`; with nothing to
  take it from, the status line says so).

A MIDI clip's notes stay where they are in the piano roll: its groove moves what the engine plays
([app/engine-bridge.md](../app/engine-bridge.md#grooves)), until Apply writes it into them.

## The piano roll

### Layout

```
┌──────────┬────────────────────────────┬───┐
│hear bend │ PianoRuler (24 px)         │   │
├──────────┼────────────────────────────┼───┤
│ PianoKeys│ NoteGrid, ChordLane on top │ v │
│ (64 px)  │             ┌──BendTools┐  │ b │
│          │             └───────────┘  │ a │
│          │             ┌───────────┐  │ b │
│          │             │ NoteTools │  │ a │
│          │             └───────────┘  │ r │
├──────────┼────────────────────────────┼───┤
│ Velocity │ VelocityLane (72 px, drag) │   │
├──────────┼────────────────────────────┤   │
│          │ hbar                       │   │
└──────────┴────────────────────────────┴───┘
```

`PianoRollView` makes the `PianoRoll` (`session: Session`) and gives it to every item (`roll: pianoRoll`); a
`RollPlayhead` sits over the ruler (`ruler: true`), the note grid and the velocity lane. Above the keys, the
headphones button (`preview`) and the bend button (`bend`, objectName `bendMode`: bend mode, as B). `ChordLane` (18
px, shown with `Session.harmony.shown`), `NoteTools` and `BendTools` (shown in bend mode, below the chords) are
children of the note grid, over it: the lane along its top, under
the playhead, taking no clicks. `focusNotes()` gives the note grid the keyboard; clicks on the keys, the ruler and
the velocities give it to the notes too (`PianoRoll::focusGrid()`). The scroll bars follow the roll except while
dragged (then the roll follows them: `scrollToX`, `scrollToY`). The velocity lane's top edge (`velocityHandle`, across the
"Velocity" label and the lane) drags to resize it (`velocityHeight`, 32 px up to what leaves the note grid 48 px);
double-clicking it puts it back to 72 px. A view too short for the dragged height shows the lane shorter
(`shownVelocityHeight`, never under 32 px) and gives it back when it grows. The ruler, the keys, the note grid and the velocity
lane have `clip: true` (and the clip view's `ClipWaveform`): a scene-graph item isn't clipped to itself, and notes or
the playhead scrolled out of the grid would show over the keys and the browser.

### Time and pitch

- **Time is roll beats**: one clip shows in its content beats (the beats of its notes, the ruler's 1 being its first
  content beat, not the arrangement's); several show in the arrangement's beats, each clip where it is in the song.
  `origin()` is the arrangement beat at roll beat 0 (one clip: `startBeat - offsetBeats`; several: 0), `shift(clip)`
  the roll beat of a clip's content beat 0, `rollStart(note)` where a note is. Notes stay in their clip's content
  beats in the model and in the roll (`ClipNote`). The roll has its own `timeline::Timeline` (see
  [arrangement.md](arrangement.md#arrangement-and-coordinates)), so `beatToX`, zoom, the adaptive grid, snapping and
  `gridLines`/`drawGrid` work as in the arrangement. Its grid level starts at 1, a little wider than the
  arrangement's (1/16 notes across a bar).
- **The part each clip plays** is from its `offsetBeats` to its `windowEnd()` (`windows()`, in roll beats): lit, with
  the rest dimmed (`Theme::outsideClip()`), and notes outside their clip's drawn at half strength (a note plays if it starts
  in the window). Notes outside it are kept: trimming a clip hides notes, never deletes them.
- **Converting**: the playhead is `beat - origin()` while the arrangement plays inside a clip shown; the ruler's
  click goes back with `+ origin()` (`requestLocate()` → `locateRequested`). `startBeat()` (the start marker) is the
  arrangement's insert marker in roll beats when it is inside a clip shown. `pasteBeat()` (the paste marker) is the
  roll's own: where the grid was last clicked (`setPasteBeat()`), none for new clips; it never moves the start marker
  or the playhead.
- `origin()` and `shift()` are worked out in `relayout()` on every `refresh()` (any change of the clips shown), not
  looked up for each call: drawing and hit-testing ask for a note's place note by note, and `Project::findClip` is a
  linear search.
- **Pitch** runs 127 at the top to 0 at the bottom, a row of `rowHeight` pixels each (`kRowHeight` 12 by default,
  `kMinRowHeight` 5 to `kMaxRowHeight` 36): `pitchTop(pitch) = (127 - pitch) * rowHeight - scrollY`, and
  `pitchAt(y)` the inverse, clamped to 0..127. `scrollY` is in pixels.
- `zoomRows(notches, anchorY)` (Alt+wheel over the grid or the keys) changes the row height by `kRowHeightStep`
  (1.5 px) a notch, keeping the pitch under the mouse in place. It keeps an exact height (`rowHeightExact_`) so a
  trackpad's small steps add up, and rounds it to draw.

### The clips and their edits

`setClips(refs)` shows MIDI clips, the first leading (`setClip(trackId, clipId)`: one, or nothing). `clipAt(i)`
(`clip()`: the lead) looks a clip up in the project each time, so the roll never holds a stale clip (the project
replaces it on every edit, and holds tracks by value); a clip that is gone is skipped. On new clips the selection,
the marker and the tools reset and a fit is pending: `fitIfReady()` zooms to the parts the clips play (96 % of the
grid's width) and centres their notes vertically (C3, `kDefaultPitch`, without notes), but only once the grid has its
size and shows (the clip view may not be laid out yet).

A note is a `ClipNote`: its clip (an index into the roll's clips, 0 the lead) and the note in that clip's content
beats. `allNotes()` is every clip's, the lead's last (drawn on top). A note stays in its clip whatever is done to it;
a new one goes into the clip playing where it is drawn (`clipFor(beat)`: the lead if it plays there, else the first
that does, else the lead).

Every edit goes through one method:

```cpp
roll.commitNotes({{clip, notes}, ...}, text, mergeKey, selected);  // commit(notes, ...): the lead's
```

It sets `selected`, then calls `editor.setClipsNotes({{ref, notes}, ...}, text, mergeKey)`: each changed clip's whole
new list of notes, one undo command, merged with the previous one when they share the merge key. Gestures pass their
own key (a `QUuid` made at the press), so a drag is one undo step, while the model (and what plays) changes live as
the mouse moves. Every edit is heard at once, because the bridge sends the tracks' notes to the engine on
`clipsChanged`. Edits of notes in several clips work out each clip's notes on its own (`notes::place` per clip);
moves are limited so no note of any clip leaves its range.

`clipsChanged` for a track the roll shows calls `refresh()`, which drops selected notes that no longer exist (undo,
redo) and repaints (`contentChanged`).

The selection is `roll.selectedNotes()`, a set of `ClipNote`s (a sorted vector, [NoteSet.h](../../ui/src/pianoroll/NoteSet.h);
`selected()`: their notes, `setSelection(notes)`: the lead's). A `Note` is a value: a moved note is a new note, which
is why gestures pass the moved notes as the new selection. A rubber band also selects the stretch of time it covered
(`selectedSpan()`, snapped out to the grid lines around it), drawn tinted under the notes; any other new selection
forgets it.

### Note grid

[NoteGrid](../../ui/src/pianoroll/NoteGrid.h).

**Painting.** Rows (black keys `Theme::blackKeyRow()`, lines at B|C and E|F), the grid, the dimmed outside of the clips'
windows (with several, a line in the clip's track colour at each window's ends), the selected stretch's tint, then
the notes: each in its clip's track colour (halfway to red, `Theme::outOfKey()`, for a note out of the song's key:
`PianoRoll::outOfKey()`; grey, `Theme::deactivatedClip()`, for a deactivated one, as its velocity stem is), more opaque for louder notes (alpha from velocity) and lighter when selected (white
outline), with the note's name when it is at least 30 by 10 px. Then the rubber band, the start marker
(`startBeat()`, a line in `Theme::insertMarker()`) and the paste marker (`pasteBeat()`, dashed in `Theme::pasteMarker()`); the
playhead is the `RollPlayhead` over it.

**Hit-testing.** `PianoRoll::noteRect(note)` (at least 3 px wide) and `noteAt(pos)` → `Hit{note, zone}` for the
topmost note: `End` or `Start` within `kEdgeGrab` (5 px, or a quarter of a short note) of an end, else `Body`.

**Mouse.**

| Press | Gesture |
|---|---|
| Ctrl+Alt | `PanGesture`: scrolls both ways, as in the arrangement |
| empty space | `SelectNotesGesture`: a rubber band; Ctrl or Shift adds to the selection. A click (no drag) places the paste marker at the snapped beat (`setPasteBeat()`) |
| a note's body | `MoveNotesGesture` on the selected notes (the note is selected first if it wasn't) |
| a note's end | `ResizeNotesGesture` on the selected notes |

A press on a note auditions it. Clicking one of several selected notes selects just it, and Ctrl-clicking a selected
note deselects it, but only when the mouse comes up without dragging (`selectOnClick_`): dragging moves the whole
selection instead.

Gestures start after `kDragThreshold` (3 px):

- `MoveNotesGesture`: the time delta is snapped against the grabbed note's start (Alt: off the grid), the pitch delta
  from the rows moved; `notes::clampMove` keeps every note in range. Ctrl copies (the originals stay). Each move
  commits `notes::place(base, moving, moved)` from the notes as they were at the press, and auditions the grabbed
  note at its new pitch.
- `ResizeNotesGesture`: moves the grabbed edge, snapped; the shortest length is a grid step while snapping, else
  `kMinNoteBeats`; `notes::resized` applies it to all.
- `SelectNotesGesture`: selects the notes the band touches (of every clip); each note sounds as the band catches it
  (`previewNotes()` with the notes it touches now and didn't before). On release, a band that was dragged selects
  them with the tools (`selectNotes(notes, true)`) and selects the stretch it covered (`setSelectedSpan()`, floored
  and ceiled to the grid).

Double-click: on a note, deletes it; elsewhere, adds a note one grid step long in the grid cell clicked (Alt: where
clicked), at the row's pitch, into the clip playing there (`clipFor()`), and auditions it. Adding or deleting a note this way never brings up the tools.

Wheel (`PianoRoll::wheel()`): Alt makes the rows taller or shorter (either axis, as Qt may report Alt+wheel as
horizontal); Ctrl zooms time around the mouse (1.2 a notch); Shift or a horizontal wheel scrolls sideways (80 px a
notch); otherwise it scrolls three rows a notch. The keys forward their wheel events to the same function.

### Keys

The grid takes `Delete`/`Backspace`, `Ctrl+A`, `Ctrl+D`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Ctrl+U`, `Ctrl+Shift+U`, the arrow keys,
`0`, `B` and `V` (`handles()`). In bend mode, Delete and Ctrl+A act on bend points instead (below). The main window has shortcuts for most of them too (for clips); the grid accepts their
`ShortcutOverride` event in
`event()`, so Qt delivers them to the grid as key presses instead of firing the window's actions, while it has the
focus. A hidden grid gives the focus up, so the window's shortcuts work again once the clip view goes.

| Key | Does |
|---|---|
| Ctrl+A | selects every note (of every clip), with the tools |
| Ctrl+U | `quantize()`: the selected notes, or every note when none are |
| Ctrl+Shift+U | `applyGroove()`: the groove last applied (Swing at first), on the selected notes or every note |
| Delete, Backspace | deletes the selected notes |
| Ctrl+D | `duplicateSelected()`: copies of the selected notes by the selected stretch's length (right after it; a rubber band's stretch reaches from where the drag started), else right after the last of them; selected, and the stretch after it selected (Ctrl+D again goes on) |
| Ctrl+C / Ctrl+X | `copySelected()` / `cutSelected()`: the selected notes copied (their track, their times from the stretch's start); cut takes them out |
| Ctrl+V | `paste()`: the copied notes at the paste marker (else right after where they were copied), each into the clip of its track that plays there (else the one there, else the lead); selected, the marker moving to their end |
| 0 | deactivates the selected notes (`notes::withActive`), or activates them if they all are; they stay selected |
| B | bend mode on or off (below; on: with the Draw tool) |
| V | the vibrato tool (into bend mode if it is off); again, back to Draw |
| G | the glide tool (into bend mode if it is off); again, back to Draw |
| Up / Down | a semitone (Shift: an octave) |
| Left / Right | a grid step (Shift: a bar) |

Moves go through `notes::clampMove` and do nothing at the edge.

### Keys and ruler

[PianoKeys](../../ui/src/pianoroll/PianoKeys.h) draws the keyboard (black keys 60 % wide, the key sounding lit, C's
labelled with `noteName`). Pressing a key selects every note on that pitch (Shift adds) and auditions it; dragging
over the keys selects every note on the keys from the one pressed to the one under the mouse (`selectKeys()`) and
plays each key in turn; releasing stops it.

[PianoRuler](../../ui/src/pianoroll/PianoRuler.h) draws bar numbers in roll time, a bar in each clip's track colour
under the part it plays, the start marker (a triangle at the top) and the paste marker (a tab at the bottom, in
`Theme::pasteMarker()`, over the grid's dashed line). A click plays from the snapped beat (`requestLocate()`, converted to
the timeline); a drag pans or zooms as the arrangement's ruler does (decided on its first 3 px).

### Velocity lane

[VelocityLane](../../ui/src/pianoroll/VelocityLane.h) draws a stem per note at its start (every clip's, in its track's
colour), as tall as its velocity (1 to 127 over the lane's height), white for selected notes. `stemAt(x)` finds the stem nearest the mouse within
`kStemGrab` (6 px), preferring selected notes. Dragging a stem selects its note (if it wasn't) and changes every
selected note's velocity by the same amount (`notes::withVelocity`), committed live with one key; the selected
notes' values show while dragging.

### Note tools

[NoteTools.qml](../../ui/qml/pianoroll/NoteTools.qml) is a small rounded bar: a note count, Legato, ×2, ÷2, Quantize
with its grid (`quantizeGrids`: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32; 1/16 at first) and amount (100 %), Groove ▾, a
menu (`grooveMenu`) of Swing (its percent in a box at its right, `grooveSwing`: 58 % at first, 50 to 75), Project
(the default groove, named; greyed out without one: `Session.grooves.defaultKind`), the project's grooves (from
`Session.grooves.grooves`; the last applied in bold), then the amounts the groove is applied at, each a caption and a
number box that chooses nothing (`grooveTiming` 0 to 200 %, `grooveVelocity` -100 to 100 %, both 100 % at first;
`grooveRandom`, `grooveQuantize` 0 to 100 %, 0 at first), and Humanize ▾,
a menu (`humanizeMenu`) of Velocity (`humanizeVelocity`) and Timing (`humanizeTiming`), each with its own amount in a
number box at its right (`humanizeVelocityAmount`, 100 % at first; `humanizeTimingAmount`, 25 %): clicking an entry's
name humanizes, the box keeps its clicks. Velocity is greyed out ("Velocity (no model)") when the model isn't next to
the application (`velocityModelAvailable`). Its buttons and boxes never take the focus, so the notes keep the
keyboard (the menu has it while open, and gives it back to the grid when an entry is chosen); it keeps its clicks to
itself (the grid is underneath), while wheel turns outside its boxes still scroll the grid.

When it shows is the roll's: `toolsWanted` is set by `setSelection(notes, true)` (a rubber band that was dragged, or
Ctrl+A) and cleared by any other new selection (a click, a new clip, adding or deleting a note with a double-click,
an empty selection); edits through `commit()` keep it, so the bar stays while that group is edited. `placeTools()`
(on every repaint, scroll and release) sets `toolsShown`, `toolsArea` (the selected notes' bounding rectangle) and
`toolsCount` while it is wanted and no drag is under way (`NoteGrid::dragging()`), else hides it. Clicking or drawing
single notes doesn't bring it up.

The bar sits centred above the notes (below them when there is no room), kept inside the grid. It fades in rising
8 px (180 ms, ease out) and fades out sinking (120 ms); a reversed animation goes on from where the last one left
off, taking the rest of its time.

The actions are the roll's: `legato()`, `scaleTime(2.0)` / `scaleTime(0.5)`, `quantize()`, `humanizeVelocity()`,
`humanizeTiming()`, and `transpose(semitones)` (the header's Notes menu: all the targets together, held by
`notes::clampMove` so none leaves the keyboard; with none able to move the roll's `statusMessage` says so). Each takes `toolTargets()` (the selected notes, or all when none are, by time on the roll),
computes the changed notes, and commits `notes::place(clip.notes, targets, changed)` for each clip as one undo step
(`applyTool()`: "Humanize Velocity", "Humanize Timing"). Legato works in each clip on its own (up to its own notes and
its end); Quantize, Humanize › Timing and ×2 / ÷2 work on the notes' times on the roll (`applyOnRoll()`: with several
clips, the arrangement's grid), each note then back in its clip. `humanizeTiming` (and Groove's Random) uses the roll's own
`QRandomGenerator` (`seedRandom()` for tests); `humanizeVelocity` hands the targets (each with its track and clip) to
`Session.humanizer`, which judges each track's notes with the notes it plays around them and levels the model's
velocities to their own (nothing changes if the model can't be loaded: the status line says why). The parameters are
the roll's properties: `quantizeGrid`, `quantizeAmount`, `humanizeVelocityAmount` (how far velocities move toward the
model's), `humanizeTimingAmount` (at 100 %, notes move by up to `humanizeBeats`, a 32nd note).

`applyGroove(choice)` (Apply Groove) is a tool too ("Apply Groove", one undo step): `choice` is "swing"
(`groove::swing(grooveSwing)`, on 16ths), "project" (the default groove; without one the roll's `statusMessage` says
so, which the clip view passes on, and nothing changes) or a library groove's id; "" is the last one applied
(`grooveChoice`, "swing" at first: Ctrl+Shift+U). The notes are placed on the timeline (roll beats plus `origin_`), so
the groove's cycle counts from the song's start whatever clip they are in, by `notes::grooved` at the menu's amounts
and a new Random seed each time (from the roll's generator). Each clip's notes are grooved together, so they are
untangled as the clip's groove would play them (a note grooved into the next on its key ends where that one starts),
and go back in with `notes::place`.

### Bends, vibrato and slides

A note's bend (MIDI 2.0's per-note pitch bend: [app/model.md](../app/model.md#clips-and-notes)) shows over the rows
as a curve, a semitone a row from the middle of the note's own (`PianoRoll::bendY()`, `bendSemitones()`), sampled
from the note's `bendAt()` finely enough for its fastest vibrato. Out of bend mode a bent note shows its curve faintly
over it. **Bend mode** (`PianoRoll.bendMode`: B, or the bend button; turned on, always with the Draw tool) shows
every note's curve, a flat line at its pitch for a note without a bend, dims the notes (so the curves stand out),
hides the note tools and shows the bend bar; the grid's presses, double-clicks, cursor and keys go to
[NoteGridBends.cpp](../../ui/src/pianoroll/NoteGridBends.cpp) (Ctrl+Alt still pans).

**Holding Shift** in bend mode gives the notes back for as long as it is held: `PianoRoll::peeking()` (Shift held,
in bend mode, with the Draw or Glide tool: the vibrato tool's Shift is its speed), and `bendView()`, bend mode but
for that, is what decides what the grid paints, where its presses, double-clicks and keys go, its cursor, the note
tools (`placeTools()`) and the bend bar's visibility. The grid tells the roll (`setPeeking()`) from every modifier
change it hears (`onModifiers()`: its key presses and releases, its hover and press events; `focusOutEvent()` lets
go), but not while a bend gesture runs (Shift is the vibrato's then; once it ends, a Shift still held shows the
notes). A gesture started on the notes this way is a note gesture (`peekGesture_`), its modifiers handed on without
Shift (`gestureModifiers()`): moves and resizes snap and a click selects as out of bend mode (Shift is the view's
there, so Ctrl alone adds to a selection), and it goes on to its end if Shift is let go first. Painted per note: the curve (white, with a shadow; thicker while Alt hovers a segment), the
hand-drawn curve faintly under a vibrato (`Note::curveAt()`: what the vibrato swings around), an orange bar along the
bottom of the row for each vibrato, the points (selected ones filled with the selection colour, a hovered one
larger), a ghost point where a click would add one, and by a dragged point its value ("+2.00 st") or by a vibrato
being drawn its depth, rate and ramp (`Gesture::label()`).

With the **Draw** tool (`bendTool` `"draw"`), the curves are edited as an automation envelope is
(`bendHitAt(pos, modifiers)`: a point within `kPointGrab`, 6 px; the curve within `kLineGrab`, 5 px; with Alt a
segment within `kSegmentGrab`, 14 px). (A press with Shift held is the Shift view's, above.)

| Press | Does |
|---|---|
| on a curve | adds a point there (on the grid; at the curve's own value, so the line doesn't move) and drags it where the mouse goes: one undo step |
| on a point | `MoveBendPointsGesture`: drags the selected points (of every note) in time and pitch, the grabbed one onto the grid and a whole semitone (Alt: freely), the others by as much (`notes::withBendPointsMoved`: they stay in order and in their note). A click without a drag deletes the point (not with Ctrl) |
| Ctrl on a point | adds it to the selected points, or takes it out |
| Alt on a segment | `CurveBendGesture`: bends the segment from its first point; up bulges it upward (`kCurvePixels`, 150 px, from straight to the most) |
| empty space | `SelectBendsGesture`: a rubber band over points (Ctrl adds to them); a click lets them go |

Double-click puts a point at the pitch clicked (a whole semitone; Alt: anywhere) on the note whose curve is nearest
(`curveNear()`); where the click before just put one, that one goes there instead. Delete deletes the selected
points (never notes, in bend mode); Ctrl+A selects every point.

The **Vibrato** tool (`bendTool` `"vibrato"`, V) is `VibratoGesture` on the note whose curve is near the press, else
the note under it: dragging across the note draws vibrato over the stretch dragged across (on the grid; Ctrl:
anywhere), at the bend bar's rate and ramp, dragging up deepens it from the bar's depth (`depth_`, 0.05..12
semitones); a click adds vibrato from there to the note's end, or, on a vibrato, takes it away. While Shift is held,
dragging sideways changes its rate instead (`rate_`: `kVibratoRatePixels`, 100 px, doubles or halves it, within
`notes::kMinVibratoRate`..`kMaxVibratoRate`, 0.5..20 Hz), and while Alt is, its ramp (`fade_`:
`kVibratoRampPixels`, 200 px, from none to all of it); the cursor is a sideways arrow meanwhile. What the mouse
moves while either is held goes into `held_`, taken from the position the stretch and depth follow, so they stay
where they were and go on from there once the key is let go. Drawn, the rate is rounded to a tenth of a hertz and
the ramp to a percent; the bend bar's own settings don't change.
`notes::withVibrato()` gives the new vibrato the stretch it covers (those already there are shortened, split, or
go). Its rate is in cycles a second (`vibratoRate`, 5.5 at first), its depth in semitones either way
(`vibratoDepth`, 0.5) and its ramp the share of its length it takes to reach its depth (`vibratoFade`, a percent,
30; `Vibrato::fade`). It adds to the curve and begins and ends on it, so it follows whatever is drawn by hand.

The **Glide** tool (`bendTool` `"glide"`, G) is `GlideGesture` on the note `toolNote()` finds (as the vibrato tool's),
sliding it into the note after it in its clip (`slideTarget()`: `notes::nextNote`, the next to start after it, of a
chord the nearest in pitch, deactivated notes left out; with none, no gesture, and the cursor is a no-entry sign).
It starts at the press (on the grid; Ctrl: anywhere) and ends where the drag is, never before the start (a click: the
note's end); `notes::withSlide()` makes it two bend points, from the curve's value at the start (no step) to the
interval at the end (held to ±48), replacing the points it covers. While Alt is held, dragging sideways curves it
(`amount_`: `kGlideCurvePixels`, 150 px, from straight to the most; positive arrives later) instead, the stretch held
as the vibrato's is (`held_`); the stored curve is its sign turned for the direction it goes (positive bulges up),
so right always means later. A slide starts with the roll's `glideCurve` (a percent, -100..100; 0 at first), and a
curve set with Alt becomes it (rounded to a percent, as the slide is drawn), so the next slide starts curved the
same; the bend bar shows it in a box (`glideCurve`) while the glide tool is picked. The label shows the note it goes to, the interval and the curve. The vibrato and glide
gestures tint the stretch they draw over (`Gesture::area()`). A slide is ordinary bend afterwards, edited with Draw.

Edits go through `PianoRoll::commitBend(from, to, text, mergeKey, points)` (and `commitBends` for several notes): the
note is replaced by its new self in its clip (`setClipsNotes`, merged per gesture key), the selected notes and points
following it ("Add Bend Point", "Move Bend Points", "Delete Bend Point", "Bend Segment", "Draw Vibrato", "Remove
Vibrato", "Add Slide"; Delete's "Delete Bend Points"). `clearBends()` (the bar's Clear, "Clear Bends") takes the selected notes' bends away, every note's with none selected.
`selectedBends()` are `BendRef{note, point}`s; `refresh()` drops those whose note is gone. The bend bar
([BendTools.qml](../../ui/qml/pianoroll/BendTools.qml)) is as the note tools: its buttons and boxes never take the
focus and it keeps its clicks. Auditioning a note plays it without its bend; the engine plays it bent
([engine/midi.md](../engine/midi.md#bends-midi-20s-per-note-pitch-bend)).

### Chords, the key and Generate

The song's harmony is the session's (`Session.harmony`, [intelligence.md](../intelligence.md#harmony-the-application-side)):
its chords on the timeline and its key (the project's, else inferred from its MIDI). While it is shown (`shown`, View ›
Chords and Key, C), `refreshHarmony()` maps the chords over the part the clips play (the first one's `startBeat` to
the last one's `endBeat()` on the timeline) into roll beats, as `RollChord`s (`chords()`: start, end, name, root, minor), and keeps the key
(`scaleKey()`), on the GUI thread, for the items to draw: when the harmony says it changed (40 ms after an edit
anywhere in the song), when it is shown or hidden, when a clip opens and when the grid shows. Hidden, there are no
chords and no key, and nothing is inferred.

[ChordLane](../../ui/src/pianoroll/ChordLane.h) draws a see-through band for each chord along the grid's top, coloured
by its root around the circle of fifths from blue at C (`chordColor`; darker for minor and diminished chords), with a
line at its start and its name, kept at the lane's left while its start is scrolled away. It accepts no mouse
buttons: clicks reach the notes under it. `NoteGrid` tints notes out of the key (`outOfKey(pitch)`: not in the major,
or natural minor, scale).

The clip view's header shows the key (`keyLabel`: "Key: A Minor", "(inferred)" when the project has none) and
*Generate ▾*, a menu of *Chords*, *Bass* and *Progressions…* ([below](#progression-ideas)): `generateChords()` and
`generateBass()`. Each writes into the lead clip:
it takes the song's chords over the part it plays (a progression in the key, C major without one, where there are none:
`starterProgression`), writes the part (`chordPart`, `bassPart`; bars on the timeline's) in content beats, and commits
the clip's notes with them as one undo step ("Generate Chords", "Generate Bass"), the written notes selected. The
clip's own notes win: a written note overlapping one on its key is shortened or left out (`notes::resolveOverlaps`
with the clip's notes as winners), and nothing is committed if nothing would change (Generate again).

### Progression ideas

*Generate ▾ › Progressions…* shows [ProgressionPanel.qml](../../ui/qml/clipview/ProgressionPanel.qml) at the clip
view's right (the roll narrower by its width; `wanted` keeps it there with the roll, × and Esc clear it) and calls its
`open()`: ideas made if there are none, the keys to it. Its model is the clip view's
[ProgressionIdeas](../../ui/src/pianoroll/ProgressionIdeas.h) (`progressionIdeas`, on the roll), a list model of
`kIdeas` (6) ideas: each `Row` holds the intelligence module's `Idea` (its chords, and the mood's own progression it
came from), the key and mood it was made in, a lock per chord and whether it is kept. Its roles are `chords` (each
{name, numeral, locked}, as played: with `sevenths`, `withSeventh()` of each), `kept`, `tried` and `source` (its
origin, for ▶'s tooltip); `publish()` compares each row's with what QML last had and sends `dataChanged` for those
that changed only, so the other rows' delegates (and the chips under the mouse) stay.

- **The key** (`settings()`): the harmony's (`Session.harmony.key()`: the project's, else inferred), else A minor
  (`keySource`: "project", "inferred", "assumed"). Each idea keeps the key it was made in: its numerals are in it,
  and redrawing or varying it stays in it.
- **New Ideas** (`newIdeas()`; also on a change of `mood` or `length` once there are ideas): kept rows stay; a row
  with locked chords is drawn again round them (`redrawIdea()`) in its key; the rest, and missing rows, come from `progressionIdeas()`,
  avoiding the chords of the rows that stay. **More Like This** (`moreLike(row)`): `ideaVariations()` of that row in
  place of every row but it and the kept ones, its locks carried over. **Swap** (`alternatives(row, chord)`, then
  `swap(row, chord, index)`): `chordColours()`, then `chordAlternatives()` (`kAlternatives`), without those that
  would play as the chord already does or as one before them in the list. The seed starts random and goes up by one a
  call (`seedRandom()` for tests).
- **Try** (`tryIdea(row)`): `PianoRoll::writeProgression(chords, bass, base, mergeKey)` writes the played chords
  over `base` (the clip's notes and length besides): `layOut()` from the clip's start, a chord a bar on the timeline's
  bars, round and round to the clip's end, the clip made long enough to play every chord once (to the bar line that
  many bars on from the one it starts in) and never shorter; `chordPart()` (and `bassPart()`), in content beats; the
  base's notes win overlaps (`notes::resolveOverlaps`). It commits the notes and the length together through
  `ProjectEditor::updateClips` ("Try Progression", merge key `kMergeKey` + the clip's track and id), the written
  notes selected. Each clip's last try is kept (`tries_`, by `clipKey()`: what it wrote over and what it left), so
  tries in another clip in between don't count. The base: if the clip is as its last try left it, the base that try
  had, so a run of tries is one undo step that each writes over the clip as it was before the first (as
  `updateClips` does with a merged step); otherwise the clip now, without the notes its last try wrote (by value,
  where they still are). `tried()` is the row whose played chords (and Bass) were written while the clip
  is still as written (`clipChanged()`, on the roll's `contentChanged` and `clipChanged`); a change to that row (a
  swap, 7ths, Bass) tries it again. `step(delta)` (↑ ↓) tries the next row.

The panel claims ↑, ↓ and Return over the window's shortcuts while it has the focus (`Keys.onShortcutOverride`); its
buttons never take the focus, so Space stays play and stop. A chord chip toggles its lock on a click and opens the
swap menu (a `Popup` of the colours, then the alternatives) on a right-click.

### Hearing notes

`audition(pitch, velocity, clip)` sounds a key on the instrument of the clip's track through
`bridge.previewNote(trackId, pitch, velocity)` until `releaseAudition()` (a `previewNote` with velocity 0); one key
sounds at a time. `previewNotes(notes)` sounds notes for a moment (a rubber band's, as it catches them), each key
once on its track, as loud as its loudest note, deactivated notes left out, with those sounding already (a key
sounding isn't struck again); all of them stop `kChordPreviewMs` (400 ms) after the last were caught
(`previewing()`: the keys sounding). A key auditioned stops them. The headphones button (`preview`) turns both off. Hiding the grid or changing
the clips releases them. Preview notes go through a
lock-free queue to the next audio block, straight to the track's instrument (see [engine/midi.md](../engine/midi.md)).

### Playhead

`onPosition` (from the bridge's `positionChanged` and `transportChanged`) works out the roll-beat playhead while
playing inside a clip shown and emits `playheadChanged`; only the `RollPlayhead` items repaint, not the notes.

## Extending it

- **A new note tool**: write the pure function in [model/Notes.h](../../app/src/model/Notes.h) (notes in, changed
  notes out), add a `Q_INVOKABLE` on `PianoRoll` that calls `applyTool(targets, changed, text)`, and a button in
  NoteTools.qml.
- **A new gesture**: subclass `NoteGrid::Gesture` ([NoteGridGesture.h](../../ui/src/pianoroll/NoteGridGesture.h))
  in [NoteGrid.cpp](../../ui/src/pianoroll/NoteGrid.cpp), or in [NoteGridBends.cpp](../../ui/src/pianoroll/NoteGridBends.cpp)
  for bend mode; use `started(pos)` for the drag threshold, commit with the gesture's `key`, and give `rubberBand()`
  if it draws one, `label()` for a value shown by the mouse.
- **A new key**: add it to `NoteGrid::handles()` (so it overrides a window shortcut with the same key) and to
  `keyPressEvent`.
- **A new clip setting**: a method on `ClipViewController` that calls `update(change, text, mergeKey)` with a function
  from clip to clip (or a knob in its `knobs` table and `nudge()`), and its control in ClipView.qml (a groove setting:
  `setGrooveValue()` and ClipGrooveControls.qml).

## Gotchas

- Always go through `roll.clipAt()`; a clip is replaced on every edit.
- Notes are in their clip's content beats; draw and hit-test them with `noteRect(ClipNote)` / `rollStart()`, never
  with a bare `note.start`.
- Gestures commit from the notes as they were at the press (`base_`), not from the current clip, or a drag would
  compound.
- Notes are compared by value: two identical notes on the same key and time are one in a set (`noteLess` is
  `notes::lessFull`, bends included). `notes::place` decides how overlaps resolve.
- A bend edit makes a new note: hold a `ClipNote` across a bend gesture only as `commitBend()` returns it.
- `ProjectEditor::updateClips` with a merge key applies the change to the clips as they were before the merged step
  began, and merges by the tracks changed: a run of tries must write the clip whole (notes and length), and its key
  must name the clip (another clip's run on the same track would undo the first's).
- A chord chip in the panel is made again when its row's chords change (a lock): find it again after a click.

## Tests

| Test file | Covers here |
|---|---|
| [test_ui_pianoroll.cpp](../../tests/app/test_ui_pianoroll.cpp) | Through the clip view: a MIDI track with the Synth and a clip opened in the piano roll; notes drawn and heard, dragged to move, resize and copy; the header's Notes menu on every note of the clips shown (or the selected ones); the notes' keys taking precedence over the window's shortcuts; the rubber band, the keys and the velocity lane; Alt+wheel and Ctrl+Alt drags; the note tools floating by notes selected by dragging (Humanize's menu: Velocity and Timing, each with its amount); the song's chords along the top (over the part the clip plays, hidden with the key, clicks going through them), notes out of the key in red, Generate › Chords and › Bass; Groove on the song's grid (Ctrl+Shift+U with Swing at first, a groove of the project's from the menu, its amounts, the project's default and the status line without one) |
| [test_ui_pianoroll_bends.cpp](../../tests/app/test_ui_pianoroll_bends.cpp) | Bend mode: B and the bend button showing the curves (and the note tools staying away); a point added on a curve dragged where the mouse goes; points clicked away, selected and deleted; Alt-dragging a segment; double-clicking a point at the pitch clicked; the vibrato tool's drags and clicks, and the bend bar's settings; Shift and Alt setting a vibrato's speed and ramp as it is drawn (the stretch held meanwhile), Ctrl drawing it off the grid; B and the button always starting with Draw; the glide tool sliding into the next note (of a chord, the nearest), Alt curving it (the next slide starting with that curve, until it is changed), a click to the note's end, Ctrl off the grid, nothing after the last note; Shift showing the notes to move, lengthen, add and select (the note tools coming up), not with the vibrato tool, not during a curve's drag; Clear |
| [test_ui_clipview.cpp](../../tests/app/test_ui_clipview.cpp) | Audio clips: one clip opened, its settings and waveform; several edited in unison; warping and transposing reaching the audio; warp markers added, dragged and removed on the waveform, marked at transients and snapped to them; the clip gain making the waveform taller; which clips open with a MIDI clip among them, and the Audio and MIDI switch (the choice kept when shown again, gone with other clips, the other kind shown when the clips shown are deleted); an audio clip's groove (Swing warping it, its step and timing, Apply into its warp markers, None, Extract); the start marker and the playhead on the waveform, a click on the ruler playing from there; zooming and scrolling (Ctrl+wheel around the mouse, the wheel and Shift+wheel, Ctrl+Alt drags, the ruler's drags, the window's zoom keys, the closest zoom, other clips opening whole); going back (Esc, ×, the clips deleted, the project reset) |
| [test_ui_progressions.cpp](../../tests/app/test_ui_progressions.cpp) | Progression ideas through the clip view: Generate › Progressions… opening the panel beside the roll with ideas in the song's key (A minor without one), × and Esc; trying an idea writing its chords (the clip made four bars long), ↑ and ↓, a click on a row, a run of tries one undo step; the clip's own notes kept, and an edit between tries; each clip's last try replaced in it after a try in another; Bass and 7ths writing the tried idea again; locked chords and kept ideas staying for New Ideas; a chord swapped from the right-click menu; More Like This; eight bars, another mood, the project's key |
| [test_midi_model.cpp](../../tests/app/test_midi_model.cpp) | The note maths |
