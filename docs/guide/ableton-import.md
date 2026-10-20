# Importing Ableton Live Sets

*File › Import Ableton Live Set…* opens a Live Set (`.als`, from Live 10, 11 or 12) as a
new project. SUBstation asks about unsaved changes first, as *Open…* does. The project is
untitled but named after the set: the title bar says its name, and *Save* asks where to
save it, starting with that name (`My Song.gilproj`). Until it is saved it counts as
unsaved (the title has a `*`), so *New*, *Open* and quitting ask first. The set itself is
only read.

Once it is open, a message lists what didn't come across as it was (devices SUBstation
has nothing like, plug-ins that aren't installed, automation of what was left out...),
with how many of each, and the status line says how many tracks and clips came in.
Then its plug-ins load, as they do for any project.

## What comes across

**The song.** Its tempo, time signature, loop and key. A tempo or time signature that
changes during the song changes as it does: tempo ramps and jumps come across, and so does
each time signature change. A tempo past SUBstation's 20 to 999 BPM plays at that limit, and
the message says so. The key is Live 12's scale if it is a major or minor one; Live's own C
major (before a scale is chosen) leaves the key unset, so the piano roll finds the key
the MIDI is in.

**Tracks.** Audio, MIDI and group tracks, nested and folded as in Live, return tracks and
the main track, each with:

- its name and colour (Live 10's colours too). Live's numbered default names become SUBstation's: "4-Serum"
  is "# Serum", so it shows its place in the arrangement as tracks come and go.
- its volume, pan, activator and solo, and its sends (before or after the fader, as
  Live's *Sends Pre*).
- where its audio goes: into its group, past it to the main track, into another
  track's *Track In*, or nowhere (*Sends Only*). A track playing into Live's audio
  device outputs plays into the main track (noted).
- an audio track's input: the audio device's channels, another track (after its
  mixer, before it, or before its effects), and *Resampling* where the track is armed
  or monitors *In*. Its monitoring (*In*, *Auto*, *Off*) too.

**MIDI clips.** Their notes, deactivated notes and clips too, from the clip's start marker
to its end. A clip that loops is written out loop after loop (SUBstation's clips don't
loop), its notes cut at the loop's end as Live cuts them.

**Audio clips.** Their files, start marker, length, gain, transpose, detune, warp mode
and fades, and whether they are deactivated.

- Files are found where the set says, else beside the set (a project folder that was
  moved or copied). Those found in neither place keep the path the set has, and the
  [File Manager](file-manager.md) lists them, to find them again.
- A clip without warping plays at its own speed, as in Live.
- A warped clip keeps its warp markers and plays at the tempo they give it, one clip
  as in Live. Markers set so far apart or so close that a stretch would play slower
  than 20 BPM or faster than 999 can't be kept: that clip is cut into a clip per
  stretch instead, each at its own tempo.
- A looping clip is written out loop after loop, like a MIDI one.
- Warp modes: *Beats* and *REX* become *Transients*, *Texture* *Smooth*, *Re-Pitch*
  *Re-Pitch*, *Complex Pro* *Formants*, and *Tones* and *Complex* *Standard*.

**Plug-ins.**

- VST3 plug-ins come with their settings (what Live saved of them). One that isn't
  installed stays in its place and loads once it is (rescan the plug-ins), as in any
  project; the message lists them.
- A VST2 plug-in that is installed comes as itself, with its settings (those Live kept as
  a whole; one whose settings Live kept as values comes at its default settings, and the
  message says so). One that isn't becomes the VST3 of it that is installed: with its
  settings if that VST3 is made to replace the VST2 (most plug-ins whose VST2 and VST3
  share a maker's ID are), else at its default settings (the message lists those: set
  them up again). A VST2 plug-in with neither installed is left out. Their automation
  comes along only with a VST3's own.

**Live's own devices.** These come across, each as the SUBstation device like it:

| Live | SUBstation |
|---|---|
| Utility | Utility (gain, balance, width; Mono as width 0) |
| EQ Eight | EQ: its eight bands, their modes, frequency, gain and Q, its output and scale; in L/R or M/S mode sixteen bands, placed on each side |
| Compressor | Compressor (threshold, ratio, attack, release, knee, makeup, dry/wet) and its sidechain |
| Delay | Delay: sync, sixteenths and times, offsets, link, feedback, freeze, filter, mode, ping pong, dry/wet |
| Simpler, and a Sampler of one sample | Sampler: its sample, mode (Classic, One-Shot, Slicing), root key, transpose, detune, start and end, volume, envelope |
| Auto Filter (Live 12.2's and the older one) | Auto Filter: type, circuit, slope, frequency, resonance, drive, morph, the envelope, the LFO (rate or synced rate, waveform, morph, quantize, phase or spin, offset), clip, output, dry/wet and its sidechain. Live 12.2's types SUBstation's hasn't (DJ, Comb, Resampling, Notch+LP, Vowel) come as a low-pass, and the message says so |
| Overdrive | Overdrive: frequency, width, drive, tone, dynamics, dry/wet |
| Saturator | Saturator: drive, type (Live 11's list and 12.1's, with the Bass Shaper), its threshold, output, dry/wet, post clip, color, DC, Hi-Quality and the Waveshaper's controls |
| Amp | Amp: type, gain, bass, middle, treble, presence, volume, dual mono, dry/wet |
| Redux (Live 11's, and Live 10's) | Bitcrusher: bit depth, sample rate (Live 10's downsampling as a rate), jitter as dither, dry/wet |
| Chorus-Ensemble | Chorus-Ensemble: mode, taps and delay time, rate, amount, feedback and invert, width, offset, shape, warmth, high-pass, output, dry/wet |
| Erosion | Erosion: frequency, width, amount; its mode as Noise Blend and Stereo (Sine, Noise, Wide Noise) |
| Gate | Gate: threshold, return, attack, hold, release, floor, lookahead, flip, its sidechain with gain, mix, listen and EQ |
| Limiter | Limiter: gain, ceiling, release, auto release, lookahead, mode, routing, link, Maximize with its threshold and output |
| Multiband Dynamics | Multiband Dynamics: crossovers and their switches, each band's activator, input, output, thresholds, attack and release, amount, time, output, soft knee, Peak or RMS, its sidechain. Their ratios come at 1:1 (the message says so) |
| Phaser-Flanger | Phaser-Flanger: mode, notches, center, spread, blend, the delay times, amount, feedback, both LFOs, duty cycle, phase or spin, the envelope follower, Safe Bass, warmth, output, dry/wet |
| Reverb | Reverb: everything (the input filter, early reflections and spin, density, smoothing, size, stereo, the shelves and high filter, decay, freeze, flat and cut, diffusion, scale, chorus, the levels, dry/wet) |
| Vocoder | Vocoder: carrier (an External one from its sidechain), its noise, oscillator and pitch tracking, enhance, unvoiced, bands, range, bandwidth, Precise or Retro, formant, gate, depth, attack and release, channels, level, dry/wet |
| Arpeggiator | Arpeggiator: its style (the nearest of SUBstation's), sync and rate, gate, hold, and transposing by octaves as Octaves |
| Audio Effect Rack, Instrument Rack | a rack, its chains with their devices, volume, pan and activator (not its macros) |

A **Drum Rack** becomes a Drum Rack on its track, with a pad for each of Live's on the
same key, named as in Live: its devices (its Simpler a Sampler), its volume, pan and
activator, and their automation; the clips keep their notes. A pad plays its devices at
C3, so one that sent Live's devices another note is listed in the message. A compressor
sidechained from a pad listens to that pad, where Live took it (Pre FX, Post FX, Post
Mixer); from a pad of a Drum Rack inside another rack, to the whole Drum Rack (its track,
after the rack), as the message says. To mix the pads on tracks of their own, *Separate
Pads into Tracks* ([devices.md](devices.md#drum-rack)).

Other Live devices, MIDI effects but the Arpeggiator, and Max for Live devices are left
out; the message lists them.

**Automation** of the mixers (volume, pan, activator, sends, rack chains' volume and pan),
of devices' on/off, of plug-ins' parameters and of the parameters of Live's devices that
came across, each where its target now is (a pad's on its track).

**Grooves.** The set's groove pool comes into the project's grooves (*Edit › Grooves…*),
each made from its notes on its Base grid and named as in Live. A MIDI clip, or a warped
audio clip, playing one of them plays it as its groove ([grooves.md](grooves.md)), at that
groove's Timing, Random, Velocity and Quantize, as Live's Global Amount scales them. The
message says how many grooves came and how many clips play them. Ableton's groove files
(`.agr`) can be added to any project too ([grooves.md](grooves.md#your-groove-files)).

## What doesn't

- The Session View.
- Clip envelopes, follow actions, take lanes.
- Live devices SUBstation has nothing like, MIDI effects but the Arpeggiator, Max for
  Live devices, racks' macros and their mappings, Drum Racks' return chains.
- Multiband Dynamics' ratios (Live keeps them in a form of its own).
- Frozen tracks come unfrozen (their devices are all there).
- Curved automation segments come straight.

---

For developers: [../app/live-import.md](../app/live-import.md).
