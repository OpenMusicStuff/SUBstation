# Plug-in sandboxes

Plug-ins can run in **sandboxes**: processes of their own, of the plug-in host program `substation-plugin-host`
([tools/pluginhost](../../tools/pluginhost)), instead of in the application. A plug-in in a sandbox loads while the
window goes on, and one that crashes takes down only its sandbox: the plug-ins there pass their input on and say so,
and can be loaded again. The code is in [engine/src/plugins/sandbox](../../engine/src/plugins/sandbox), the host in
[tools/pluginhost](../../tools/pluginhost), and what they stand on in the platform layer's
[Ipc.h](../../platform/src/platform/Ipc.h). How the user chooses is in
[guide/plugins.md](../guide/plugins.md#plug-in-hosting).

## Why

VST3 requires plug-ins to be created and called on the UI thread (every call but `process()` is marked
`[UI-thread]`), and JUCE plug-ins take the thread that creates them for their message thread: in the application,
the UI thread is the window's, so a plug-in that takes a second to load (or to restore a big instrument) holds up the
window for that second, however the loads are spread out ([app/engine-bridge.md](../app/engine-bridge.md#opening-a-project)).
A sandbox has a main thread of its own, which is the plug-ins' UI thread there: the window never runs plug-in code.

It also keeps SUBstation running when a plug-in crashes, as Bitwig's sandboxes do, and it is how plug-ins will run
alongside a macOS port: each sandbox's main thread is its own process's main thread, where AppKit wants plug-in views.

## Hosting modes

`Engine::setPluginHosting()` ([PluginHosting.h](../../engine/src/PluginHosting.h)) says where plug-ins loaded from then
on run; the user's preference sets it (*Preferences › Plug-ins › Plug-in Hosting*,
[app/session.md](../app/session.md)). The modes are Bitwig's:

| Mode | Sandboxes | Key ([PluginSandboxes.cpp](../../engine/src/plugins/sandbox/PluginSandboxes.cpp)) | Label |
|---|---|---|---|
| `InProcess` (*Within SUBstation*) | none: `Vst3Processor` (or `Vst2Processor`) in the application, as before | | |
| `Together` (the default) | one for every plug-in | `all` | "All plug-ins" |
| `ByManufacturer` | one for each maker (the scan's vendor) | `vendor:<vendor, lower case>` | the vendor ("Unknown maker" for none) |
| `ByPlugin` | one for each plug-in class; its instances share it | `plugin:<class id>` | the plug-in's name |
| `Individually` | one for each instance | `instance:<n>` | the plug-in's name |

A sandbox starts when the first plug-in that goes to it loads, and quits when no plug-in has used it for a while
(`setSandboxRetireDelay()`, 5 s: a plug-in deleted and its deletion undone doesn't start one again). Plug-ins loaded
already stay where they are when the mode changes, until loaded again: `Engine::reloadProcessor()` loads one again in
its place (the same processor id, chain, sidechain and MIDI input, from its state then) where the mode says now, in
or out of the application; the preferences' *Reload Plug-ins* does it for every plug-in.

## The parts

```
 application (the engine)                                   substation-plugin-host (one a sandbox)
 ───────────────────────                                    ──────────────────────────────────────
 Engine ── PluginSandboxes ── Sandbox ── ChildProcess ═══channel═══ reader thread ─► queue
             (modes)            │  reader thread ◄─════════════════ main thread (HostLoop): loads,
                                │                                    every call but process(), editors,
 Renderer ─► SandboxedProcessor ┘                                    idle() and its reports
             (a Processor)  ═══ Block, Doorbell (shared memory) ═ audio workers (a few for all)
```

| File | What it holds |
|---|---|
| [PluginHosting.h](../../engine/src/PluginHosting.h) | the modes |
| [SandboxProtocol.h](../../engine/src/plugins/sandbox/SandboxProtocol.h) / [.cpp](../../engine/src/plugins/sandbox/SandboxProtocol.cpp) | the messages and their fields; the audio `Block` |
| [Sandbox.h](../../engine/src/plugins/sandbox/Sandbox.h) / [.cpp](../../engine/src/plugins/sandbox/Sandbox.cpp) | one sandbox: its process, its channel, its reader thread, calls waiting for replies, its plug-ins (`SandboxClient`), its editors' keys |
| [PluginSandboxes.h](../../engine/src/plugins/sandbox/PluginSandboxes.h) / [.cpp](../../engine/src/plugins/sandbox/PluginSandboxes.cpp) | an engine's sandboxes: which a plug-in goes to, starting and retiring them, editor keys and the foremost editor across them |
| [SandboxedProcessor.h](../../engine/src/plugins/sandbox/SandboxedProcessor.h) / [.cpp](../../engine/src/plugins/sandbox/SandboxedProcessor.cpp) | a plug-in in a sandbox as a `Processor` |
| [EngineSandboxes.cpp](../../engine/src/EngineSandboxes.cpp) | the engine's API for them: the mode, the host program, reloading, prioritising, waiting, the sandboxes listed |
| [tools/pluginhost](../../tools/pluginhost) | the host: `main.cpp`, [HostServer.h](../../tools/pluginhost/HostServer.h) / [.cpp](../../tools/pluginhost/HostServer.cpp) (the protocol, loads, the audio workers), [HostLoop.h](../../tools/pluginhost/HostLoop.h) and a file per system (the main thread's loop) |
| [platform/Ipc.h](../../platform/src/platform/Ipc.h) | `ChildProcess`, `Channel`, `SharedMemory`, `IpcSignal`: [IpcWin32.cpp](../../platform/src/platform/IpcWin32.cpp), [IpcPosix.cpp](../../platform/src/platform/IpcPosix.cpp) |

## The host process

`substation-plugin-host --parent=<pid> --channel=...` is started by the engine only (`ChildProcess::start`). It ends
with the engine: on Windows the engine's job object kills it when the engine goes; on Linux the parent-death signal
does (`childProcessStarted()`), and its reader thread ends it as soon as the channel ends, even with its main thread
stuck in a plug-in. A crash ends it without the system's dialog (Windows: `SetErrorMode`).

- **The reader thread** reads the engine's messages into a queue and wakes the main thread, so the engine never waits
  to write one, however long a load takes.
- **The main thread** (`HostServer::run`) is the plug-ins' UI thread: it works through the queue in order, but for
  loads (`Create`), which wait while anything else is there to do: then one loads, and the loop goes round again. So a
  slow plug-in holds up only its own sandbox's plug-ins, and only between messages (a value's text asked for waits
  for that one load, not for every load queued). `Prioritize` reorders the loads (the selected track's first, an
  editor asked for next). Messages for a plug-in that hasn't loaded yet wait with it, and are dealt with as it has
  loaded, before it says so (`Loaded`), those that came while it was loading too: a state set right after it was
  added is its state from its first block, and the values it says it has are that state's.
  Every 30 ms it idles the plug-ins (`Processor::idle()`: restarts, parameters, editors) and reports what changed
  (`Report`): the values that changed since the engine last heard them, a rebuilt parameter list, a new latency or
  preset name, and the `ProcessorEvent`s. `HoldIdle` pauses that while a render runs in the background, as
  `Engine::idle()` pauses it in the application.
- **Audio workers**, at real-time priority, as many as the plug-ins loaded, up to half the cores: each takes the
  next plug-in off the sandbox's doorbell, processes its block, and says so. One idle worker spins for 50 µs for the
  next block (the blocks of one callback come microseconds apart); the others sleep until the engine rings. One
  thread a plug-in had each block wait 10-18 µs for a sleeping thread to wake, and left most of them without
  real-time priority: Windows' MMCSS takes a few dozen threads a process (32), and a sandbox with 138 plug-ins had
  22 % of its blocks processed on threads with it. A plug-in stuck in `process()` keeps its worker (the main
  thread starts another once one has been in a block for 250 ms) and, when it is destroyed, its memory (the host
  leaves it as it is after a second, and the sandbox's other plug-ins go on).
- **The loop** ([HostLoop.h](../../tools/pluginhost/HostLoop.h)) is the one part that is the system's:
  [HostLoopPosix.cpp](../../tools/pluginhost/HostLoopPosix.cpp) polls a wake-up pipe and what the editors' X11
  connections and the plug-ins' run loop watch (it is the engine's editor event loop in that process,
  [EditorHost.h](../../engine/src/EditorHost.h)); [HostLoopWin32.cpp](../../tools/pluginhost/HostLoopWin32.cpp) waits
  on window messages and an event, and makes the process per-monitor DPI aware as the application is. A system whose
  plug-ins need its own run loop (macOS's) has a file of its own there; nothing else changes.

## Messages

Each message is a frame on the channel ([SandboxProtocol.h](../../engine/src/plugins/sandbox/SandboxProtocol.h)): its
size, its kind, the plug-in instance it is about, the request it asks or answers (ids unique across every sandbox: a
plug-in loaded again elsewhere is never taken for what it was), then its own fields (`ByteWriter`'s, little-endian).
The host says `Ready` first, with the protocol's version (4: `Create` carries the plug-in's format since VST2 (3), `ResetOffline` whether a render follows (4)) and the
block's size; a sandbox that speaks another isn't used. The host loads a plug-in with its format's `instantiate()`
(`pluginFormat()`), and hands the UI's values on through `PluginProcessor` (`setControllerParam`, `queueParamChange`),
whichever format it is ([plugins.md](plugins.md#pluginformat)).

| Engine → host | | Host → engine | |
|---|---|---|---|
| `Create` | path, class id, rate, block size, the block's memory, state, name, its slot on the doorbell, its format ("VST3", "VST2") | `Attached` | it has the block (its name may go) |
| `Destroy`, `Prepare`, `ResetOffline` | | `Loaded` / `LoadFailed` | what it is (`LoadedInfo`) / why not |
| `SetParam` | its controller (the audio hears it in the block) | `Report` | values, parameters, latency, preset, events |
| `ParamText`, `GetState`, `SetState` | answered with `Reply` | `Reply` | ok, then the text, state or error |
| `OpenEditor`, `CloseEditor`, `ShowEditor`, `SetEditorTitle` | | `Busy` | its main thread is loading a plug-in |
| `CloseForemostEditor`, `Prioritize`, `SetKeys`, `HoldIdle`, `Quit` | | `Key`, `EditorRaised` | an editor's key taken; an editor came to the front |

## Audio

Each plug-in has a `Block` in shared memory (`SharedMemory`, made by the engine, opened by the host) with a wake-up
on its `done` counter (`IpcSignal`: a futex on Linux, a named auto-reset event on Windows, a polled wait elsewhere),
and a slot on its sandbox's `Doorbell` (given with `Create`). The doorbell, one a sandbox (made by the engine, named
to the host as it starts: `--doorbell=`), is a queue of the slots whose blocks are to be processed (Vyukov's
bounded queue, many to many, lock-free; a plug-in has one block on its way at most, so it never fills), the
workers asleep, and a wake-up on its counter `rung`.
`SandboxedProcessor::process()`, on whichever thread renders the plug-in:

1. If the plug-in hasn't loaded (or crashed), it returns: the audio passes by, as for a missing plug-in.
2. Offline, the first block waits for the reset asked for before the render. `resetOffline()` sends `ResetOffline` (with
   whether a render follows or one ended: the host passes it on, so a plug-in there renders in its offline mode too)
   without waiting for it (it runs on the main thread, under the engine's lock, and the sandbox's main thread may be
   loading another plug-in, or stuck in one); the host's main thread does it, its workers the blocks. The wait
   ends 10 s after the reset was asked for, and every plug-in was asked at once: a stuck sandbox costs a render 10 s,
   not 10 s a plug-in.
3. If the host still has the previous block (it didn't make it in time), this one passes by too: a plug-in that hangs
   costs the rendering thread one wait, not one a block. Offline, it is waited for, once: after an offline block that
   didn't make it, the next ones pass by until the host catches up.
4. It fills the block: the context (`BlockContext`), the notes and MIDI, the automation for this call
   (`Processor::automation()`), the values the UI set since (a queue the main thread fills, the format's ids (VST3's
   `ParamID`, VST2's index) and normalized values), the audio and the sidechain, and a reset if one was asked for; then moves `request` and queues its slot
   on the doorbell (`Sandbox::ring()`), which wakes a worker only if they all sleep.
5. It waits for `done` to match: a spin of a few microseconds, then on the wake-up, at most twice as long as the block
   lasts (3 to 50 ms) live, a minute offline (`Engine::setSandboxOfflineWait()`), and never past its sandbox ending.
   Done, it copies the audio back; not, the audio passes through unprocessed (`lateBlocks()` counts those).

A worker takes the slot, applies what the block holds to the plug-in (`Processor::automate()`,
`PluginProcessor::queueParamChange()`, `setSidechain()`), calls `process()` in place, moves `done` and wakes the
rendering thread. One worker at a time is in a plug-in's block; one that finds another there leaves it, and that
one looks again once out, so a block handed over meanwhile is never left behind. A round trip costs about 25-30 µs
on Linux; on Windows (a 24-core Core Ultra 7, a 138-plug-in project, Together) about 15 µs besides the plug-in's own
time: 4-5 µs for a worker to take the block, 10 µs for the rendering thread to wake. `benchmarks/project_bench.cpp`
plays a real project in each mode and measures it.

This is the one place the rendering thread waits for another thread: the real-time rules
([architecture.md](../architecture.md#real-time-rules-and-the-boundary-with-the-audio-thread)) say so.

## What the engine keeps of a sandboxed plug-in

`SandboxedProcessor` keeps copies of what the application asks about, from the host's reports, taken on the main thread
in `idle()` (`Engine::idle()`): its parameters, their values (atomics, as `Vst3Processor`'s), latency and tail,
sidechain and MIDI input, whether it has an editor, its preset's name. Setting a value changes the copy at once.
`ProcessorEvent`s say what changed, as for a plug-in in the application, and these besides: `Loaded`, `LoadFailed`,
`Crashed`, `Problem` (its state wasn't taken), `EditorOpened`, `NoEditor`. `Engine::processorInfo()` gives its status
(`PluginStatus`: `Loading`, `Ready`, `Failed`, `Crashed`), what went wrong, and its sandbox.

Questions wait for their answer only when the sandbox is free (not `Busy`), and briefly:

- a value's text (`paramText`): 250 ms; while the sandbox loads another plug-in it is asked without waiting, and shows
  when it comes (`ParamsChanged`); texts are kept;
- its state (`getState`, to save): up to 15 s; past that, or with the sandbox gone, or while it is still loading, the
  last state known stands in (the one it loaded with, or the last saved). It can be asked for without waiting too
  (`requestState()`, `Engine::requestProcessorState()`): every plug-in's at once, a save's or a backup's
  ([app/engine-bridge.md](../app/engine-bridge.md#plug-in-states)). The answer is taken from the reader thread as it
  comes (`received()` keeps it, not the inbox: `idle()` needn't run), and `requestedState(until)` gives it, waiting
  until then at most; the wait goes on while the sandbox is heard from (`Sandbox::lastHeard()`: it answers what was
  asked before it, one after another on its main thread), up to 15 s after it last was, and a minute after it was
  asked at most (a plug-in that runs a message loop as it saves could keep its sandbox talking for good); past
  that, or with the sandbox gone, the last state known again (said so: `answered` false, as
  `answersState()` is while it loads or once its sandbox ended);
- a state set (`setState`): up to 10 s, to hear whether it was taken; while loading, the answer comes later (`Problem`
  if it wasn't).

Editors open in the sandbox, owned by the main window (an HWND or an X11 window, which other processes may own windows
by); the engine lets the sandbox bring a window to the front first (`ChildProcess::allowForeground()`: Windows only
lets the process the user works in do that). Opening is asked, not waited for: `openEditor()` says it asked, and
`EditorOpened` or `NoEditor` follows.

**Keys**: the sandbox's editors' keys never reach the application, which can't be waited for either. So the application
hands every sandbox a key table (`Engine::setSandboxEditorKeys()`: the keys its shortcuts take, worked out by the UI's
`PluginEditorKeys`), and the host keeps those keys from its plug-ins (Linux: the X11 editor's key filter, `EditorHost.h`;
Windows: its message loop) and passes them on (`Key`, `Engine::takeSandboxEditorKeys()`). **The foremost editor**:
each host says when one of its editors comes to the front (`EditorRaised`); `closeForemostSandboxEditor()` asks the
sandbox whose editor came last to close its foremost (on Windows the UI finds it among every process's windows).

## Crashes

When a sandbox's process ends, its reader thread sees the channel end: every plug-in in it is told
(`SandboxClient::sandboxEnded()`), a rendering thread waiting on one goes on at once, and at the next `idle()` each
says `Crashed` (its problem: "its sandbox (*label*) stopped"). They pass their input on from then on. Loading one again
(`Engine::reloadProcessor()`) starts it in a new sandbox, from its last state known. The application says so in the
device view, with *Reload* ([app/engine-bridge.md](../app/engine-bridge.md#plug-ins-in-sandboxes)).

## Tests

- [tests/platform/test_ipc.cpp](../../tests/platform/test_ipc.cpp): the channel both ways and its end, a killed child,
  shared memory, wake-ups between processes (with a child of its own, `ipc_test_child`).
- [tests/engine/test_plugin_sandboxes.cpp](../../tests/engine/test_plugin_sandboxes.cpp): a plug-in in a sandbox
  rendering sample for sample as in the application (notes, parameters, automation, latency, sidechains); parameters,
  texts, the transport and state from there; a state set as it loads, before or while it loads (*SUB Test Effect*
  loading slowly); states asked for all at once, answered after a load under way and taken as they come, and the last
  one known at once when the sandbox ends meanwhile; adding one not waiting for it to load (*SUB Test Crasher* loading slowly), prioritising; the
  sandboxes each mode makes; a crash taking down its sandbox and no other, together taking down all, reloading as last
  known; a hung plug-in holding up the audio thread for one block, and an offline render for one wait; a render not
  waiting on the main thread for a sandbox loading; reloading into another mode and back; no host program; a plug-in
  that can't load there; a garbled message's counts.
- [tests/engine/test_plugin_sandboxes_x11.cpp](../../tests/engine/test_plugin_sandboxes_x11.cpp): sandboxed editors on
  Xvfb: opened, titled, hidden, closed by the user; the key table; the foremost of several sandboxes' editors.
- [tests/app/test_bridge_sandboxes.cpp](../../tests/app/test_bridge_sandboxes.cpp): a project opening without waiting
  for its plug-ins, states, automation and sidechains once loaded, a crash and Reload, Reload Plug-ins, the preference.
