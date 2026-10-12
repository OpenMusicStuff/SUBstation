# Valhalla VintageVerb renders

Synthetic test signals run through Valhalla VintageVerb (VST3, `C:\Program Files\Common Files\VST3\ValhallaDSP`), for
analysing its sound and building a lush vintage reverb like it.

| File | What it is |
|---|---|
| [make_renders.py](make_renders.py) | makes the test signals, renders the matrix, writes `renders/` and its manifest |
| [parameters-full.txt](parameters-full.txt) | every parameter, with the plug-in's own text at 201 points of its range (`substation-plugin-render --list`) |
| `renders/` | the audio (about 3.3 GB, not in git: make it again with the script, in under a minute) |
| [tools/plugin-render](../../tools/plugin-render/main.cpp) | `substation-plugin-render`: runs raw audio through a VST3 effect with the engine's own host |

## Making them

```bash
cmake -S . -B build-render -G Ninja -DSUBSTATION_BUILD_APP=OFF -DSUBSTATION_BUILD_TESTS=OFF -DSUBSTATION_TEST_PLUGINS=OFF -DSUBSTATION_ASIO=OFF
```

```bash
cmake --build build-render --target substation-plugin-render
```

```bash
python research/vintageverb/make_renders.py
```

(MinGW's `bin` on `PATH`, as for the other builds; `--only modes,decay` renders some groups again.)

## Conventions

- 48 kHz, stereo, 32-bit float WAV. Outputs are 100% wet (`Mix` 1) and, except in `predelay` and `lush`,
  with no predelay, so an impulse response starts at the impulse. Outputs are as long as their input file in
  `renders/inputs/` (the dry signal and silence for the tail), sample-aligned with it.
- Each render starts from an emptied plug-in (deactivated and activated) at its defaults, then gets its parameters
  and 1.5 s of silence (smoothing settles) before the signal. Parameters not named are the plug-in's defaults.
- `renders/manifest.json`: one entry per render: `file`, `group`, `case`, `signal`, `input`, `params` (all of them,
  normalized 0..1 as the host sends them), `params_shown` (as the plug-in shows them: "4.00 s", "Concert Hall") and
  `peak_db`.
- `ReverbMode` is `floor(v * 24)`: 1 Concert Hall ... 22 Hall1984 (0 is Concert Hall too); `ColorMode` is
  `floor(v * 3)`: 0 and 1 seventies, 2 eighties, 3 (v = 1) now. Continuous parameters' curves (Decay 0.2..70 s,
  PreDelay 0..500 ms, ModRate 0.1..10 Hz, HighCut 100 Hz..20 kHz, ...) are in parameters-full.txt.

## Signals

| Signal | What it shows |
|---|---|
| `impulse_L`, `impulse_R` | the impulse response: L in gives L→L and L→R, R in gives R→R and R→L |
| `impulse_L_-40dB` | linearity (with `impulse_L`) |
| `burst_pink_stereo_500ms` | 0.5 s of decorrelated pink noise: band decays with the modulation averaged out |
| `sine_<f>Hz_<level>dB_3s` | 3 s tones then silence: the modulation's pitch spread in the tail, level dependence, saturation |
| `sweep_20-20000Hz_8s` | Farina exponential sweep (`0.5 sin(2π f1 T/k (e^{tk/T} − 1))`, `k = ln(f2/f1)`, 50 ms fade-in, 10 ms fade-out): deconvolve with its time-reversed copy weighted by `e^{−tk/T}` for an IR, with harmonics separated |
| `silence` | what the plug-in makes by itself |
| `snare`, `staccato_chords`, `vocal_ah` | synthetic program material, for listening and comparing against our reverb |

## Groups

| Group | Cases |
|---|---|
| `modes` | all 22 modes × 3 colors at defaults (4 s decay): impulses L and R, pink burst, silence |
| `decay`, `size`, `attack`, `diffusion`, `moddepth`, `modrate` | sweeps of one parameter, Concert Hall, Plate, Random Space and Chorus Space in seventies color; `impulse_L` (and the 1 kHz tone for the modulation) |
| `highcut`, `lowcut`, `bassmult`, `bassxover`, `highshelf`, `highfreq`, `predelay` | the filters and the decay's EQ, Concert Hall seventies, `impulse_L` |
| `repeat` | the same impulse five times, with default modulation and with ModDepth 0: how far the response varies from run to run |
| `level` | Concert Hall in each color: impulses and tones at several levels, silence |
| `program` | the 8 core modes (Concert Hall, Bright Hall, Plate, Chamber, Random Space, Chorus Space, Smooth Random, Hall1984) in seventies and eighties color: tones, sweep, program material |
| `lush` | five lush settings as one might dial them (long, dark, deeply modulated): impulses, sweep, program material |

## First observations (from checking the renders)

- **Linear**: a −40 dB impulse gives the same response 40 dB down, in every color.
- **Decorrelated stereo**: L→L and L→R correlate at about 0; the cross channel starts later (Concert Hall: first
  energy at about 9 ms on the same side, 41 ms on the other).
- **Decay is longer than shown**: measured RT60 (from T30) is about 1.2× the Decay shown for 2 s and longer
  (Concert Hall: 4.00 s shows → 4.9 s; 16.2 s → 20.8 s); short settings bottom out near 1.3 s at "0.49 s".
- **Modulation makes it time-variant**: with default modulation two takes of the same impulse differ by −5 dB
  (Concert Hall) to +1.7 dB (Chorus Space: as different as two uncorrelated tails). With ModDepth 0 Concert Hall,
  Chorus Space and Plate are deterministic (−130 dB), but Random Space still varies (−7 dB): its randomness is not
  all in the depth control. Average several takes, or analyse energy, not waveforms.
- **Some modes add noise**: Sanctuary, Dirty Hall, Dirty Plate, Smooth Plate, Smooth Room, Smooth Random and Nonlin
  (and a little Chaotic Hall and Hall1984) make a steady floor of about −80 to −96 dB peak by themselves, with silence
  in, varying by color. It stops their decays at −90 dB: subtract it (Lundeby) before fitting a decay.

## The clone: Vintage Hall

The built-in device `vintage_hall` ([VintageHall.cpp](../../engine/src/builtin/devices/VintageHall.cpp), its numbers
in [VintageHallDesign.h](../../engine/src/builtin/VintageHallDesign.h), described in
[docs/engine/devices.md](../../docs/engine/devices.md#vintage-hall-builtinvintage_hall-audioeffect)) is Concert Hall in
the 1970s color, rebuilt from these measurements:

- It runs at 24 kHz (half of 48): the shelves reach their gain at 12 kHz, a steep elliptic resampler leaves nothing
  above 13 kHz and rings at the edge, and the moving delays are read linearly (lossy at that rate).
- One 560 ms loop (13442 samples at 24 kHz) of two halves; the left comes in at its start, the right half way round,
  each followed by the high shelf and two Schroeder all-passes (7 + 12 ms and 6 + 10 ms; Early Diffusion), then two
  nested all-passes (Late Diffusion; the second in each half three levels deep, its innermost delay the modulated
  one), plain delays, and the loss (Decay's gain for the half, a one-pole bass crossover).
- Four taps a side (three of its own half, one of the other's); Attack crossfades their weights.
- The modulation: a constant-speed random walk to random targets, 0 to 2.67 ms times Mod Depth, the whole range in
  1 / Mod Rate seconds.

Found by: sparse impulse responses (diffusion 0: the loop's taps), small diffusion (each all-pass leaves an echo
before and after a tap at its length), slow modulation (which delays move), the ratio of a pulse's second lap to its
first (the loss), and fits of each part's transfer function.

| Script | What it does |
|---|---|
| [vv.py](vv.py) | `render()` / `render_many()`: audio through VintageVerb or a built-in device, from Python |
| [clone.py](clone.py) | `both()`: the same settings through VintageVerb and Vintage Hall (VintageVerb's normalized values mapped to the clone's units) |
| [cmp2.py](cmp2.py) | the two's waveforms compared with the modulation off, the input/output phase difference taken out: what is left is the loop's own difference (dB below the signal) |
| [modstats.py](modstats.py) | with the modulation on: band RT60s and how far held tones spread, for both |
| [analyze.py](analyze.py) | the measurements both use (RT60 per octave, spectra, echo density, onsets, correlation, a tone's spread) |

`renders/ab/` has program material (snare, chords, a sung vowel) through both, at three settings, with the dry
sound: `<setting>__<sound>__vintageverb.wav` and `__vintage_hall.wav`.
