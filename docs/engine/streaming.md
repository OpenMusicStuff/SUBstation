# Disk streaming (design)

**Status: a design, not implemented.** Today every `AudioSource` is a whole file decoded into memory
([warp.md](warp.md#overview)). This page is the plan for streaming long files from disk instead: what streams,
how the audio thread reads without waiting on the disk, how the disk threads know what to read, and what each
system's file calls should be. It is goal 22 of the [roadmap](../../ROADMAP.md) ("Big-project scalability, part
two"). Read [warp.md](warp.md), [rendering.md](rendering.md) and the real-time rules in
[architecture.md](../architecture.md) first.

## Why

A source is 32-bit float at the engine's rate: about 2.2× a 16-bit WAV and about 20× an MP3. 32 stereo tracks of
5 minutes at 48 kHz is about 3.7 GB, all of it resident whether it plays or not. With streaming, memory follows
what is *playing soon*, not what is in the project: a few seconds per playing clip, plus the start of each file.

## Overview

```
 edit side (API threads)            disk threads (1–4)                       audio thread
 ──────────────────────             ──────────────────                       ────────────
 Engine::loadSource()               StreamScheduler                          Renderer
   probe, choose Resident             reads the published snapshot,            SourceCursor::fetch(start, n)
   or Streamed                        the playhead and the loop                  page table lookup (atomic)
   load the head into memory          → which source frames are needed          copy into the worker's scratch
   (and each clip's start)            in the next few seconds                  a page missing: silence for
   transcode to a cache file          → requests ordered by deadline            those frames (faded), and
   if the file can't be streamed      reads pages (aligned, unbuffered)         count an underrun
   as it is                           decodes/converts into pool pages
                                      publishes them (release)
                                      evicts pages no snapshot needs,
                                      reused after an epoch has passed
```

The audio thread never reads a file, never waits for a page, never allocates. The disk threads never take the
engine's mutex (like the warp cache's thread: what they need is in the snapshot they hold).

## What streams

- **A size threshold.** A source streams if its decoded size at the engine rate is over a threshold (a preference,
  default about 30 seconds of audio). Short files (one-shots, loops, most of a sample library) stay resident, as
  today. That keeps the common case as fast and as simple as it is now.
- **Always resident**: the Sampler's and Vital's samples (a device reads its sample anywhere, note by note), the
  browser preview of short files, and `fromSamples()` sources (a frozen track's render, which is already in memory).
  A preview of a long file streams like a clip.
- **Recordings** finish as WAV files at the session's rate, so a take streams straight from its own file.

## Storage: what is read from disk

| The file | What is streamed |
|---|---|
| PCM WAV or AIFF at the engine's rate | the file itself. The disk thread converts 16-bit, 24-bit and float samples to float and lays out the channels |
| Anything else (FLAC, MP3, another rate, more than two channels) | a **cache file**: decoded once in the background (the engine bridge's decoding pool, as today) at the engine's rate, mixed down to stereo as `load()` does, kept at the file's bit depth (16 bits or 24 bits as integers, float as float) |

- A cache file is made of pages: page *k* holds frames `[k * P, (k + 1) * P)` of every channel, one channel after the
  other, with each page starting on a 4 KiB boundary. A page is then one aligned read whatever the system.
  `P` is 32768 frames (about 0.7 s at 48 kHz, 128 KiB of float a channel).
- Cache files live in a cache folder (a preference; the platform layer gives the default under the user's local
  application data). A cache file's name comes from the source's `fileKey()`, its `stamp()` (size and last write)
  and the engine rate, so a file that changed or a new rate gets a new cache file. The folder has a size budget
  and the least-recently-used files go first.
- **Peaks** are computed in the same pass (the decode, or one scan of a WAV) and stored in a peak file next to the
  cache file. The six levels of `peaks()` stay in memory for drawing (level 0 is 1/16 of the float data; it can be
  dropped for streamed sources and read from the peak file when zoomed in).
- **A sample rate change** makes the cache files at the old rate useless for playback. They are made again in the
  background, and clips whose source isn't ready yet are left out of the snapshot until it is, as a clip whose
  source is cached at another rate is today. A WAV that was streamed as it is now needs a cache file, and the
  other way round.

## The head and the clip starts

Playback has to start the moment the user presses Play, wherever the playhead is, before any read could finish.

- **The head**: the first 2 seconds of each streamed source stay in memory.
- **Clip starts**: for each clip on a streamed source, the 2 seconds from its `sourceOffset` are kept in memory too
  (a clip that starts later in the file than its head). Each snapshot build asks the stream cache to keep them
  (`StreamCache::pin()`), and they go when no clip needs them, as the warp cache forgets renders (`beginBuild()`,
  `endBuild()`).
- **The loop start**: with the loop on, the 2 seconds after the loop's start in each clip there, so the jump back is
  never a miss.

These regions are ordinary pages that the cache won't evict.

## The audio thread's side: `SourceCursor`

Today the renderer takes `stereoChannel()` pointers and indexes them by the timeline sample (`Direct`), or reads
around each position for the sinc kernel (`Resample`, [Warp.cpp](../../engine/src/Warp.cpp)), or hands the voice's
stretcher whatever it asks for. Streaming puts one call in front of all three:

```cpp
// Copies source frames [start, start + n) of both sides into outL/outR (n up to the worker's scratch).
// Frames before 0 or past the end are silence. Frames whose page isn't loaded are silence too, faded
// over 64 frames at each edge, and counted. Never blocks, never allocates. Returns the frames that were there.
int SourceCursor::fetch(int64_t start, int n, float* outL, float* outR) noexcept;
```

- A resident source's cursor is a `memcpy` from `channelData()`. The renderer could keep its pointer reads for those
  and use the cursor only for streamed sources: a streamed clip costs the copy (a fraction of a nanosecond a frame,
  as Direct does).
- **Direct**: `fetch(clip.start - clip.sourceOffset + from, to - from)`, then the clip's gain, pan and fades as today.
- **Resample**: fetch the span the kernel reaches over the block (`sourceAt(from)` to `sourceAt(to)`, widened by
  `kHalfWidth / cutoff` on both sides) into scratch, and run `renderResampled()` on that span instead of on the whole
  file. A `Channel`-like view that offsets the indices keeps the kernel code as it is.
- **Stretch**: the `WarpVoice` feeds its stretcher through a cursor. The warp cache's renders are a clip's whole
  length in memory, so for streamed sources they are written to the cache folder and stream like a cache file
  (the warp cache's thread reads its input with blocking reads: it isn't real-time).
- **The page table**: each streamed source has an array of `std::atomic<const Page*>`, one per page of the file,
  allocated when the source is opened (edit side). The audio thread loads an entry (acquire); null means not
  loaded. The disk thread publishes a page with a release store after every frame of it is written.
- **Freeing pages**: the disk thread clears an entry, then gives the page back to the pool only once the epoch has
  passed (`DeferredReleasePool`, as retired snapshots), so a callback that loaded the pointer just before can
  finish reading it.
- **Underruns**: `SharedState` gets a counter of missed frames by source, read by `idle()` and reported like a
  take's `droppedFrames`. The audio thread also pushes the first missed page of each block into an `SpscQueue` for
  the disk thread: a page asked for that way goes to the front of the queue.

## The disk threads' side: `StreamScheduler`

The scheduler doesn't need the audio thread to say what it will play: the snapshot says it.

- **The horizon**: every 20 ms (or when woken: a locate, a new snapshot, a missed page) the scheduler reads the
  published snapshot (holding it through the epoch, as any reader does), the playhead and the loop. For each track
  it walks the clips in `[playhead, playhead + horizon)` (the horizon defaults to 4 seconds) and turns their
  timeline samples into source frames the same way the renderer does: linear for Direct, `sourceAt()` for
  Resample and live Stretch (the `WarpCurve`), plus the stretcher's look-ahead. With the loop on, the window wraps
  to the loop's start. Every page those frames fall in is wanted, with a **deadline**: the time until the playhead
  reaches it.
- **Ordering**: requests are served earliest deadline first, not first in first out. Missed pages come first, then
  pages due within one horizon, then pages for the next clips.
- **Reading**: a read is one page (the request for the next page of a playing clip can be merged into larger reads:
  2 to 8 pages at once, as one I/O). The disk thread converts the samples into a pool page and publishes it.
- **The pool**: a fixed number of pages allocated when streaming is first used (a preference, default 256 MiB).
  Pages are evicted least recently wanted first, never a pinned head or clip start, and never one inside the current
  horizon. If the horizon doesn't fit in the pool (hundreds of streamed clips at once), the horizon shrinks before
  anything inside it is evicted.
- **Threads**: one disk thread a drive is enough for spinning disks; an NVMe drive wants several reads in flight
  (2–4 threads, or one thread with an asynchronous queue: [the system's calls](#the-systems-file-calls)). The disk
  threads run at a raised normal priority, never `enterBackgroundMode()`, which lowers I/O priority too.

## Moving the playhead

- **Playing from a clip start, a loop start or the head** starts at once: those frames are in memory.
- **Playing from anywhere else** (a locate into the middle of a long clip): the transport waits for the pages
  there, up to 150 ms, before it rolls. `Engine::setPositionBeats()` and `play()` wake the scheduler first, so the
  reads have usually finished by the time the transport asks. A read that takes longer starts the transport anyway, and the clip comes in
  with a fade once its pages are there (the miss is counted).
- **Scrubbing and dragging the playhead while playing** request pages at the new position at once. While the playhead
  is moving faster than the disk keeps up, misses are expected and not reported as a problem.

## Offline renders and the other readers

- **Offline renders and exports** hold the engine lock and render on a thread of their own, so they can wait: their
  cursors use a blocking read for a missing page (`SourceCursor::fetchBlocking()`). An export of a project is then
  the same whether its files are in memory or on a slow disk, as it is today.
- **Off the audio thread** (the waveform drawn at sample level, `SampleSlices`, audio analysis, `ReverseJob`): these
  call `channelData()` today. They move to `AudioSource::read(channel, start, n, out)`, which reads the pages it
  needs through the cache, blocking. `channelData()` stays only on resident sources.
- **Sources at the edge of the threshold**: a source that is resident and one that is streamed play the same frames,
  so a project sounds the same whatever the threshold.

## The system's file calls

All of this goes behind one new header in the platform layer ([platform.md](../platform.md)), `StreamFile.h`: open a
file by UTF-8 path for streaming, read aligned spans at offsets into aligned buffers, and (later) queue several reads
and collect them. Each system has a file of its own behind it, as the rest of the platform layer does.

| System | First version | Later |
|---|---|---|
| Windows | `CreateFileW` with `FILE_FLAG_NO_BUFFERING` and `FILE_FLAG_OVERLAPPED`, `ReadFile` with an `OVERLAPPED` per read and an I/O completion port. Offsets, sizes and buffers are multiples of the volume's sector size (pages are 4 KiB-aligned) | IoRing (`BuildIoRingReadFile`, Windows 11) for many reads in one call |
| Linux | `pread()` with `posix_fadvise(POSIX_FADV_SEQUENTIAL)` and `POSIX_FADV_WILLNEED` on the next pages; `O_DIRECT` where the file system accepts it | `io_uring` (liburing: a new dependency) for a deep queue on NVMe |
| macOS | `pread()` with `fcntl(F_NOCACHE)` and `F_RDADVISE` for read-ahead | `dispatch_io` |

- Bypassing the system's file cache matters: the pool is the cache, and the system's copy of the same pages would
  only push other programs (and the plug-ins' samples) out of memory.
- **Memory-mapping** cache files looks simpler but isn't real-time safe: the audio thread touching a page that isn't
  resident takes a page fault, which waits on the disk. A mapping could only be read by the disk threads, and then
  it is just a slower `pread`.

## Files (planned)

| File | What it would hold |
|---|---|
| `engine/src/StreamCache.h` / `.cpp` | `StreamCache`: the pool, the page tables, pinning (the head, clip starts, the loop start), eviction, the cache folder and its budget |
| `engine/src/StreamScheduler.h` / `.cpp` | `StreamScheduler`: the disk threads, the horizon, requests by deadline, misses from the audio thread |
| `engine/src/SourceCursor.h` | `SourceCursor`: `fetch()` (real-time), `fetchBlocking()` (offline) |
| `engine/src/CacheFile.h` / `.cpp` | writing and reading cache files and peak files |
| `platform/src/platform/StreamFile.h`, and its code in `PlatformWin32.cpp` / `PlatformPosix.cpp` | unbuffered, aligned reads by UTF-8 path |
| `engine/src/AudioSource.h` | a source's storage: `Resident` (as today) or `Streamed` (its page table, its head); `read()` for the readers off the audio thread |

## Steps

Each step can be merged by itself, and nothing a user hears changes until step 3.

1. **The cursor over resident sources.** Add `SourceCursor` with only the resident storage and move the renderer,
   `renderResampled()`, `WarpVoice` and the preview to it. Benchmarks show the cost ([testing.md](../testing.md)).
   This is also when to do the roadmap's "decode in place without the second copy".
2. **Cache files and peak files**, made in the background on loading, while sources are still decoded into memory
   as today. Opening a project gets faster too: its peaks come from their files.
3. **Streaming Direct clips** from WAV files and cache files, one disk thread, synchronous reads, the head and clip
   starts pinned, the underrun counter shown in the UI. Behind a preference, off by default until it has had a
   Canary release ([releasing.md](../releasing.md)).
4. **Resample and Stretch** through cursors; warp cache renders to disk for streamed sources; offline renders with
   `fetchBlocking()`; the readers off the audio thread moved to `read()`.
5. **Locate pre-roll, loop pinning, several reads in flight** (overlapped I/O, then IoRing and io_uring), and the
   preferences: threshold, pool size, cache folder and its budget. On by default.

## Testing

- **Same output**: every renderer test that plays an audio clip runs twice in `engine_tests`, once resident and once
  streamed with a fast disk (threshold 0), and the outputs must be identical, sample for sample.
- **A slow disk**: a test `StreamFile` that delays each read (fixed, or at random with a seed). With a delay inside
  the horizon nothing is missed; with a delay past it, misses are counted and the output is silent exactly where
  they were, with the fades.
- **Never blocking**: under the slow disk, the audio callback's time stays the same as with a fast one (the test
  measures the longest callback).
- **Offline renders** are identical with any disk delay.
- **Cache files**: made again when the file changes (its stamp) or the rate changes; the folder's budget evicts the
  oldest.

## Open questions

- The threshold's default, the pool's default and the horizon: measure on a spinning disk and on a project with 100+
  long tracks.
- Whether the warp cache's renders should be kept as 16-bit in the cache folder (half the reads) or as float (the
  output stays bit-exact with a live render).
- Projects on network drives and removable disks: a file that goes away mid-playback has to read as silence and be
  reported as missing (the File Manager, [guide/file-manager.md](../guide/file-manager.md)), never as a crash.
- Anticipative rendering (the other half of goal 22) would write tracks' rendered output ahead of the playhead; it
  can share the pool and the disk threads.
