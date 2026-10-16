# SUBstation

A DAW for Windows (and Linux) built around an Ableton-style arrangement view, written
from scratch in C++20. It has a real-time audio engine with ASIO®, WASAPI and PipeWire,
VST3 hosting and delay compensation everywhere. It comes with more than twenty built-in
devices, including the whole of [Vital](https://github.com/mtytel/vital) compiled in from
its source. It can import Ableton Live Sets. It also has some ideas you won't find in
other DAWs: tracks that describe themselves, a sidechain that fits its curve to your
kick, sample hot-swapping while the song plays, and an MCP server so AI agents like
Claude Code can read and edit your song while you watch.

> This project kinda started out as a meme, but I'm starting to think it's actually much
> better for my specific workflow than Ableton is. You may or may not find it a good
> replacement, but I know for certain a lot of issues that bothered me the most with
> Ableton (crashing on switching audio devices, slow project save and load times) are
> solved with this DAW. The end goal, however, is to re-invent the way I make music with a
> DAW. Right now, features are very limited to my specific workflow, but I will continue
> adding stuff that make it more versatile and suited for everyone's music production needs.

## What makes it different

- **Tracks that know what they are.** Hover over a track's name and it tells you what it
  is in a few words ("Washed Out Serum Pluck", "Orchestral Trumpet", "Sad Piano Line").
  The label is worked out from the track's name, its instrument's preset, its files, its
  notes, its effects and their settings, and its mixer. Press **Q** to find a track by
  name *or* by what it is: `horn` finds the orchestra track that's playing horns.
- **AI agents in the session.** A built-in MCP server (off until you turn it on) lets
  Claude Code, or any other agent that speaks MCP, read the whole project: tracks, what
  each one is, devices and every parameter, clips, notes, chords and key. It can also
  edit them: "darken the pad", "pull the drums down 2 dB", "write a better piano part
  over the bars I selected". An agent's changes are kept out of your undo history. They
  show up in their own list (*View › Agent Changes*), where you can rewind or restore
  each one separately. ([Agents guide](docs/guide/agents.md))
- **A sidechain that listens.** The Sidechain device ducks the bass along a curve you
  draw or one it *fits to the kick*. It compares the two spectra, shows where they clash,
  and shapes the curve so the bass is gone while the kick's energy in the clash peaks and
  comes back as that energy fades. Tight, Natural or Loose; Lows Only; lookahead; or Auto,
  which refits at every hit.
- **Hot-swap samples while the song plays.** Right-click an audio clip › *Hot-Swap Sample*
  and every file you click in the browser plays in its place on that track, in the song,
  right away.
  The sounds most like the original are listed first. The File Manager can also find
  missing samples and replace one file everywhere it plays.
- **Find Similar Sounds.** The browser analyses your library in the background (timbre,
  envelope and pitch, using Essentia's audio descriptors), so any sample can pull up its
  closest matches out of hundreds of thousands of files.
- **Harmony-aware MIDI.** The song's chords and key are worked out from its MIDI and shown
  over the piano roll, with out-of-key notes in red. *Generate* writes block chords
  (voice-led so each chord moves as little as possible) or a bass line from them.
- **Humanize that learned from pianists.** The piano roll's humanize sets velocities with a
  machine-learning model trained on real piano performances, not random jitter.
- **Per-note bends.** Draw pitch bends, vibrato and glides on single notes (MIDI 2.0
  per-note pitch bend), sent to VST3 plug-ins as note expression.
- **Vital, built in.** The spectral warping wavetable synth is compiled in from its own
  source with all of it included: three wavetable oscillators, the sampler, both filters,
  envelopes, LFOs, 64 modulations, its effects, and `.vital` presets that load and save.
- **Effects with something to show.** Vintage Hall clones a classic 1970s digital concert
  hall. Disperser runs up to 64 all-pass stages and draws its group delay. Transient
  Shaper draws each hit as it plays. Over The Top is tuned against Ableton's OTT renders.
- **Fast and steady.** Rendering runs on many threads and gives bit-identical output on
  any number of them. Switching audio devices doesn't take the session down, and plug-in
  scanning runs in a separate process so a crashing plug-in can't take SUBstation with it.
- **Themes.** Default, *Disableton* (Live 12's look), *Flashbang* (light) and *Gay*
  (rainbow over purple-black). ([Look and Feel](docs/guide/look-and-feel.md))

## Features

### Arrangement and mixing

- Audio and MIDI tracks, group tracks (nesting, folding), return tracks and sends, and a
  master with its own effects. Track headers are laid out like Ableton's.
- An In/Out column routes each track the way Ableton does. *Audio From* can be Pre FX,
  Post FX or Post Mixer. *Audio To* can be the track's group, Main, another track's input,
  a device's sidechain, or Sends Only.
- Clip editing with snapping: trimming, stretching, sliding a clip's content. Also clip
  fades with curves, deactivating clips (**0**, as in Ableton), time selections, and edge
  scrolling.
- Sidechains, and delay compensation on every path, including per send.
- Freezing tracks, groups and returns in the background (**Ctrl+Shift+F**), and flattening
  frozen tracks.
- Undo and redo for every edit.

### Audio

- Real-time warping (Signalsmith Stretch) with several warp modes, transpose and detune,
  clip gain and pan, and reversing.
- Tempo and key are read from sample file names, so loops come in already set up.
- Recording from ASIO inputs, with a count-in and latency-corrected takes. Any track or
  the master can be resampled.

### MIDI

- A piano roll with legato, timing, quantize and humanize tools, a velocity lane, and a
  bend mode for per-note pitch bends, vibrato and glides.
- Chords and key detection, and chord and bass generation (see above).
- MIDI input from controllers (WinMM on Windows, ALSA on Linux) and from the computer
  keyboard. A MIDI track's notes can feed other tracks' devices.

### Devices

- **Instruments**: Vital, Synth, and Sampler. The Sampler follows Ableton's Simpler, with
  Classic, 1-Shot and Slice modes, slicing at transients, warping, a filter and an LFO.
- **Dynamics**: Compressor (with sidechain), Gate, Limiter, Multiband Dynamics, Spectral
  Compressor, Over The Top, Transient Shaper, Sidechain.
- **Colour and distortion**: Saturator, Amp, Erosion.
- **Time and modulation**: Delay, Chorus-Ensemble, Phaser-Flanger, Reverb, Vintage Hall,
  Disperser.
- **Pitch**: GigaTune (pitch correction for a voice: a key's notes, retune speed, pitch and
  formant shifts), Hue.
- **Voice**: Vocoder (as Ableton's: Noise, External, Modulator and Pitch Tracking carriers, 4 to 40
  bands, Precise or Retro, Formant, Enhance, Unvoiced), Vocal Rider (rides a vocal's level to a target,
  holding between phrases, following the music on its sidechain).
- **Utility**: EQ, Utility.
- Racks with parallel chains, macros and presets. Every parameter can be automated and is
  played sample-accurately.
- **VST3 plug-ins**: instruments and effects, with their own editors, presets and
  sidechains, latency compensation, and crash-safe scanning.

### Browser

- Instant search over hundreds of thousands of samples, preview, and results ranked by how
  often you use them.
- Find Similar Sounds and hot-swapping (see above).

### Projects and export

- `.gilproj` projects that save and load quickly, and a template every new project starts
  from.
- **Importing Ableton Live Sets**: tracks, groups, clips, VST3 plug-ins with their
  settings, Live's EQ Eight, Compressor, Delay, Utility and Simpler, Drum Racks (one track
  per pad), and automation.
- WAV and MP3 export of the arrangement, the loop or a time selection.
- **Updates** from inside the program: Stable, Nightly or Canary builds, checked for daily
  and installed on closing if you like ([releasing](docs/releasing.md)).

See the [user guide](docs/guide/README.md) for how each of these behaves, the
[keyboard shortcuts](docs/guide/shortcuts.md), and
[what isn't implemented yet](docs/guide/limitations.md).

## Under the hood

SUBstation is one C++20 program in layers:

- a real-time audio engine (ASIO, WASAPI through miniaudio, PipeWire on Linux; VST3
  hosting) and a browser backend, neither of which uses Qt;
- an application layer on Qt Core and Qt Gui that holds the project, its editing and
  undo, and mirrors it into the engine;
- a Qt Quick (QML) UI whose arrangement, piano roll, envelopes, meters and device displays
  are C++ scene-graph items.

The render workers run at real-time priority, and the engine keeps to strict real-time
rules (see [Architecture](docs/architecture.md)). It runs on Windows 10/11 and Linux.

## Setup

You need a C++20 compiler, CMake 3.26 or newer with Ninja, and Qt 6.4 or newer
(Core, Gui, Qml, Quick, QuickControls2, Test). On x86-64 it needs a CPU with AVX2; on arm64 it builds for the baseline (NEON).

- **Windows 10/11**: Qt's *MinGW 64-bit* build of a Qt 6 release (6.5 or newer
  recommended) with the MinGW toolchain Qt's online installer ships with it: what
  the releases are built with. Or Visual Studio 2022 or newer with the *Desktop
  development with C++* workload and Qt's *MSVC 2022 64-bit* build (see
  [docs/building.md](docs/building.md#building)).
- **Linux** (Debian/Ubuntu): GCC 13 or Clang 16, `cmake ninja-build`, and Qt's
  packages (`qt6-base-dev qt6-declarative-dev` and the QML modules the UI imports:
  the list is in [docs/building.md](docs/building.md#requirements)), plus `xvfb`
  to run the UI's tests headless. Audio goes through PipeWire (`libpipewire-0.3-dev`;
  elsewhere the system's default backend, the *System* driver), MIDI input through
  ALSA's sequencer (`libasound2-dev`), and VST3 plug-ins show their editors as X11
  windows (`libxcb1-dev`).

Linux:

```sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

Windows, in the *Qt 6.x (MinGW 64-bit)* command prompt (MSVC's is in
[docs/building.md](docs/building.md#building)):

```bat
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\mingw_64 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
ninja -C build
C:\Qt\6.8.0\mingw_64\bin\windeployqt.exe --qmldir ui\qml build\bin\substation.exe   # once
```

Re-run `ninja -C build` after changing anything (the build is incremental).

**ASIO** needs Steinberg's ASIO SDK, which isn't in the repository (its licence
doesn't allow passing it on). Download it from <https://www.steinberg.net/asiosdk>,
unzip it into the project folder as it comes, and configure again. Without it the
engine builds with WASAPI only.

More in [docs/building.md](docs/building.md): where the build looks for the ASIO
SDK, build options and targets, and the layers' boundaries.

## Run

```sh
build/bin/substation                  # or: build/bin/substation path/to/song.gilproj
```

(`build\bin\substation.exe` on Windows.) The first launch uses the system default
output device; change it in *Options → Preferences* ([audio setup](docs/guide/audio-setup.md)).
The browser starts with your Music folder as a Place. Add more with *Add Folder…*.

To let Claude Code into your session, turn on *Options › MCP Server* and run:

```sh
claude mcp add --transport http substation http://127.0.0.1:7381/mcp
```

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The tests need no sound card and see none of the installed plug-ins: the build
makes a few test VST3 plug-ins for them, and a fake ASIO driver when the ASIO SDK is
there (without it, the ASIO tests are skipped). Engine tests render offline; the
application layer's tests drive it without a window; the UI's tests drive the real
QML views (on Linux under `xvfb-run`). See [docs/testing.md](docs/testing.md).
[benchmarks/](benchmarks/README.md) measures the renderer, the built-in devices, Vital
and the browser backend.

## Project layout

```
engine/src/          the real-time audio engine and plug-in hosting (sub_engine; no Qt)
engine/third_party/  miniaudio (public domain / MIT-0), Signalsmith Stretch and Linear, the VST 3 SDK subset (MIT),
                     fastpitch's pYIN and swift-f0's SwiftF0 model (MIT), LAME's MP3 encoder (LGPL)
intelligence/third_party/  Essentia 2.1-beta5's core and the algorithms used (AGPLv3), HUMANBRO's runtime
browser/src/         the browser's file index and search (sub_browser; no Qt)
app/src/             the application layer (sub_app; Qt Core and Gui): model/, editor/, io/, audio/,
                     session/, browser/, plugins/, analysis/
ui/                  the Qt Quick UI: qml/ (QML module SUBstation), src/ (C++ scene-graph items),
                     style/ (the controls' style), main.cpp (the substation executable)
tools/scanner/       substation-scan, the VST3 scanner's child process
tests/               engine/ (engine_tests), app/ (Qt Test), vst3_plugins/, asio_driver/
benchmarks/          the renderer's and the browser backend's benchmarks (not run by ctest)
cmake/               compiler warnings, the layers' boundary check
docs/                documentation
```

## Documentation

- [User guide](docs/guide/README.md) and [keyboard shortcuts](docs/guide/shortcuts.md)
- [Agents](docs/guide/agents.md): connecting Claude Code and other MCP agents
- [Architecture](docs/architecture.md): layers, threads, real-time rules
- [Building](docs/building.md) and [testing](docs/testing.md)
- [Code reference](docs/README.md): the engine, the application layer, the UI, the browser
- [Licensing](docs/licensing.md): MIT, and what Essentia's AGPLv3 asks of a build you distribute

## Licence

SUBstation's own code is under the [MIT licence](LICENSE). Builds include
[Essentia](intelligence/third_party/essentia) (AGPLv3), so a build you distribute is,
as a whole, under the AGPLv3's terms: ship it with its source. See
[docs/licensing.md](docs/licensing.md), which also covers the ASIO SDK and the velocity
model.

ASIO is a registered trademark of Steinberg Media Technologies GmbH. VST is a registered
trademark of Steinberg Media Technologies GmbH.
