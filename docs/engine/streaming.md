# Disk streaming

Long files can be read from disk as they play instead of being decoded into memory whole: a streamed source keeps
in memory what plays soon, the start of the file and of each clip, and its peaks. This page is how: what streams,
what is read from disk, how the audio thread reads without waiting for it, how the disk threads know what to read,
and each system's file calls. It is goal 22 of the [roadmap](../../ROADMAP.md) ("Big-project scalability, part
two"). Read [warp.md](warp.md), [rendering.md](rendering.md) and the real-time rules in
[architecture.md](../architecture.md) first.

Streaming is a preference (`Engine::setStreaming()`, Preferences › Audio's *Disk Streaming*), **off by default**
until it has had a Canary release ([releasing.md](../releasing.md)); with it off everything is as it was: every
source decoded into memory.

## Why

A source is 32-bit float at the engine's rate: about 2.2× a 16-bit WAV and about 20× an MP3. 32 stereo tracks of
5 minutes at 48 kHz is about 3.7 GB, all of it resident whether it plays or not. With streaming, memory follows
what is *playing soon*, not what is in the project: a few seconds per playing clip (the pool), plus the first
2 seconds of each file and of each clip, plus the peaks (1/16 of the float data at their finest level).

## Overview

```
 edit side (API threads)             disk threads                              audio thread (and its workers)
 ──────────────────────              ────────────                              ──────────────────────────────
 Engine::loadSource()                StreamScheduler                           Renderer
   long enough: StreamCache::open()    planner, every 20 ms (or woken):          SourceCursor::fetch(start, n)
     a WAV/AIFF at the engine's rate     the snapshot it holds, the playhead,      page table lookup (atomic)
     as it is; else its cache file       the loop, the preview                     copy into the worker's scratch
     (made once: decoded, at the         → the pages the clips play from in        a page missing: silence there,
     engine's rate)                        the horizon, each with a deadline        faded at its edges, counted,
   its peak file (made once)           readers (1 to 8): earliest deadline         the page noted for the planner
   its head read, pinned                 first, up to 8 pages a read,
 each snapshot build:                    unbuffered, converted to float          Renderer (offline renders)
   StreamCache::pin(): each clip's       into pool pages, published (release)     SourceCursor::fetchBlocking():
   start, the loop's start             the pool: least recently wanted pages       waits for the disk (leases)
                                         go first, never pinned ones nor those
                                         wanted now; reused once no callback
                                         that may have read them still runs
```

The audio thread never reads a file, never waits for a page, never allocates. The disk threads never take the
engine's mutex (like the warp cache's thread: what they need is in the snapshot they hold).

## What streams

- **A threshold.** While streaming is on, a file at least `StreamingSettings::thresholdSeconds` long (30 by
  default) streams; shorter ones (one-shots, loops, most of a sample library) are decoded into memory, as they
  always were. `Engine::loadSource()` decides: a cached source kept another way than the settings now say (they
  changed) is loaded again, and replaces it (its clips play the old one until then).
- **Always resident**: built-in devices' samples (the Sampler's, Vital's: a device reads its sample anywhere, note by
  note), through `Engine::loadResidentSource()` (cached apart from the clips' sources when the clips' streams);
  `fromSamples()` sources (a frozen track's render, already in memory); the warp cache's renders of resident
  sources.
- **The browser's preview** of a long file streams like a clip: its head is in memory, so it starts at once, and
  the disk threads read ahead of its position (`SharedState::previewPosition`).
- **Recordings** finish as 32-bit float WAV files at the session's rate, so a long take streams straight from its
  own file.

## Storage: what is read from disk

[CacheFile.h](../../engine/src/CacheFile.h):

| The file | What is streamed |
|---|---|
| WAV (RIFF, RF64, WAVE_FORMAT_EXTENSIBLE) or AIFF (AIFF, AIFF-C `NONE`/`twos`/`sowt`/`fl32`) at the engine's rate, one or two channels, 16-bit or 24-bit integers or 32-bit floats, whose length the decoder agrees on | the file itself (`asIsLayout()`): frame by frame, the channels of each together. The disk threads convert to float as miniaudio does (integers scaled by 2⁻¹⁵ or 2⁻²³: exact either way) and lay out the channels |
| Anything else (FLAC, MP3, another rate, more than two channels, 8-bit or 32-bit integers, big-endian WAV) | a **cache file**: decoded once (`makeCacheFile()`, on the thread that loads it: the engine bridge's decoding pool) at the engine's rate, mixed down to stereo, through the very loop `AudioSource::load()` decodes with (`AudioSource::decode()`) |

- A cache file is a 4 KiB header, then pages: page *k* holds frames `[k * P, (k + 1) * P)` of every channel, one
  channel after the other. `P` (`kStreamPageFrames`) is 32768 frames (about 0.7 s at 48 kHz), so every page is a whole
  number of 4 KiB blocks: a page is one aligned read whatever the system. The last page is padded with zeros.
- Its samples are 16-bit or 24-bit integers where the decoder's floats are exactly those integers scaled (the
  decoder's native format is an integer one, and nothing was resampled or mixed down: a 16-bit FLAC at the engine's
  rate), else floats. `CacheFileWriter` checks every sample (and a -0, which no integer gives back) and refuses a page
  that doesn't hold exactly; the file is then written again as floats. So **a streamed source plays the very frames a
  resident one does**: a project sounds the same whatever the threshold, to the bit.
- Cache files live in the cache folder (`StreamingSettings::cacheFolder`; by default `StreamCache::defaultFolder()`:
  the platform layer's `userCacheFolder()`'s `SUBstation/Streaming`). A cache file's name (`cacheFileName()`) comes
  from the source's `fileKey()` (hashed), its `stamp()` (size and last write) and the engine's rate, so a file that
  changed, or another rate, gets a new cache file; its header holds the same and is checked again (with a checksum,
  and the file's size). A cache file is written to a temporary name beside it and moved into place when complete,
  so a crash never leaves half of one under its name; two loads of one file at once make it once (the second waits).
- The folder has a budget (`cacheBudgetBytes`, 20 GB by default). Each cache and peak file is touched (its last
  write set to now) when it is used; past the budget, the least recently used go first (`trimFolder()`, after a new
  one is made and when the budget changes), never one in use.
- **Peaks** are made in the same pass (the decode, or one scan of a file streamed as it is: `scanPeaks()`) by a
  `PeakBuilder`, the same min/max levels `AudioSource::buildPeaks()` makes, and kept in a peak file beside the cache
  file (`.subpeaks`; files streamed as they are have theirs there too). Opening a streamed source then reads no
  audio but its head: its peaks come from their file. All six levels stay in memory (level 0 is 1/16 of the float
  data).
- **A sample rate change** makes the cache files at the old rate useless for playback. `reloadSourcesLocked()`
  opens again, under the engine's lock, only what needs no new file (`StreamCache::open(..., mayMake = false)`: a WAV
  at the new rate with its peak file, or a cache file at that rate made before); the rest are let go, and the engine
  bridge, which fetches every source again after a rate change, has them made in the background meanwhile, their
  clips left out of the snapshot until they are ready (as a clip whose source is cached at another rate always was).

## The head and the clip starts

Playback has to start the moment the user presses Play, wherever the playhead is, before any read could finish.

- **The head**: the first 2 seconds (`StreamCache::kHeadSeconds`) of each streamed file are read before
  `StreamCache::open()` returns, and pinned for as long as the source lives.
- **Clip starts**: each snapshot build (`Engine::pinStreamsLocked()`) pins the frames each clip of a streamed source
  plays from in its first 2 seconds (`clipSpans()`: its source's, for Re-Pitch with the kernel's reach, for a
  stretched clip with the stretcher's look-ahead, or its render's once ready). A clip that starts later in the file
  than its head is then ready too.
- **The loop start**: with the loop on, the same for what each track plays in the 2 seconds after the loop's start,
  so the jump back is never a miss.

These are ordinary pages that the pool won't let go of. Pins not asked for again by the next build are let go
(`beginPins()`, `pin()`, `endPins()`, as the warp cache forgets renders); new ones not in memory are read before
anything else but what is missed or due. Pinned pages come on top of the pool's size.

## The audio thread's side: `SourceCursor`

[SourceCursor.h](../../engine/src/SourceCursor.h) puts one call in front of every read the renderer makes of a
source:

```cpp
// Copies source frames [start, start + n) of both sides into outL/outR. Frames before 0 or past the end
// are silence. Frames whose page isn't loaded are silence too, faded over 64 frames at each edge, and
// counted. Never blocks, never allocates. Returns the frames that were there.
int64_t SourceCursor::fetch(int64_t start, int64_t n, float* outL, float* outR, bool expected = false) const noexcept;
```

- A resident source's cursor copies from `channelData()`; the renderer keeps its reads in place for resident
  sources, so nothing changes for them, and fetches only streamed ones.
- **Direct**: `fetch(clip.sourceOffset + (from - clip.start), to - from)` into the worker's scratch, then the clip's
  gain, pan and fades as for any clip.
- **Resample** (`renderResampled()`, [Warp.cpp](../../engine/src/Warp.cpp)): the kernel reads through a
  `SourceSpan` (pointers and the frame they start at). A resident source's span is the whole file, read in place.
  A streamed source's is fetched a piece of the block at a time into the worker's `span` scratch (`kResampleSpan`,
  8192 frames a channel: the piece is halved until the frames its kernel reaches fit). The cutoff is the block's,
  whatever the pieces, and the kernel code is as it was: the same frames, the same sums.
- **Stretch**: the `WarpVoice` feeds its stretcher through an input that reads the source in place
  (`SourceInput`), from the page table frame by frame, live (`StreamInput`: a page not in memory reads as silence,
  faded at its edges, its frames counted first by `noteMissing()`), or offline from pages leased first
  (`LeasedInput`). The warp cache's renders of streamed sources are written to the cache folder and stream back like
  a cache file (see [The warp cache's renders](#the-warp-caches-renders)).
- **The page table**: each streamed source (`StreamedAudio`, [StreamCache.h](../../engine/src/StreamCache.h)) has an
  array of `std::atomic<StreamPage*>`, one per page of the file, allocated when it is opened. The audio thread loads
  an entry (sequentially consistent: a plain load on x86); null means not in memory. A disk thread publishes a page
  with a store after every frame of it is written.
- **Missing frames** are silence; the frames of a present page within `kMissFade` (64) of a page that isn't there
  are faded, `(d + 1) / 65` at `d` frames from it, so a miss's edges never click. The fade depends only on the
  frame and what is in memory, so a frame reads the same through `fetch()` and through the stretcher's input.
- **Underruns**: each streamed source counts the frames it missed (`missedFrames()`), or, while misses are expected
  (`kJumpGrace`, a second after the playhead jumps while playing: a locate, a drag), apart (`expectedMisses()`), and
  notes the first page it missed in each read (`takeUrgent()`): an atomic of its own rather than a queue, since the
  workers rendering tracks miss at once. The planner reads those first. `Engine::takeStreamMisses()` gives what each
  source missed since it was last asked, and the files that became unreadable.

## The disk threads' side: `StreamScheduler`

[StreamScheduler.h](../../engine/src/StreamScheduler.h). The scheduler doesn't need the audio thread to say what it
will play: the snapshot says it. The engine makes it once the first source streams, hands it each snapshot it
publishes, and wakes it on a locate (`locate()`), Play, a new preview and new pins.

- **The planner** (one thread): every 20 ms (`kPassInterval`), or when woken, it reads the snapshot it holds, the
  playhead (`SharedState::positionSamples`, or for a second after a locate, where it goes) and the loop. For each
  track it walks the clips in `[playhead, playhead + horizon)` (the horizon is 4 seconds by default) and turns
  their timeline samples into source frames the way the renderer does (`clipSpans()`: linear for Direct,
  `sourceAt()` for Re-Pitch and stretched clips, with the kernel's reach and the stretcher's look-ahead, and a
  ready render's frames). With the loop on, the window wraps to the loop's start at its end (as the renderer does
  from anywhere before it). Every page those frames fall in is wanted, with a **deadline**: the samples until the
  playhead gets there. The preview's next horizon of frames too.
- **Ordering**: pages the audio thread missed first (deadline -1), then by deadline, then pinned pages not in memory
  yet. `StreamCache::plan()` marks the wanted pages (they stay until the next pass) and returns those to read: not in
  memory, not on their way, not failing.
- **Reading**: the readers (1 to 8 threads, 2 by default: `diskThreads`) take the earliest first. A file's
  neighbouring pages are one read of up to 8 pages (`kMaxPagesARead`), one I/O, through a buffer of the reader's own.
  The pages are converted into pool pages and published.
- **The pool**: pages are made as they are wanted, up to `poolBytes` (256 MiB by default: 1024 pages of 256 KiB,
  room for two channels of 32768 floats); pinned pages come on top. When it is full, the page let go is the least
  recently wanted, never a pinned one, and never one wanted in this pass. If what is wanted doesn't fit (hundreds of
  streamed clips at once), the horizon shrinks: the earliest deadlines are kept, the rest not read, before anything
  inside it is let go.
- **Freeing pages**: a page let go of is taken out of its table, then given back only once no audio callback that
  may have read it still runs (the engine's epochs: `StreamCache::setAudioEpochs()`, as retired snapshots), and no
  lease holds it. Until then another page is made, so the pool may be over its size by the pages let go of within
  one callback.
- **Threads**: they run at a raised normal priority (`platform::raiseNormalPriority()`: above normal on Windows; on
  Linux the best-effort I/O class at its highest level, and nice -5 where allowed), never
  `enterBackgroundMode()`, which lowers their reads' priority too. A failed read isn't tried again for a second.
  They read a file at once through one handle (each at an offset of its own); it is closed after a failed read (to
  be opened again: the file may come back) only once none of them is reading it.

## Moving the playhead

- **Playing from a clip start, a loop start or the head** starts at once: those frames are in memory.
- **Playing from anywhere else** (Play after a locate into the middle of a long clip): the renderer's pre-roll
  (`Renderer::waitsForStreams()`) holds the playhead, chunk by chunk, until every page the snapshot plays from over
  the next half second (`kPreRollWindow`) is in memory (`streamsReady()`), for at most 150 ms (`kPreRollSeconds`).
  It is the audio thread that waits, never the UI: Play and Stop return at once. `Engine::setPositionBeats()` and
  `play()` wake the scheduler first, and it reads the playhead's pages while the transport is stopped too, so the
  reads have usually finished by the time Play comes. A read that takes longer starts the transport anyway, and the
  clip comes in with a fade once its pages are there (the miss is counted). A count-in clicks first; the wait comes
  after it.
- **Locating and dragging the playhead while playing** ask for pages at the new position at once (`locate()`), and
  don't wait. While the playhead moves faster than the disk keeps up, misses are expected: counted apart for a
  second after each jump (`kJumpGrace`), not reported as a problem.

## Offline renders and the other readers

- **Offline renders and exports** (and freezing) render on a thread of their own (or under the engine's lock) and
  can wait: their reads (`StreamReads{blocking}`) lease each page (`StreamedAudio::lease()`), reading it first if it
  isn't in memory (and the next 3 with it: they read on). A leased page isn't reused while held, even if the pool
  lets it go meanwhile; it is found and leased under the cache's lock, under which pages are let go of and freed, so
  it can't go between the two. An export of a project is the same whether its files are in memory or on a slow disk.
- **Off the audio thread** (the waveform drawn at sample level, the Sampler's slices and Mark Transients, audio
  analysis, reversing a clip): they read through `AudioSource::read(channel, start, count, out)` (the application's
  `Waveform::read()` and `samples()`), which leases the pages it needs, waiting for the disk. `channelData()` is null
  for a streamed source.
- **The warp cache's thread** feeds its stretcher with leased pages too.

## The warp cache's renders

A stretched clip of a streamed source is rendered whole as any other ([warp.md](warp.md#the-warp-cache)), but into
a file of the cache folder's `renders/` (`StreamCache::temporaryFile()`), as 32-bit floats (bit-exact with a live
render), opened with `openFile()` (its head read and pinned) and streamed back like a cache file:
`WarpRender::audio()` is then a streamed source, and `left()`/`right()` are null. The renderer fetches it through a
cursor as it does a Direct clip; a voice crossfading from it reads it through the page table. The file goes when the
render does; those a crash left behind go once they are a day old.

## The system's file calls

[platform/StreamFile.h](../../platform/src/platform/StreamFile.h) ([platform.md](../platform.md)): open a file by
UTF-8 path for streaming, read aligned spans at offsets into aligned buffers (`AlignedBuffer`), from several threads
at once. Each system's code is in its platform file:

| System | Now | Later |
|---|---|---|
| Windows | `CreateFileW` with `FILE_FLAG_NO_BUFFERING` (sharing read, write and delete), `ReadFile` with an `OVERLAPPED` giving each read's offset (synchronous). Offsets, sizes and buffers are multiples of 4 KiB (`kAlignment`): of every volume's sector size | overlapped reads and an I/O completion port; IoRing (`BuildIoRingReadFile`, Windows 11) for many reads in one call |
| Linux | `pread()` on a file opened `O_DIRECT` where the file system accepts it; elsewhere (tmpfs) buffered, with `POSIX_FADV_SEQUENTIAL` and `POSIX_FADV_WILLNEED` | `io_uring` (liburing: a new dependency) for a deep queue on NVMe |
| macOS | `pread()` with `fcntl(F_NOCACHE)`, and `F_RDADVISE` for read-ahead | `dispatch_io` |

- A file is opened unbuffered first; if its first read is refused (an alignment a volume won't take), it is opened
  again through the system's cache.
- Bypassing the system's file cache matters: the pool is the cache, and the system's copy of the same pages would
  only push other programs (and the plug-ins' samples) out of memory.
- Several reads are in flight as several reader threads, each with synchronous reads: what an asynchronous queue
  would do, simpler, at the cost of a thread each.
- **Memory-mapping** cache files looks simpler but isn't real-time safe: the audio thread touching a page that isn't
  resident takes a page fault, which waits on the disk. A mapping could only be read by the disk threads, and then
  it is just a slower `pread`.
- A file that goes away or is cut short reads as failed: its pages stay out of memory (silence), the source says it
  is `unreadable()`, and the engine bridge says so in the status line; never a crash. (On POSIX systems a file
  deleted while open stays readable: it plays on.)

## Settings and the application

- **The engine**: `Engine::setStreaming(StreamingSettings)` (`enabled`, `thresholdSeconds`, `poolBytes`,
  `cacheFolder`, `cacheBudgetBytes`, `diskThreads`, `horizonSeconds`); sources loaded from then on follow it. The pool,
  the folder and the disk threads take it at once. `Engine::setDefaultStreaming()` sets what new engines start with
  (the tests run every engine streaming with it). `streamingStats()` tells the pool's state, `waitForStreams()`
  waits until what the disk threads want is in memory (tests, benchmarks), and `setStreamReadHook()` runs before each
  read (tests slow the disk down with it).
- **The application** ([app/engine-bridge.md](../app/engine-bridge.md#streaming)): Preferences › Audio's *Disk
  Streaming* (on or off, *Stream Files Longer Than*, *Streaming Memory*, *Disk Reads at Once*, *Cache Folder*,
  *Cache Size Limit*), saved (`StreamingPreferences`) and applied by the engine bridge; when what streams changes,
  the files in use are fetched again. The bridge polls `takeStreamMisses()` with the plug-ins' reports (every 33 ms):
  the transport bar shows *Disk* while files stream, and *Disk late N×* once the disk has come late; the status line
  says how much of which file played as silence (at most every 2 seconds), and which became unreadable.

## Files

| File | What it holds |
|---|---|
| [engine/src/AudioSource.h](../../engine/src/AudioSource.h) / [.cpp](../../engine/src/AudioSource.cpp) | a source's storage: resident (`channelData()`) or streamed (`stream()`); `decode()` (the decoding loop `load()` and cache files share); `read()` for readers off the audio thread; `PeakBuilder` |
| [engine/src/CacheFile.h](../../engine/src/CacheFile.h) / [.cpp](../../engine/src/CacheFile.cpp) | `StreamLayout` (where pages are, converting them), `asIsLayout()` (WAV and AIFF), cache files (`cacheFileName()`, `readCacheFile()`, `CacheFileWriter`, `makeCacheFile()`), peak files, `scanPeaks()` |
| [engine/src/StreamCache.h](../../engine/src/StreamCache.h) / [.cpp](../../engine/src/StreamCache.cpp) | `StreamedAudio` (a streamed file: its page table, the real-time `fetch()`/`sample()`, leases, `read()`), `StreamCache` (opening, the pool, pins, eviction by epochs, loads, the cache folder and its budget, misses) |
| [engine/src/StreamScheduler.h](../../engine/src/StreamScheduler.h) / [.cpp](../../engine/src/StreamScheduler.cpp) | `clipSpans()`, `streamsReady()`, `StreamScheduler` (the planner, the readers, the horizon, deadlines) |
| [engine/src/SourceCursor.h](../../engine/src/SourceCursor.h) | `SourceCursor`: `fetch()` (real-time), `fetchBlocking()` (offline); `StreamReads` |
| [platform/src/platform/StreamFile.h](../../platform/src/platform/StreamFile.h), and its code in `PlatformWin32.cpp` / `PlatformPosix.cpp` | unbuffered, aligned reads by UTF-8 path; `AlignedBuffer`; `userCacheFolder()` and `raiseNormalPriority()` beside them |
| [app/src/audio/BridgeStreaming.cpp](../../app/src/audio/BridgeStreaming.cpp) | the preferences applied, misses reported |

## What it costs

Measured with [warp_bench](../../benchmarks/warp_bench.cpp)'s `-streamed` cases (every page of the clip in memory:
what reading through the pages costs over reading in place; results in
[benchmarks/README.md](../../benchmarks/README.md#results)): a streamed clip costs what a resident one does, within
the runs' noise (Direct about 0.6 ns a frame, Re-Pitch 8 and 34 ns, stretched live about 200 ns). Direct copies the
frames into the worker's scratch instead of reading them in place; a stretched clip's input looks each frame's page
up as the stretcher reads it. A miss costs nothing more: its frames are zeros. The disk threads' cost is the reads
themselves, and converting a page (a few tenths of a millisecond).

## Testing

- [tests/engine/test_streaming.cpp](../../tests/engine/test_streaming.cpp): WAVs (16-bit, 24-bit, float,
  extensible, mono and stereo) and AIFFs stream as they are and play the frames and peaks `load()` has; another rate,
  three channels, an MP3 stream from a cache file made once (and read, not decoded, the next time), made again when
  the file or the rate changes; integers kept where they hold every sample exactly; the folder's budget; a missing
  page is silence, faded at its edges, counted (expected misses apart); the pool lets the least recently wanted pages
  go, never pinned ones nor those wanted now, and reuses one only once the audio thread is done with it and no lease
  holds it; offline renders (and freezing a track's signal) are the same streamed as from memory, to the bit, with a
  slow disk too; live, streamed clips (Direct, Re-Pitch, stretched) play what they play from memory once their pages
  are in; a disk slower than playback: what comes late is silence, faded, and counted, the rest as from memory; the
  audio thread never waits for the disk (the longest block with a 300 ms read stays far under a read); playback
  starting where pages aren't in memory waits for them, up to 150 ms; a file that becomes unreadable is silence and
  reported; the warp cache's render of a streamed clip is the in-memory render's frames; an engine streams what its
  settings say, keeps heads and clip starts in memory and lets clips' go.
- **Every engine test, streamed**: CTest's `engine_streamed` runs all of `engine_tests` again with
  `SUBSTATION_TEST_STREAMING` set, so every engine the tests make streams every file it loads (threshold 0, a cache
  folder of the run's own): every test that plays a file plays it streamed, and must come out as it does from memory.
- [tests/app/test_bridge_streaming.cpp](../../tests/app/test_bridge_streaming.cpp): the preferences, saved and
  applied; a long file streams and its waveform reads from disk; switched off, files are fetched again into memory;
  what the disk came late for is counted and said. `test_ui_dialogs` shows the preferences.
- [tests/platform/test_platform.cpp](../../tests/platform/test_platform.cpp): `StreamFile`'s aligned reads, the
  last one short, several threads at once.

## Still open

- **On by default**, once streaming has had a Canary release.
- The threshold's default, the pool's and the horizon: measure on a spinning disk and on a project with 100+ long
  tracks.
- Overlapped reads and an I/O completion port, then IoRing on Windows, `io_uring` on Linux, `dispatch_io` on macOS
  (several reader threads stand in for them).
- Level 0 of a streamed source's peaks could be dropped from memory and read from its peak file when zoomed in.
- Whether the warp cache's renders of streamed clips should be kept as 16-bit (half the reads) rather than as floats
  (now: the output stays bit-exact with a live render).
- Projects on network drives and removable disks: a file that goes away mid-playback reads as silence and is
  reported in the status line; the File Manager lists it as missing when it next looks.
- Anticipative rendering (the other half of goal 22) would write tracks' rendered output ahead of the playhead; it
  can share the pool and the disk threads.
