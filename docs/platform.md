# Platform layer

What every layer needs from the operating system, in one place: [platform/src/platform](../platform/src/platform),
the static library `sub_platform` (namespace `sub::platform`). The engine, the browser backend and the intelligence
module stand on it, and so, through them, the application layer. It has no Qt and nothing of the layers on it
(`ctest -R boundaries` checks: [architecture.md](architecture.md#the-boundaries-checked)).

Each system's code is in files of its own, chosen by CMake ([platform/CMakeLists.txt](../platform/CMakeLists.txt)):
[PlatformWin32.cpp](../platform/src/platform/PlatformWin32.cpp) and [IpcWin32.cpp](../platform/src/platform/IpcWin32.cpp)
on Windows, [PlatformPosix.cpp](../platform/src/platform/PlatformPosix.cpp) and
[IpcPosix.cpp](../platform/src/platform/IpcPosix.cpp) elsewhere. The headers declare the same functions for
every system, so what calls them has no `#ifdef`s. Supporting another system (macOS's own calls, say) means another
file behind the same headers, or a branch in the POSIX one; nothing that calls them changes.

Headers are included by their folder: `#include "platform/Paths.h"`.

## Files

| File | What it holds |
|---|---|
| [Unicode.h](../platform/src/platform/Unicode.h) / [.cpp](../platform/src/platform/Unicode.cpp) | WTF-8 one code point at a time (`decodeUtf8`, `appendUtf8`, inline: the browser's text matching runs on them), `isAscii`, and WTF-8 to UTF-16 and back (`toWide`, `fromWide`). |
| [Paths.h](../platform/src/platform/Paths.h) | `NativeString` (UTF-16 on Windows, bytes elsewhere), `NativeChar`, `kSeparator`, `kCaseSensitivePaths`, `isSeparator()`, `appendName()`; `toNative()`/`fromNative()` and `toPath()`/`fromPath()` (a UTF-8 path as the system's calls and `std::filesystem` take it, and back); keys: `nameKey()`, `pathKey()`, `normalPath()`, `fileKey()`. |
| [Files.h](../platform/src/platform/Files.h) / [Files.cpp](../platform/src/platform/Files.cpp) | By UTF-8 path: `FileStamp`/`stamp()` (size and last-write time), `modifiedTime()` (a file's or folder's last write, in nanoseconds since 1970, the same from every compiler), `replaceFile()`, `openFile()`, `readFile()` (a whole file), `writeFileAtomically()` (to `.tmp`, then over the file). |
| [Bytes.h](../platform/src/platform/Bytes.h) | Binary files' fields, little-endian on every system: `ByteWriter`, `ByteReader` (a read past the end makes `ok()` false for good), and `fnv1a()`, their checksum (and a hash of keys). The browser's saved index and the sound similarity's store are made of these. |
| [Cpu.h](../platform/src/platform/Cpu.h) / [Cpu.cpp](../platform/src/platform/Cpu.cpp) | `cpuRunsEngine()`: on x86-64, AVX2 and FMA by `cpuid`, with the system saving the YMM registers (`xgetbv`); elsewhere true. `reportUnsupportedCpu()`: a message box on Windows, standard error elsewhere. `ui/main.cpp` asks first thing, before anything built for AVX2 runs (this file is built without it). |
| [Threads.h](../platform/src/platform/Threads.h) | Priorities: `enterBackgroundMode()` (the browser's indexer, the sound similarity's threads) and `ScopedRealtimePriority` (the engine's render workers); audio workgroups: `AudioWorkgroup`, `ScopedWorkgroupMembership`. |
| [Ipc.h](../platform/src/platform/Ipc.h) | Processes of our own: `ChildProcess` and its `Channel`, `parentChannel()` and `childProcessStarted()` (the child's), `SharedMemory`, `IpcSignal` (a wake-up on a shared counter), `uniqueIpcName()`: see [Processes](#processes). |
| [IpcWin32.cpp](../platform/src/platform/IpcWin32.cpp), [IpcPosix.cpp](../platform/src/platform/IpcPosix.cpp) | `Ipc.h` on Windows (pipes, a job, file mappings, events) and elsewhere (a socket pair, `posix_spawn`, `shm_open`, futexes on Linux). |
| [PlatformWin32.cpp](../platform/src/platform/PlatformWin32.cpp) | Windows: `LCMapStringEx` for keys, the wide (`W`) file calls, `MoveFileExW`, `THREAD_MODE_BACKGROUND_BEGIN`, MMCSS (`AvSetMmThreadCharacteristicsW`, "Pro Audio"). |
| [PlatformPosix.cpp](../platform/src/platform/PlatformPosix.cpp) | Elsewhere: `stat`, `rename`, `fopen`; on Linux the thread's nice value and idle I/O class, `SCHED_FIFO` and rtkit (libdbus, `dlopen`ed); on macOS the time-constraint policy and `os_workgroup`. |

## Paths

Paths cross every layer as UTF-8 strings. On Windows they are WTF-8: UTF-8 that may also hold unpaired surrogates,
which Windows file names can contain, so a name read from the system and handed back is the same name
(`toWide`/`fromWide` keep them; `std::filesystem`'s own UTF-8 conversion would not). Where a path meets the system it
is in the system's own form: `toNative()` (UTF-16 on Windows; the bytes as they are elsewhere), or `toPath()` for
`std::filesystem` (never its `char` constructor, which on Windows reads the ANSI code page). Code that opens files
through a library with narrow and wide calls (miniaudio) uses the wide one on Windows and the narrow one elsewhere:
the engine's [MiniaudioFiles.h](../engine/src/MiniaudioFiles.h), the intelligence module's `AudioReader`.

Keys say when two paths are one file. `pathKey()` is what `os.path.normcase()` made, so keys match those the
Python-era files hold: on Windows backslashes and Windows' own lower case (`LCMapStringEx`, the invariant locale; ASCII
is lowered directly, which gives the same), elsewhere the path as it is, since names that differ in case are different
files there. `fileKey()` normalises the path first (`normalPath()`: "." and ".." and doubled separators gone, the
system's separators): the engine's decoded sources and loaded plug-in modules are kept by it.

## Threads

`enterBackgroundMode()` lowers the calling thread for good: CPU, I/O and memory priority on Windows
(`THREAD_MODE_BACKGROUND_BEGIN`), the lowest nice value and the idle I/O class on Linux (both per thread there). It
does nothing yet on other systems.

`ScopedRealtimePriority` raises the calling thread while it lives, and puts it back as it was after: the engine's
render workers ([engine/scheduler.md](engine/scheduler.md)) hold one for their lives. `active()` says whether the
system raised it; where it refuses, the thread stays as it was.

- **Windows**: MMCSS's "Pro Audio" task, as the driver's own thread has.
- **Linux**: `SCHED_FIFO` at priority 70: under the audio servers' threads (PipeWire's data thread asks for 88),
  which the workers wait on and must not preempt, and over everything that isn't audio. First set directly
  (`pthread_setschedparam`), which works with `CAP_SYS_NICE` or up to `RLIMIT_RTPRIO` (`rtprio` in
  `/etc/security/limits.conf`; a lower limit is used if that is all there is). Else it is asked of rtkit
  (`org.freedesktop.RealtimeKit1.MakeThreadRealtime` on the system bus), lowered to its `MaxRealtimePriority`
  (20 by default); rtkit sets `SCHED_RR` with `SCHED_RESET_ON_FORK`, which an unprivileged thread may not clear, so
  going back keeps that flag. The thread's scheduling is read and set with `sched_getscheduler`/`sched_setscheduler`
  (the calling thread's on Linux), not `pthread_getschedparam`: glibc keeps a copy of its own, which misses what
  rtkit sets from outside. libdbus is loaded when first needed (`dlopen("libdbus-1.so.3")`), so a build needs no D-Bus
  headers and a system without it only goes without; the requests go over a private connection of their own (not
  libdbus's shared one, which Qt's D-Bus may use), one at a time, each given 500 ms: a scheduler waits for its
  workers' requests while live audio is held, so once one goes unanswered (rtkit hanging) rtkit isn't asked again. rtkit only raises a process that limits its
  real-time threads' time without blocking (`RLIMIT_RTTIME`, at most its `RTTimeUSecMax`, 200 ms by default; past it
  the kernel ends the process), so the limit is lowered to that first: the scheduler's workers block at least
  every 20 ms ([engine/scheduler.md](engine/scheduler.md#workers)). A thread that is real-time already is left as it
  is (`active()` true).
- **macOS**: the Mach time-constraint policy (`THREAD_TIME_CONSTRAINT_POLICY`): a 2.9 ms period, computing for up
  to 75% of it, done within 85%, preemptible (the values audio software commonly gives its audio threads).

`AudioWorkgroup` is an audio device's workgroup: on macOS the `os_workgroup_t` of the device's I/O thread
(`kAudioDevicePropertyIOThreadOSWorkgroup`, which the Core Audio backend will hand over); copies share it
(`os_retain`/`os_release`). Threads that render for the device join it (`ScopedWorkgroupMembership`, real-time safe:
`os_workgroup_join`/`leave`, macOS 11 and later), so the system schedules them with the device's thread, to its
deadline, and on Apple Silicon keeps them on the performance cores. Other systems have none: every `AudioWorkgroup`
is empty there and joining one does nothing.

## Processes

[Ipc.h](../platform/src/platform/Ipc.h) is what the plug-in sandboxes are made of ([engine/sandboxes.md](engine/sandboxes.md)):
a process of SUBstation's own with a private channel to it, memory both map, and a wake-up one gives the other. Its
code is a pair of files of its own: [IpcWin32.cpp](../platform/src/platform/IpcWin32.cpp) and
[IpcPosix.cpp](../platform/src/platform/IpcPosix.cpp).

- **`ChildProcess::start(program, arguments)`** starts a program with a `Channel` to it (a byte stream both ways,
  blocking; either side closing or ending ends it on both). The child gets its end from an argument the parent adds
  (`--channel=...`, `parentChannel()`); its own stdin, stdout and stderr stay as they were, so a plug-in printing
  never garbles the channel. Windows: two anonymous pipes, the child inheriting its two ends and nothing else
  (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), no console window. POSIX: a socket pair (its end is the child's descriptor 3),
  `posix_spawn`, and a peer that went makes a write fail rather than raise `SIGPIPE`.
- **It ends with the parent**, even stuck: on Windows it is in a job that kills it when its last handle (the
  parent's) closes; on Linux it asks for `SIGKILL` when its parent dies (`childProcessStarted()`, first thing in
  the child, which also turns off Windows' crash dialog: the parent hears of a crash at once). The object ending
  kills it if it is still there.
- **`SharedMemory`**: named, made by one process (zeroed) and opened by another; the maker removes the name once the
  other has it (`forgetName()`), so nothing is left behind should either die. Windows: a named file mapping
  (`Local\`); POSIX: `shm_open` (names short: macOS takes 31 characters, `uniqueIpcName()`).
- **`IpcSignal`**: a wake-up on a 32-bit counter in shared memory, which the audio thread uses: neither `notify()` nor
  `wait()` allocates or locks. Linux: a futex on the counter itself (shared, not `FUTEX_PRIVATE_FLAG`); Windows: a
  named auto-reset event beside it (the counter decides: an event may hold a wake-up for an earlier change);
  elsewhere a polled wait, 50 µs at a time (macOS's own, `os_sync_wait_on_address`, can take its place).
- **`allowForeground()`** (Windows): lets the child bring a window of its own to the front now (Windows only lets
  the process the user works in do that), before it opens a plug-in's editor.

## Performance

Nothing here costs the layers on it anything they didn't pay before: what runs per character or per field
(`decodeUtf8`, `appendUtf8`, `isAscii`, `appendName`, `ByteWriter`, `ByteReader`, `fnv1a`) is inline in the headers,
and numbers are copied as they are on little-endian CPUs (every one SUBstation runs on); only a big-endian one would
swap their bytes. What calls the system (keys, stamps, files, priorities) was a call into another file before as well.
