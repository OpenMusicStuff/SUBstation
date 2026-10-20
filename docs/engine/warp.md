# Warping and audio sources

How audio files become clips the engine can play: decoding into memory with waveform peaks
([AudioSource.h](../../engine/src/AudioSource.h), [AudioSource.cpp](../../engine/src/AudioSource.cpp)), and playing a
clip at another speed or pitch: time stretching with stretch voices, and Re-Pitch resampling
([Warp.h](../../engine/src/Warp.h), [Warp.cpp](../../engine/src/Warp.cpp)). The stretcher is
[Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch), vendored in
`engine/third_party/`.

What the user sees (the clip view, warp modes, transpose, tempo and key from file names) is in
[guide/audio-clips.md](../guide/audio-clips.md). How clips are mixed into a track is in
[rendering.md](rendering.md).

## Overview

- An `AudioSource` is a file at the engine's sample rate, with six levels of min/max peaks for drawing waveforms:
  decoded whole to 32-bit float, planar (resident), or, while streaming is on, a long file read from disk as it
  plays (streamed: [streaming.md](streaming.md)). It is immutable once loaded, so the UI, snapshots and the audio
  thread share it freely.
- The snapshot turns each clip's beats into samples by the tempo map ([music.md](../music.md)). A warped clip's two
  ends sit on their beats, and it plays at `tempo / segment BPM` speed: under a tempo that changes along it, the
  tempo where it is, by its `WarpCurve`.
- Clips that need it are time-stretched on the audio thread by Signalsmith Stretch (MIT, header-only; vendored with
  its FFT library). Nothing else is needed to build it.
- Stretchers keep state from block to block, so they live in *voices* (`WarpVoice`). These are allocated on the edit
  side, pooled per warp-mode block size, and handed to the audio thread in the snapshot. The renderer binds a voice
  to a playing clip.
- A voice re-seeks when playback jumps (start, locate, loop, clip start). It uses the stretcher's `outputSeek` to
  pre-compute its latency, so the first frame is exactly aligned. A tempo change keeps the source position, so the
  clip carries on without a re-seek.
- Live playback doesn't keep stretching: a background thread renders each stretched clip whole (the *warp cache*,
  `WarpCache`), and once the render is ready the clip plays from it, at the cost of a clip played as it is. It takes
  over where a voice would start again (start, locate, loop), never in the middle of a stretch under way.
- Offline renders and exports use fresh voices with a fixed random seed, so they are repeatable and don't disturb
  live playback. They don't read the warp cache's renders.
- Clips at their own tempo with no transposition skip the stretcher and play bit-exact. Re-Pitch uses windowed-sinc
  resampling, with the cutoff lowered when speeding up; it is vectorised, and costs a fraction of what stretching does
  (see [What it costs](#what-it-costs)).

## Files

| File | What it holds |
|---|---|
| [AudioSource.h](../../engine/src/AudioSource.h) / [.cpp](../../engine/src/AudioSource.cpp) | `AudioFileInfo`, `AudioSource::probe()`, `decode()`, `load()`, planar data (or a streamed file: [streaming.md](streaming.md)), `read()`, peak mipmaps (`PeakBuilder`) |
| [SourceCursor.h](../../engine/src/SourceCursor.h) | `SourceCursor`: a source's frames wherever they are (memory, or the stream cache's pages), real-time or waiting for the disk |
| [Warp.h](../../engine/src/Warp.h) / [.cpp](../../engine/src/Warp.cpp) | `WarpMode`, `StretchConfig`, `stretchConfigFor()`, `stretchTiming()`, `configureStretcher()`, `kStretchSeed`, `WarpVoice` (and its inputs: `SourceInput`, `StreamInput`, `LeasedInput`), `WarpVoiceSet`, `renderResampled()` (and, inside, its kernel: `SincTable`, `SincPhases`, `SourceSpan`, `phasedFrame()`, `kernelFrame()`) |
| [WarpCache.h](../../engine/src/WarpCache.h) / [.cpp](../../engine/src/WarpCache.cpp) | `WarpRender` (a stretched clip rendered whole), `WarpCache` (its thread, `request()`, the budget) |
| [Snapshot.h](../../engine/src/Snapshot.h) | `ClipRender` (`Playback::Direct / Resample / Stretch`, `rate`, `sourceAt()`, `key`, `rendered`), `TrackBuffers::kMaxClipVoices` |
| [EngineSnapshot.cpp](../../engine/src/EngineSnapshot.cpp) | clips to `ClipRender`s, choosing the playback, asking the warp cache for renders; `ensureWarpVoicesLocked()` |
| [Engine.cpp](../../engine/src/Engine.cpp) | the source cache: `loadSource()`, `cachedSource()`, `releaseUnusedSources()`, `reloadSourcesLocked()`, `sourceKey()` |
| [EngineOffline.cpp](../../engine/src/EngineOffline.cpp) | fresh voices for offline renders (`prepareOfflineLocked`) |
| [Renderer.cpp](../../engine/src/Renderer.cpp) | `assignVoices()` (a render or a voice), `carriedOn()`, `acquireVoice()`, `renderClips()` |
| [third_party/signalsmith-stretch](../../engine/third_party/signalsmith-stretch) | Signalsmith Stretch 1.3.2 (MIT), one header, unmodified |
| [third_party/signalsmith-linear](../../engine/third_party/signalsmith-linear) | Signalsmith Linear 0.6.4 (MIT): only `stft.h` and `fft.h`, what Stretch needs, plus a platform FFT backend of SUBstation's (`platform/fft-kfr.h`: its power-of-two FFTs on KFR's, `SIGNALSMITH_USE_KFR`) |
| [third_party/miniaudio](../../engine/third_party/miniaudio) | the decoders `AudioSource` uses (compiled once in [miniaudio_impl.c](../../engine/src/miniaudio_impl.c)) |

Both Signalsmith libraries are added as `SYSTEM` include directories in [CMakeLists.txt](../../CMakeLists.txt), and
`Warp.cpp` includes the header with MSVC warnings off. See [building.md](../building.md).

## Audio sources

### Decoding

`AudioSource::load(utf8Path, sampleRate)` decodes with miniaudio's decoder (WAV, FLAC and MP3):

1. It opens the file once to learn its native channel count, rate and sample format.
2. It opens it again asking for float output at the engine's rate, with miniaudio's linear resampler at its highest
   low-pass filter order (`MA_MAX_FILTER_ORDER`). Mono files stay mono; anything with two or more channels is decoded
   as stereo (more than two are downmixed by miniaudio).
3. It reads 16384 frames at a time, de-interleaving each straight into its place in one planar buffer (channel `c`
   occupies `[c * frames, (c + 1) * frames)`), made as long as the decoder expects (longer if it was wrong: copied
   once then), the second channel moved down at the end if the file was shorter. No second copy of the file.
4. It builds the peaks.

Steps 1 to 3 are `AudioSource::decode(path, rate, begin, take)`, which hands over the decoded format and then the
frames a chunk at a time: the stream cache's cache files are decoded through it too ([streaming.md](streaming.md)),
so a streamed file plays the same samples as a resident one.

It throws `std::runtime_error` if the file can't be opened or contains no audio. Paths are UTF-8 and opened with
`initDecoderFile()` ([MiniaudioFiles.h](../../engine/src/MiniaudioFiles.h)): miniaudio's wide call on Windows, so any
Windows path works, and its narrow one elsewhere, so a path never goes through the C library's locale.

`AudioSource::probe(utf8Path)` reads only what is needed for length and format (`AudioFileInfo`: frames at the file's
own rate, channels, rate, duration). For streams that report no length it counts the frames by decoding.

Accessors: `path()`, `channels()`, `frames()`, `sampleRate()` (the rate it was decoded at), `fileSampleRate()`,
`duration()`, `resident()`, `data()`, `channelData(c)` (null for a streamed source), `stream()` (a streamed source's
`StreamedAudio`), and `read(channel, start, count, out)`: frames from memory, or from disk through the stream cache,
waiting for its reads (any thread but the audio thread's).

### Peak mipmaps

Six levels (`kNumPeakLevels`). Level *n* holds one (min, max) pair per channel for every `32 * 4^n` frames
(`samplesPerPeak(level)`: 32, 128, 512, 2048, 8192, 32768). Level 0 is computed from the samples; each coarser level
combines four peaks of the one below. The layout of `peaks(level)` is `[numPeaks][channels][2]`.

The application layer hands them to the UI without copying: a `Waveform`
([app/src/audio/Waveform.h](../../app/src/audio/Waveform.h), from `EngineBridge::waveform(path)`) holds a
`shared_ptr` to the source, which keeps it alive, and gives `peaks(level)` and `channelData(c)` as they are (a
streamed source's frames through `read()`). A streamed source's peaks come from its peak file, made by a
`PeakBuilder` (the same levels, to the bit) as its frames were decoded or scanned. The
arrangement's waveforms and the Sampler's editor draw from these ([ui/arrangement.md](../ui/arrangement.md)); the UI
never includes the engine's headers.

### The source cache

The engine keeps one source per file, keyed by `Engine::sourceKey()` (the path normalised, made preferred and
lower-cased, so two spellings of a path share it).

- `Engine::loadSource(path)` returns the cached source if it is at the engine rate. Otherwise it decodes *without the
  engine lock* (the engine bridge calls it from its decoding threads), then stores it and rebuilds the
  snapshot, so clips waiting for this file become audible. If the device's rate changed meanwhile it decodes again
  (up to four tries, then `std::runtime_error`).
- `cachedSource(path)` returns what is cached, at whatever rate.
- `cacheSource(source)` caches a source `loadSource()` returned again, if the cache let go of it meanwhile (and it is
  at the engine rate), and rebuilds the snapshot as `loadSource()` does. The engine bridge hands back what it holds:
  a file it decoded, by the time it hears so, or one it asks for again.
- `releaseUnusedSources()` drops sources no clip uses (except the one previewing).
- When the device's sample rate changes, `reloadSourcesLocked()` decodes every cached source again at the new rate;
  files that vanished or became unreadable are dropped, and their clips go silent. (Streamed ones are opened again
  where no new cache file is needed, else let go of until they are loaded again: [streaming.md](streaming.md).)
- While streaming is on, `loadSource()` opens a file at least the threshold long streamed
  ([streaming.md](streaming.md)).
- A snapshot only includes a clip whose source is cached *at the engine rate*; until then the clip is skipped
  ("still loading").

Built-in devices that play files (the Sampler) get theirs via a `SourceLoader` (see [devices.md](devices.md)):
`Engine::loadResidentSource()`, the same cache while the file wouldn't stream, else a decoding of its own (a device
reads its sample anywhere, note by note: always in memory). The engine bridge decodes sources in a thread pool of its own
([app/engine-bridge.md](../app/engine-bridge.md)).

The Sampler warps its sample too (its Warp: the whole sample in so many beats at the tempo), with the same modes: it
resamples for Re-Pitch, and otherwise runs a Signalsmith stretcher per note, configured as the clips' are
(`configureStretcher()`: the block sizes of `stretchTiming()`, split computation) with the same seed
(`kStretchSeed`). Its stretchers are its own, made on the main side while
it stretches ([devices.md](devices.md#sampler-builtinsampler-instrument)).

## Warp modes and how a clip plays

```cpp
enum class WarpMode : uint8_t { Transients, Standard, Smooth, Formants, RePitch };
```

The order matches the application's list (`kWarpModes` in [app/src/model/Clip.h](../../app/src/model/Clip.h)). Projects
saved with the earlier Ableton-style names load into the equivalent mode (a model concern; see
[app/serialization.md](../app/serialization.md)).

| Mode | Playback | Stretch config | Block / interval |
|---|---|---|---|
| Transients | Stretch | `Transient` | 80 ms / 20 ms: tight attacks (drums) |
| Standard | Stretch | `Standard` | 120 ms / 30 ms: Signalsmith's default preset |
| Smooth | Stretch | `Smooth` | 200 ms / 50 ms: pads, textures, noise; softer attacks |
| Formants | Stretch | `Standard` | as Standard, keeping formants in place when transposing |
| Re-Pitch | Resample | (none) | windowed-sinc; speed and pitch change together |

When the snapshot is built (`rebuildSnapshotLocked`, see [rendering.md](rendering.md)), each `ClipDesc` becomes a
`ClipRender`:

- `start` = the sample the tempo map puts `startBeat` at (`RenderSnapshot::sampleAt`); `sourceOffset` = `offsetSec`
  in samples.
- **Warped** (`warp` and `segmentBpm > 0`): `rate = tempo / segmentBpm`; the end sits on its beat:
  `endBeat = startBeat + durationSec * segmentBpm / 60`, and `length` runs from start to `endBeat` in samples. Both
  ends follow the tempo. Where the tempo changes along the clip it has a `curve` (`WarpCurve`, Warp.h: points
  (timeline samples from its start, source frames from its offset), straight between): its source plays each of
  its beats where the map puts that beat. The points are its ends, the map's points within it, and every 256
  samples through a ramp (`warpCurve()`, EngineSnapshot.cpp); `rate` is then its average. One tempo all along it:
  no curve, that tempo's rate.
- **Warped from a source map** (`ClipDesc::sourceMap`: a track frozen under a tempo map): the source's own beats
  are that map's (its seconds from the file's start), so `endBeat = startBeat + map.beatAt(offset + duration) -
  map.beatAt(offset)`, and it always has a curve: each point's source frame is where the source map has the clip's
  beat at that sample (the song's points and the source map's within it, steps through either's ramps). Fades
  count their beats by the source map too.
- **Unwarped**: `rate` 1, and the length is `durationSec` in samples, so its length in beats follows the tempo.
- The length is cut so the clip never plays past the end of the file.
- Playback:
  - Re-Pitch: `Resample` if the speed changes, else `Direct`.
  - Otherwise `Stretch` if the speed changes or `transpose` is not 0 (Transpose and Detune shift the pitch without
    changing the speed, warped or not); else `Direct`, bit-exact. `transpose` is in semitones (fractions for
    detune); `preserveFormants` is set for Formants. Re-Pitch ignores transpose.
- `key` identifies the clip across snapshots: a hash of the clip's `id` (or its index if it has none) mixed with
  the track id. Edits that rebuild the snapshot don't interrupt a stretching clip because the key is stable.
- `fadeIn` / `fadeOut`: its fades in timeline samples (its own, in source seconds, at its speed; or the short one
  against clicks where an edge cuts into the file), and their curves: see [rendering.md](rendering.md).

`ClipRender::sourceAt(t)` = `sourceOffset + (t - start) * rate` (or along the curve): the fractional source frame
heard at timeline sample `t`; `rateAt(t)` how fast it goes there. The stretcher's lead and seek, the tolerance a
voice carries on within and Re-Pitch's anti-aliasing (the fastest it goes in the block) take the rate where the
clip is.

## Stretch voices

### Allocation (edit side)

`WarpVoice(config, sampleRate)` configures a `SignalsmithStretch<float>` for two channels with its config's block
and interval, with *split computation* on: each spectral block's work is spread over the following interval instead
of being done in one callback. That adds an interval of latency, which the alignment below compensates. Constructing a
voice allocates; `render()` does not.

`ensureWarpVoicesLocked(needed)` grows the pools (`Engine::warpVoices_`, a `WarpVoiceSet`: one vector per
`StretchConfig`) up to what the snapshot needs, at most 64 per configuration. Per track it counts at most 3 stretched
clips per configuration, since clips on a track don't overlap and only a few play in any one block. Pools only grow;
the snapshot holds the pools (`snap->warpVoices`).

### Binding voices (audio thread)

In the chunk's prologue `Renderer::assignVoices()` walks the clips a track plays in this chunk's segments, in the
order `renderClips()` will play them, and gives each stretched clip a slot (at most `kMaxClipVoices` = 32 per track
per chunk). `acquireVoice()` picks the voice that played this clip last (same `key`) if there is one; otherwise the
voice idle the longest; never one already used in this block. `continuing` is true when the voice played this clip in
the previous block. If there are more simultaneous stretched clips than voices, the extra clip stays silent. A
monitored track plays its input instead of its clips, so it gets no voices.

Live, a slot gets the clip's render instead of a voice (`ClipVoice::rendered`) when the render is ready and the
voice that played the clip in the block before doesn't carry it on (`carriedOn()`: that voice exists, was used in the
block before, and `WarpVoice::carriesOn()` says its output lines up with where the clip is now, the test `render()`
uses to decide whether to re-seek). So a stretch under way goes on to its end (the render and a voice that started
mid-way differ a little, and switching would click), and the render takes over at the next start, locate or loop
jump. A clip playing from its render needs no voice. `renderClips()` reads the render in place, frame `t - start`
for timeline sample `t`, as it reads a source played as it is.

The other way round happens when a clip playing from its render changes mid-way (a new tempo, or transpose): its new
render isn't ready, so a voice takes over, and it must seek, which would click (the restarted stretch and the render
don't line up in phase). Instead the voice crossfades from the render it leaves, over 10 ms
(`WarpVoice::kFadeSeconds`):

- The snapshot build gives the changed clip the render it had before (`ClipRender::fading`, found in the previous
  snapshot by `key`), or, if that one wasn't ready yet, what it was fading from. Once the new render is ready, the next
  build lets the old one go (at most one extra render per changed clip, for that long).
- Playing from a render, the renderer notes on it the block and the source position it got to
  (`WarpRender::notePlayed()`). A voice that takes over (not continuing) crossfades when the clip's `fading` render was
  sounding in the block before, up to where the clip is now (`Renderer::soundedUpTo()`, the same tolerance as
  `carriesOn()`: a tempo change keeps the source position). A locate or a loop's jump doesn't fade.
- `WarpVoice::fadeFrom()` copies the next 10 ms of the old render, read along the clip's new mapping (between frames,
  at its own rate) into a buffer the voice allocated when it was made, so nothing of the render is kept;
  `render()` mixes the voice in linearly over it.

### `WarpVoice::render()`

```
 source frames:   ... [inputPos - lead] ........ [inputPos] -> fed next
                         ^ the frame being output now
 lead = inputLatency() + rate * outputLatency()
```

1. If the transpose or formant setting changed (or the voice is new), it sets `setTransposeSemitones()` and
   `setFormantFactor(1, preserveFormants)`.
2. It works out where the output is (`inputPos - lead`) and where it should be (`sourceAt(position)`). If the voice
   is not continuing this clip, or isn't primed, or the two differ by more than `2 + 2 * rate` frames, it re-seeks:
   `outputSeek()` with `outputSeekLength(rate)` frames from `floor(wanted)`, which pre-computes the pre-roll so the
   very next output frame is that source frame. Starts, locates and loop jumps are sample-accurate. A tempo change
   rescales timeline positions but keeps the source position within that tolerance, so it carries on.
3. It feeds exactly enough input to reach the source position the block should end on:
   `llround(sourceAt(position + frames) + lead) - inputPos`. The target is absolute, so rounding never accumulates.
4. It writes (does not add) the output into the caller's buffers. A mono source feeds both channels. Outside the
   file the input is silence.

The renderer then applies the clip's gain, fades and pan per sample (`renderClips`).

### Offline renders

`Engine::prepareOfflineLocked()` makes fresh voices, as many as live playback has, for the offline renderer
(`setWarpVoices`). With the fixed seed (`kStretchSeed`, the stretcher randomises some phases), offline renders and
exports come out the same every time, and live voices keep their state. They never read the warp cache's renders:
what they produce doesn't depend on which renders happen to be ready.

## The warp cache

[WarpCache.h](../../engine/src/WarpCache.h): live playback reads stretched clips rendered whole in the background,
so a stretched clip costs the audio thread what a clip played as it is costs (a fraction of a nanosecond a frame
instead of a few hundred; see [What it costs](#what-it-costs)).

- **A render** (`WarpRender`) is a clip's output over its whole length, stereo float: frame `n` is what a fresh voice
  starting at the clip's first frame plays `n` frames later. The cache's thread makes it with a fresh `WarpVoice`
  (the fixed seed) fed in blocks of 1024, so a render is the same every time. It depends only on what shapes the
  output: the source, `sourceOffset`, `length`, `rate`, its curve (by what it holds: relative to the clip), the
  stretch configuration, `transpose`, `preserveFormants` and the sample rate. Where the clip sits on the timeline,
  its gain, pan and fades don't matter (the renderer applies those as for any clip), so moving a clip, or changing
  its gain, keeps its render, unless it moves to where the tempo changes differently under it. A change of tempo
  changes the rate (or curve) of the warped clips, so they are rendered again. `WarpRender::frameOf()` finds the
  frame that played a source position (a voice taking over mid-way fades from there). Its audio (`audio()`) is an
  `AudioSource` of its own: in memory (`left()`, `right()`; no peaks), or, for a clip of a streamed source, a file in
  the stream cache's folder, streamed back as its source is ([streaming.md](streaming.md#the-warp-caches-renders)),
  its voice's input read with leased pages (`StreamReads{blocking}`).
- **Asking.** Each snapshot build brackets its clips with `beginBuild()` and `endBuild()`, and asks for each
  stretched clip's render (`request()`), which the `ClipRender` carries (`rendered`). A render asked for before is
  shared; a new one is queued. `endBuild()` forgets the renders no clip asked for: an unfinished one stops (it
  checks between blocks), and the memory goes with the last snapshot holding it, on the edit side, as retired
  snapshots are freed (never on the audio thread).
- **The thread** (one, at background priority: `platform::enterBackgroundMode()`) renders the longest-waiting
  render whose clip has stayed the same for 250 ms (`kDefaultSettle`), so that a tempo dragged through many values
  isn't rendered at each of them. It never takes the engine's mutex: a render has all it needs (it holds the source).
  `ready()` turns true (release) once every frame is written; the audio thread reads it (acquire) and only then the
  frames. Until a clip's render is ready, the clip stretches live as before.
- **Changing mid-way**: a clip that changes while playing from its render goes on through a voice, which crossfades
  from the render (see [Binding voices](#binding-voices-audio-thread)), and its new render takes over at the next
  start, locate or loop jump.
- **The budget**: at most 512 MiB of renders per snapshot (`kDefaultBudget`, about 23 minutes of stereo at
  48 kHz). Clips past it, and a render that couldn't allocate, stretch live.
- `Engine::waitForWarpRenders()` waits until every render the project needs is ready (tests and benchmarks).

## Re-Pitch resampling

`renderResampled(clip, position, frames, outL, outR, span, reads)` is stateless: for each output frame it convolves
the source around `sourceAt(t)` with a windowed-sinc (Lanczos) kernel of 8 zero crossings each side, tabulated at 512
points per zero crossing and linearly interpolated (`SincTable`, built during static initialisation, never on the
audio thread). It reads the source through a `SourceSpan` (the frames from one on): a resident source's whole file in
place, or a streamed source's frames the kernel reaches over a piece of the block, fetched into `span` (the worker's
scratch, `kResampleSpan` frames a channel) as `reads` says ([streaming.md](streaming.md)). The cutoff is the
block's either way, so the output doesn't depend on how it was read.

Speeding up moves content above the output Nyquist, so the cutoff is lowered to `1 / rate` and the kernel widened by
the same factor: the content is filtered out instead of aliasing. The rate used for this is clamped to 1..4
(`kMaxRateFiltered`), a cost cap. The output is scaled by the cutoff to keep the gain.

How a frame is computed (the same kernel and taps either way; the result is within float rounding of a tap-by-tap sum,
about 3e-7 at most, more than 135 dB under the signal):

- **Up to the source's speed** (rate at most 1, so the full cutoff), away from the file's ends: a frame's 16 taps lie
  whole source frames apart, so all of them fall the same fraction between two table points. `SincPhases` holds the
  table by phase (513 rows of the 16 taps' values, 33 KB, built from `SincTable` during static initialisation), and
  `phasedFrame()` mixes the two rows around the frame's fraction: no lookup per tap. With AVX, a vector of eight taps
  per half and channel.
- **Faster** (or near the file's ends): `kernelFrame()`. With AVX2 it takes eight taps at a time. Their positions in the
  table are computed in float, the whole frames and the fraction apart, which keeps them within a thousandth of a table
  step of the double-precision lookup. Each tap's two neighbouring table values are loaded as one 64-bit pair and
  shuffled apart, not with gather instructions: CPUs running Intel's Downfall (GDS) mitigation run those very slowly
  (where they are fast, gathers measured about 1.5 times faster still). Taps past the kernel's last are masked: never
  read, weighing nothing.
- **Without AVX** (arm64): the phase table's plain loop (which compilers leave scalar: each frame's sum ends in one
  value), and `SincTable` tap by tap otherwise.
- The taps are summed in eight lanes per channel and the lanes added pairwise at the end (in the same order with or
  without AVX), not one after the other. The order is fixed, so offline renders stay repeatable.

Re-Pitch at rate 1 never comes here: the snapshot makes such a clip `Direct`.

## What it costs

Measured with [warp_bench](../../benchmarks/warp_bench.cpp) (a 30 s stereo clip at 48 kHz in blocks of 256; results
and the profiles behind them in [benchmarks/README.md](../../benchmarks/README.md#warping)), per output frame:

- **Direct**: under 1 ns (it reads the source in place).
- **Re-Pitch**: about 8 to 9 ns up to the source's speed, 35 to 45 ns faster (the kernel widens with the rate), on a
  2.1 GHz Xeon; about 6 times and 1.5 to 2.3 times less than the tap-by-tap loop it replaced.
- **Stretched** (any mode): 260 to 300 ns there (about 1.3 % of a core per clip), Formants about 30 % more. Most of it is
  Signalsmith Stretch's own spectral processing: the phase-vocoder prediction per bin (complex products, a division
  and a square root, about half), its FFTs and windowing (about a third), and feeding and reading it sample by sample
  (under a tenth). The C library's maths isn't a part of it, MinGW's x87 functions included: Signalsmith calls `sin`
  and `exp` only when configuring and twice per channel per block. Silent input is already cheap: once it has been silent
  for two analysis blocks' length, the stretcher passes it through without spectral processing. That is what a
  stretched clip costs offline, and live until its render is ready.
- **Stretched, from its render** (live, once the warp cache has it): about 0.5 ns, as Direct. The render costs about
  what playing the clip stretched once does (Standard 270 ns a frame, Formants 350), once, on the cache's thread:
  under half a second for a 30 s clip there. A render takes 8 bytes a frame (23 MB a minute at 48 kHz).

## Invariants and real-time rules

- `AudioSource` is immutable after `load()`; snapshots hold `shared_ptr`s, and retired snapshots are freed on the UI
  thread once the audio thread's epoch has moved past them. (A streamed source's frames don't change either: which of
  its pages are in memory does, [streaming.md](streaming.md).)
- `WarpVoice` construction and `configure()` allocate: only on the edit side (and on the warp cache's thread, for its
  own voices). `render()`, `outputSeek()` and `renderResampled()` don't allocate or lock, live; offline, reading a
  streamed source, they wait for the disk.
- A `WarpRender` is written only by the warp cache's thread, and read by the audio thread only once `ready()`; it is
  freed on the edit side or on the cache's thread, never on the audio thread.
- A voice is used by one clip per block: `acquireVoice()` never hands out a voice stamped with the current block.
- Clips on one track don't overlap (the model resolves overlaps), which is what bounds voices per track.

## Extending it

- **A new warp mode**: add it to `WarpMode` (and to `WARP_MODES` in the model, in the same order), map it in
  `stretchConfigFor()`, and if it needs another block size add a `StretchConfig` and a `kTimings` entry
  (`kNumStretchConfigs` grows with it). Anything that changes the stretcher's settings per clip goes in
  `ClipRender` and is applied at the top of `WarpVoice::render()`.
- **Warp markers** (warping within a clip) are not implemented: a warped clip has one segment BPM. Its source
  position is already a piecewise mapping under a changing tempo (`WarpCurve`): markers would add their points
  to it.
- **Another way to read a source** (a resident source at its native rate, or in 16 bits): `SourceCursor` is the one
  place the renderer reads sources through ([streaming.md](streaming.md)); a storage behind it needs `fetch()`, the
  stretcher's input and Re-Pitch's span.

## Gotchas

- `ClipDesc::durationSec` and `offsetSec` are in seconds of *source* audio; a warped clip's length in beats is
  derived from them and `segmentBpm`.
- A clip whose source is cached at another rate is left out of the snapshot until the source is decoded again.
- The Sampler's sample keeps the rate it was loaded at; it plays at `source rate / engine rate`, so its pitch stays
  right after a rate change.
- More than 64 voices per configuration, or 32 stretched clips on a track in one chunk, leave the extra clips
  silent.

## Tests

- [tests/engine/test_warp.cpp](../../tests/engine/test_warp.cpp): transpose shifts pitch but not length (and detune
  in fractions), warped clips follow the tempo and keep their pitch in every mode, slow down, are bit-exact at their
  own tempo, only warped clips rescale on a tempo change, are sample-aligned (also after a locate and when
  transposed), offline renders are repeatable, Re-Pitch changes speed and pitch together and filters rather than
  aliases, stereo and mono sources, clip pan, many warped clips in sequence, and exports include warped audio.
- [tests/engine/test_warp_cache.cpp](../../tests/engine/test_warp_cache.cpp): a render is what a voice plays (bit for
  bit), live playback plays a ready render (and offline renders don't), a stretch under way goes on and the render
  takes over at a locate, a tempo change while a clip plays from its render crossfades into the stretcher (without the
  fade it clicks), renders no snapshot asks for are forgotten and the budget holds, an engine's renders finish and
  leave exports as they were, a clip warped along a curve is rendered by it (and kept for an equal curve).
- [tests/engine/test_tempo_engine.cpp](../../tests/engine/test_tempo_engine.cpp): warped clips on their beats
  through a tempo jump and a ramp, Re-Pitch and stretched.
- [benchmarks/warp_bench.cpp](../../benchmarks/warp_bench.cpp) (not a test): what each playback costs, from a render
  too (the `-cached` cases), and from a streamed source (the `-streamed` cases).
- [tests/engine/test_engine_render.cpp](../../tests/engine/test_engine_render.cpp): source peaks and samples,
  resampling to the engine rate, `probe`, clips waiting for their source.
- [tests/engine/test_streaming.cpp](../../tests/engine/test_streaming.cpp): streamed sources play the frames and
  peaks resident ones have; Direct, Re-Pitch and stretched clips of them render the same, live and offline; a streamed
  clip's render is the in-memory one's. CTest's `engine_streamed` runs every engine test again with every file
  streamed ([streaming.md](streaming.md#testing)).
