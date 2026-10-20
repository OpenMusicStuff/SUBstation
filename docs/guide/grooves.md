# Grooves

A groove is swing and feel: notes (or an audio clip's hits) moved off the grid the way a
player puts them, the offbeats late, the snare laid back, the accents where they fall.
SUBstation plays grooves in four places, all in the same way and from the same grooves:

- **The Groove device**, a MIDI effect: it grooves the notes reaching it as they play, on a
  track or on one Drum Rack pad, and its knobs can be automated and modulated (see
  [devices.md](devices.md#groove)).
- **A MIDI clip's groove**: the clip's notes play grooved, while the notes themselves stay
  where they are until you apply it (see [midi.md](midi.md#a-clips-groove)).
- **An audio clip's groove**: the clip's hits move, the audio between them stretching (see
  [audio-clips.md](audio-clips.md#groove)).
- **Groove ▾ in the piano roll**: the groove written into the notes, as Quantize writes the
  grid (see [midi.md](midi.md#note-tools)).

Each of them plays **Swing**, the project's **default groove**, or one of the **project's
grooves**.

## How a groove moves notes

A groove is 1, 2 or 4 bars long, cut into steps (eighths, sixteenths, their triplets, or
32nds), and repeats through the song, counted in bars of four beats from its start. Each step
says how late or early a note on it plays (a share of a step, and milliseconds, which stay
the same at any tempo), how much louder or softer, and how much longer or shorter. A groove
can have **lanes** for some keys (the hats, the snare) with a feel of their own, so the hats
can swing while the kick stays straight; the first lane plays every other key.

A note takes the feel of the step nearest it on the groove's **Base** grid; a note between
two steps moves in proportion, so notes on one key never swap places. Wherever a groove
plays, it plays at amounts you set:

- **Timing** (0 to 200 %): how far notes move; 100 % is as the groove has them.
- **Velocity** (−100 to 100 %): how much the groove's accents change velocities; below zero
  they are turned round.
- **Random** (0 to 100 %): a jitter on each note's timing, up to a quarter of a step either
  way at 100 %. It is the same every time the song plays (and in an export), and differs from
  one bar to the next.
- **Quantize** (0 to 100 %): notes pulled onto the Base grid before the groove moves them. At
  0 % a played part keeps its own feel and gets the groove's on top; at 100 % only the
  groove's is left.

**Swing** is the classic groove: every other step late, landing that far through its pair of
steps. 50 % is straight, 66.7 % a triplet feel (a shuffle), 75 % the most.

## The project's grooves

*Edit › Grooves…* opens the project's grooves. The window isn't modal: the song plays on
while you edit, and everything playing a groove follows each change at once.

On the left are the project's grooves (the default one marked), and:

- **Add ▾**: a **Built-in** groove (MPC swing from 50 to 75 % on 16ths and on 8ths, triplet
  and lazy shuffles, and played drum grooves: *Laid Back Backbeat*, *Boom Bap 16*, *Funk 16
  Ghosts*, *Drunk 16*, *Pushed Hats 16*), one of **your groove files** (below), or a **Swing**
  (54 to 71 % on 16ths). What you add is the project's own copy, to change as you like.
- **Duplicate**: a copy, to change apart from the groove it came from.
- **Remove**: whatever plays it (a Groove device, a clip, the default groove) keeps its own
  copy, so nothing changes sound; its groove list then shows it as its own.
- **Save File**: saves it among your groove files, for other projects.
- **Make the Project's Default** (see [below](#the-default-groove)).

Grooves also come from clips (*Extract*, in the clip view, or *Extract Groove* in a clip's
right-click menu in the arrangement) and from Groove devices (*Add to Grooves*).

On the right is the groove chosen: its name (type to rename it), **Bars** (1, 2 or 4: its
steps repeat to fill them, or are cut), **Base** (the grid notes are found on, 1/4 to 1/32)
and the size of its steps. Under them is a row for each lane, a bar a column:

- Drag a step's handle **late or early** (up to half a step either way); double-click it to
  put it back on the grid.
- Under it, drag a step's **velocity** up (louder) or down (softer); double-click: no change.
- The notes or hits the groove was made from show faintly under its steps.
- The first lane plays every key no other lane takes. **+ Lane** adds a lane for some keys,
  starting as the first lane is: type its keys as MIDI note numbers (36 is a Drum Rack's
  kick, 38 its snare, 42 its closed hat; it starts with the hat, or the snare or the kick
  if a lane has it already). **×** removes a lane (its keys
  go back to the first), **Straight** puts a lane's steps back on the grid.

Each drag is one undo step, and so is every other change here. The grooves are saved with
the project.

### Your groove files

Your groove files are in your preset library's *Grooves* folder
(`Documents\SUBstation\Presets\Grooves`): SUBstation's own (`.gilgroove`, what *Save File*
writes; a file of the same name is replaced) and Ableton's (`.agr`: copy them there). *Add ▾ ›
From Groove Files* adds one to the project; an Ableton groove's Timing and Velocity are worked
into its steps.

The browser lists them with the presets, under *Grooves* (see
[browser.md](browser.md#presets)). Double-click one, or drag it onto the device view or a
track, for a Groove device playing it as its own; drop it onto a Groove device for that one to
play it (its knobs back at their defaults).

## The default groove

The project has one **default groove**: none (straight), a swing, or one of its grooves.
Groove devices and clips set to *Project* play it, the piano roll's *Groove ▾ › Project*
writes it, and devices that play on the beat follow it with their **Groove** button (Follow
Groove): the [Arpeggiator](devices.md#arpeggiator), the [Delay](devices.md#delay) and the
[LFO](devices.md#lfo).

- *Edit › Default Groove* chooses it: *None*, a swing (54, 58, 62, 66.7 or 71 % on 16ths),
  or one of the project's grooves.
- **Swing**, in the transport bar after the time signature, is the default groove as a swing:
  drag it (Shift for fine steps; double-click to type) from *Off* (50 %) up to 75 %. While the
  default is one of the project's grooves it shows that groove's name; dragging it makes the
  default a swing again.
- Changing it is an undo step (a drag is one), and it is saved with the project.

## Ableton's grooves

A Live Set's groove pool comes in with it, and its clips go on playing their grooves (see
[ableton-import.md](ableton-import.md#what-comes-across)). Ableton's groove files can be
added to any project: [Your groove files](#your-groove-files).

What grooves don't do yet: [limitations.md](limitations.md#grooves).

---

For developers: [../groove.md](../groove.md) (the design), [../app/model.md](../app/model.md#grooves),
[../ui/README.md](../ui/README.md#dialogs) (the groove window).
