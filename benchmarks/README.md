# Benchmarks

Programs to run by hand, not part of the test suite (ctest doesn't run them).
Nothing they do touches your settings, use counts, browser index or plug-ins.

| | |
|---|---|
| [parallel_render_bench.cpp](parallel_render_bench.cpp) | Tracks rendered on one thread and on several: offline (best of three, checked bit-identical on any number of threads) and live through the fake ASIO driver. The engine alone, no Qt. |
| [browser_backend_bench.cpp](browser_backend_bench.cpp) | The browser's backend on a large synthetic library: indexing, starting from the saved index, searches. Checks every query against the reference the tests use, and fails if any differs. |
| [builtin_devices_bench.cpp](builtin_devices_bench.cpp) | Each built-in audio effect alone, as the audio thread runs it: what it costs a core, at its defaults or at settings given. The engine alone, no Qt. |
| [warp_bench.cpp](warp_bench.cpp) | An audio clip's playback alone, as the renderer asks for it: Direct, Re-Pitch at several rates, each stretch configuration, transposed, Formants. Nanoseconds a frame and the load on a core. The engine alone, no Qt. |
| [vital_bench.cpp](vital_bench.cpp) | The built-in Vital against Vital's own VST3 plug-in: the same notes and settings held through each, what each costs a core. The engine alone (its VST3 host), no Qt. |
| [sound_similarity_bench.cpp](sound_similarity_bench.cpp) | Sound similarity's fingerprints (Essentia's descriptors) on a real sample library: how fast they are made and searched, how fast the index analyses a library, and how often a sound's nearest sounds are of its kind; `--split` holds half the queries out, `--tune` searches the aspects' weights. The intelligence module alone, no Qt. |
| [LibraryGen.h](LibraryGen.h) | Makes the synthetic sample libraries. |
| [PyRandom.h](PyRandom.h), [Json.h](Json.h) | Python's random numbers (so a library is the one the Python generator made), and the reports as JSON. |

Two more benchmarks measured the Python UI (`browser_ui_bench.py`: the
`BrowserPanel`; `display_fps_bench.py`: the device editors' displays). They went
with that UI; their recorded results stay below and in `results/`, and the
scripts are in the history (before the Qt Quick UI: `git show
bd845a9:benchmarks/browser_ui_bench.py`). The Qt Quick UI's own drawing is
timed by its tests (`test_ui_sg`: the scene-graph painter with tens of
thousands of shapes).

## Building and running

The benchmarks are built with `-DSUBSTATION_BUILD_BENCHMARKS=ON` (off by
default), into the build's `bin` folder with everything else. Always in Release.
`browser_backend_bench` needs the application layer (Qt Core, as the tests do);
`parallel_render_bench`, `builtin_devices_bench` and `warp_bench` need only the engine, and `sound_similarity_bench` only the
intelligence module (no Qt either; it wants a sample library of your own).

Linux:

```sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DSUBSTATION_BUILD_BENCHMARKS=ON
ninja -C build parallel_render_bench browser_backend_bench builtin_devices_bench warp_bench sound_similarity_bench
build/bin/parallel_render_bench --tracks 32 --threads 1,2,4,8 --json parallel.json
build/bin/builtin_devices_bench --seconds 5 --json devices.json
build/bin/warp_bench --json warp.json
build/bin/browser_backend_bench --size 200000 --json backend.json
build/bin/sound_similarity_bench --folder ~/Samples --cache /tmp/fingerprints.bin --json similarity.json
```

Windows, in a *Developer PowerShell for VS 2022* (MSVC and Ninja on the path), with
Qt's MSVC build and, for `--live`, the ASIO SDK (see [docs/building.md](../docs/building.md)):

```powershell
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DSUBSTATION_BUILD_BENCHMARKS=ON `
      -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64 -DSUBSTATION_ASIO_SDK=C:\src\asiosdk_2.3.3_2019-06-14
ninja -C build parallel_render_bench browser_backend_bench sound_similarity_bench
build\bin\parallel_render_bench.exe --tracks 32 --live 64,256 --json parallel.json
$env:PATH = "C:\Qt\6.8.0\msvc2022_64\bin;$env:PATH"   # browser_backend_bench runs with Qt's DLLs
build\bin\browser_backend_bench.exe --size 200000 --audio --json backend.json
build\bin\sound_similarity_bench.exe --folder D:\Samples --cache $env:TEMP\fingerprints.bin --json similarity.json
```

`--help` lists each one's options. The results below were measured with the
Python-era builds (Python 3.12 driving the engine and the browser backend through
the nanobind bindings, `python -m benchmarks.<name>`), before the benchmarks were
ported to C++; the C++ programs measure the same things the same way, and print
and write reports in the same shape.

# Browser benchmarks

```
browser_backend_bench [--size 200000] [--used 500] [--audio] [--json out.json]
```

It makes a synthetic sample library ([LibraryGen.cpp](LibraryGen.cpp): packs,
categories and sub-folders of 20-150 files, names like real sample names, mostly
WAV with some FLAC/MP3, Ableton `.asd` files and artwork beside them; the files are
empty, the browser only reads names) once per size, in
`%TEMP%\sub-browser-bench\lib-<size>-1` (`$TMPDIR` or `/tmp` elsewhere), and
measures on it. The names come from Python's random numbers, so the library is the
same, file for file, as the Python generator made it, and one made by either is
reused by the other. `--used` items (picked as the Python benchmark picked them)
get a use count, so ranked searches have something to rank.

It compares the backend alone with the reference: the Python index and search from
before (`tests/browser_reference.py`), ported to C++ for the tests
([tests/app/support/BrowserReference.h](../tests/app/support/BrowserReference.h)).
Every query's results must be the same items in the same order; it reports which
differ and fails (exit code 1) if any does.

- Index: building it (the reference's walk; the native scan with a new index
  file, and when its first files showed), and starting again with the saved one.
- Query: the reference's search on the calling thread; the native search on its
  own thread, and from asking until the results can be taken on the caller's
  thread (polled every 0.2 ms); and making the first page (256 rows) of them. For
  the whole library and for a pack's folder (a place), ranked and by name, for
  `""`, `kick`, `e`, `808 bass`, `kick deep` and `zzqx`.
- Indexing: native searches while a full rescan runs.
- `--audio` plays an arrangement (8 tracks, half of them time-stretched) through the
  default output the whole time, silently (master gain 0). It reports the engine's
  CPU load, and how far its playhead fell behind the wall clock at worst: a late
  audio callback is a dropout and leaves the playhead behind for good.

The reference's times are those of the C++ port now, not of the Python code the
results below compare with, so its column (`reference_ms`; the Python version's
reports had `python_ms` and `python_first_ms`) is no longer the "before". The
native columns and the index's are the same as before.

`browser_ui_bench.py` (removed with the Python UI) measured the real (Python)
`BrowserPanel`: time until results were laid out and painted, and the longest time
the UI thread couldn't run (a 1 ms timer's gaps).

## Results

Measured with the Python-era builds: the Python benchmarks, `browser_ui_bench.py`
and the Python `browser_backend_bench.py`.

200 000 audio files (plus ~60 000 other files) in 2 364 folders; Windows 11, Python
3.12, Focusrite USB (WASAPI, 10 ms buffer). Warm file-system cache. Before is commit
63776d3; after is this branch. Raw reports are in `results/`.

**What the user waits for** (`browser_ui_bench --audio`; the runs without audio are
the same within noise):

| | before | after |
|---|---|---|
| First results with a new index | 1678 ms (after the whole walk) | 50 ms (while scanning; done at 668 ms) |
| First results on the next start | 1781 ms (walks again) | 135 ms (the saved index; checked at 262 ms) |
| Search results, median / slowest of 30 | 286 / 749 ms | 11.6 / 23.7 ms |
| After the last key when typing | 249 ms | 9.9 ms |
| Search results during a full rescan | up to 1245 ms | up to 23 ms |
| Longest UI stall: searching | 602 ms | 10 ms |
| Longest UI stall: typing | 94 ms | 4.4 ms |
| Longest UI stall: during a rescan | 1245 ms | 3.2 ms |
| Longest UI stall: start / next start | 407 / 330 ms | 19 / 4.5 ms |
| Engine CPU load, max | 5.7 % | 5.9 % |
| Playhead's worst lag (10 ms buffer) | 10.9 ms | 10.2 ms |

Times include the 150 ms the old search waited for typing to pause; the new one
doesn't wait. Neither version made the audio drop out: the playhead never fell more
than a buffer behind.

**The backend alone** (`browser_backend_bench`; Python's time is time the UI thread
was blocked, the native search runs on its own thread):

| Samples, 200 000 files | results | Python | native search | native, asked to taken |
|---|---|---|---|---|
| (everything), Rank | 200 000 | 22 ms (240 ms the first time) | 2.5 ms | 2.7 ms |
| "kick", Rank | 13 268 | 97 ms | 7.5 ms | 8.0 ms |
| "e", Rank | 132 674 | 298 ms (440 ms the first time) | 17.7 ms | 18.2 ms |
| "808 bass", Rank | 425 | 72 ms | 7.0 ms | 7.4 ms |
| (everything), Name | 200 000 | 18 ms | 1.2 ms | 1.6 ms |
| "kick", Name | 13 268 | 69 ms | 6.8 ms | 6.9 ms |
| a place, any query | ≤ 447 | 23-27 ms | 0.3-0.4 ms | 0.5-1.0 ms |

"The first time" is the first Rank search after indexing, which made every item's
key. Searches during a full rescan: 1.6-20 ms. Building the index: Python 877 ms,
native 548 ms (first files after 34 ms); starting with the saved index: 238 ms until
checked, first files after 114 ms (reading it 68 ms, the first snapshot 50 ms). The
"asked to taken" column polls every 0.2 ms, so it includes up to that much waiting.

**Variance.** Reading a saved index that was written a moment before sometimes
took 0.5-1 s instead of ~70 ms, once per newly written file, in both benchmarks
(`after-ui-200k.json`: 852 ms to first results on the restart). Reading the same
file again, sooner or later, is fast; it looks like the file being scanned by
antivirus after it was written. It only delays the first results; the UI stays
responsive (4 ms longest stall in that run). A real restart reads a file written
long before.

## How the time was spent before

`results/before-profile-200k.txt` is the profile of the Python browser that led to
the design (same library, warm cache):

- Walking: 810 ms, of which the file system calls were ~200 ms; the rest was Python
  per file (working out the folder's name again for every file: 460 ms). It ran on a
  thread, but held the GIL, so the UI stalled for up to 100 ms meanwhile.
- Every search ran on the UI thread: ~73 ms to filter 200 000 names, up to 230 ms to
  rank the matches, and 480 ms more for the first ranked search after indexing (the
  item keys).
- Showing the results: Qt's list view lays out every row it has, about 1 µs a row,
  so 200 ms for 200 000 rows on each change, whatever language made the list.
- Nothing was kept: every start and every change of places walked everything again.

So the native backend keeps a saved index and updates it by folder, searches on its
own thread with no Python in between, and the list is filled a page (256 rows) at a
time as it scrolls.

# Parallel track processing

```
parallel_render_bench [--tracks 32] [--device synth|ott|plugin] [--plugin PATH --name NAME]
    [--heavy N [--heavy-otts 8]] [--compare-ordering] [--seconds 20] [--threads 1,2,4,N]
    [--live 64,256] [--live-seconds 5] [--json out.json]
```

Every track plays eight-voice chords on the built-in Synth (`--device ott`: through
an OTT; `--device plugin --plugin PATH --name NAME`: a noise clip and the notes
through a VST3 plug-in, for example
`--plugin build/testplugins/SUBTestPlugins.vst3 --name "SUB Test Effect"`). It
renders offline (1024-frame chunks, best of three) on 1, 2, 4 and the default number
of threads (or `--threads`), and fails if any render differs from the one on one
thread. `--live` plays the same through the fake ASIO driver (tests/asio_driver) in
manual mode, buffers back to back on one thread: the time per buffer against its
length is the audio thread's load. The driver is built with the tests where the
engine has ASIO (Windows, with the ASIO SDK, and `SUBSTATION_BUILD_TESTS` and
`SUBSTATION_TEST_PLUGINS` on, as by default); elsewhere `--live` says it was
skipped. It is Qt-free, and uses the engine tests' helpers (`tests/engine/harness`:
WAV files, seeded random numbers, the fake driver), so its chords' roots and its
noise are not numpy's: the same amount of work, not the same notes.

On 4 cores, Linux (8 tracks, 5 s, `--threads 1,2,4`):

```
8 tracks (synth), 4 logical cores; offline, 5 s:
 threads    order      time  x realtime  speed-up
       1     cost    0.174s       28.7x     1.00x
       2     cost    0.096s       52.0x     1.81x
       4     cost    0.056s       89.4x     3.12x

Live: skipped (the engine was built without the ASIO SDK)

(tracks rendered on workers: 4209)
```

## Results

Measured with the Python-era build (`python -m benchmarks.parallel_render_bench`).
Intel Core Ultra 7 270K Plus (24 logical cores, so 23 threads by default), Windows 11,
Python 3.12. Raw reports are in `results/parallel-*.json`.

32 Synth tracks, 10 s:

| threads | offline | speed-up | live, 64 frames (mean / p99) | live, 256 frames (mean / p99) |
|---|---|---|---|---|
| 1 | 0.657 s (15× realtime) | 1.00× | 94 / 192 µs | 359 / 613 µs |
| 2 | 0.335 s | 1.96× | 55 / 99 µs | 195 / 332 µs |
| 4 | 0.176 s | 3.74× | 33 / 64 µs | 108 / 229 µs |
| 23 | 0.072 s (139× realtime) | 9.11× | 22 / 101 µs | 55 / 158 µs |

8 Synth → OTT tracks, 10 s: 1.96× on 2 threads, 3.69× on 4, 5.19× on 23 (there are
only 8 tracks to share). Live at 64 frames: 46 µs per buffer on one thread, 15 µs on 4.

A project with two tracks renders at 32 frames as it did on one thread (below the
threshold, chunks render serially) and a little faster at 128 (13 → 9 µs).

### Linux, real-time workers

The C++ build on Linux (platform support, stage 1: the workers at real-time
priority, `platform::ScopedRealtimePriority`). A 4-core cloud VM (Intel Xeon,
2.1 GHz), Ubuntu 24.04, GCC, Release; 32 Synth tracks, 10 s, offline. "Real-time":
the workers at `SCHED_FIFO` 70 (the process allowed to set it); "rtkit": a user
without that, given `SCHED_RR` 20 by rtkit (and `RLIMIT_RTTIME` 200 ms); "refused":
a user with neither, the workers at normal priority. Raw reports:
`results/parallel-32-synth-linux-*.json`.

| threads | real-time | speed-up | rtkit | speed-up | refused | speed-up |
|---|---|---|---|---|---|---|
| 1 | 1.953 s (5.1× realtime) | 1.00× | 1.871 s (5.3× realtime) | 1.00× | 1.858 s (5.4× realtime) | 1.00× |
| 2 | 1.006 s | 1.94× | 0.973 s | 1.92× | 1.015 s | 1.83× |
| 4 | 0.543 s (18.4× realtime) | 3.59× | 0.543 s (18.4× realtime) | 3.45× | 0.565 s (17.7× realtime) | 3.29× |

The speed-ups are those of Windows above (1.96× on 2 threads, 3.74× on 4), on
slower cores. Live (`--live`) needs the fake ASIO driver, so it waits for the
PipeWire backend (stage 2) on Linux; and the comparison on the same hardware, and
on macOS, are still to do.

### Starting the heaviest tracks first

`--heavy N` puts a chain of OTTs on the last N tracks (the last in routing order,
so without cost ordering they start last); `--compare-ordering` renders each
thread count both ways. 16 Synth tracks, 2 of them with 8 OTTs, 10 s
(`results/parallel-16-heavy-ordering.json`):

| threads | heaviest first | routing order | live, 128 frames (mean): heaviest first / routing order |
|---|---|---|---|
| 1 | 0.665 s | (same) | 182 µs |
| 2 | 0.334 s | 0.336 s | 99 / 98 µs |
| 4 | 0.200 s | 0.267 s | 59 / 79 µs |
| 8 | 0.193 s | 0.244 s | 66 / 78 µs |

With 4 threads or more the render is down to about as long as the heaviest track
takes alone. With 2 threads the order hardly matters: every track starts within
the first few slots anyway. With 32 equal tracks both orders are the same within
noise, and timing every track costs nothing measurable on one thread.

# Sound similarity

```
sound_similarity_bench --folder <sample library> [--threads N] [--limit N] [--cache file]
                       [--weights t,m,s,e,p,r,ts,c,ss,to,x] [--tune] [--split] [--show N]
                       [--index] [--json out.json]
```

Fingerprints every audio file under the folder with the index's extractor
(`EssentiaExtractor`; on `--threads` threads, normal priority; `--cache` keeps
them between runs, made again when the extractor's schema changes), then measures
one search over all of them, measuring the library's statistics, and how well the
nearest sounds match what a sound is. A file's kind comes from its name and its
folder's: kick, snare, clap, closed and open hi-hat, tom, cymbal, rim, shaker,
snap, 808, and drum machines' abbreviations (BD, SD, CP, CH, OH...); TidalCycles'
Dirt-Samples folders (bd, sn, cp, hh, oh, lt/mt/ht, cr, rm, stab, pluck, bass,
jungbass, arpy...) as kick, snare... synth stab, pluck, synth bass, sub bass, arp;
a name with "loop", "fill", "bpm", "breaks"... is a loop. Each labelled one-shot
is a query over the whole library, and precision@k is the share of its k nearest
sounds (itself left out) that are one-shots of its kind; each loop is a query too,
its hits other loops. It prints that per kind and each aspect alone. `--weights`
gives the aspects' weights in `Aspect`'s order (Timbre, TimbreMotion, Spectrum,
Envelope, Pitch, Rhythm, TimbreSpread, Contrast, SpectralShape, Tonality,
Embedding); those left off the end keep the extractor's. `--split` halves the queries by their folder and reports each half;
`--tune` searches the weights for the best mean precision@10 over the kinds of
the first half (so the second shows what the tuning is worth on sounds it didn't
see). `--index` also runs the `SoundIndex` itself over the library at background
priority: from nothing, then from its saved fingerprints. `--show N` prints the N
nearest of one query per kind. How the fingerprint works is in
[docs/intelligence.md](../docs/intelligence.md).

## Results

[Dirt-Samples](https://github.com/tidalcycles/Dirt-Samples) (not in the
repository: `git clone --depth 1 https://github.com/tidalcycles/Dirt-Samples`):
2 068 WAV files in 221 folders, at 44.1, 22.05, 48 and 16 kHz and a few odd rates,
plus 30 drum loops made from its kicks, snares and hats (90–174 BPM, 4 s). A
release build on a 4-core cloud machine (Linux, GCC 13):

| | |
|---|---|
| analysing 2 068 files, 4 threads | 8.6 s (240 files/s); a file: median 10 ms, 95% 58 ms (a thread); one thread 59 files/s |
| the index, 4 analysers at background priority | 8.2 s; the next start reads the 0.8 MB store in 3 ms and has checked every file's stamp 8 ms later; a search 0.7 ms |
| one search over 2 068 fingerprints | 0.2 ms (the comparison only); measuring the library's statistics 2 ms |
| queries | 583 labelled one-shots, 40 loops |

Precision@10, Essentia's descriptors against the module's own (the extractor it
replaced, built from the commit before; the same files, labels and queries):

| | one-shots, mean over kinds | one-shots, all | P@1, mean | loops | kick | snare | synth stab | synth bass |
|---|---|---|---|---|---|---|---|---|
| the module's own descriptors | 0.467 | 0.580 | 0.671 | 0.462 | 0.455 | 0.692 | 0.804 | 0.414 |
| Essentia's (default weights) | **0.492** | **0.602** | **0.683** | **0.528** | **0.504** | **0.692** | **0.870** | **0.504** |

Each aspect alone (mean precision@10 over kinds): timbre 0.34, timbre motion 0.37,
spectrum 0.34, envelope 0.18, pitch 0.13, rhythm 0.11, timbre spread 0.22,
contrast 0.39, spectral shape 0.32, tonality 0.22. With `--split`, the six aspects
the module had: 0.389 / 0.469 (the two halves); with the four added: 0.407 /
0.514. Weights `--tune`d on the first half did worse on the second (0.498), so the
defaults stay round. The full table per kind is in
[docs/intelligence.md](../docs/intelligence.md#how-well-it-works).

The module's own descriptors, on a Splice library of 5 091 files (1 403 packs) on
an Intel Core Ultra 7 270K Plus (24 threads), Windows 11, MinGW GCC 13: precision@10
0.694 over all queries (kick 0.84, snare 0.79, 808 0.73, closed hat 0.53, open hat
0.59, clap 0.56); 1 250 files/s on 12 threads, 1.4–9.5 ms a file. That library's
labels are cleaner than Dirt-Samples' file names: compare extractors on one set,
not the sets with each other.

# Built-in devices

```
builtin_devices_bench [--device ID[,ID...]] [--seconds 10] [--block 256] [--rate 48000]
    [--set PARAM=VALUE ...] [--input sound|silence|burst] [--json out.json]
```

Each built-in audio effect (or those named), made from the registry and prepared
at the rate, plays stereo noise and two tones at about -12 dBFS in blocks of
`--block` frames, as the audio thread gives them; the time its `process()` takes,
against how long the audio plays, is its load on one core (best of three).
`--set` changes a parameter, by id and in its own units, on every device that has
one: the heavy settings below. `--input silence` plays zeros throughout (a device on
an idle track: what it costs doing nothing; a reverb that sleeps in silence costs
next to nothing), `burst` half a second of the sound then silence (a tail dying
away, then what it costs after). No engine, no graph: the device alone.

## Results

Linux, Intel Xeon at 2.1 GHz (a cloud VM's 4 cores, other work on it), GCC 13,
Release, 48 kHz, blocks of 256, 5 s. The raw report is in
`results/builtin-devices.json`.

| device | defaults | heavy settings |
|---|---|---|
| Amp | 0.63 % | 1.37 % (Dual Mono, Lead, Gain 10) |
| Chorus-Ensemble | 0.31 % | 0.67 % (Ensemble, Warmth 100, High-pass, Feedback 80) |
| Compressor | 0.12 % | |
| Delay | 0.14 % | |
| Disperser | 0.23 % | |
| EQ | 0.02 % | |
| Erosion | 0.22 % | |
| Gate | 0.14 % | |
| Limiter | 0.35 % | 0.68 % (True Peak, Gain +12 dB) |
| Multiband Dynamics | 0.66 % | |
| Over The Top | 0.40 % | |
| Phaser-Flanger | 0.23 % | 0.75 % (42 notches, Feedback 95, Amount 100) |
| Reverb | 1.43 % | 1.39 % (Size 500, Predelay 250 ms, Decay 60 s, Spin and Chorus 100, Diffusion, Scale and Shape 100) |
| Saturator | 0.10 % | 0.65 % (Hi-Quality, Waveshaper, Color, DC, Drive +12 dB) |
| Sidechain | 0.09 % | |
| Spectral Compressor | 0.61 % | 0.73 % (Stereo Link 0, Upward 10, Smoothing 100, Threshold -60, Ratio 20) |
| Utility | 0.02 % | |

Oversampling (the Saturator's Hi-Quality, the Amp) runs the shared half-band
stages a tap at a time over the block, which the compiler vectorises. Before it
did, the same machine measured about 1.4 % for the Saturator's heaviest
Hi-Quality settings and 2.1-2.2 % for the Amp in Dual Mono.

## Windows: the math library (MinGW against MSVC)

Windows 11, Intel Core Ultra 7 270K Plus, 48 kHz, blocks of 256, 5 s, best of
three (after: of two runs of that); MinGW GCC 13 (Qt's, msvcrt-based) and MSVC
14.51 (Visual Studio 18), both Release with AVX2. Before: the C library's exp,
log2, log10, exp2 and sin; after: `engine/src/rt/FastMath.h`'s, where devices
call them per sample or per bin (and RtUtils.h's dB helpers). The raw numbers
are in `results/builtin-devices-mingw-msvc.json`.

| device | MinGW before | MinGW after | MSVC before | MSVC after |
|---|---|---|---|---|
| Amp | 0.310 % | **0.312 %** | 0.397 % | 0.397 % |
| Chorus-Ensemble | 0.194 % | **0.129 %** | 0.179 % | 0.178 % |
| Compressor | 0.347 % | **0.066 %** | 0.050 % | 0.052 % |
| Delay | 0.062 % | **0.062 %** | 0.088 % | 0.082 % |
| Disperser | 0.124 % | **0.123 %** | 0.245 % | 0.246 % |
| EQ | 0.008 % | **0.008 %** | 0.013 % | 0.014 % |
| Erosion | 0.085 % | **0.084 %** | 0.097 % | 0.096 % |
| Gate | 0.072 % | **0.071 %** | 0.077 % | 0.077 % |
| Limiter | 0.143 % | **0.138 %** | 0.161 % | 0.164 % |
| Multiband Dynamics | 0.547 % | **0.360 %** | 0.388 % | 0.396 % |
| Over The Top | 0.697 % | **0.223 %** | 0.224 % | 0.241 % |
| Phaser-Flanger | 0.274 % | **0.150 %** | 0.208 % | 0.215 % |
| Reverb | 0.751 % | **0.630 %** | 0.831 % | 0.848 % |
| Saturator | 0.041 % | **0.041 %** | 0.071 % | 0.070 % |
| Sidechain | 0.053 % | **0.051 %** | 0.045 % | 0.045 % |
| Spectral Compressor | 0.626 % | **0.292 %** | 0.346 % | 0.352 % |
| Transient | 0.177 % | **0.184 %** | 0.162 % | 0.169 % |
| Utility | 0.004 % | **0.003 %** | 0.006 % | 0.006 % |
| **all 18** | 4.51 % | **2.93 %** | 3.59 % | 3.65 % |

Profiled (the sampling `--profile` of vital_bench, on a build of the devices),
MinGW's devices spent up to 85 % of their time in its C library: its `expf`
converts to double and calls an x87 `exp` (29 ns a value; MSVC's library 1.3 ns,
FastMath.h's 1.5), and its `exp2`, `log2` and `sin` are as slow (25, 15 and 29
ns; FastMath.h's 1.3 to 2.5). FastMath.h's functions are branch-free, so GCC
vectorises the loops calling them, and within 2 ulp (float) and 4 ulp (double)
of the exact result (`test_fast_math.cpp`). With MSVC they are its library's
own, clamped alike: its library is as fast, and MSVC vectorises loops calling
it (it doesn't those calling FastMath.h's: 2 times as slow there), so MSVC is
unchanged. Elsewhere GCC's code is the faster: the built-in Vital, 8 notes, 1.27 %
(MinGW) against 1.56 % (MSVC); Disperser 0.12 % against 0.25 %.

# Warping

```
warp_bench [--case NAME[,NAME...]] [--seconds 30] [--block 256] [--rate 48000]
    [--repeat 3] [--input sound|silence] [--json out.json]
```

An audio clip's own rendering, as `Renderer::renderClips()` asks for it
(`renderResampled()`, `WarpVoice::render()`), in blocks of `--block` frames: no
engine, no graph, no gain or pan. The clip is `--seconds` of stereo noise and
tones at about -12 dBFS (`--input silence`: zeros), written to a float WAV and
decoded as the engine does. Each case plays it from the start, at most
`--seconds` of output; the cost is in nanoseconds per output frame and as the
load on one core (best of `--repeat`). The cases: `direct` (the source read in
place), `repitch-R` (Re-Pitch at rate R), `transients-`, `standard-` and
`smooth-R` (each stretch configuration at rate R), `transpose+7` and `formants+7`
(seven semitones up at the clip's own tempo, Standard, without and with
Formants).

## Results

Intel Xeon at 2.1 GHz (a cloud VM, AVX2), 30 s, 48 kHz, blocks of 256, best of
three (and of two such runs). Linux: GCC 13, Release. MinGW: MinGW-w64 GCC 13
(msvcrt) with the engine's flags, run under Wine. Before and after Re-Pitch's
kernel was vectorised (see [docs/engine/warp.md](../docs/engine/warp.md#re-pitch-resampling));
the stretcher is unchanged, and differs only by noise. The raw numbers are in
`results/warp.json`.

| case | Linux before (ns/frame) | Linux after | MinGW before | MinGW after | MinGW after, load |
|---|---|---|---|---|---|
| direct | 0.4 | **0.4** | 0.8 | **0.7** | 0.003 % |
| repitch-0.5 | 51.9 | **8.4** | 52.8 | **8.7** | 0.042 % |
| repitch-0.9 | 53.7 | **8.5** | 54.1 | **8.8** | 0.042 % |
| repitch-1.05 | 54.8 | **35.5** | 55.8 | **36.0** | 0.173 % |
| repitch-1.5 | 74.7 | **37.3** | 77.4 | **39.3** | 0.189 % |
| repitch-2 | 95.3 | **42.3** | 99.9 | **43.3** | 0.208 % |
| transients-0.9 | 257.2 | **263.8** | 258.9 | **258.2** | 1.239 % |
| standard-0.9 | 261.1 | **263.9** | 262.9 | **268.0** | 1.286 % |
| smooth-0.9 | 298.9 | **304.7** | 272.4 | **276.6** | 1.327 % |
| standard-1.25 | 265.7 | **264.1** | 270.6 | **266.8** | 1.281 % |
| transpose+7 | 265.0 | **263.4** | 258.6 | **265.6** | 1.275 % |
| formants+7 | 339.5 | **346.5** | 337.8 | **349.6** | 1.678 % |

With silent input (`--input silence`, Linux), a stretched clip costs about 8 ns a
frame (`standard-0.9` 8.2, `formants+7` 8.8): the stretcher passes silence through
once it has lasted two analysis blocks. Re-Pitch costs the same either way.

## Where the time goes

Profiled with `perf` (Linux; the MinGW build under Wine too, its samples matched
to the executable's symbols with `nm`, not `addr2line`):

- **Stretching**: Signalsmith Stretch's `processSpectrum` about 48 % (the
  phase-vocoder prediction per bin: complex products, a division and a square
  root per bin, bounds-checked band lookups; with Formants, `updateFormants`
  another 20 %), its STFT and FFTs about 35 %, feeding it and reading its output
  sample by sample about 8 %. MinGW's x87 maths isn't in it: the only `sin` in
  the MinGW profile was the benchmark's own test signal, and `exp` 0.2 %
  (Signalsmith calls them when configuring and twice per channel per block). MinGW
  and Linux cost the same, and compiler flags on the stretcher alone
  (`-fno-trapping-math`, `-freciprocal-math`, `-O2`) changed nothing beyond the
  noise. So the stretcher is unchanged: making it cheaper would mean changing
  Signalsmith's algorithm or its FFT.
- **Re-Pitch**: its own loop, three quarters of the time (the rest was the
  benchmark's setup): per tap a double-precision table lookup, then one serial sum
  per channel. GCC doesn't vectorise it (the tap's table position differs per tap,
  and GCC 13 no longer emits AVX2 gathers by default). Now: up to rate 1 a phase
  table (a frame's taps share one fraction, so its weights are two table rows
  mixed), and above it eight taps at a time with each tap's two table values
  loaded as one pair (6.2 times and 1.5 to 2.3 times less). Hardware gathers
  measured about 1.5 times faster again on this CPU, but CPUs running Intel's
  Downfall (GDS) mitigation run them very slowly, so they aren't used.

# Vital: built-in against the plug-in

```
vital_bench [--vst3 PATH] [--seconds 10] [--block 256] [--rate 48000]
    [--notes N] [--set PARAM=VALUE ...] [--json out.json]
```

The built-in Vital and Vital's VST3 plug-in (by default the installed
`C:\Program Files\Common Files\VST3\Vital.vst3`, through the engine's VST3 host),
each a new instance at Vital's init patch, hold the same notes from the first
block; the time `process()` takes against how long the audio plays is its load on
one core (best of three). Without `--notes` or `--set`, the scenarios below.
`--set` takes the built-in's id and plain value; the plug-in gets the same value
through its parameter of the same name (each setting is printed as both show it).
The level column (the left side's RMS) shows the two played alike.

## Results

Windows 11, Intel Core Ultra 7 270K Plus, MinGW GCC 13 (Release, `-O3 -mavx2
-mfma`), against Vital 1.5.5's own build; 48 kHz, blocks of 256, 10 s, best of
three. The built-in is Vital 1.0.6's source (the open-source release) on KFR's
FFT; "before" is the same source on the KISS FFT it falls back on. The raw report
(after) is in `results/vital-vs-vst3.json`.

| scenario | built-in, KISS FFT (before) | built-in, KFR | VST3 |
|---|---|---|---|
| silence, no notes | 0.04 % | 0.03 % | 0.23 % |
| 1 note | 0.72 % | 0.49 % | 0.53 % |
| 4-note chord | 1.20 % | 0.73 % | 0.84 % |
| 8-note chord | 2.19 % | 1.27 % | 1.44 % |
| 8 notes, osc 1 unison 8 | 2.80 % | 1.91 % | 2.15 % |
| 8 notes, osc 1 unison 16 | 3.60 % | 2.68 % | 3.12 % |
| 8 notes, 3 oscs unison 8 | 7.06 % | 4.30 % | 4.94 % |
| 8 notes, oversampling 4x | 3.10 % | 2.13 % | 2.56 % |
| 16 notes, polyphony 16 | 4.19 % | 2.25 % | 2.68 % |

Playing, the built-in now costs 10-20 % less than the plug-in; silent, almost
nothing (asleep after a second without voices), where the plug-in keeps running.

Where the time went: `--profile samples.txt` (Windows) samples the thread playing
each, and `vital_profile_report.py` resolves the built-in's samples with addr2line
(build with `-DCMAKE_CXX_FLAGS=-g`, which doesn't change the code). Holding 8
notes on KISS FFT, about 42 % of the built-in's time was the FFT (`kf_bfly4`,
`transform`): the oscillators' inverse transforms, one a block per voice. Vital's
own builds use a platform FFT (Intel IPP, JUCE's, Apple's vDSP:
`fourier_transform.h`), which the vendored code doesn't have.

The FFTs measured for it, each wrapped in Vital's layout as `fourier_transform.h`
uses it (on Vital's 16-byte-aligned buffers, the spectrum's copy included), N =
2048, best of seven rounds of 20 000 (a program outside the tree, with each
library downloaded and built for it; every result within 3e-7 of a
double-precision DFT):

| FFT | inverse | forward |
|---|---|---|
| KISS FFT (Vital's fallback) | 10 195 ns | 10 473 ns |
| Signalsmith Linear 0.6.4 | 3 072 ns | 3 201 ns |
| PocketFFT (C++) | 2 300 ns | 2 446 ns |
| pffft (marton78's) | 1 210 ns | 1 062 ns |
| FFTW 3.3.10, AVX2, FFTW_MEASURE, in place unaligned | 1 355 ns | 1 059 ns |
| FFTW 3.3.10, AVX2, FFTW_MEASURE, copied to aligned buffers | 923 ns | 857 ns |
| KFR 7.2.0, AVX2, out of place | **784 ns** | 880 ns |
| KFR 7.2.0, AVX2, in place | 885 ns | **748 ns** |

(FFTW_PATIENT planned no faster than FFTW_MEASURE.) The built-in uses KFR: out of
place for the inverse, in place for the forward.
