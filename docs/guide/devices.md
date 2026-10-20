# Devices

The device view, along the bottom of the window (as in Ableton: under the browser and
the arrangement, beside the info view), shows the selected track's (or the master's)
device chain. This page covers working with devices, the built-in devices, racks with
their chains, macros and presets, folding, and cut, copy and paste. VST3 plug-ins have
their own page: [plugins.md](plugins.md).

## The device view

- It shows the built-in Synth and Sampler instruments, the Arpeggiator and Groove MIDI effects, the
  Utility device, Over The Top, Compressor, Gate, Limiter, Multiband Dynamics, Spectral
  Compressor, Saturator, Overdrive, Amp, Erosion, Auto Filter, Bitcrusher, Tape Stop, Glitch, Delay,
  Chorus-Ensemble, Phaser-Flanger,
  Reverb, Vintage Hall, Disperser, Transient Shaper, Vocoder, Vocal Rider, EQ and Sidechain, the LFO
  and Envelope modulators, and plug-ins, all through the same interface: they show alike.
- On a MIDI track the instrument comes first; the master takes audio effects only. A MIDI
  effect (the Arpeggiator, the Groove) sits after the instrument but plays before it: it changes the
  notes the chain's instrument hears, wherever it is in the chain.
- **Ctrl+Alt+L** (*View › Device View*) shows or hides it, with the info view.
- The **info view**, at the bottom left, says what the control under the mouse is: its
  tooltip shows there at once instead of popping up. *View › Info View* hides it; the
  tooltips then pop up again (as they always do in dialogs).
- Add a device by dragging it from the browser onto the chain or onto a track, or by
  double-clicking it in the browser (see [browser.md](browser.md)).

### Title bar

Each device has a title bar, as in Ableton (teal while the device is selected), with:

- the **fold** triangle (see [Folding](#folding));
- its **on/off switch** and **name** (the name's tooltip shows the device's latency, if
  it has any). The switch can be automated (*Device On*: right-click it; see
  [automation.md](automation.md#switching-off-and-on));
- a plug-in's **editor window** button (see [plugins.md](plugins.md));
- the **sidechain** button, on devices with a sidechain input (see
  [mixing.md](mixing.md#sidechains));
- the **MIDI From** button (a note), on devices that play notes: instruments, and effects
  with a MIDI input (see [MIDI from another track](#midi-from-another-track));
- the **parameter page arrows** ‹ ›;
- a **save** button: saves the device as a preset (see [Presets](#presets)).

### Parameters

- Each parameter gets a knob, or a list for parameters that choose between named
  values. Frequency and time knobs turn logarithmically.
- Most devices show four parameters at a time, in a 2×2 grid; the page arrows show the
  others. The Sampler, the Compressor, the Gate, the Limiter, Multiband Dynamics, the
  Spectral Compressor, the Saturator, the Overdrive, the Amp, Erosion, the Auto Filter, the Bitcrusher, the Tape
  Stop, the Glitch, the Delay, the
  Chorus-Ensemble, the Phaser-Flanger, the Reverb, the Vintage Hall, the Disperser, the
  Transient Shaper, the Vocoder, the Vocal Rider, the EQ, the Sidechain, the Arpeggiator, the Groove, the LFO
  and the Envelope have editors of their own (below).
- Clicking a parameter shows its automation in the arrangement. Right-click one to
  *Show Automation*, *Delete Automation* or *Re-Enable Automation*, and, in a rack,
  *Map to Macro* (see [automation.md](automation.md) and [Macros](#macros)); and, on a
  track with a modulator, *Modulate With*, *Invert Modulation* and *Remove Modulation*
  (see [Modulators](#modulators-lfo-and-envelope)).
- A modulated parameter's knob shows the range its modulator moves it over (in the
  modulation colour) and, as it plays, where it is now; its own value shows faintly.
- A parameter mapped to a macro whose automation plays follows that automation (with
  the red dot), as if it were its own.
- Automated parameters are marked with a red dot (grey while overridden) and follow
  their automation as it plays.

### Selecting, moving and deleting

- Click a device (its title or background) to select it, **Shift-click** to select a
  range, **Ctrl-click** to add or remove one.
- Drag effects (or right-click one › *Move Left* / *Move Right*) to move them along the
  chain; the instrument stays first.
- Drag devices onto another track in the arrangement to move them there, with their
  automation; plug-ins move as they are, without loading again.
- Delete a device with the **Delete** key or its right-click menu.
- **Ctrl+Alt drag** anywhere on the chain scrolls it, as in the arrangement.

### MIDI from another track

A device that plays notes can take them from another MIDI track instead of its own:
an effect with a MIDI input, such as a vocoder, a harmonizer or a pitch corrector
(Zynaptiq's Pitchmap, a "chroma" or scale-mapping plug-in), on a vocal or any audio
track, playing along with the chords of a MIDI track; or an instrument layered from
another track's part.

- The **MIDI From** button (a note) in the title bar of such a device opens its menu:
  *Own Track* (what it hears without one), then the project's MIDI tracks (a search
  field over them has the keyboard as the menu opens). The button is lit while it takes
  another track's notes, and its tooltip names that track.
- It hears that track's notes as the track plays them: its clips' notes, with their
  bends and vibrato; the notes you play on the track in the piano roll; and its MIDI input
  while it monitors (armed, or *In*). A MIDI track with no instrument of its own can
  just feed such a device.
- Built-in instruments (Synth, Sampler) and plug-ins with a MIDI (event) input have the
  button; of the built-in effects, Hue (its notes from the chords played) and Changeling (the
  note it sings or transposes by) have it too.
- The notes reach the device when they play on the timeline: plug-in latency on the
  device's own track before it (delay compensation) doesn't delay them. A track's mute
  and solo don't stop its notes reaching a device elsewhere, and a frozen MIDI track
  still plays its notes there (its own devices are in its frozen audio).
- It stays with the device: moved to another track, or copied with it (a copy taking
  the notes of a track copied with it takes the copy's). Deleting the source track, or
  flattening it into audio, gives the device its own track's notes again, in the same
  undo step. Presets don't keep it. Changing it is an undo step, and it is saved with the
  project.

## Built-in devices

### Synth

A polyphonic subtractive synth (16 voices) with sine, triangle, saw and square
oscillators (band-limited saw and square), an ADSR envelope, a resonant low-pass
filter and volume. Velocity sets the level; each voice follows its note's bend
([midi.md](midi.md#pitch-bends-vibrato-and-slides)).

| Parameter | Range |
|---|---|
| Wave | Sine, Triangle, Saw, Square |
| Attack, Decay | 1 to 5000 ms |
| Sustain | 0 to 100 % |
| Release | 1 to 10 000 ms |
| Cutoff | 20 Hz to 20 kHz |
| Resonance | 0 to 100 % |
| Volume | −60 to +6 dB |

### Supersaw

An instrument for one sound, the supersaw, as thick and as clean as it can be made: up
to three stacks of detuned saws on every note, each of 1 to 64 saws, so a chord can
hold thousands of them. 16 notes.

- **Clean at any pitch.** Every saw is band-limited at the instant it jumps, to a
  fraction of a sample, and made at twice the sample rate (below 80 kHz), so nothing
  folds back into what you hear: its aliases are more than 130 dB down, even on the
  highest notes and through bends and glides. The saws keep exact time and exact pitch,
  and a stack is as loud with 64 saws as with one.
- **A stack** is a number of saws spread around its pitch:
  - **Saws** (1 to 64).
  - **Detune**: how far the outermost saws are from the middle, up to a semitone (the
    knob is squared, so most of it is where supersaws live: 50 % is 25 cents).
  - **Curve**: above 0 the saws gather towards the middle (as Roland's JP-8000 spreads
    them, which keeps their beating from settling into a pattern); below 0 towards the
    edges, a wider wash; at 0 evenly spaced.
  - **Blend**: the middle saw against the others. 0 % is the middle saw alone, 75 %
    every saw alike, 100 % the others louder.
  - **Width**: how far the saws spread across the stereo field. Each side hears flat
    and sharp saws alike; at 0 % both sides are the same.
  - **Transpose** (±48 semitones), **Fine** (±100 cents) and **Level**.

  Stack 1 plays on its own at first; switch on stack 2 (an octave down) and stack 3 (an
  octave up), or tune them anywhere, for the fattest stacks.
- **Phase** (0 to 360°) and **Random Phase**: where the saws start on each note. At
  100 % every saw starts anywhere (the classic supersaw); at 0 % they all start
  together at Phase, and the note opens with one big edge before they spread out.
- **Drift**: each saw wanders slowly in pitch on its own, a few cents at most, as an
  analog oscillator does.
- **Sub**: a sine 0, 1 or 2 octaves under the note (at 100 % as loud as a stack).
- **Filter**: Off, Low 12, Low 24, High 12, High 24 (12 and 24 dB an octave) or Band,
  with Cutoff, Resonance, Key Track (the cutoff follows the note) and Env (how far its
  own envelope, Attack, Decay, Sustain and Release, moves the cutoff, up or down).
- **Attack, Decay, Sustain, Release**, **Velocity** (how much a note's velocity sets
  its level) and **Volume**.
- **Mode**: Poly, Mono (one note at a time, its envelopes struck again by each note) or
  Legato (one note, the envelopes struck again only when no key was held). **Glide**:
  how long a note takes to slide from the last note's pitch (in Mono always; in Legato
  between held keys; in Poly from the last note played). When a held key is let go in
  Mono or Legato, it goes back to the newest key still held.
- **Bends**: each voice follows its note's bend ([midi.md](midi.md#pitch-bends-vibrato-and-slides));
  MIDI pitch bend bends every note over **Bend Range** (up to 24 semitones). The sustain
  pedal holds notes. A 17th note takes the place of the quietest releasing one (else
  the oldest), which fades out in 4 ms rather than stopping with a click.

**Its editor** has three pages, named in its title bar: **Stacks**, **Filter** and
**Voice**. On every page the stacks are drawn, a lane each: every saw where it plays,
across by its detune and up and down by where it sits from left to right, as large as it
is loud; while sound plays the saws glow and shimmer as fast as each one beats. Beside
each lane is its stack's switch. Press a lane to show that stack's knobs; drag in it
across for its Detune and up and down for its Width (Shift for finer steps), and
double-click to reset them. Knobs that do nothing as things are (a stack's while it is
off, Blend with fewer than three saws, the filter's while it is off) are dimmed, but you
can still set them.

It is a little late, by 0.5 ms at 48 kHz (1 ms at 44.1 kHz), the time its filters take
to keep everything above 20 kHz from folding back; delay compensation lines it up with
the other tracks.

### Vital

[Vital](https://github.com/mtytel/vital), Matt Tytel's spectral warping wavetable synth,
built in: its own synthesis engine, so it sounds as Vital does, with all of it: three
wavetable oscillators (each with frequency morphing and wave warping, up to 16 unison
voices, stacks and stereo spread), a sample oscillator, two filters (analog, dirty, ladder,
digital, diode, formant, comb and phaser models), six envelopes, eight LFOs, four random
LFOs, four macros, 64 modulations, and its nine effects (chorus, compressor, delay,
distortion, EQ, filter, flanger, phaser, reverb), up to 32 voices, legato and glide, and
oversampling up to 8×. Every one of Vital's parameters is a parameter here, shown as Vital
shows it, and can be automated (all but Oversampling and the effects' order) and mapped to
a rack's macros.

Its editor has nine pages, named in its title bar. On the left, on every page, a small
screen with the preset's name and author over the sound as it plays, the page's choices
(which oscillator, filter, envelope, LFO or random LFO), and the four macros, named as the
preset names them. Beside it a panel draws what the page is about, with its switch (the dot:
click it to switch the oscillator, filter or effect on or off), its name and lists; then the
knobs. Knobs that do nothing with the settings as they are (the formant's without the
Formant model, an LFO's rate while it follows the tempo) are dimmed, but you can still set
them.

| Page | The panel | The knobs |
|---|---|---|
| Osc | Oscillator 1, 2 or 3's wavetable, its frames stacked, the one playing lit and large in front: **drag up and down** to move through the frames (Shift for finer steps; double-click for the first). Above it the wavetable's name, **Wavetable…** (load a `.wav` wavetable, Serum's or Vital's own exported ones, or Vital's `.vitaltable`; any audio file vocoded or pitched into a wavetable as Vital does; or a basic shape: Init, Sine, Triangle, Saw Up, Saw Down, Square) and where it plays (Filter 1, 2, both, the effects, straight out); below it the frequency morph's, the warp's and the unison stack's types | Level, pan, transpose, tune, frame, phase and its randomness, unison voices, detune and blend; morph, warp and their spreads, the warp's phase, the detune's range and power, stereo, the frames' spread |
| Sample | The sound as it plays; where the sample oscillator plays; Loop, Bounce, Key Track, Random Phase | Level, pan, transpose, tune |
| Filter | Filter 1 or 2's response, over the sound's spectrum: **drag** its handle across for the cutoff and up and down for the resonance (double-click to reset); its model and style below | Cutoff, resonance, drive, blend, key track, mix; the comb's offset and the formant's controls |
| Env | Envelope 1 to 6 (Envelope 1 is the voices' amplitude), drawn as it plays, with a dot moving through it as a note plays: **drag** the stages' ends across (the decay's up and down for the sustain) and the small dots on the curves up and down to bend them; double-click a handle to reset it | Delay, attack, hold, decay, sustain, release and their curves |
| LFO | LFO 1 to 8's shape, with a dot riding it as a note plays; **Shape…** sets a basic one; below it its mode (trigger, sync, envelope, loop), its timing (seconds, the tempo, dotted, triplets, the key) and Smooth | Rate or tempo, phase, stereo, delay, fade in, smoothing time, key track |
| Random | The sound as it plays; the random LFO's style, timing and Sync | Rate or tempo, stereo, key track |
| FX | The nine effects as tiles, lit while on (the dot switches one); click one to show it | That effect's controls |
| Matrix | The modulations: choose a source (an envelope, an LFO, a random LFO, a macro, velocity, the note, aftertouch, the wheels...) and a destination (any of its parameters) and **Add**; each row has its amount (the knob, and a bar from the middle), Bipolar, Stereo, Bypass, and × to remove it | |
| Voice | The sound's spectrum, and how many voices play | Volume, voices, velocity, bend range, tune, transpose, glide and its slope, the wheels; voice priority and stealing, legato, glide always, octave scaling, stereo routing and mode, quality (oversampling), amplitude |

The filter's drawing is the shape of its response (what Vital's own window draws), not its
exact curve.

**Vital's presets.** Right-click Vital › *Load Vital Preset…* loads a `.vital` file (Vital's
own presets, and those made by others: older versions' are upgraded as Vital upgrades them);
*Save Vital Preset…* saves the patch as a `.vital` file Vital opens. Dropping a `.vital` file
(from a file manager, or the browser's Presets, where `.vital` files in the preset library's
folders show) on a Vital loads it, and on a track or between devices adds a Vital with it. A
loaded preset is one undo step, and the patch (its wavetables, LFO shapes, sample and
modulations too) is saved with the project and in SUBstation presets. Vital's own factory
presets and wavetables come with Vital, under their own licence: install Vital to have them,
and load them from its folder.

**Notes.** Each note plays on its own MIDI channel, as an MPE controller sends them, so a
note's bend ([midi.md](midi.md#pitch-bends-vibrato-and-slides)) bends that note alone, up to
48 semitones either way. MIDI pitch bend (over *Bend Range*), the mod wheel, sustain and
sostenuto pedals, channel and polyphonic pressure and CC74 (MPE's slide) reach every note.

Changing the patch's wavetables, LFO shapes or modulations, a preset, or Oversampling builds
Vital's engine anew in the background; the notes held are struck again on it while the old
sound fades out over 10 ms. What Vital's own window has that this editor doesn't yet: the wavetable editor, drawing
LFO shapes and modulation curves (presets keep theirs), loading a sample, tunings (presets
keep theirs) and the effects' order.

### Sampler

An instrument that plays one audio file, as Ableton's Simpler does, in one of three
modes (the tabs at its left). Each note follows its bend
([midi.md](midi.md#pitch-bends-vibrato-and-slides)), in every mode:

- **Classic**: played across the keyboard, pitched from its root key (up to 32 notes at
  once, *Voices*), with an ADSR envelope; it plays from Start to End, or with **Loop**
  on loops from Loop Start to End while a note holds and as it releases. The loop's
  **Fade** crossfades its end into what leads to its start, so a loop that doesn't
  meet itself cleanly doesn't click. With one voice and a **Glide** time, notes played
  legato glide from one to the next (letting go of the top note glides back to the one
  still held).
- **1-Shot**: one note at a time (a new one cuts the last), pitched; **Trigger** plays
  the whole of Start to End however short the note, **Gate** fades out when the note
  ends. **Fade In** and **Fade Out** shape each note (it also fades out before End).
- **Slice**: the sample cut into slices, a slice per key from C1 up (C1 the first,
  C#1 the next...), each at the sample's pitch: at its **transients** (more
  **Sensitivity** finds quieter ones), at **beats** (every 1/16 to 4 bars of the length
  Warp gives it), or into equal **regions**. **Mono**: a slice cuts the one before;
  **Poly**: they overlap; **Thru**: a slice plays on to End. Trigger, Gate and the fades
  as 1-Shot's.

Its display shows the sample, the part that plays lit (Start and End flagged at the
top), and by mode the loop (bracketed, its crossfade shaded), the fades, or the slices
(numbered, the one playing lit); the newest note's position as it plays; the time along
the bottom.

- Drop an audio file on the display (from the browser or the desktop) or double-click it
  to load one; the right-click menu has *Load Sample…*, *Clear Sample* and *Reverse*.
  An audio clip dragged down out of the arrangement onto it loads its part of its file
  (Start and End set to that part).
- **Similar sounds**: the **‹** and **›** arrows at the display's bottom right swap in the
  sounds most like the sample (as the browser's *Find Similar Sounds* finds them in its
  places), most like it first, one at a time; each swap is an undo step. They step from
  a **reference**: the sample you started stepping from, so **‹** goes back through what
  you heard to it. Right-click › *Save as Similar Reference* makes the sample playing the
  reference (it stays until you save another, whatever you load meanwhile); *Return to
  Reference* plays it again. The reference is saved with the project.
- Drag Start, End and (Classic, looping) Loop Start on the display.
- Under the display: **Gain**; the mode's own settings; **Snap** (Start, End, Loop
  Start and slices move to the nearest zero crossing, so notes don't click as they
  start); **Warp** *as* a length: the whole sample lasts that many beats at the song's
  tempo and follows it. Its warp modes are the clips': Transients, Standard, Smooth and
  Formants stretch it (the keys transpose it, not its length), Re-Pitch speeds it up or
  slows it down like a record (the pitch goes with it). **:2** and **\*2** halve and
  double the length.
- Under that: the **Filter** (low-pass, high-pass, band-pass or notch, 12 or 24 dB an
  octave, frequency, resonance), the **LFO** (sine, triangle, saw up and down, square or
  random; its rate in Hz or synced to the song, from 1/32 to 8 bars), the envelope
  (Classic's Attack, Decay, Sustain, Release; the others' Fade In and Fade Out),
  **Transp**, **Vol < Vel** (how much velocity sets the level) and **Volume**.
- The **Controls** page (its tab in the title bar) has the rest: the root key, detune,
  voices and glide; Start, End, Loop Start and Loop Fade as knobs, **Reverse** (the
  sample plays backwards, and is drawn so: the markers and slices are places in it as it
  plays); where the LFO goes (**Volume**, **Pitch**, **Filter**, **Pan**) and
  **Retrig** (each note starts the LFO from the start of its cycle; synced without it,
  the LFO follows the song's beats); **Pan** and Gain.
- Loading a sample can be undone, and the sample is saved with the project. Files load
  in the background, and the engine swaps them in without stopping the audio.
- Warped notes that stretch (not Re-Pitch) cost more: each one runs a stretcher (up to
  8 at once), and starting a note computes the stretcher's first block ahead. Warped to
  play more than 4 times as fast (a long sample in few beats), notes are resampled
  instead.

| Parameter | Range |
|---|---|
| Mode | Classic, 1-Shot, Slice |
| Root Key | MIDI note 0 to 127 (C3 = 60 by default) |
| Transpose | −48 to +48 semitones |
| Detune | −100 to +100 cents |
| Start, End | 0 to 100 % of the sample |
| Gain | −24 to +24 dB |
| Reverse, Snap | Off, On |
| Warp | Off, On |
| Warp Length | 1 beat to 64 bars (the whole sample) |
| Warp Mode | Transients, Standard, Smooth, Formants, Re-Pitch |
| Loop | Off, On (Classic) |
| Loop Start | 0 to 100 % of the sample (from Start at the earliest) |
| Loop Fade | 0 to 100 % of the loop |
| Attack | 0.1 to 5000 ms |
| Decay | 1 to 10 000 ms |
| Sustain | 0 to 100 % |
| Release | 1 to 10 000 ms |
| Voices | 1 to 32 (Classic; Slice's Poly) |
| Glide | 0 to 2000 ms (one voice) |
| Trigger Mode | Trigger, Gate (1-Shot, Slice) |
| Fade In, Fade Out | 0.1 to 2000 ms (1-Shot, Slice) |
| Slice By | Transient, Beat, Region |
| Sensitivity | 0 to 100 % |
| Slice Division | 1/16, 1/8, 1/4, 1/2, 1 Bar, 2 Bars, 4 Bars |
| Regions | 2 to 64 |
| Playback | Mono, Poly, Thru |
| Filter | Off, On |
| Filter Type | Low-pass, High-pass, Band-pass, Notch |
| Filter Slope | 12 dB, 24 dB |
| Filter Freq | 20 Hz to 22 kHz |
| Resonance | 0 to 100 % |
| LFO, LFO Sync, LFO Retrigger | Off, On |
| LFO Wave | Sine, Triangle, Saw Up, Saw Down, Square, Random |
| LFO Rate | 0.01 to 30 Hz |
| LFO Synced Rate | 1/32 to 8 Bars |
| LFO > Volume, Filter, Pan | 0 to 100 % (Filter: 4 octaves either way at 100 %) |
| LFO > Pitch | 0 to 1200 cents either way |
| Pan | Left to right |
| Vol < Vel | 0 to 100 % (how much velocity sets the level) |
| Volume | −60 to +6 dB |

### Utility

Gain (−60 to +24 dB), pan and stereo width (0 to 200 %).

### Over The Top

A multiband upward/downward compressor in the style of the one every drop uses, with a
single big **Soundgoodize** knob (depth) and an **Output** trim (±24 dB). Three bands
(split at 88 Hz and 2.5 kHz), each squashed from above and dragged up from below. At
0 % it passes the audio through untouched.

### Compressor

A soft-knee compressor, keyed by its own input or, with a sidechain, by another track's
signal (for ducking; see [mixing.md](mixing.md#sidechains)).

| Parameter | Range |
|---|---|
| Threshold | −60 to 0 dB |
| Ratio | 1:1 to 20:1 |
| Attack | 0.1 to 200 ms |
| Release | 5 to 2000 ms |
| Knee | 0 to 24 dB |
| Makeup | −12 to +24 dB |
| Dry/Wet | 0 to 100 % |

Its editor shows every knob at once and, beside them, the gain reduction over the last
second (growing downward, up to 24 dB) and meters of what keys it (the threshold
marked) and of its output, from −60 to 0 dBFS.

### Gate

A gate after Ableton's: it lets through only what is louder than the **Threshold** and
turns everything else down to the **Floor**. Use it to take out the hiss or hum between
notes, a drum mic's spill between hits, or, with the threshold higher, to cut a reverb's
or a delay's tail short. It can be opened by another track instead (see
[mixing.md](mixing.md#sidechains)).

| Parameter | Range |
|---|---|
| Threshold | −70 to +6 dB |
| Return | 0 to 24 dB |
| Attack | 0.02 to 150 ms |
| Hold | 1 to 1500 ms |
| Release | 0.1 to 3000 ms |
| Floor | −inf to 0 dB |
| Lookahead | 0, 1 or 10 ms |

- **The display** shows the last two and a half seconds: what comes in as a light grey
  band, what comes out darker with a white outline over it, so what the gate takes away
  is the light part above the dark. Where the gate was open the whole height is tinted
  blue. The LED at the top left says whether it is open now ("Idle" while nothing comes
  through: the device switched off, or the audio stopped), and the two meters on the
  right show the input's level (**In**) and how far the gate turns it down (**Gate**),
  against the same figures as the display.
- **Threshold**: the gate opens when the level reaches it. It is the blue line across
  the display: drag it up and down (or drag anywhere in the display's graph), Shift for
  small steps, double-click it to put it back, right-click it for its menu (its
  automation, for instance). Set it so the sound you want crosses it and the noise
  doesn't: the dot on the display's right edge is the level now (it falls quickly rather
  than jumping), blue while the level is at or above the threshold and grey as soon as
  it is below.
- **Return**: how far the level has to fall below the threshold before the gate closes
  again: the orange line. Raise it when the gate chatters, opening and closing quickly
  on a sound that hovers around the threshold. Drag the orange line down for more (at
  0 the two lines are one: press on or just below it to drag Return, above it for the
  threshold).
- **Attack**: how long the gate takes to open. Very short can click; longer softens the
  start of each sound.
- **Hold**: how long it stays open once the level has fallen below the orange line, to
  bridge short gaps (the default 10 ms keeps it open through a low note's waves).
- **Release**: how long it then takes to close.
- **Floor**: how far a closed gate turns the sound down: at −inf, silence; at 0 dB, not
  at all. In between the gate only dips the sound between notes.
- **Flip**: the gate works the other way round: only what is quieter than the threshold
  passes (the loud parts are turned down to the floor).
- **Lookahead**: the gate sees what is coming this much ahead, so it is already open
  when a sharp attack arrives and doesn't cut its first moment off. It delays the track
  as much (1 ms by default, as in Ableton); the rest of the mix is delayed to match.

**The sidechain**: click the **Sidechain** strip at the device's left edge to unfold its
section (click it again to fold it; it lights up while what opens the gate isn't simply
the track itself).

- **Where the key comes from**: the button at the top (or the sidechain button in the
  title bar) chooses a track to open the gate with: a kick to gate a bass or a noise pad
  in time, for instance. That track is never heard through the gate. **Gain** sets how
  loud it is to the gate, and **Dry/Wet** blends it with the track's own sound (100 %:
  only the other track opens it; 0 %: only its own sound). Both are dimmed without a
  sidechain, but can be set beforehand.
- **EQ**: opens the gate from one band of the key only, so a snare mic opens for the
  snare and not for the hi-hat (a low-pass, or a band-pass around the snare's body; a
  high-pass keeps the kick out), or a kick mic for the kick's lows (a low-pass). Pick
  the filter with the six buttons (low shelf, bell, high shelf, low-pass, band-pass,
  high-pass), then set **Freq**, **Q** (how narrow: bell, low-, band- and high-pass) and
  **Gain** (shelves and bell; the one a filter doesn't use is greyed out). The curve
  under them shows the filter; drag its dot across for the frequency and up and down for
  the gain or the Q (the bell's Q with Ctrl held, or the mouse wheel over the dot),
  double-click it to reset them. It works on the track's own sound too, without a
  sidechain. While the EQ is off its controls and curve are dimmed, but they can still
  be set, ready for when it is switched on.
- **Listen** (the headphones): hear the key, what the gate listens to (the sidechain,
  through the EQ), instead of the gate's output: handy for tuning the EQ. Switch it off
  again to hear the result.

Every control except Lookahead can be automated (Listen too, as in Live), and none of them
clicks when it moves.

### Limiter

A brick-wall limiter after Ableton's (Live 12.1): nothing comes out louder than its
ceiling, however loud what goes in. It looks a few milliseconds ahead, so it turns the
gain down smoothly just before a peak instead of clipping it. Put it last, on the master
(or on a track or bus you want held under a level), and turn **Gain** up until the gain
reduction shows the loudness you want: a few dB is transparent; much more squashes the
sound.

- **Gain** (−24 to +24 dB): pushes the input into the ceiling (or pulls it back).
- **Ceiling** (−24 to 0 dB): the most that comes out. Set it in the box at the top left
  of the display, or drag the line across the display up and down (Shift for finer
  steps; double-click it for −0.3 dB).
- **Maximize**: loudness from one control. The ceiling becomes a **Threshold** (the
  line, and its box) and the Gain knob an **Output** knob: lower the Threshold and
  everything comes up by as much, while what reaches it comes out at the Output level.
  At their defaults (both −0.3 dB, as the Ceiling's) switching it on changes nothing but
  setting Gain aside; lower the Threshold from there.
- **Release** (0.1 ms to 3 s): how fast the gain comes back after a peak. Short is
  louder and punchier; long is smoother. **Auto** (on by default) sets it from the
  music: quick after short peaks (about 50 ms), slower the longer limiting goes on (up
  to 600 ms), so a held bass note isn't distorted by the gain following its every cycle,
  and the level comes back soon after a lone peak. The Release knob is dimmed while Auto
  is on; you can still set it, for when you switch Auto off.
- **Lookahead** (1.5, 3 or 6 ms): how far ahead it sees peaks. Shorter is punchier but
  can distort the lows; take 6 ms for bass-heavy material. It is the device's latency:
  the other tracks are delayed to line up with it.
- **Mode**: **Standard** keeps every sample under the ceiling. **Soft Clip** rounds
  peaks off as they near the ceiling (from 6 dB under it) instead of turning everything
  down: louder, with some crunch. It works at the project's rate (no added latency), so
  bright sounds (above about 8 kHz) driven into it get some inharmonic grit as well: on
  cymbals or a bright synth alone, Standard is cleaner. **True Peak** also keeps the
  peaks *between* samples under it (what a converter or a lossy encoder may make of
  them): use it for streaming and mastering deliveries, with the ceiling at −1 dB.
- **L/R** or **M/S**: limit left and right, or the middle and the sides. **Link** (0 to
  100 %) is how much of one channel's gain reduction the other shares: at 100 % both are
  turned down together (the stereo image stays put); lower, each more on its own. In M/S
  with Link down, a loud centre (the kick, the vocal) no longer pulls the sides down
  with it. On very wide material, where the sides are as loud as the centre, M/S limits
  more than L/R: left and right are still kept under the ceiling.
- **The display** shows the last second and a half: the input in grey (red where it goes
  over the line), the output in light grey inside it, and the gain reduction in orange,
  hanging from the top. Beside it are meters of the input (In), the gain reduction (GR)
  and the output (Out), with their peaks of the last second under them, and the gain
  reduction of the last half second at the bottom left. The line glows orange while it
  limits. In Soft Clip a band shows where it rounds off, and what it rounded off shows
  in lighter orange under the gain reduction (it counts in both gain reduction figures,
  and warms the line too). With Maximize the history shows the input as it comes in and
  the output as far under the Output as the line is under it, so the loudest output
  meets the line; the Out meter and its figure still read the level that comes out.
- Every control but Lookahead can be automated, and changes don't click: the levels
  glide, the modes crossfade, and a new Lookahead fades out and back in (for a few
  milliseconds) as the latency changes.

### Multiband Dynamics

A three-band compressor and expander after Ableton's Multiband Dynamics, made mainly for
mastering and for shaping a mix's parts apart: tame the highs (de-essing) without
dulling the rest, tighten the lows, lift the quiet detail of a band for density, or the
squashed, everything-loud sound of Over The Top. Each band has **two** thresholds, so
two kinds of processing work on it at once:

| Region | Ratio over 1:1 (1:2, 1:4...) | Ratio under 1:1 (1:0.500...) |
|---|---|---|
| **Above** the Above threshold | downward compression: loud parts come down | upward expansion: loud parts go further up |
| **Below** the Below threshold | upward compression: quiet parts come up | downward expansion: quiet parts go further down (a gate, at the extreme) |

Ratios read as Live writes them, "1:4": how many dB past the threshold come out as one.
At 1:4 above −20 dB, a level of −8 dB (12 dB over) comes out at −17 dB (3 dB over); at
1:4 below −40 dB, −60 dB comes up to −45 dB. Under 1:1 the distance grows: at 1:0.500 it
doubles. 1:1 does nothing, which is where a new device starts: it changes nothing until
you set a ratio.

**The editor** has a row per band, High on top:

- **High**, **Mid** and **Low**: the bands' buttons, their activators. A band switched
  off here is bypassed: its Input, Output and dynamics stop acting, but its frequencies
  stay its own (no other band shapes them). **S** solos a band (more than one can be
  soloed).
- The **crossovers** under High and Low (Low-Mid 30 Hz to 3 kHz, Mid-High 300 Hz to
  15 kHz; 120 Hz and 2.50 kHz to start) set where the bands split, 24 dB per octave.
  Left as they are, the bands add back up to the input, with no dip or bump at the
  splits. The switch beside each splits its band off: switched off, the High (or Low)
  band's frequencies belong to the Mid band, which shapes them with its own settings
  (with both off the device is a single-band compressor and expander, Mid's controls
  acting on everything). As in Live, these two switches can't be automated. The
  controls of a band switched off or bypassed dim but can still be set.
- **Input** and **Output** per band (±24 dB): a band's level before its dynamics (moving
  it against its thresholds) and after them.
- **The display**: each band's lane, from −80 dB at the left to +6 dB at the right. The
  thick bar is the band's level after its dynamics, the thin bar under it its level
  before them, and the stretch between them shows the change, in orange where the level
  is brought down and teal where it is brought up, its figure at the lane's top right. A
  small triangle over the bar marks where the level is heading while attack or release
  are still on their way. The Below region is the block at the left, the Above region
  the block at the right, tinted the same way by what they do, and the more densely
  striped the further their ratio is from 1:1. A side glows while it is acting on the
  band. Hover a lane to read what a level comes out at ("−30.0 → −34.5 dB").
  - Drag a block's edge left or right for its threshold (Above and Below never cross:
    one pushes the other).
  - Drag inside a block up or down for its ratio: the block's level follows the mouse,
    so in the Above block *down* compresses and *up* expands, and in the Below block
    *up* lifts the quiet parts and *down* pushes them further down.
  - **Ctrl** while dragging moves every band's threshold (or ratio) together; **Alt**
    moves both thresholds of a band together, keeping the gap between them; **Shift**
    drags finely. Double-click an edge for its threshold's default, a block for 1:1. The
    mouse wheel works on edges (half a dB a notch) and blocks too; a touchpad's small
    steps add up, and a turn of the wheel stays with the edge it started on as the edge
    moves away from the mouse. Over the device chain, Shift with the wheel and
    Ctrl+Alt-dragging scroll the chain, as everywhere there.
- **T**, **B** and **A**, over the bands' buttons, switch the two fields beside every band
  between its **T**ime (Attack and Release, each 0.1 ms to 5 s), the **B**elow threshold
  and ratio, and the **A**bove threshold and ratio (−80 to 0 dB; 1:0.250 to 1:100). Type a
  ratio as Live writes it ("1:4", Over The Top's "1:66.7"), as the number after "1:" ("4",
  "0.5"), or as a compressor's ("4:1"); type a time as "250 ms" or "1.5 s" (a bare number
  is milliseconds). Above, Attack is how fast compression or expansion comes when the
  level rises past the threshold and Release how fast it lets go when it falls back;
  Below, the same as the level drops under the threshold and comes back.
- **Amount** (0 to 100 %) scales every ratio's effect: at 0 % nothing is compressed or
  expanded, so it dials a heavy setting back. **Time** (10 to 1000 %) scales every
  attack and release together. **Output** (±24 dB) is the device's output.
- **Soft Knee** brings compression and expansion in gradually over 6 dB around the
  thresholds. **Peak** reacts to short peaks; **RMS** (the default) to the average
  level, letting short peaks through. On stereo, both follow the louder side.
- **Sidechain** (the button, or the title bar's): another track's signal keys the bands,
  each by its own band of it, so a kick ducks the lows and a vocal the mids. **S/C Gain**
  (−70 to +24 dB) sets the key's level, **S/C Mix** how much of the trigger is the key
  rather than the device's own input; both can be set before a sidechain is chosen.
  **Listen** (the headphones) lets you hear what the bands react to instead of the
  output: the key, as much of it as S/C Mix takes (without a sidechain, the input). See
  [mixing.md](mixing.md#sidechains).

Every control can be automated (but the solos, the split switches and Listen, as in
Live) and changes without clicks. The device
adds no latency. Upward compression fades out between −72 and −96 dB, so silence, and
whatever lies under −96 dB, isn't lifted; a noise floor above that is, as Over The Top
lifts it (hiss at −80 dB under a Below of −40 dB at 1:4 comes up 20 dB).

The Over The Top sound, by hand: on every band Above −30 dB at 1:66.7 and Below −40 dB
at 1:4, attack 10 to 50 ms, release 130 to 280 ms, Output +6 dB; then bring Amount down
until it is enough.

### Spectral Compressor

A compressor for every frequency at once. It splits the sound into about a thousand
narrow bands, and turns each one down where it is louder than a threshold line drawn
across the spectrum, and (if you want) up where it is quieter than a second line,
leaving the rest alone. Use it to tame harshness and ringing resonances where they
happen, to even out a spectrum, to bring up low-level detail and air, or, keyed by
another track, to duck one sound's frequencies only where they clash with another's (a
bass under a kick, a pad under a vocal).

**The display** shows the sound in the middle of the device, from 20 Hz at the left to
20 kHz at the right:

- grey (filled): the spectrum coming in; blue (a line): the spectrum going out;
- the **orange line**: the threshold. Where the orange glows between it and the
  spectrum, the sound is over it, and that is what is being turned down;
- orange **hanging from the top**: how far each frequency is turned down right now
  (24 dB reaches halfway down), with a thin line holding the deepest recent cut for a
  moment before it falls away;
- the **green line** (while Upward is past 1:1): Below. Green **rising from the
  bottom**: how far each frequency is brought up;
- the dimmed sides: outside the **Focus** band, where nothing is changed;
- the figures at the top right: the deepest cut now (and the biggest lift); the
  **Sidechain** badge at the top left lights up while another track keys it (click it to
  choose one, as with the sidechain button in the title bar); the meters at the right
  edge are the level in and out.

**Levels are measured against pink noise**: pink noise at −20 dBFS reads −20 dB at every
frequency, so a dense mix reads around its loudness across the spectrum (a mix at −14 dB
roughly −14 to −20 dB). A pure tone reads about 20 dB above its peak level (all its
energy is in one place), which is why resonances, and bass notes, stand out. As it
comes, with the threshold at −18 dB and a ratio of 2:1, it takes a few dB off a dense
mix, most where tones stand out (bass notes, ringing), and leaves pink noise at −20 dBFS
nearly alone (at −14 dBFS about 2.5 dB).

**Setting it up**:

- **Threshold** (−72 to +12 dB) and **Ratio** (1:1 to 20:1): drag the orange line (its
  dot at 1 kHz, or anywhere on it) up and down, or turn the knobs. Start with the line
  just under the loud parts and lower it until the orange glow covers what bothers you.
  (At 1:1 nothing is turned down, and the glow goes out.)
- **Tilt** (−6 to +6 dB per octave): drag either end of the orange line (the small
  diamonds at 100 Hz and 10 kHz; turned steeply enough to leave the display, a diamond
  waits where the line leaves it) to turn it, and the green line with it, about 1 kHz.
  At 0 the threshold follows pink noise; most mixes sit a little under pink at the top,
  so at 0 their highs are treated more gently than their lows. A slightly negative tilt
  lets the line follow such a mix, treating the highs as firmly as the lows; raise it to
  leave the highs alone.
- **Below** (−72 to +12 dB) and **Upward** (1:1 to 10:1): with Upward past 1:1, the
  green line appears at Below; frequencies under it are brought up towards it, for
  density and detail. Below is never above the threshold (dragged over it, it stays on
  the orange line). Silence and hiss far down (under −90 dB) are left alone. While
  Upward is at 1:1 the Below knob is dimmed: it does nothing then, but you can still
  set it ready.
- **Smoothing** (0 to 100 %): how wide a band each frequency's level is measured over.
  Low, each narrow band is judged alone: the most selective, for single resonances
  (whistles, ringing, a harsh "s"). High (up to two octaves), broad and gentle: for tone
  shaping.
- **Knee** (0 to 24 dB): how gradually it starts acting around each line. **Range**
  (0 to 48 dB): the most any frequency is turned down or up, a safety net.
- **Focus** (Low and High, 20 Hz to 20 kHz): drag the faint edges at the plot's bottom
  corners in from the sides, or set the two boxes beside the display (drag, scroll or
  type a value; right-click for automation and macros), to work on one region only, for
  example 2 to 8 kHz for harshness or 150 to 500 Hz for mud. Nothing outside is changed;
  the change fades out over a third of an octave beyond each edge.
- **Attack** (1 ms to 1 s) and **Release** (10 ms to 5 s): how fast each frequency's
  gain follows its level down and recovers, as on the Compressor (the same 150 ms
  releases as fast here as there). (The analysis itself smooths onsets over about 20 ms,
  so the shortest attacks act alike.)
- **Stereo Link** (0 to 100 %): at 100 % both channels get the same gains (the louder
  one's), keeping the stereo image; at 0 each channel is compressed on its own.
- **Dry/Wet** (0 to 100 %) and **Output** (±24 dB). The dry sound is delayed to stay in
  time with the processed one, so they blend cleanly.
- **Delta**: hear only what the device changes: what it takes away (what it adds comes
  out inverted). The blue line turns red and shows that spectrum (the orange from the
  top fades back, as the line now shows it). Use it while setting Threshold and
  Smoothing: you should hear only the harshness or the ringing, not the music.

Shift drags a tenth as fast, from where you press it on; double-click a line, a handle
or an edge to reset it. Every drag is one undo step, and every control can be automated.

**Keyed by a sidechain**, the other track's spectrum sets the gains: where it is loud,
this track is turned down at those frequencies only. The other track's levels show as a
dashed yellow line.

It adds **latency**: 53 ms at 48 kHz (58 ms at 44.1 kHz). Playback is compensated, but
you'll feel it while monitoring live input through it.

### Hue

Pulls a sound into key. Hue keeps the parts of the spectrum that sit on the notes you
choose, moves the notes a semitone off them onto them, takes the other notes out, and lets
what lies between notes through more quietly: a growl, a reese or a riddim bass keeps its
texture while its partials line up with the song's scale. It is made for basses and other
dense, harmonic sounds, and works on anything.

- **The editor** is a spectrum of what comes out, across the device: each frequency coloured
  by the note it is, as vivid as **Color** makes the sound, over a faint line of what goes
  in; thin lines in the notes' colours mark the notes it pulls the sound onto. A soft
  filter curve shows the band processed: drag across the far left for **Low Cut** and the
  far right for **High Cut** (Shift: finely); double-click there to reset it. Drag up and
  down in the band for **Color** (the gauge at the right edge shows it); double-click to
  reset it. Over its top edge: the **Key** (a
  root and a scale) and **HQ**; the arrow at the right slides open the note wheel, the
  twelve notes round a colour wheel (C at the top): lit notes are kept, shaded ones with an
  arrow are moved onto the note beside them, dark ones are taken out; click a note to
  switch it. Inside it the sound plays as petals (what comes out) over a faint outline
  (what goes in), and the middle glows in the colour the sound is now. Beside the wheel:
  **Wet Gain**, **Pre-Shift**, **Post-Shift**, **Mute Low** and **Mute High**.
- **The notes**: at first the key is **Auto**: Hue follows the song's key, the project's
  key or, without one, the key its MIDI tracks are in (as the piano roll's chord lane
  infers it), using its major or natural minor scale; the key menu shows it ("Auto · Am").
  With no key to follow yet, the sound passes. Choose a root (C to B) to pick the key
  yourself: a scale menu appears beside it (seventeen scales from Major to Power Chord
  and Root Only; at first the song key's major or minor). Clicking notes on the wheel also
  leaves Auto, starting from the notes it had. A note in any octave counts. Notes that
  are no scale show as Custom, and a new root moves them with it. With no notes, or all
  twelve, the sound passes as it is. Choose Auto again to follow the song.
- **MIDI**: play Hue notes and they choose the notes instead of the key: put it on a MIDI
  track after an instrument, or pick a MIDI track with **MIDI From** (see above) so the
  scale follows a chord part. Each chord holds until the next one; until the first note
  comes (and after the transport stops) the key's notes apply. The Key shows *MIDI Notes*
  meanwhile, and the wheel and the spectrum show the notes as they are played. (The
  parameter **MIDI Notes**, on at first, switches this off, for automation.)
- What happens to each note: an enabled note is kept and made louder (the fewer notes, the
  louder: about +8 dB for one); a note a semitone from an enabled one is moved onto it
  (a note between two enabled ones goes down); the others are taken out. What lies between
  notes (detuned, noisy parts) passes more quietly, more so the fewer notes are enabled.
- **Low Cut** and **High Cut** (the parameters Start and End; 80 Hz to 1.8 kHz, 400 Hz to
  8 kHz): the band that is processed; below and above it the sound passes as it is (the
  low band keeps a bass's sub untouched). The band works within 160 Hz to 4 kHz whatever
  they are set to.
- **Color** (0 to 200 %): from the plain sound (0 %) to the processed one (100 %); past
  100 % the notes get louder and narrower and what lies between them quieter.
- **Wet Gain** (±6 dB): the level of the processed band.
- **Pre-Shift** (±50 cents): shifts the band's pitch before the notes are picked (to catch
  a detuned sound); **Post-Shift** (±50 cents): tunes the notes themselves.
- **Mute Low** and **Mute High**: take the bands outside Start and End away, to hear the
  processed band alone.
- **Quality**: **Low Latency** moves the notes a semitone off through two shared shifters,
  landing a few cents from the note with a little roughness (part of its sound).
  **High Quality** moves each note through a shifter of its own, tuned to that note: it
  lands on the note exactly and cleanly. The latency is the same; it costs about twice the
  CPU (still about half a percent of a core). Switching crossfades.

Hue's latency is about 11 ms (the engine lines the other tracks up with it). Changing
the notes takes a few milliseconds to be heard and fades in, so chords played from MIDI
change smoothly.

### GigaTune

Pitch correction for a single voice: a sung or rapped part, a lead line. GigaTune hears the
pitch, picks the note of the key it is nearest, and moves the voice onto it, its formants
(the character of the voice) staying where they are. It is made to be exact: a held note
comes out on its note's frequency to a hundredth of a cent, and with nothing to do the sound
passes untouched, sample for sample.

- **Key** and **Scale**: the notes it corrects to. At first both are **Song**: GigaTune
  follows the song's key (the project's key or, without one, the key its MIDI tracks are
  in), with its major or natural minor scale. Choose a root (C to B) or a scale to pick
  them yourself: Chromatic, Major, Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian,
  Lydian, Mixolydian, Locrian, Major Pentatonic, Minor Pentatonic or Blues. With Song and
  no key known it corrects to all twelve notes. Another note takes over only once it is
  nearer by a fifth of a semitone, so a voice hovering between two notes doesn't flip back
  and forth.
- **Retune Speed** (0 to 400 ms): how fast the voice is pulled onto the note (the
  correction's time constant), from its own pitch at the start of each note. At 0 it is on
  the note at once and held there exactly: the hard, stepped sound. Slower settings keep
  scoops and vibrato and only pull the note's centre into tune.
- **Strength** (0 to 100 %): how much of the correction is made. At 0 the voice passes as
  it is.
- **Pitch** (±12 semitones): moves the corrected voice up or down, its formants kept.
- **Formant** (±12 semitones): moves the formants without moving the pitch: deeper or
  smaller-sounding. It acts on the voiced sound; breaths and sibilants pass as they were.
- **Quality**: how it hears the pitch, refined to a hundredth of a cent either way.
  **High Quality** (at first) uses SwiftF0, a small neural network trained to find a
  voice's pitch: it holds on to the voice through breath, noise and reverb, where other
  trackers drop out or slip to an octave or another multiple below. **Low Latency** uses
  pYIN, the probabilistic tracker librosa uses (from the vendored fastpitch library), and
  works with as little latency as MAutoPitch: it hears each moment from the 32 ms around
  it and decides at once, holding the last pitch it heard for the few milliseconds it
  hasn't heard yet. It tunes held notes about as closely as High Quality; where a note
  starts it is a few milliseconds behind. It is also better on stacked or doubled vocals
  and harmonies, where it follows the period the voices share (so correcting it keeps
  their intervals), while High Quality picks one voice. Quality can't be automated:
  changing it changes the latency, and the device starts again (a short silence).
- It moves the pitch by cutting the voice into grains one period long, centred on its
  pulses, and laying them a new period apart (PSOLA), so the voice keeps its timbre.
  It is made for one voice at a time: on chords, doubled vocals or a full mix it has no
  single pitch to follow. Both channels are moved alike, by the pitch of their sum.
- It adds **latency**: 21 ms with Low Latency (MAutoPitch's: 1024 samples at 48 kHz) and
  about 277 ms with High Quality (SwiftF0 decides each moment 193 ms after it, and the
  grains need the periods either side), at any sample rate. Playback is compensated; Low Latency is short enough to sing through
  while recording, High Quality isn't.
- **The editor** is compact: Key, Scale and **HQ** (Quality: lit for High Quality, off for
  Low Latency) along the top, and under them a tuner-like display beside the Retune Speed,
  Strength, Pitch and Formant knobs. The display shows the note the voice is being
  corrected to, as it comes out (under it the note that comes out, while Pitch moves it),
  and a cents scale of that note: the needle is the pitch heard, the lit dot where the
  correction takes it, and the band in the middle glows green while the result is in tune.
  Along the bottom are the twelve notes, those the key and scale correct to lit and the
  one being corrected to glowing. Song's choices name the song's key once it is known.

### Changeling

A pitch and formant shifter for a single voice: turn a vocal up or down, make it bigger or
smaller, snap it to notes, hold it on one, or mangle it. Its controls are Soundtoys' Little
AlterBoy's, with Polyverse Manipulator's on a second page, and both were measured to make it:
what each control does here is what it does there, set out below where Changeling differs.
It moves the voice by cutting it into grains centred on its pulses and laying them a new
period apart (PSOLA, as GigaTune does), so the voice keeps its timbre; with nothing to do the
sound passes untouched, sample for sample.

- **Mode**: **Transpose** moves the voice by Pitch, every note and slide as sung.
  **Quantize** pulls each note onto the nearest note of Key and Scale (the vibrato gone),
  then moves it by Pitch. **Robot** holds the voice on one note: C4 moved by Pitch.
- **Pitch** (±24 semitones, in hundredths): up or down, the formants kept. In Robot it picks
  the note, from C4.
- **Formant** (±24 semitones): the formants moved, the pitch kept: down sounds bigger and
  darker, up smaller and brighter. **Link** moves them with the pitch instead, as a tape sped
  up or slowed down would (the chipmunk and the giant); Formant then moves them on from there.
- **Glide** (1 ms to 1 s, 20 ms at first): how long the voice takes to reach a new note in
  Quantize and Robot, or a new transposition. Little AlterBoy's Quantize moves in about
  17 ms, its Robot in about 60.
- **Key** and **Scale**: Quantize's notes, as GigaTune's (Song follows the song's key). At
  first Chromatic: every semitone, as Little AlterBoy's Quantize.
- **MIDI**: give it a MIDI track's notes (the **MIDI From** button in its title bar: see
  [MIDI from another track](#midi-from-another-track)) and in Robot the voice sings the last
  note played; in Transpose and Quantize the last note moves the voice by its distance from C4
  (C5: an octave up). The note holds after you let go, as Little AlterBoy's does. Notes act
  where they are played (Little AlterBoy's take effect 50 ms early, its latency).
- **Drive** (0 to 10): Little AlterBoy's saturation, measured and fitted: its tone (a little
  bass taken out, a lift from 5 kHz up), then a lopsided clipper whose gain rises 1.7 dB a step
  while its ceiling falls, compressing past 6. At 0 there is none at all.
- **Mix**: the voice as it was against the shifted voice, both as late as each other. **Output**:
  the level out.
- **Quality**: **Live** (21 ms of latency) for singing through; **Studio** (49 ms, about Little
  AlterBoy's 51) hears each note from its start, so a note's first moments are shifted with the
  rest; **High Quality** (about 316 ms) hears the pitch with SwiftF0, for mixing. Quality can't
  be automated (it changes the latency: the device starts again).

The **Mutate** page:

- **Harmonics** (±4): Manipulator's harmonic shifter. The voice's harmonics move up or down by
  that many of them while its pitch stays: up, the lowest ones are gone and the voice turns thin,
  nasal, telephone-like; down, the spectrum folds back on itself. Between whole numbers it rings.
- **Twin** and **Interval** (±24 semitones, an octave down at first): a second voice at Interval
  from the first, read as a tape at that speed would play it, its formants moved with it (an
  octave down is dark and huge, up small and quick). Twin is its level; together they are as loud
  as the voice alone. Breaths and sibilants aren't doubled.
- **Detune** (0 to 50 cents): the left side that many cents lower, the right higher: a doubled,
  wide voice.
- **Width**: every other grain of the voice to one side, the rest to the other. Each side
  sounds rougher and an octave lower on its own; together they are the voice.
- **Smear**: grains taken from up to 80 ms back, their timing moved at random: the voice blurs
  and roughens, as if through a broken tape.

Things to know:

- Breaths, sibilants and noise pass through unshifted, as in both originals; Twin, Width and
  Harmonics leave them alone too.
- It follows one voice at a time, from C2 to C7 (wider than Little AlterBoy's or Manipulator's,
  which lose a voice above about 600 Hz). On chords or a full mix it has no single pitch to follow.
- Where it differs from Little AlterBoy on purpose: it keeps the level as the pitch moves (Little
  AlterBoy loses 5 to 7 dB shifting down, Manipulator gains 3 dB an octave up), its 0 is the input
  exactly (Little AlterBoy's is 2.4 dB quieter and slightly coloured), it goes ±24 semitones, and
  its grains are centred on the voice's pulses, so an octave down is a clean octave (Little
  AlterBoy's carries a ghost of the original pitch).
- **The editor**: along the top the modes, Quantize's Key and Scale (dimmed in the other modes)
  and the quality; down the left a display of the last 3 s: the pitch heard (grey), the pitch out
  (bright), and in Quantize and Robot the note held, with the note out now and its cents in the
  corner. The Shift page has Pitch and Formant, large, with Link between them, and Glide, Drive,
  Mix and Output; the Mutate page the rest.

### Saturator

Saturation and distortion after Ableton's Saturator: the sound is driven into a curve
that rounds off, clips or folds its peaks, from a touch of warmth on a bus to a
fuzzed-out bass. Its editor is laid out like Live 12's with the expanded view open: the
front panel (Drive, the curve, Output and Dry/Wet), then Color, then the curve's own
controls.

- **Drive** (−36 to +36 dB): how hard the sound goes into the curve. The more, the
  further up the curve it reaches and the more it is squashed. Turn **Output** down by
  about as much to compare at the same loudness.
- **Type**, the list under Drive: the curve.
  - **Analog Clip** (the default) and **Digital Clip** clip the peaks: Analog Clip with
    a rounded corner (and untouched below about −2.5 dBFS), Digital Clip flat and hard.
  - **Soft Sine**, **Medium Curve** and **Hard Curve** bend the sound more gently, for
    warmth: Soft Sine is the roundest (and a little louder), Hard Curve keeps the most
    of the straight part before it bends.
  - **Bass Shaper**, for 808s and sub bass: straight below its **Threshold** (−50 to
    0 dB, the knob on the right) and saturating smoothly above it. A low threshold is a
    soft saturation, a high one close to a hard clip; low thresholds with plenty of
    Drive keep a bass's low end round while it gets louder and grittier.
  - **Sinoid Fold**: below full scale like Soft Sine, but driven past it the sound folds
    back over itself: a bright, strange distortion for effects.
  - **Waveshaper**: a curve of your own from the six knobs on the right: **Drive** how
    much of it is heard (0 % none), **Lin** the slope of its straight part, **Curve** a
    bend that adds mostly third harmonics, **Damp** a very fast gate that flattens quiet
    sound around silence, **Depth** and **Period** ripples of a sine over the curve (how
    big, and how many): metallic, folded sounds. Below Drive 100 % the Waveshaper can
    get very loud: use Post Clip. Its knobs are dimmed while another curve is chosen
    (still editable).
- **The curve** shows the shaping: the input across, the output up, the dashed diagonal
  the untouched sound. As the sound plays, two dots ride the curve at its level and the
  part of the curve it uses lights up, from amber to red the harder it is squashed;
  where loud hits have just been stays faintly lit for a moment. Red bars flash at the
  sides when the input itself goes over full scale. The strip under the curve is the
  input's level (it ends under the dots) and the strip at its right the output's; the
  ticks are their recent peaks. Drag the curve up and down for Drive, and across for the
  Bass Shaper's Threshold or the Waveshaper's Curve (Shift for finer steps);
  double-click it to set Drive back to 0 dB. The curve is worked out from the very same
  arithmetic the device plays.
- **Post Clip**, the list under the curve (**No Clip** to start with): **Soft Clip**
  (the Analog Clip curve again) or **Hard Clip** after everything else but Output, the
  dry sound Dry/Wet blends back in included, so the output never goes over full scale
  (times Output): a safety ceiling against Color's boosts, a loud Waveshaper or a hot
  input. Its ceiling shows as red dashes over the curve. With HQ on it clips at four
  times the rate too, so it adds no harsh, unrelated tones of its own; the price is that
  bright, hard-clipped sound can then peak a little over the ceiling.
- **Output** (−36 to 0 dB): the level out. **Dry/Wet** (0 to 100 %) blends the untouched
  sound back in: around 30–50 % for parallel saturation, which keeps the transients and
  adds the grit underneath.
- **Color** steers which frequencies saturate: an EQ before the curve and its exact
  opposite after it. A clean sound passes unchanged; once the curve bends, what the EQ
  boosts is driven harder (and comes out quieter, a dip), and what it cuts stays clean
  (and comes out louder, like a resonance). **Amt Lo** (±36 dB) does this to the lows
  below about 150 Hz: negative keeps the fundamental clean and full under a distorted
  top. **Amt Hi** does it to a band of your choice, set by **Freq** and **Width**. (They
  are Live 12.1's names; before it Live called them Base and Depth.) The graph shows
  the EQ over the input's spectrum (filled) and the output's (the line), with the Color
  switch above it; drag its dots (Amt Lo's up and down, the band's across for Freq and
  up and down for Amt Hi), which also switches Color on; double-click a dot to set it
  back to 0 dB. While Color is off its knobs are dimmed (still editable).
- **DC** removes DC offset from the input (useful on recordings that sit off centre).
  **HQ** (Hi-Quality) shapes and clips at four times the sample rate, so loud, bright
  sounds driven hard don't fold back as harsh, unrelated tones; it costs more CPU and
  adds 36 samples of latency, which is compensated (the tracks stay in time).
- Everything but HQ can be automated, and nothing clicks as it changes: the knobs glide
  and the lists cross-fade.

### Amp

A guitar amplifier after Ableton's Amp: seven classic amps, each with an amp's dials.
Like a real amp's line output (and like Live's Amp), it has no speaker: alone it sounds
bright and fizzy. Put a cabinet after it, or an EQ's low-pass somewhere around 5 kHz, to
hear it as a miked amp. Its editor is laid out like Live's: the models across the top,
the six dials under them, Output and Dry/Wet at the right, and under them what the amp
is doing.

- **The models** (the row of buttons; a line in the model's colour slides under the one
  chosen):
  - **Clean**: the bright channel of a classic 1960s British amp: chiming, clean until
    pushed. Clean chords, funk.
  - **Boost**: the same amp's other channel with more gain: edgy rhythm parts and riffs,
    crunchy at noon.
  - **Blues**: a bright 1970s amp with plenty of clean headroom and a deep mid scoop:
    country, blues, rock. Turned up, its power amp breaks up and sags.
  - **Rock**: a classic 1960s 45-watt rock amp: crunchy, with strong mids. Riffs and
    classic rock.
  - **Lead**: the modern channel of a high-gain amp: tight, saturated and fizzy. Leads
    and metal.
  - **Heavy**: the same amp's vintage channel, looser and darker: crunchy at noon, heavy
    with Gain up; grunge, dirty rhythm parts. Turned up, its power amp breaks up and
    sags: Volume distorts it more.
  - **Bass**: a rare 1970s PA: a powerful low end and a fuzzy, lopsided edge. Bass, or
    fuzz on anything.

  Every model is level-matched at its defaults, so switching between them compares their
  sound, not their loudness; a switch glides from one amp to the next in about 50 ms,
  keeping the level on the way, so it can be automated.
- **Gain** (0 to 10): how hard the sound goes into the preamp: the main control of how
  much the amp distorts.
- **Bass**, **Middle**, **Treble** (0 to 10): the tone stack, a real amp's passive one
  with each model's own parts. As on a real amp they interact: turning one moves the
  others' part of the curve too, and they are gentle (a few dB each way at their ends;
  Middle at 0 scoops the mids most). Turning them up drives the next stage harder, so
  more of them can mean more distortion too.
- **Presence** (0 to 10): the power amp's edge: a shelf on the highs where they meet the
  power stage, 8 or 9 dB either way at the ends (5 is flat). Up, it adds crispness and
  bite; down, it smooths the top.
- **Volume** (0 to 10): the power amp's level, 12 dB either side of 5. On Blues, Heavy
  and Bass the power amp breaks up near the top and the supply sags under loud notes (a
  squash and bloom): turned up, Volume adds distortion of its own. On the others it is
  mostly the level.
- **Output**: **Mono** sums both channels through one amp (half the CPU: right for a
  guitar or a mono bass); **Dual** runs an amp for each channel, keeping a stereo
  source's width at twice the CPU. Its parameter is *Dual Mono*, as in Live.
- **Dry/Wet** (0 to 100 %): blends the amp with the untouched sound, for parallel
  distortion (a bass keeps its clean low end under the grit). The dry sound is delayed
  to line up with the amp's, so they never comb.
- **The tubes** at the left glow as hard as each stage is driven: the three preamp tubes
  (V1, V2, V3) and the power tube (P), flaring with each note and cooling after. The
  power tube turns blue as the supply sags.
- **Drive**, under the tubes, is the amp's transfer curve at these settings: for a tone
  going in at each level (across, full scale at the edges), how high its wave comes out
  at the right and how low at the left (up), worked out from the amp's own stages and
  filters. Gentle settings bend it into an S; high gain turns it into a step, the sound
  clipped flat. It is scaled to its own height, so it shows the shape: a curve whose top
  and bottom differ is clipping one side of the wave first (the even harmonics of a tube
  amp). As the sound plays, two dots ride it at the input's peaks, where the sound's
  peaks come out, with a short trail behind them, and the part the sound uses lights up;
  the input's peak is read out at the bottom right. As the supply sags, the curve's
  shoulder dips.
- **Tone** shows the tone stack and Presence as they sound, from 30 Hz to 16 kHz, worked
  out from the very filters the amp plays. Drag the **B**, **M**, **T** or **P** handle
  up and down, or turn the wheel over it, to set Bass, Middle, Treble or Presence (Shift
  for finer steps; one undo step per drag); double-click one to put it back to 5;
  right-click one for its automation menu, as on its knob, whose red dot it shows too.
- **The pilot lamp** at the bottom right glows with the output and dims as the supply
  sags, over the model's name. The meter at the far right is the output's level.
- Every control can be automated, and nothing clicks: the dials and the models glide,
  and Output crossfades. The amp delays the sound by 37 samples (the oversampling),
  which other tracks are delayed to line up with.

### Erosion

Digital grit, after Live 12.4's Erosion: the sound is read out of a very short delay
(2 ms) whose length a sine or filtered noise wobbles very fast. Every frequency in the
sound is smeared into sidebands around it, the more the higher it is: lows survive,
highs turn to fizz and hiss, and what is pushed past the top of the spectrum folds back
down as aliasing, the "digital" sound the device is for. A sine gives metallic,
ring-modulated tones; noise erodes; narrow noise at a low Frequency is a random,
wobbling vibrato.

- **Frequency** (20 Hz to 18 kHz): the sine's frequency, or the middle of the noise's
  band: the colour of the distortion.
- **Amount** (0 to 100 %): how far the modulation moves the delay, up to about ±1.4 ms.
  It grows with the square of the knob, so the lower half is the fine part. At 0 % the
  sound passes through untouched. There is no Dry/Wet (Live's has none either): to blend
  it in, put it in a rack chain beside a dry one.
- **Width** (Filter Width, 0.1 to 10 octaves): how wide the noise's band is. Narrow, the
  noise is nearly a wandering tone and erodes selectively; wide, it hisses over
  everything. It does nothing to the sine, and is dimmed while Noise Blend is 0 %.
- **Noise Blend** (0 to 100 %): from the sine alone to the noise alone, both in between
  (at equal power, so the strength stays the same). The sine and noise pictures beside
  it light up with each.
- **Stereo** (Stereo Width, 0 to 100 %): from the same modulation on both sides to
  independent noise on each side, and the sine's sides a quarter cycle apart. Each
  Erosion has noise of its own, so two set alike (on a pair of double-tracked parts,
  say) don't wobble together.
- Live's old Erosion (Erosion Legacy) had a Mode: **Sine** is Noise Blend 0 %, **Noise**
  is Noise Blend 100 %, and **Wide Noise** is Noise Blend 100 % with Stereo 100 %.
- **The display** shows the input's spectrum (filled, grey) and the output's (a line
  over it: what Erosion adds shows at the top), the noise's band (orange: as wide as
  Width, as high as Amount, worked out from the very filter that plays) and the sine (a
  blue spike). Drag the dot: across for the Frequency, up and down for the Amount (a
  click puts it where you click). Shift-drag moves it finely; Alt-drag up and down sets
  the Width, and so does the mouse wheel (with Ctrl, finely). Each drag is one undo
  step. While the sound is being eroded the band shimmers (with two edges as Stereo
  widens), the dot glows and the sine's spike trembles; in silence it all rests.
- **L/R Mod** shows the modulation, left against right: a line in mono, opening into an
  ellipse and then a round cloud (noise) or a circle (the sine) as Stereo widens.
- Every control can be automated, and changes glide (in about 50 ms), so they never
  click.
- It adds 2 ms of latency (the middle of its delay): the other tracks are delayed by as
  much, so everything stays in line.

### Overdrive

A pedal's drive, after Ableton's Overdrive: a band of the sound pushed into soft,
slightly lopsided clipping (so it adds even harmonics as well as odd, as a real pedal
does).

- **Frequency** (50 Hz to 20 kHz) and **Width** (0.5 to 9 octaves): the band that goes
  into the drive, a 12 dB/octave high-pass and low-pass around Frequency. Narrow and
  mid-range, it is the honky sound of a classic pedal; wide, everything is driven.
- **Drive** (0 to 100 %): how hard the band is pushed (up to 42 dB).
- **Tone** (0 to 100 %): darker to brighter, a low-pass after the drive from 1 kHz to
  20 kHz.
- **Dynamics** (0 to 100 %): how much of the input's level changes come back after the
  clipping has squashed them: at 0 % everything comes out about as loud; higher, quiet
  parts stay quieter, as they came in.
- **Dry/Wet**: 0 to 100 %.
- **The display** shows the band (drag it: across for Frequency, up and down for Width;
  double-click resets them), Tone dashed over it and the input's spectrum behind. While
  sound plays the band glows with the output and the Drive knob warms with how hard it is
  driven. In silence it all rests.
- Every control can be automated; Frequency, Width and Tone glide (about 15 ms), Drive
  and Dry/Wet ramp (20 ms), so they never click. No latency.

### Auto Filter

A resonant filter that moves by itself, after Ableton's Auto Filter (its editor laid out
as Live 12.2's): an envelope follower and an LFO sweep its cutoff.

- **Freq** (20 Hz to 20 kHz), under it the **filter type** (Lowpass, Highpass, Bandpass,
  Notch, or **Morph**, which sweeps from Lowpass through Bandpass, Highpass and Notch back
  to Lowpass: its **Morph** box takes the slope buttons' place), the **slope** (12 or 24 dB
  an octave) and **Res** (Resonance, 0 to 125 %).
- At the right: **Drive** (0 to 24 dB), the **circuit** (**Clean** is a clean state-variable
  filter; **OSR**, **MS2**, **SMP** and **PRD** saturate as analog filters do, the input and
  the ringing resonance alike, and hear Drive, which is dimmed on Clean), **Clip** (rounds
  off the output's peaks), **Output** (−24 to +12 dB) and **Dry/Wet**.
- Under the display: **LFO Amt** (at 100 % four octaves either way), **Rate** (0.01 to
  40 Hz, or with **Mode** at Sync a note value following the song), **Wave** (Sine,
  Triangle, Saw Up, Saw Down, Square, S&H, Smooth), the LFO's **Morph** (leans the
  waveform's cycle one way or the other) and **Envelope** (−100 to 100 %, at 100 % six
  octaves up with a full-scale sound; negative closes the filter).
- Along the display's bottom, the LFO's strip: **Phase** (the right side's LFO this far
  ahead of the left's) or, clicked, **Spin** (the right side's LFO this much faster), the
  **offset** (↻, turns both), and **Quantize** (None, or Steps: the LFO's value in as many
  levels as the box beside it says).
- The **Sidechain** fold at the left opens the envelope's **Attack** and **Release**. With a
  sidechain chosen (the title bar's sidechain button) the envelope follows that instead of
  the input.
- **The display** shows the filter's curve over the input's spectrum. Drag it: across for
  Freq, up and down for Res; double-click resets them. While sound plays the curve sweeps
  where the envelope and the LFO take it (the right side's curve beside it, thin, where
  Phase or Spin part them); it glows as the envelope opens it, the dot's halo breathes
  with the LFO, and the bar along the bottom is how far they can take it, a tick where
  the cutoff is now. In silence it eases back and rests.
- Every control can be automated; the type, slope, morph and resonance glide (10 ms), so
  even switching them doesn't click. No latency.

### Delay

A stereo delay after Ableton's, with an editor laid out like it.

- **Left / Right**: each side's time. With **Sync** on it is 1 to 16 sixteenths of the
  tempo (the grid), lengthened or shortened by up to 33 % (the field under it, for a
  swing feel); with Sync off, a time knob (1 ms to 5 s). The **link** button between
  them makes the right side follow the left (its controls grey out).
- **Feedback** (0 to 95 %, under the sides) and **Freeze** (∞): frozen, what is in the
  delay goes round for ever and new input is ignored.
- **Filter**: a band-pass on the echoes (and so on what feeds back), from a 12 dB/octave
  high-pass to a 12 dB/octave low-pass, **Width** octaves apart around its frequency.
  Drag the curve's dot across for the frequency, up and down for the width. Behind the
  curve is a spectrum of the delay's input.
- **Character**, under the filter, where Ableton's has its modulation:
  - **Swing** (0 to 100 %): every other echo comes late, by up to half the time (at
    about 67 % the echoes fall in triplets, a shuffle); the others stay on time.
  - **Drive** (0 to 100 %): tape-like saturation in the feedback loop: loud echoes are
    rounded off, and each time round they get a little dirtier. Quiet echoes pass as
    they are.
  - **Duck** (0 to 100 %): the echoes dip while the input plays, and swell back in the
    gaps, so a vocal or a lead stays clear of its own echoes.
- **Mode**: how a change of time sounds. **Repitch** glides to it, pitching the echoes as
  a tape delay does; **Fade** crossfades to it; **Jump** switches at once.
- **Ping Pong**: the input, in mono, goes left first, and the echoes bounce from side to
  side.
- **Groove › Follow** (Follow Groove, beside Duck): the swing is the project's default groove's
  ([grooves.md](grooves.md#the-default-groove)): every other echo comes as late as the
  groove's offbeat steps are, on average. Without a default groove, Swing as it is set.
- **Dry/Wet**: 0 to 100 %.

### Bitcrusher

Fewer bits and a lower sample rate: the sound of early samplers and game consoles.

- **Bits** (1 to 16): how many levels the signal is rounded to (8 bits: 256). Fractions
  move smoothly between the whole numbers' steps.
- **Rate** (200 Hz to 48 kHz): the sample rate it holds the signal at. Between its
  samples the signal stays put, and what is above half the rate folds back down
  (aliasing), as it did in early samplers.
- **Drive** (0 to 24 dB): louder into the crusher, so more of its levels are used, then
  clipped at full scale.
- **Dither** (0 to 100 %): noise of up to one level added before the rounding: a hiss in
  place of the steps' buzz on quiet sounds.
- **Tone** (0 to 100 %): smooths the steps' edges, a low-pass from 1 kHz up (open at 100 %).
- **Output** (−24 to +12 dB) and **Dry/Wet**.

Its editor draws one cycle of a sine as the Bitcrusher has it, as the parameters stand: the
steps across are Rate's, the steps up and down Bits', its height Output's (whose box sits over
it). While sound comes in the wave runs along, glowing with the input's level.

### Tape Stop

The sound slowing to a halt, as a tape machine or a turntable does when its motor is cut, its
pitch falling with it; let go, it spins back up.

- **Stop**: the button. Pressed, the tape slows to a stop and stays silent; pressed again, it
  spins up. Automate it to stop on the beat (a step up in its lane where the stop starts, back
  down where the sound comes back).
- **Stop** time (20 ms to 4 s) and **Start** time (1 ms to 4 s): how long it takes to slow to a
  halt, and to spin up again. Let go before it has stopped, it speeds up from where it got to.
- **Curve** (−100 to +100 %): 0 slows it evenly; above, the speed holds and then drops off
  late; below, it drops at once and crawls to the end. Spinning up runs back along the same curve.
- **Tone** (0 to 100 %): how much darker it gets as it slows.
- **Dry/Wet**.

Running, it passes the sound exactly as it is. Its editor is laid out as Kilohearts' Tape Stop:
two tape reels turning as fast as the tape goes, the Stop button between them (a play triangle
while it runs, a square while it is stopped), and the knobs in a row underneath.

### Glitch

Cuts the sound up on the beat: at each step of a grid, the step may glitch, at random.

- **Grid** (1/32 to 1 bar): how long each step is. Steps follow the song while it plays, and
  run on their own at the tempo while it doesn't, so a live input glitches too.
- **Chance** (0 to 100 %): how likely each step is to glitch.
- **Stutter**, **Reverse**, **Tape** and **Gate** (0 to 100 % each): how often a glitch is each
  kind, against the others (all at 0: nothing glitches).
  - **Stutter** repeats the step's start, a half to an eighth of it, over and over.
  - **Reverse** plays the step before backwards.
  - **Tape** slows the step to a stop, its pitch falling.
  - **Gate** chops the step into 4, 6 or 8, every other part silent.
- **Var** (Variation, 0 to 99): another roll of the dice. The same place in the song always
  glitches the same way, so what you hear is what an export has; change Variation for
  another pattern.
- **Dry/Wet**.

Its editor shows the last sixteen steps, each coloured by what it did (the knobs are in the
same colours), the one playing outlined. When a step glitches the editor goes with it: the
cells jolt, the strip flashes and tears, static crackles, the kind's name flies across, and the
step's cell splits, ghosts and moves as its kind sounds (rippling for Stutter, sweeping back for
Reverse, sinking for Tape, blinking for Gate). Between glitches it is still.

### Chorus-Ensemble

Thickening, chorus and vibrato, after Ableton's Chorus-Ensemble: the sound is copied
into short delays whose length an LFO keeps moving, and the copies, slightly detuned and
spread across the stereo field, are added to it. Gently it thickens and widens a sound;
deeper it is the classic chorus shimmer; with feedback it heads towards flanging.

- **Mode**, the three tabs over the display:
  - **Chorus** (Live's Classic): one or two delays a side. **Taps** chooses: **2** (the
    classic dual chorus, the two moving opposite ways) or **1** (simpler and thicker, as
    a hardware pedal). **Time** chooses how long the delays are: **Auto** follows the
    Amount (a short, gentle chorus at low Amount, a deeper and longer one at high),
    while **7**, **10**, **20**, **35** and **50 ms** hold the delay where it is
    whatever the Amount: the longer ones avoid the phasing a short chorus puts on basses
    and guitars.
  - **Ensemble**: three delays a side, their movements a third of a cycle apart: a
    thicker, smoother, string-machine sound.
  - **Vibrato**: one delay a side, moved further: its pitch wobbles. Fully wet (Dry/Wet
    100 %) it is a pure vibrato. **Offset** (0 to 180°) sets how far apart the left and
    right move (180°: opposite, a wide, swirling vibrato), and **Shape** morphs the
    movement from a sine (0 %) to a triangle (100 %), whose pitch jumps between two
    notes.
- **Rate** (0.1 to 15 Hz): how fast the delays move. Slow, a gentle drift; fast, a
  warble.
- **Amount** (0 to 100 %): how far they move, and so how far the copies are detuned. At
  0 % the copies are steady (a short doubling, or with Warmth a gentle saturation of its
  own).
- **Feedback** (0 to 100 %, Chorus and Ensemble): how much of each side's output goes
  back into its delays: more extreme, brighter, metallic towards the top, and the delays
  ring on for a moment after the sound stops. **Ø** flips the feedback's polarity:
  hollow at high Feedback. Vibrato has no feedback: there both are dimmed, and what you
  set them to waits for Chorus or Ensemble.
- **Width** (0 to 200 %, Chorus and Ensemble): the chorused sound's stereo width: 0 %
  mono, 100 % as it is, wider above. Even a mono sound comes out in stereo: the two
  sides' delays move differently.
- **Warmth** (0 to 100 %): a subtle saturation and darkening of the chorused sound, as
  an analogue chorus's: mostly even harmonics, and no squashing (quiet sounds keep their
  level, a full-scale one loses about a dB). With Amount at 0 % it works as a gentle
  saturation of its own.
- **High-pass** (the switch under the display) and its frequency (20 Hz to 2 kHz): below
  it the sound isn't chorused; the lows pass through steady and whole at any Dry/Wet,
  keeping a bass or a kick solid. With it on, the dry sound goes through the same
  crossover, even fully dry: its level is unchanged, but its phase turns around the
  frequency (so it stays in step with the steady lows, and Dry/Wet blends them without
  cancelling).
- **Output** (−36 to +6 dB): the level of the chorused sound. **Dry/Wet** (0 to 100 %):
  the dry sound blended with it.
- **The display** shows each delay as it moves, newest at the right where a dot rides
  it, the left side's voices in orange and the right side's in blue, against the delay
  in ms (the range Amount 100 would cover; in Auto the dashed centre rises and falls
  with the Amount). The figure at the top right is how far the copies are detuned at
  most (in cents, or semitones). Drag up and down in it for the Rate, or across for the
  Amount (the drag's first move picks which, and it changes only that one; Shift:
  finely); each drag is one undo step. As sound passes the traces glow; in silence they
  rest, and with the device off or nothing playing they stop and dim.
- Every control can be automated, and changes don't click: Mode, Taps and Time crossfade
  to the new voices, and the rest glide. Switched on in the middle of a sound, its copies
  of the sound fade in too.
- Tips: for a surf guitar, Ensemble at 1 to 1.8 Hz with Amount at 100 %; for a vibrato,
  Vibrato with Dry/Wet at 100 % and the Rate at 5 to 7 Hz; for bursts of decaying
  oscillation, automate Ø with Feedback above 90 %.
- Its delay is the effect, not latency: other tracks aren't delayed to line up with it.

### Phaser-Flanger

Phasing, flanging and doubling in one device, after Ableton's Phaser-Flanger. The sound
is mixed with a moved copy of itself, which cuts notches into its spectrum; an LFO keeps
the copy moving, so the notches sweep: the classic swoosh of a phaser, the jet-plane
whoosh of a flanger, or (with a longer delay) a second, slightly wandering take.

- **Mode**, the tabs at the left:
  - **Phaser**: the copy goes through a chain of all-pass filters. **Notches** (1 to 42)
    is how many filters, one notch each; **Center** (70 Hz to 18.5 kHz) where the
    notches sit; **Spread** (0 to 100 %) how far apart they are (narrow filters, close
    notches at 0 %; far apart at 100 %); **Blend** (0 to 1) what the LFO moves: the
    Center at 0, the Spread at 1, both in between.
  - **Flanger**: the copy is delayed by **Time** (0.1 to 20 ms), which makes a comb of
    evenly spaced notches; a shorter time moves them up (the first notch, shown under
    the knob, is at 1 / (2 × Time)).
  - **Doubler**: a longer **Time** (20 to 150 ms): a gently moving second take, for
    width and thickness.
- **Amount** (0 to 100 %): how far the LFO (and the envelope, below) moves the sweep: up
  to three octaves of the Center either way, two octaves of the Flanger's Time, 15 % of
  the Doubler's.
- **The LFO**: **Freq** (0.01 to 40 Hz: from a slow sweep to a fast warble), or with the
  **♪** switch on, **Rate** in note values (Live's: 1/64 to 3/4 of a bar, triplets among
  them, then 1 to 8 bars; a value a wheel notch), the LFO then following the song; the
  **waveform** (Sine, Triangle, Triangle Analog: a rounded square that gets rounder and
  quieter the faster it runs, Triangle 8 and 16: stepped, Saw Up, Saw Down, Rectangle,
  Random: smooth, Random S&H: stepped); **Duty** (−100 to 100 %), which bends the shape
  (a rectangle's width, the others' skew); and **Phase** (0 to 360°), how far the right
  channel's LFO runs ahead of the left's (180°: opposite, a wide stereo sweep). With
  **Spin** on, the right LFO instead runs faster than the left by **Spin** (0 to 50 %:
  at most half as fast again), so the two drift in and out of step.
- **Feedback** (0 to 100 %): the output fed back in: sharper, ringing notches and a
  stronger, more metallic sweep. **Ø** flips its polarity, which moves the resonances
  between the notches (hollow, nasal).
- **Warmth** (0 to 100 %): a little saturation and darkening of the effect, also as it
  feeds back.
- **Output** (−36 to +6 dB) and **Dry/Wet** (0 to 100 %): at 50 % the notches are
  deepest; at 100 % only the moved copy is heard (a Flanger then becomes a vibrato, a
  Doubler a wandering delayed copy).
- **More** shows the rest:
  - **LFO 2** (0 to 100 %): crossfades the modulation from the first LFO to a second, a
    triangle at its own **Freq** (or with its ♪, **Rate**). Two rates against each other
    give a more irregular sweep.
  - **Env** follows the input's level and moves the sweep with it, by **Env Amount**
    (−100 to 100 %: negative moves it the other way): louder notes sweep further.
    **Attack** (0.1 to 30 ms) and **Release** (0.1 to 400 ms) are how fast it follows.
  - **Safe Bass** (Off at 5 Hz, up to 3 kHz): everything below it stays out of the
    effect, so a bass line or a kick keeps its weight while the rest sweeps.
- **The graph** shows the response as it plays: the notches (or the comb) moving with
  the LFO, each Phaser notch marked along the bottom, the right channel's curve in blue
  when it differs, the envelope's level as a bar at the left, and In and Out meters. A
  comb too fine to draw as a line (the Doubler's, high up) is shown as a band between
  its peaks and its notches. Under it the LFO's shape with a dot riding it (the right
  channel's in blue) and, at the right, a bar of the total modulation the sweep follows.
  With nothing playing it shows where the knobs put things. Drag in it: across for the
  Center (Flanger: the comb's first notch lands under the mouse; Doubler: the Time), up
  and down for the Spread (Flanger and Doubler: the Feedback); each drag is one undo
  step; double-click to reset them (one undo step too, which takes them back to where
  they were).
- Every control can be automated, and changes don't click: the controls glide, a change
  of mode or of Notches crossfades, and an LFO that jumps (a new waveform, Sync
  switched, the song jumping) fades to its new place. Switched on while something plays,
  the Flanger's and the Doubler's delayed copy comes in smoothly too.
- Tips: a slow Triangle (0.1 to 0.3 Hz) with 4 to 8 notches and some Feedback is the
  classic phaser; a Flanger at 1 to 3 ms with 70 % Feedback is the jet whoosh (Ø for a
  hollower one); a Doubler at about 30 ms with Phase 180° and little Amount widens a
  vocal or a guitar.

### Reverb

An algorithmic reverb after Ableton's: a room's early reflections, then a diffuse tail
that dies away, each band for its own time. Like Live's, it is mono in, stereo out: left
and right are summed before the reverb (a sound panned hard left reverberates in the
middle), and **Stereo** sets how wide the reverb comes out. On a return track, set
**Dry/Wet** to 100 %; on a track, blend it in (40 % to start with).

A good order to set it in: the band it hears, **Predelay** and **Size** first, then the
reflections (**Shape**, **Spin**), then how long it rings (**Decay** and the graph's
handles), then the levels. The editor goes left to right:

- **Input**: **Lo Cut** and **Hi Cut** take the lows and highs off what goes into the
  reverb (not off the dry sound): a reverb with less mud and hiss. The pad shows the
  band over a spectrum of the input: drag its dot across for the band's centre, up and
  down for its width (0.5 to 9 octaves; Shift for finer steps), or type them into the
  boxes under it. The band glows as sound comes in.
- **Early reflections**: the first echoes off the room's walls; the first comes
  **Predelay** after the sound (0.5 to 250 ms: 1 to 25 ms for natural rooms; longer
  keeps a vocal clear of its reverb). **Shape**: low, the reflections fade slowly and
  the tail starts early, blending with them; high, they fade fast and the tail starts
  later, apart from them. **Spin** makes them drift in time and swing around the stereo
  field, so the early sound is less fixed and coloured; the tail hears the room drift
  with them. A little (the default) keeps them alive; fast, it gives doppler pitch and
  swirling pans. (For a tail that rings less metallic, use **Chorus**, below.) Its pad
  shows the reflections as dots, placed across by where they sit in the stereo field and
  down by when they come (the first at the top), as big as they are loud; they light up
  as they sound and swing as Spin moves them. Drag across for Spin's rate and up and
  down for its amount. At the top right is when the tail starts after the sound comes
  in.
- **Global**: **Size** (0.22 to 500; 100 is a medium room) scales every delay of the
  reverb: small sizes ring metallic, huge ones grainy. **Smooth** is how a change of
  Size is heard: **None** at once (the tail's pitch sweeps fast), **Slow** or **Fast**
  gliding. **Stereo** (0 to 120°): mono at 0; at 120 each side hears a reverb
  independent of the other, as in a real room; the default 100 is a little narrower.
  **Density** trades richness for CPU: **High** (the default) is the smoothest;
  **Sparse** is grainy and light, a lo-fi sound of its own.
- **Diffusion network** (the tail): **Decay** (0.2 to 60 s) is how long it takes to fall
  60 dB. The graph shows how long each frequency rings, with the tail's spectrum dying
  away behind the curve and a meter of its level beside it. Drag the middle handle up
  and down for Decay. The **Lo** and **Hi** shelves make the lows and highs die away
  sooner (a darker, or less boomy, room): drag their handles across for where they start
  and up and down for how long their band rings, as a share of Decay (20 to 100 %; the
  dashed line out to the edge is where the curve settles), or use the boxes under the
  graph. Double-click a handle (or use the buttons above) to switch a shelf off. The
  high filter can be a **Low-pass** instead of a shelf: every pass round the room
  darker, like a real room's air; it has no share, and its handle moves across only.
  **Diffusion** is how quickly the echoes blur into a smooth tail; **Scale** how coarse
  that blur is (most noticeable in small rooms). **Chorus** makes the tail's echoes
  drift in pitch, against a metallic ring: the knob under its switch is how far, **Rate**
  how fast; the curve ripples with it while the tail sounds.
- **Freeze**: the tail holds for ever, for pads and drones (the curve rises to the top,
  in blue, and the handles dim: they are what it thaws to). With **Cut** (on by default)
  new sound no longer reaches it, so play a chord, freeze, and play over it; without
  Cut, what you play keeps adding to it. With **Flat** (on by default) every band holds;
  without it the shelves still take their bands away, and the frozen tail slowly
  darkens. Flat and Cut are dimmed while Freeze is off: they only act frozen. Changing
  **Density** while frozen keeps the tail in the lines the two share (High and Mid share
  them all; going down to Low or Sparse loses part of it).
- **Output**: **Reflect** and **Diffuse** (−30 to +6 dB) are the early reflections' and
  the tail's levels: more Reflect for a close, defined room, more Diffuse for a
  washed-out space. **Dry/Wet** blends the input with the reverb. Longer decays are
  louder, as in Live; at the same Decay a bigger room is a little quieter.
- Every control can be automated, and changes don't click: Size and Predelay glide (Size
  at Smooth's pace, and changing Smooth mid-glide eases the sweep), the switches and
  Density crossfade, the levels glide. Its sound is the reverb, not latency: nothing is
  delayed to line up with it. It costs about 1.4 % of a core at the defaults (Sparse
  under half of that), and almost nothing once its tail has died away.

### Vintage Hall

A lush 1970s digital concert hall, cloned from a classic: a dark, wide, chorused tail with
a soft bloom and nothing above 12 kHz, as the hardware of the time sounded. It is stereo
in and out. On a return track leave **Dry/Wet** at 100 % (its default); on a track, blend
it in.

- **Decay** (0.2 to 70 s) is how long the middle of the tail rings; **Bass Mult** (0.25 to
  4×) makes what is below **Bass Xover** ring that many times longer or shorter. **High
  Shelf** (−24 to 0 dB) at **High Freq** darkens the tail every time round (and the sound
  going in once): −24 dB at 6 kHz, the default, is the classic dark hall; 0 dB keeps it
  bright.
- **Size** shrinks the hall from 100 % (its natural size, a 560 ms loop) down to a small
  room; **Predelay** (0 to 500 ms) is how long before the reverb starts.
- **Attack** shapes the start: low, the reverb starts at once; high, it swells in over
  about a quarter of a second.
- **Early Diffusion** smears the sound as it comes in; **Late Diffusion** how quickly the
  tail turns from echoes into a smooth wash. Both at 0 give the bare loop's echoes, a
  slapback lo-fi room.
- **Mod Depth** and **Mod Rate** make the tail's delays wander, a gentle chorus that keeps
  it from ringing metallic: more depth or rate for a lusher, more seasick tail; 0 for a
  still one.
- **High Cut** and **Low Cut** take the highs and lows off what goes into the reverb.
- Its editor draws the hall as what it is: one loop the sound runs round, a ring (the
  left half amber, the right blue). It glows where the sound is now; each note or hit
  sends a comet round from each side at the loop's own speed, fading as Decay says and
  smearing out as Late Diffusion spreads it. Inside are its diffusers, wavering with the
  modulation; outside, the taps it is heard from, as big as Attack weighs them. Drag the
  ring up and down for Decay, across for Size (Shift: finely); double-click to reset
  them. Beside it Decay, Predelay, Size, Attack, High Cut, Low Cut and Dry/Wet are
  always shown; **Advanced** opens the rest (the tail's bass and high shelf, the
  diffusion and the modulation).
- Every control can be automated; Size and Predelay glide (the tail bends in pitch as
  they move, as tape would). It costs about 0.25 % of a core while it plays, and next to
  nothing once its tail has died away (it sleeps until sound comes back).

### Disperser

Phase dispersion, after Kilohearts' Disperser: a chain of all-pass filters that delays
what is around one frequency more than the rest, without making anything louder or
quieter. A kick's click turns into a zap, a snare into a laser, a pad's attack softens
into a chirp; sustained sounds change little.

- **Amount**: how many all-pass stages the sound goes through, 0 to 64. At 0 it passes
  through untouched. Each stage adds the same delay again.
- **Frequency** (20 Hz to 20 kHz): where the stages delay the sound most. (Near the top
  it is kept below half the sample rate: at 44.1 kHz, 20 kHz plays as 19.8 kHz.)
- **Pinch** (Q 0.1 to 10): how narrow the band they delay. Pinched, a narrow band comes
  out much later (a pitched, ringing zap); open, a wide band a little later.
- **Dry/Wet** (0 to 100 %): blends the untouched sound with the dispersed one. In between
  they add up as a phaser's do, cutting notches where the dispersion has turned a
  frequency half a circle (deepest at 50 %); fully wet, nothing is louder or quieter.
- **Bypass**: the sound passes through untouched, fading over about 20 ms. The stages
  keep running, so switching it back is seamless (the device's on/off switch stops them).
- **The graph** shows how late each frequency comes out, from 0.1 ms at the bottom to 30 s
  at the top, a decade per line (64 narrow stages at 20 Hz hold 20 Hz back by 20 s). The
  scale stays put: an octave lower, the same settings delay twice as long, and the curve
  slides up by the same distance, keeping its shape. It is worked out from the very
  filters the engine plays, at its sample rate. Drag the dot across for the Frequency, up
  and down for the Pinch. Bypassed or fully dry, it is greyed.
- Every control can be automated, and changes don't click: Amount crossfades to the new
  number of stages (the stages added fade in from silence), and Dry/Wet, Bypass,
  Frequency and Pinch glide. Swept quickly through many narrow stages, Frequency and Pinch are
  heard as the sweep itself (a zap), never louder than what went in.
- Its delay is the effect, not latency: other tracks aren't delayed to line up with it.
  A long dispersion rings on after the sound stops, as a reverb does.

### Transient Shaper

Makes the hits in a sound punchier or softer, and their tails fuller or tighter, however
loud they are: drums that cut through or sit back, a room tamed or brought out.

- **Attack** (−100 to +100 %): turns each onset up (punchier) or down (softer). At +100 %
  an onset rises twice as far; at −100 % only as fast as Attack Time lets it.
- **Sustain** (−100 to +100 %): turns what rings on after the onset up (fuller, longer) or
  down (tighter, drier). At +100 % a tail falls only as fast as Release; at −100 % twice
  as fast as it did.
- **Attack Time** (1 to 100 ms): how long an onset counts as one. Short shapes only the
  click of a hit; long, its whole body.
- **Release** (20 ms to 2 s): how long a tail is held up against.
- **Threshold** (−80 to 0 dB): under this level nothing is shaped (fading out over 12 dB),
  so a noise floor or bleed between hits isn't lifted.
- **Output** (±24 dB) sets the level of what comes out, after **Dry/Wet** (blend the
  untouched sound in for parallel shaping). **Clip** bends whatever goes over −6 dBFS
  smoothly towards full scale, so a boosted onset never goes over.
- **Lookahead** (0, 1 or 5 ms): the shaper hears what is coming that much ahead, so its gain
  moves before an onset reaches the output. It adds as much latency, which is compensated.
- **The left display** shows what it does to a hit at these settings, worked out with the
  very maths the device plays: the hit untouched (dashed) and shaped (filled), orange where
  Attack changed it, blue where Sustain did, morphing as you turn the knobs. Drag the orange
  handle up and down for Attack, across for Attack Time; the blue one up and down for
  Sustain, across for Release (Shift: finely). Double-click a handle to reset it,
  right-click it for its menu. Each time the device hears a hit, a glowing head runs along
  the curve as the hit plays out.
- **The right display** shows the last two seconds of the sound: the input faint, the output
  over it, glowing orange or blue where the shaping turned it up (bright) or down (dimmer),
  the gain itself in a lane underneath with its value now, and sparks flying at each hit. A
  meter at the right shows the output's level, red at the top when it reaches full scale.
- At its defaults (Attack and Sustain 0 %) it changes nothing, bit for bit. Every control
  can be automated, and changes don't click. It follows a sound's envelope, not its
  waveform, so even a bass note's level is read smoothly.

### Vocoder

Makes one sound speak with another, as Ableton's Vocoder does: the input (the **modulator**, usually a
voice) is split into bands, and each band's level shapes the same band of the **carrier** (a synth, noise),
so the carrier takes on the voice's words.

- **Carrier**: what speaks.
  - **Noise**: its own noise. **Rate** (100 Hz to 20 kHz) holds it at a lower sample rate for a grainier
    sound; **Crackle** (0 to 100 %) thins it into ever sparser clicks, as loud.
  - **External**: the sidechain. Choose a track (a pad, a chord synth) as the device's sidechain in its title
    bar; with none, nothing comes out.
  - **Modulator**: the input itself; with Enhance, a filter bank's colour of it.
  - **Pitch Tracking**: an oscillator following the input's pitch, monophonically (a saw, or pulses of
    50, 25 or 12.5 %). **Pitch** moves it by semitones (±24); **Low** and **High** limit the pitches it looks
    for. Unpitched moments keep the last pitch.
- **Enhance**: brings every band of the carrier to the same steady level: a brighter, clearer voice from any
  carrier. Without it the carrier's own spectrum and dynamics colour the result.
- **Unvoiced** (0 to 100 %): adds noise where the input has no pitch (s, f, t), as loud as its highs, so
  consonants come through even on a dark carrier. **Sens.** sets how readily a sound counts as unvoiced;
  **Fast** or **Slow**, how quickly that is heard.
- **Bands** (4 to 40, 20 at first) spread evenly from **Low** to **High** (80 Hz to 12 kHz at first). **BW**
  (10 to 200 %) sets their width: at 100 % neighbours meet at −3 dB; wider is smoother, narrower more
  resonant. **Precise**: every band as wide and as loud. **Retro**: gentler slopes, the high bands narrower
  and louder, as old hardware vocoders were.
- **Formant** (±12 semitones): moves the carrier's bands against the modulator's, for a smaller or larger
  voice. A band moved past the top of the spectrum is left out.
- **Depth** (0 to 200 %): how far the input's spectrum shapes the carrier: 0 % the carrier as it is (but as
  loud as the input), 100 % the input's spectrum, 200 % its peaks stand out further. The level stays the
  same.
- **Gate** (−96 to 0 dB): bands the input is quieter in than this are silent (at −96 dB, none).
- **Attack** (0.1 to 100 ms) and **Release** (1 ms to 3 s): how quickly each band follows the input up and
  down; a long release smears the words into a pad.
- **Mono** (the input and the carrier each summed), **Stereo** (the input summed, the carrier in stereo:
  noise of its own each side) or **L/R** (each side vocoded on its own); **Level** (±24 dB) and **Dry/Wet**.

Whatever the carrier's level, what comes out is about as loud as the input. Its editor is laid out as
Ableton's: the carrier and its settings at the left, beside them the unvoiced controls; in the middle the
filter bank, each band lit as loud as the input is in it with its filter drawn over it, the carrier's bands
marked along the bottom where Formant moves them, and the pitch followed (with its note) at the top right while
tracking; under it the bands' count, range, width and Precise or Retro; at the right Formant, Depth, Gate,
Attack, Release and Level, the channel modes and Dry/Wet.

### Vocal Rider

Rides a vocal's level to a target, as an engineer riding its fader would: quiet words come up, loud ones go
down, without a compressor's sound (no attack, ratio or knee: a slow gain that follows the phrase).

- **Target** (−50 to 0 dB): the level the vocal is ridden to, as the rider hears it.
- **Range Min** (−24 to 0 dB) and **Range Max** (0 to +24 dB): the most it turns the vocal down and up.
- **Speed** (20 ms to 2 s): how quickly it rides: short follows every word, long only the phrases.
- **Vocal Sens.** (−80 to 0 dB): the level the vocal counts from. Under it (breaths, bleed, silence, a
  phrase's tail) the ride holds where it was, so nothing quiet is brought up.
- **Music Sens.** (0 to 100 %): with the music the vocal sits in on the sidechain, how far the target follows
  the music's level: at 100 % the vocal rides to the music's level as the song gets louder or quieter; at 0 %
  the music doesn't count.
- **Vocal** weighting hears the level as a listener does (BS.1770's K-weighting shelf, and no rumble under
  100 Hz); **Flat** hears every frequency alike. **Output** (±24 dB) after the ride.

Its editor shows the last 5 seconds: the vocal's level filled grey, what comes out outlined over it, the music on
the sidechain as a yellow line, and the target as a blue line (drag it up or down, Shift finely; double-click
to reset it, right-click for its menu); under them a lane of the ride itself, up for gain and down for cut, with
Range Min and Max dashed. The ride now shows at the top right.

### EQ

An equalizer after FabFilter's Pro-Q: up to 24 bands on one curve.

- **Adding a band**: hover the curve and a ghost band shows where a click adds one, and
  its type, which depends on where it is: a **low cut** at the far left, then a **low
  shelf**, **bells** across the middle, a **high shelf**, and a **high cut** at the far
  right. Keep the button down to drag it straight on. Double-clicking anywhere adds one
  too.
- **Moving a band**: drag its dot across for its frequency, up and down for its gain (a
  cut, notch or band pass: its Q). Shift drags finely; Ctrl-drag sets the Q. Taking hold
  of a shelf's dot shows its gain's automation (and Map takes its gain); other bands'
  dots their frequency's.
- **The wheel** over a band (or the selected band) sets its Q; Alt-wheel sets its slope.
  While dragging a low or high cut, the wheel sets its slope.
- **Double-click** a band to switch it off and on; **Alt-click** it (or select it and
  press Delete) to remove it. Right-click it for its type, slope, placement and the rest.
- **Types**: bell, low and high shelf, low and high cut, notch, band pass and tilt shelf.
  Cuts and shelves have a **slope** of 6 to 96 dB/octave; a cut's Q above 0.71 makes it
  resonant at its frequency.
- **Placement**: a band works on both channels (Stereo), or only the Left, Right, Mid or
  Side (shown by a letter beside its dot).
- **The panel** beside the curve has the selected band's controls (on/off, delete, type,
  Freq, Gain, Q, slope, placement). It starts collapsed, so the device takes less room:
  the faders button (top right, beside the expand button) shows or collapses it, for
  every EQ. **Output** (±36 dB) and **Scale** (0 to 200 %: every band's gain at once)
  sit in the curve's bottom corners.
- **The analyzer** behind the curve shows the input (a line) and the output (filled),
  tilted 4.5 dB/octave so music looks about level (but not near its floor: silence stays
  flat). Click its label at the top left to
  switch between Pre, Post, both and off, and the label at the top right for the
  curve's range (±3, 6, 12 or 30 dB); right-click the background for both.
- **The expand button** (top right) opens the EQ, bigger, in a window of its own. It
  stays open as you change tracks, and closes when the device is deleted.
- The curve is the one the engine plays: its filters are matched to analog ones, so a
  bell high up keeps its shape instead of squeezing against the top of the spectrum. A low
  or high cut keeps its Q wherever it is, right up to the top.

### Sidechain

Ducks a track (a bass) out of the way of a kick along a curve that you draw, or that is
fitted to the kick.

- **Choosing the kick**: put the Sidechain on the bass's track and choose the kick's
  track with the sidechain button in the device's title bar (or click the hint over
  the curve). Each kick is a **hit**, found to the sample: the first sample at the
  **Threshold**, once the kick has dropped 3 dB below it again (and at least 20 ms
  after the last hit). The meter at the curve's right shows the kick against the
  threshold, and lights up at each hit.
- **Trigger**: *Sidechain* (the kick), or *Every Bar*, *1/2*, *1/4*, *1/8* or *1/16*:
  hits on the beat while the transport plays, with no kick needed, for pumping.
- **The curve** runs from each hit for its **Length** (in ms, or in notes with **Sync**),
  then holds its last value until the next hit. At the top the input is untouched,
  at the bottom it is down by the **Depth**. Drag a point to move it (the first and
  last only up and down). Drag the curve between points to bend it, or use the wheel
  there; double-click a bend to straighten it. Click anywhere else to add a point (keep
  the button down to drag it). Double-click a point, Alt-click it, or select it and
  press Delete to remove it. Shift drags finely. Right-click for **Shapes** (Pump,
  Smooth, Snappy, Linear, Hold, Gentle, Bounce, Stutter), *Fit to Kick*, *Flip Curve*
  (ducking becomes swelling) and *Reset Curve*. As hits come, a playhead rides the
  curve.
- **Fit**: as the kick plays (and the bass with it), the view beside the curve compares
  their spectra: the kick in orange, the bass in blue, and in pink where they **clash**,
  that is, where both are loud. **Fit** (or a click on that view) fits the curve to the
  kick there: the input is fully out of the way from the hit until the kick's energy
  in the clash peaks, then comes back as that energy dies away. It sets the curve's
  points, its length and the crossover. Behind the curve the kick's envelope there
  shows in orange, and the curve the fit calls for shows dashed. **Tight** brings the
  input back while the kick is still loud (12 dB down), **Natural** later (20 dB), and
  **Loose** only once the kick has all but gone (30 dB). **Auto** fits again at every
  hit (an undo takes back all of those fits at once).
- **Smooth** (0 to 30 ms) rounds off the curve's jumps so they don't click (0: they do,
  if you want that).
- **Lookahead** (0 to 20 ms) starts the curve that much before the kick. The rest of the
  mix is delayed to match.
- **Lows Only** ducks only what is below the **Crossover**, so the bass's upper
  harmonics carry on through the kick. The fit puts the crossover above the clash.

## MIDI effects: Arpeggiator and Groove

A MIDI effect changes the notes its track plays before the instrument hears them. It goes
on a MIDI track like an audio effect does (after the instrument), but it plays first,
wherever it sits: the instrument (and an instrument in a rack, or the instruments of a
rack's chains when it is in one) hears its notes instead of the clips' or the keyboard's.
Switched off, the notes go straight through again.

### Arpeggiator

Plays the notes held one after another, in a pattern, over one to four octaves.

- **Style**: Up, Down, Up-Down, Down-Up, Converge (the lowest, the highest, the second
  lowest…), Diverge (from the middle out), Played (in the order the keys went down),
  Random, and Chord (all of them together, an octave further each step).
- **Hold**: the notes play on after they are let go, until a new chord comes with no
  keys down: that one takes over.
- **Rate**: with **Sync** on, a note value (1/1 to 1/32, triplets too); while the song
  plays the steps are on its grid, so the pattern locks to the beat. With Sync off, in
  Hz. Stopped, or free, the pattern starts with the first key.
- **Gate** (5 to 200 %): how long each note lasts, as a share of a step; past 100 % the
  notes overlap (legato).
- **Swing** (0 to 100 %): every other step comes late, by up to half a step.
- **Octaves** (1 to 4): the pattern climbs this many octaves before it starts again.
- **Velocity**: as each key was played, or **Fixed** at a value (1 to 127).
- **Groove** (Follow Groove, beside Hold): synced and while the song plays, its steps fall
  where the project's default groove puts them ([grooves.md](grooves.md#the-default-groove)),
  each as loud as the groove makes it, instead of Swing. Without a default groove (or
  stopped, or free) it plays as it would.

Its editor draws the pattern: sixteen steps of a three-note chord, walked as the style
walks it, over the octaves, each as long as the gate and the odd ones late by the swing. As it
plays, the active step lights up. Octaves and Velocity sit under Rate, Gate and Swing.

### Groove

Moves the notes reaching it into a groove ([grooves.md](grooves.md)): the same notes out,
later or earlier, louder or softer, longer or shorter. Put it on a MIDI track (after the
instrument, as any MIDI effect), or in a Drum Rack pad's chain to groove that pad alone: the
hats swung and the kick straight, from one clip. (A pad's devices hear its notes as C3, so the
groove's first lane, or a lane for key 60, plays them.)

- **Groove** (the list over its steps): what it plays. **Swing** (its Swing and Step knobs),
  **Project** (the project's default groove: [grooves.md](grooves.md#the-default-groove)), or
  one of the project's grooves: edit it in *Edit › Grooves…* and the device follows. *Its own
  groove* shows while it plays one of its own: a groove file it was loaded from, or a groove
  removed from the project while it played it.
- **Swing** (50 to 75 %) and **Step** (1/8, 1/8T, 1/16, 1/16T, 1/32), for Swing: every other
  step late, landing that far through its pair of steps (66.7 %: a triplet feel). They are
  dimmed for the other sources.
- **Timing** (0 to 200 %), **Velocity** (−100 to 100 %), **Random** and **Quantize** (0 to
  100 %): how much of the groove it plays ([grooves.md](grooves.md#how-a-groove-moves-notes)).
- **Length** (0 to 100 %): how far notes take the lengths the groove's steps give them (0 %:
  as they are).
- **Early** (Allow Early, on at first): notes from clips may move earlier as well as later, by
  up to half a step: SUBstation hands them to the device that much ahead of their time. Off,
  notes only move later (a step the groove has early plays on time).
- **Latency** (off at first): the device plays everything half a step later, and delay
  compensation keeps the track in time with the others, so notes played live (and those an
  Arpeggiator before it plays) can move earlier too. With it on, every note can move both
  ways, Early or not.
- Every knob can be automated and modulated: a note takes the values they have when it comes
  in.
- **Apply**: writes what it does into the notes of the track's MIDI clips (on a pad, that pad's
  notes) and switches the device off, in one undo step. (With Early off, no note is written
  earlier.)
- **Add to Grooves**: puts the groove it plays into the project's grooves (named after the
  track), to edit it and play it elsewhere; the device then plays that one.

Its steps show what it plays: the first lane over the groove's cycle, each step where it plays
and as tall as it is loud. As it plays, the step sounding lights up.

A note it moves later is held back and played at its time, its end moved with it, and its
bends follow it. Notes from clips can come early because SUBstation knows them ahead; notes
played live, or played by an Arpeggiator before it, only move later unless Latency is on.
While the transport is stopped, notes go straight through. Switched off, the notes sounding
end and those held back are dropped. An export plays what you hear, Random too.

## Modulators: LFO and Envelope

The LFO and the Envelope (Built-in › Modulators in the browser) don't change the sound
themselves: they move parameters of the other devices on their track, as fast as the
audio plays (a new value every 64 samples, as automation slopes do), on built-in
devices and plug-ins alike, in racks too.

- **Mapping**: click **Map** (beside Sync; it pulses), then touch any parameter on
  the track: a knob, a button, a plug-in's knob, a handle in another device's graph. It
  is mapped unipolar, from where the knob is now up to its maximum. **Esc** (or Map again) gives up. Or right-click any
  parameter › **Modulate With** › the modulator. A parameter follows one modulator at a
  time: mapping it to another moves it over.
- **Its targets**, in the list the **Targets** arrow beside the knobs unfolds (it shows
  how many there are under it): each with its device and
  parameter, a bar for the range it moves over (drag either end for its **min** and
  **max**, the middle to move both; double-click for the whole range; the values show
  in the parameter's units as you drag), a white mark where it is now, **⇅ Invert**
  (the modulator upside down) and **×** to unmap it (× at the top unmaps them all).
  A min above the max turns it the other way too.
- **The amount, on the knob itself** (as Serum's and Ableton's): a modulated knob has a small handle at its top
  left, its amount drawn round it. Drag it up or down to change how far the modulator moves the parameter, from the
  knob's own value (Shift is fine; down past zero turns it the other way); **Alt+Shift-click** it to switch between
  **unipolar** (up from the knob's value) and **bipolar** (either side of it, the same amount). Its tooltip says
  which it is. Every modulated knob has it: the devices' editors and plug-ins' parameters alike. (Track volume and
  pan can't be modulated.) Turn the knob itself and the modulation goes with it, the same amount from the new
  value (where that passes the maximum it is cut off, and comes back when you turn the knob back).
- While a parameter is modulated, its own value and its automation wait; they count
  again once it is unmapped or the modulator is switched off. Modulators can move other
  modulators (an LFO's Rate by an Envelope). A rack's macros can't be modulated: map
  the parameters they move.
- **The curve** is drawn as the Sidechain's is: drag a point (the first and last only up
  and down), drag between points or use the wheel to bend, click to add a point,
  double-click (or Alt-click, or Delete) to remove one, double-click a bend to straighten
  it. Shift is fine, **Ctrl snaps** to the grid (sixteenths across, eighths up).
  Right-click for **Shapes**, *Flip Curve*, *Reverse Curve* and *Reset Curve* (and the LFO's *Retrig*).
- **As it plays**, a playhead rides the curve with a short trail. The knobs it moves show it as a
  dot riding their blue range (their pointer stays where you set them), and the graphs of the devices they
  belong to follow it.
- Copied together with the devices they move (or with the whole track), modulators keep
  their mappings to the copies; pasted alone, or loaded from a preset, they move nothing
  yet.

### LFO

A curve played over and over: one cycle of it is what you draw (a triangle to begin with:
up from the bottom to the top at half the cycle, and down again).

- **Rate** in Hz (0.01 to 40), or with **Sync** in notes (1/64 to 8 bars). Synced, it
  follows the song's position while it plays (a 1/4 LFO starts its cycle on every beat,
  wherever playback starts or loops); stopped, it runs on at the tempo.
- **Groove** (Follow Groove, beside Sync; only with Sync on): while it follows the song (not
  with Retrig), its cycle swings with the project's default groove
  ([grooves.md](grooves.md#the-default-groove)), its offbeats late as the groove's are: a 1/16
  LFO swings with the hats. Without a default groove, it plays as it would.
- **Retrig** (in the curve's right-click menu): every note it hears (its track's, or a MIDI track's: the **MIDI From**
  button) starts its cycle again, so it fits each note; it flashes as one comes.
- **Depth** scales it round the middle of its targets' ranges; **Offset** moves it up or
  down (+100 %: by half the range). The shaded band and the dashed line show what it
  plays then (Phase and Smooth too).
- **Phase** (0 to 360°) shifts where in the cycle it is; **Smooth** (0 to 250 ms) eases
  its steps and jumps (a square without clicks).
- Shapes: Triangle, Sine, Saw Up, Saw Down, Square, Pulse, Steps, Hump, Swell.
- **Random**: a new random shape every cycle instead of the curve drawn (1 to 6 points at random times
  and levels, bent at random), each starting where the last ended, so one cycle flows smoothly into the next. The graph shows the shape
  playing; the drawn curve waits meanwhile, and choosing a shape (or flipping, reversing, resetting) turns Random
  off.

### Envelope

A curve played once from each trigger, then holding its last point's value (a pluck to
begin with: up at once, then falling away).

- **What triggers it** is what you give it: every note it hears (its track's, or a MIDI
  track's with the **MIDI From** button in the title bar), and, once you choose a track
  with the **sidechain** button in the title bar, each hit in that track. A hit is found
  as the Sidechain device finds one: the first sample at the **Threshold** (which counts
  only with a sidechain), once the key has dropped 3 dB below it again (and 20 ms on).
  The meter beside the output shows the key against the threshold. Each trigger flashes
  the graph.
- **Length** in ms (1 ms to 20 s), or with **Sync** in notes. A trigger while it plays
  starts it again.
- **Depth** scales it; **Velocity** lets softer notes play it lower (100 %: a note at
  half velocity plays it at half). **Smooth** eases it.
- Shapes: Pluck, Attack-Decay, Swell, Ramp Down, Ramp Up, Gate, Duck, Stutter.
- **Random**: a new random shape from every trigger instead of the curve drawn, starting where it was, and
  holding its last point after it, as the LFO's Random.

## Racks

Racks (device groups) work like Ableton's Audio Effect and Instrument Racks.

- Select devices in the device view and press **Ctrl+G** (or *Group* in a device's
  right-click menu) to put them in a rack, in one chain, where the first of them was.
- **Ctrl+Shift+G** (*Ungroup*) takes a rack away, its chains' devices taking its place.
  A rack with several instruments can't be ungrouped: a chain has one.
- A rack's chains each process its input, side by side, and it puts out their sum (an
  empty rack passes its input on).
- Racks nest, up to 8 deep.

### What a rack shows

A rack shows its macros. A strip of buttons at its left shows and hides the rest, as
Ableton's do, and adds and takes away macros:

- **Chain list** (the rows button): the rack's chains, beside its macros. It is hidden
  until you show it, and the rack is narrower without it.
- **Devices** (the bracket button): the devices of the chain the rack shows, beside
  it in a bracket. They show until you hide them; clicking a chain in the chain list
  shows them again.
- **+** and **−**: a macro more, or the last one taken away (see [Macros](#macros)).

The rack's right-click menu has *Show Chain List* / *Hide Chain List* and *Show
Devices* / *Hide Devices* too. What each rack shows is saved with the project, not an
undo step.

### Chains

- The chain list shows a rack's chains, a row each with its activator, name, solo
  (only the soloed chains of a rack are heard), volume, pan and meter.
- **+ Chain** (or *Add Chain* in the rack's right-click menu, which also shows the
  chain list) adds one. A row's right-click menu renames, duplicates or deletes it,
  and shows its volume or pan automation; double-click its name to rename it, or
  press **Ctrl+R** after clicking it (the chain list shows if it was hidden).
- Click a chain to show its devices beside the rack, in a bracket: drop devices there
  (or onto a chain's row), drag them in and out, select and delete them as on the
  track's own chain.
- A latent device in one chain doesn't smear the others: they are delayed to line up
  with the slowest, and the rack's latency is compensated like any device's.
- Every chain hears the track's notes, so a rack of instruments layers them (an
  *Instrument Rack*).

### Automation in racks

Devices in racks are automated like any of the track's devices (they keep their
automation when grouped, ungrouped or moved), and so are the chains' volume and pan
(*Chain Volume*, *Chain Pan* under the rack in a lane's device chooser), in time with
the latency before them. A sidechain into a device in a rack lines up with the signal
there.

### Macros

A new rack has four macro knobs, in two rows. The **+** button at the rack's left (or
*Add Macro* in a macro's right-click menu) adds one, up to 16; **−** (or *Remove Last
Macro*) takes the last one away, with what it is mapped to and its automation. Each is
one undo step.

- **Mapping**: right-click a parameter of a device in the rack and choose *Map to
  Macro*, then the macro (by its name), to have it move that parameter across its
  range (*Unmap from* the macro undoes that). A macro can move several parameters; a
  parameter follows one macro of a rack. Turning a macro sets what it moves, as one
  undo step.
- **Ranges**: right-click a macro › *Edit Mappings…* lists what it moves, each with a
  **Min** and a **Max**, in percent of the parameter's range (its value in its own
  units beside them). The parameter goes from Min to Max as the macro turns from 0 to
  100 %: *0 to 100 %* is its whole range, *50 to 100 %* its upper half, and *100 to
  0 %* turns it the other way round (**Invert** swaps Min and Max). Drag a box up or
  down, or click it and type a number (double-click puts it back to 0 or 100 %); a
  drag is one undo step. A range changed puts the parameter where the macro is in it
  at once. **Unmap** takes a parameter off the macro.
- **Names**: double-click a macro's name (or right-click it › *Rename*) to name it in
  place; Enter, Escape or clicking elsewhere keeps the name typed, and an empty name
  names it by its number again. The name shows in the parameters' *Map to Macro*
  menus and in the automation lanes. It is saved with the rack (and in its presets).
- **Automation**: a macro is automated like any parameter. Click it to show its
  automation in the arrangement (it is a lane of the rack: *Macro 1*… in a lane's
  device chooser), or right-click it › *Show Automation*, *Delete Automation*,
  *Re-Enable Automation*. While its automation plays, its knob follows it (with the
  red dot), and so does every parameter mapped to it, over its range and in time with
  the latency before it (instead of the parameter's own automation). Turning the
  macro by hand overrides its automation, as with any parameter, until it is
  re-enabled.
- The macro's tooltip lists what it moves; its name is lit while it moves something.

### Drum Rack

A rack of pads, as Ableton's Drum Rack. Add one from the browser (*Instruments › Drum
Rack*), dropped on a track or the device view, or double-clicked; with no MIDI track
selected it makes one. Each of its chains is a **pad**, playing only the notes of its own
key, which its devices hear as C3: a Sampler on a pad plays its sample at its own pitch.

- **Pads**: sixteen at a time, four by four, the lowest key (C1 at first) at the bottom
  left. The ▴ and ▾ arrows beside them (or the wheel over them) show a row of keys up or
  down; the narrow strip at their left shows all 128 keys, lit where a pad is, and a click
  there shows those pads.
- **Loading samples**: drop an audio file on a pad (from the browser or the desktop), or
  an audio clip dragged down out of the arrangement (its part of its file), or right-click
  › *Load Sample…*. The pad gets a Sampler with the sample, in **1-Shot** (a hit plays
  whole however short the note), and the sample's name. Onto a
  pad with a sample, the new one replaces it (and a pad named after its sample takes the
  new name; one you named keeps its name).
- Click a pad to play its key and show its devices beside the rack (its Sampler); drop
  devices on a pad with a sample to add them to it. Each pad has a **lock** at its top
  right, and its activator (mute) and solo along its bottom; double-click its name (or right-click › *Rename*,
  or Ctrl+R) to rename it. Its right-click menu also duplicates (onto the next free key)
  and deletes it.
- **Similar sounds**: the **‹** and **›** arrows in the Drum Rack's title bar swap
  every pad's sample for the next (or previous) sound like it, each pad from its own
  reference, as the Sampler's arrows do, in one undo step. **Locked** pads keep their
  samples. A pad's menu steps it alone, saves its sample as its reference or returns to
  it; the rack's menu has *Save Pads as Similar References* and *Return Pads to
  References* for every pad but the locked ones.
- Its chain list (the rows button) lists the pads, as a rack's chains, with their mixers;
  *Add Pad* (or **+ Chain**) adds an empty pad on the first free key.
- It has no macros: to turn several of a pad's devices with one knob, select them and
  group them into a rack (Ctrl+G), whose macros do it.
- **Pads as tracks**: right-click the Drum Rack's title › *Separate Pads into Tracks*
  makes every pad a MIDI track of its own, with its devices, volume, pan and automation,
  and the notes of its key from the track's clips (at C3, so its Sampler plays as before).
  The Drum Rack's track becomes their group, keeping its mixer, sends and the effects
  after the rack, so the drums sound as they did. Or drag one pad out into the arrangement:
  it becomes a track after the one you drop it on (or last, below the tracks), and leaves
  the rack, taking its notes with it. Both undo in one step.
- A Drum Rack is an instrument even while empty: it goes first on a MIDI track. Its
  presets load into Drum Racks only.

## Presets

- Every device's **save** button (or *Save Preset…* in its right-click menu) saves it
  as a **preset**, under a name you give it: a built-in device's settings, a plug-in's
  whole state, or a rack with everything in it (its chains, the devices in them with
  plug-ins' states, its macros with their names and ranges). Saving under a name that is taken asks before
  replacing that preset.
- Presets go into your preset library, `Documents\SUBstation\Presets`, in a folder per
  device (named as the device: a plug-in's name, *Utility*, *Audio Effect Rack*…). The
  browser lists them under *Presets*, by device (see [browser.md](browser.md#presets)).
- Drag a preset from the browser onto the chain, or onto a track in the arrangement, or
  double-click it, to add it as a new device. An instrument preset goes on a MIDI track;
  with none selected (or dropped below the tracks) it makes one.
- Drop a preset **onto a device of its kind** (the same plug-in, the same built-in
  device, or a rack of the same kind) to load it into that device: the device is
  outlined while the drag is over it. It keeps its place, its on/off switch and its
  sidechain; a rack's chains and macros are replaced (and the automation of the devices
  that leave with them). Dropped onto another kind of device, the preset goes in beside
  it as a new device.
- Loading a preset is one undo step. Loaded devices are new ones (a preset loaded twice
  makes two), without sidechains or MIDI inputs from other tracks.
- A rack is titled with the name of the preset it was saved as or loaded from (loaded
  into another rack, that rack takes the name too). A rack that never was a preset is
  called *Audio Effect Rack* or *Instrument Rack*. The name is saved with the project.
- **Default presets**: right-click a device › *Save as Default Preset*, and every new
  device of that kind (that plug-in, that built-in device) starts as this one is now:
  added from the browser, dropped, or as a new MIDI track's instrument. *Clear Default
  Preset* makes new ones start as they come again. Racks have none. Defaults are kept in
  the library's `Defaults` folder (`<device>.gilpreset`, or `<plug-in> (<class
  id>).gilpreset`), which the browser doesn't list.
- A preset of a plug-in that isn't installed loads as a missing device, as in a
  project: it keeps its settings and loads once the plug-in is back. In a rack, the rest
  of the rack works.
- Right-click beside the devices › *Load Preset…* loads a preset file from anywhere.
- Groove files (in the library's *Grooves* folder) are listed and added as presets too: each
  is a Groove device playing that groove (see [grooves.md](grooves.md#your-groove-files)).
- Plug-ins also read and write their own `.vstpreset` files (see
  [plugins.md](plugins.md#presets)).

## Folding

- The triangle at the start of a device's title bar (or *Fold* in its right-click menu)
  folds it to a narrow strip with its on/off switch and its name, reading upwards; a
  folded rack hides its chains too.
- Click the triangle again, or double-click the strip, to unfold it.
- **Ctrl+double-click** a device (its title or background) folds or unfolds it too.
- With several devices selected, folding one of them folds (or unfolds) them all.
- Folding is saved with the project, not an undo step.

## Cut, copy and paste

- While the device view has the focus (devices clicked, or the space beside them),
  **Ctrl+X**, **Ctrl+C** and **Ctrl+V** cut, copy and paste the selected devices, and
  **Ctrl+D** duplicates them.
- A device's right-click menu has them too, and right-clicking beside the devices
  pastes there.
- Pasted devices go after the selected ones (or at the end of the track's chain) and
  are selected. They are new devices with the same settings: plug-ins in their state
  when copied, racks with everything in them, folded if they were.
- Sidechains are kept, unless their source is gone or would close a cycle on the track
  pasted onto; MIDI inputs from other tracks are kept while their source is there (onto
  their source itself, they take its notes as their own track's).
- An instrument pasted goes first on a MIDI track (replacing its instrument), and not
  onto an audio track.
- To paste onto another track, select it, click beside its devices, and press Ctrl+V.

---

For developers: [../ui/device-view.md](../ui/device-view.md),
[../engine/devices.md](../engine/devices.md),
[../engine/routing.md](../engine/routing.md) (racks).
