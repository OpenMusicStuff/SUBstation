# Roadmap

Where SUBstation goes next, in order. It replaces the old TODO.md, whose phases (the master as a strip,
recording, MIDI input, groups, parallel rendering, sends, resampling, sidechains, racks, presets, freezing, Linux)
are done; its open items are folded in below or listed in [Smaller items](#smaller-items).

Written in October 2026. This plan comes from four investigations:

- a survey of current DAWs: Live 12.3/12.4, Bitwig 6, FL Studio 2026, Logic 12, Cubase 15, Fender Studio Pro 8 (formerly Studio One), REAPER 7, Pro Tools, Renoise 3.5, Ardour 9 and browser DAWs;
- an audit of the engine and the application layer for real-time safety, scalability and stability;
- an audit of the everyday workflow and UI against the guides and the code;
- research into accessibility, agents and AI, and experimental music tools.

I checked the key claims against the code. File references are to the current `master`.

## The thesis

SUBstation already does three things the big DAWs don't:

1. **It describes itself.** Tracks have labels, the arrangement map guesses sections, harmony is detected, and every sound has a fingerprint and tags.
2. **It invites agents in.** It has a built-in MCP server and an agent history that is kept separate from the user's undo, so each change can be rewound.
3. **It is deterministic.** Renders are bit-identical on any number of threads.

None of the incumbents combines these. FL Studio's Gopher, Logic's Session Players and the community MCP bridges for Live and REAPER can all *act* on a project, but none of them can **hear** it, and none can explain the project to someone who can't see the screen.

**The lane to own:** *the DAW that anyone can perceive and play with: people who can see the screen, people who can't, beginners, and agents.* The structured, spoken-word view of a song that an agent needs is the same view a blind producer needs. The theory help a beginner needs is the same harmony engine an agent uses.

To earn that, two things must come first:
1. The foundation has to scale and stay steady. The audits found real weak spots on the edit side.
2. The everyday basics that keep people from finishing songs must exist: tempo changes, markers, a mixer, MIDI files, CC, automation recording.

Users of every major DAW complain loudest that basics stay neglected while flashy features ship. Don't repeat that mistake.

The goals below are in the order to do them. Effort: **S** = days, **M** = 1–2 weeks, **L** = about a month, **XL** = a cross-layer redesign.

---

## Phase 0: Protect what works (a few weeks)

Cheap work that keeps everything after it from turning into firefighting.

### 1. A real safety net in CI. Effort: S–M
- **Sanitizers (done).** `SUBSTATION_SANITIZE` and a nightly workflow (`sanitizers.yml`): every test under ASan and UBSan, the engine's and platform layer's under TSan. Their first runs found two bugs in the application (a downcast at window teardown, three tests reading a freed menu), one in Vital (fixed in its local patch), and over-reads and a lost wake-up's mishandling in tests; all pass now (docs/testing.md#sanitizers). Leak detection is still off.
- **Real-time allocation trap.** In `engine_tests`, hook `operator new` and the locks on render threads, or use Clang's RTSan (`[[clang::nonblocking]]`). Today the engine's real-time rule ("no allocations, no locks") is enforced only by review.
- **Windows CI on every PR.** Per-PR CI is Linux only, yet Windows is the product. The ASIO live tests (`test_parallel_live`, `test_recording`, `test_midi_input`) run only nightly.
- **Fuzz the untrusted parsers.** Add libFuzzer targets for `.gilproj`, `.als`, `.vital`, `.vstpreset` and the sandbox protocol.

### 2. Fix the known crash and stall bugs. Effort: S each
- **`Engine::waitForCallbackLocked()` (done).** It gave up silently after 500 ms, and its callers then freed the scheduler, the recording session, or rendered offline with processors a stalled callback was running. It now knows whether a callback is under way (returning at once when none is) and reports a stall: switching the scheduler and offline renders refuse (`kAudioThreadBusy`), a recording's session is retired to the release pool.
- **Undo history (done, by count).** 500 steps (`Session::kUndoLimit`). `QUndoStack` takes only a count; a limit by memory needs a stack of our own. Still to do: store big states (clip lists, plug-in states) copy-on-write, so a step costs what it changed.
- **CPU check (done).** `platform::cpuRunsEngine()` first thing in `main()`: a message, not an illegal instruction.
- **Swallowed bridge errors.** `EngineBridge::guarded()` (`app/src/audio/EngineBridge.cpp:17`) only logs, so a half-applied push leaves the engine out of step with the model, silently. Report it in the status line and resync that track.
- **Crash dumps.** Add a crash handler that writes minidumps. There is no SEH or signal handling for in-process plug-ins today, so crashes in the wild can't be diagnosed.

### 3. Make edits scale: batch and coalesce snapshot rebuilds. Effort: M, then L
**This is the biggest scalability finding.**
- **The problem.** Every engine call rebuilds the *whole* `RenderSnapshot` under the engine mutex (`EngineSnapshot.cpp:288`): routing, delay compensation, every note (each with a `shared_ptr` allocation), every clip and every automation lane. There are 49 call sites and no batch API. Cases where this hurts:
  - Opening a project rebuilds once per track per property: O(tracks × project size).
  - Each decoded file triggers a rebuild, on the decoding threads, holding the mutex that `takeMeters()` needs every display frame.
  - Dragging clips across 50 tracks causes 50 full rebuilds *per mouse move* (`BridgeTracks.cpp:387`).
- **Step 1 (done).** `Engine::Batch`: edits in one build one snapshot when it ends, and every change of the project reaches the engine as one batch (opening it, plug-in loading, a drag's preview). Opening a project no longer pushes every track's routes again for each track added. Decoded files arriving together are published together. `trackIndexLocked` and `Project::findTrack` look tracks up by maps. Measured with `snapshot_bench` (benchmarks/README.md): opening a 256-track project went from 6.9 s to 0.33 s, and a drag over every clip from 1.4 s a mouse move to 15 ms.
- **Step 2 (begun).** Profiling showed a snapshot's cost was about 85 % MIDI notes: each track's notes are now made once and shared by later snapshots until its notes, the tempo or the rate change. Closing a project is one batch (it built a snapshot per track removed). One edit at 256 tracks went from 2.3 ms to 0.86 ms, and closing from 364 ms to 6 ms. What is left is spread thin (allocating the snapshot's many small lists, routing, delay compensation, automation, clips); caching whole `TrackRender`s and rebuilding routing only when it changed would take it further, but only once a profile shows it matters.
- **Measure it** with `snapshot_bench` (benchmarks/README.md).

### 4. Get heavy work off the GUI thread. Effort: M
- **Backups (done).** The backup every minute ran on the GUI thread and first asked *every* plug-in for its state, serially (each sandboxed `getState` up to 15 s). Now the plug-ins' states are gathered without the window waiting (`EngineBridge::gatherPluginStates`): those in sandboxes asked all at once (`Engine::requestProcessorState`) and taken as they answer, those in the application read one a turn of the event loop, and none asked again whose state nothing changed since it was last taken (no parameter or state set, no event it reported such as `StateDirty`, no editor of its open). Then the project is turned into JSON on the GUI thread, and encoded, written and pruned on a thread of the backups' own. Saving asks the sandboxes all at once too, so it waits for the slowest, not for each in turn. Measured with `snapshot_bench` (benchmarks/README.md): a backup of a 256-track project cost the GUI thread 192 ms, now 64 ms. Still to do: that JSON too, once a worker can read a copy of the project (copy-on-write states, as goal 2's undo by memory wants).
- **Sample-rate changes (done).** They re-decoded every source serially on the GUI thread under the engine lock. Now opening the device at another rate decodes nothing: the sources stay cached at the old rate (their clips silent, as while a project opens) and the bridge's decoding pool decodes every one again, as loading does.
- **Resampling (done).** Decoding resampled with miniaudio's *linear* resampler, audibly worse for 44.1↔48 kHz. Now a polyphase Kaiser-windowed sinc (`Resampler.h`, docs/engine/warp.md): flat to 20 kHz at 44.1, 100 dB down past the transition band, nothing folding back below 20 kHz, exact phases for the usual rates, the same to the bit whatever the chunks (so cache files, made again at version 2, still play what memory does). With AVX it takes half the linear resampler's time (`decode_bench`: three minutes of stereo from 44.1 to 48 kHz in 335 ms, 599 before).
- **Peaks (done).** A long file loaded into memory (22 s or more) keeps its peaks in the stream cache's peak file, the one a streamed source of it has, and reads them the next time instead of making them. `decode_bench` puts what that saves at about 15 ms for three minutes of stereo: a sixth of loading a file at the engine's rate, little next to decoding or resampling one, so short files don't get one.

### 5. Keep the docs honest. Effort: S
`docs/guide/limitations.md` still lists punch in/out, stacking takes while looping, tempo detection and chords from audio as missing. All four exist. Fix it, because both users and agents read it.

---

## Phase 1: Finish songs without leaving SUBstation

These basics every competitor has are listed in order of everyday impact per unit of effort.

### 6. Markers, keyboard navigation and restructuring. Effort: S–M
- **Markers.** Named locators that you jump to with keys. Live import already drops Live's locators silently; import them.
- **Arrow keys.** The arrangement has no arrow-key handling at all. Add nudge, moving between tracks and clips, and selection extension.
- **Insert/delete time** (ripple), zoom to selection, and resizable automation lanes (fixed at 44 px today).
- **Why first:**
  - Markers give agents real sections instead of guessed ones.
  - Keyboard navigation is the backbone of accessibility (goal 14).

### 7. MIDI files, stems and better export. Effort: S–M
- Import and export of MIDI files. This is a total gap: there's no `.mid` anywhere.
- Stem/per-track export, reusing the offline render.
- Export options: sample rate, normalize, dither, a tail for releases.
- Collect-and-save (copy the samples into the project folder), several templates and track templates, and "Save New Version".

### 8. A mixer view. Effort: M
- Routing, meters and sends all exist, but every bit of mixing is crammed into track headers.
- A console view would add:
  - inserts and sends of every track side by side;
  - per-track CPU, from `track_costs()` (the numbers exist; nothing shows them yet);
  - a CPU meter with peak hold (today's figure is a moving average that hides spikes).

### 9. MIDI overdub and controller data. Effort: L
- **Overdub.** Layering drum hits pass by pass is the core beat-making loop. The take and comp code exists; this needs a merge mode.
- **CC, pitch-bend, aftertouch and sustain lanes.** Notes and clips carry no controller data today, so recordings lose the sustain pedal. Add the lanes in the model, the engine, the piano roll and recording.
- **MIDI clip looping.**

### 10. Automation recording and MIDI learn. Effort: M–L
- **Automation recording.** Write, touch and latch, built on the existing override and re-enable plumbing.
- **MIDI learn.** Map any parameter to a hardware control. Modulation map-by-touch (`ModulationLearn`) is the template.
- These belong together: both turn a hand movement into a parameter value.
- Mackie/HUI control surfaces can come later.

### 11. A tempo and time-signature map. Effort: XL
- **Why:** a song with a ritardando, a half-time bridge or a 3/4 bar is impossible today.
- **Why it's hard:** the engine uses one `tempo` and `samplesPerBeat()` (`Snapshot.h`), and about 57 source files assume tempo is constant.
- **Why now:** this is the most serious "it's not a real DAW yet" gap, and warp markers depend on it. Start it as soon as goal 3 has made the snapshot cheap.
- **Approach:** a beat↔sample mapping object used everywhere, with exhaustive tests.
- **Status:** done. `music/TempoMap.h` (steps and linear ramps, time signature changes) runs through the engine,
  warped clips, the model, saving, Live import and MCP. In the arrangement, the master's Song Tempo lane draws and
  edits the tempo (steps, ramps and curves), the ruler's right-click menu sets time signatures, and frozen tracks
  keep their beats when the tempo changes. Still open (see `docs/guide/limitations.md`): a settable tempo lane
  range, Live's curved tempo, and the piano roll and harmony counting bars by the first time signature.

### 12. Audio editing depth. Effort: L
- **Warp markers.** Done: a clip's markers (`Clip::warpMarkers`, saved since version 27) play through its source map,
  the clip view's waveform adds, drags and removes them, and Live import keeps Live's (cutting the clip into stretches
  only where a stretch's tempo is out of range). Mark Transients puts markers on the clip's transients (the Sampler's detection), drawn as ticks to snap to. Still
  open: quantizing markers to the grid, auto-warping a drifting performance, MCP tools.
- **Crossfades** between overlapping clips; comp seams click today.
- **Audio bounce in place / consolidate** of a selection. Consolidate is MIDI-only today (`EditorClips.cpp:428`).
- **Clip gain envelopes.**

---

## Phase 2: The signature: a DAW anyone can perceive

This is what sets SUBstation apart. Start goal 13 alongside Phase 1, because it is cheap and builds directly on what exists.

### 13. Agents that can *listen*. Effort: M–L
**The single most distinctive thing to build.**
- **Why:** every DAW MCP server found (Ableton, REAPER with 129 tools) is control-only and can't hear the result. SUBstation's MCP tools read and edit, but none renders.
- **Render and analyse.** Add `render_audio(range, tracks | master)` returning a file plus a compact analysis:
  - LUFS (integrated and short-term), true peak, crest factor;
  - a band spectrum, stereo width and correlation;
  - the tagger's words for the result, and tempo, key and chords from the audio.
- **Compare.** Add `compare_renders(a, b)`, so an agent can check its own edit: "did darkening the pad really cut 3–6 kHz?"
- **Clash report.** Generalise the Sidechain device's spectral clash comparison to any two tracks ("is the vocal masked by the guitars?").
- **The missing tools:** markers and sections, transport ("let me hear it"), placing audio clips from the browser (with similarity: "find a snare like this one"), creating racks and macros, export.
- **Ergonomics:** change notifications through MCP resource subscriptions, an optional auth token (any local process can edit today), per-scope permissions (notes only, mixer only), and treating track and clip names as untrusted text for prompt-injection safety.
- **Engineering:** offline renders currently silence live output (`EngineOffline.cpp:344`). Rendering on a separate copy of the processors means an agent can listen without interrupting you. It also unlocks freezing while playing.

### 14. Accessibility, built from the keyboard up. Effort: S → L
- **Today:** there are no `Accessible.*` uses anywhere in `ui/qml`. The arrangement, piano roll and knobs are scene-graph items that screen readers can't see.
- **Market:** REAPER with OSARA is the only real option for blind producers. Live 12 added screen-reader support but has gaps.
1. Label every control, menu and dialog (QML `Accessible.name/role` and the increase/decrease actions). Everything works from the keyboard, and goal 6 provides the arrangement's keys.
2. **Accessible arrangement and device models.** Tracks × clips as focusable cells. Device parameters as sliders with value text: `ParamSpec::format` already produces "1.2 kHz".
3. **Announcements** through `QAccessibleAnnouncementEvent` (Qt 6.8): position in bars, the track and its *label* ("Sad Piano Line"), the clip under the cursor, the section ("chorus 2"). Track labels and the arrangement map are exactly what a blind producer wants read out. No other DAW has them.
4. **Hands-free and screen-reader workflows through the agent.** Claude Code in a terminal with a screen reader can already drive the whole session. Document and promote this, and add a `describe_position` tool.
5. **Motor and cognitive basics:**
   - UI scaling beyond the OS's DPI;
   - larger hit targets;
   - typed values instead of drags;
   - sticky modifiers;
   - reduced motion;
   - a colour-blind-safe palette: red is used both for out-of-key notes and for overridden automation.
- **Test with NVDA and Narrator early.** Qt Quick's Windows UIA behaviour for custom items is poorly documented.

### 15. Help for beginners: theory and onboarding. Effort: S → M
- **The need:** the top beginner struggle in a survey of 1,000+ producers is "not knowing where to start". Scaler, Logic's Chord Track and Session Players, and FL's Gopher show the demand.
- **What to build:**
  - **Scale snap/fold** in the piano roll and the computer keyboard (S). The key is already known; let it constrain input.
  - **Progression ideas (done).** Generate ▾ › Progressions…: progressions in a mood (Sad, Dark, Nostalgic) to try in the clip one after another, from free-midi-chords' progressions (MIT) and chords drawn as they follow each other in the McGill Billboard songs (CC0); locking chords, keeping ideas, swapping a chord for its colours or the chords around it, variations (docs/intelligence.md#progression-ideas).
  - A **chord track** that Generate's chords and bass follow, a **chord palette by function** (I–IV–V, borrowed chords) to audition, and generated parts with rhythm, not just block chords (the progression ideas' too), and more moods for them.
  - **Explain mode:** "why is this note red", "what did this compressor do", with a before/after render (goal 13) by the agent.
  - A **first-run guided song** and a **Simple mode** that hides advanced panels.
  - **Localization:** QML already uses `qsTr` (76 files), but there is no translator loader or `.ts` files, and the C++ strings aren't wrapped.

### 16. Headless SUBstation. Effort: M
- **What:** `substation --headless` or a `substation-cli` that renders a project or a range, dumps the project as JSON, applies a batch of MCP tool calls, and serves MCP over **stdio**.
- **Who it's for:** agents in cloud sandboxes with no audio device or display, CI for people's songs, and batch work.
- **Render hashes:** publish a hash of each render, so an edit can be checked as "changed nothing outside bars 17–24". The bit-identical renderer makes this almost free.
- **Determinism:** seed every random operation.

### 17. Songs you can diff, branch and compare ("git for songs"). Effort: M–L
- **Format:** `.gilproj` is already sorted, indented JSON. Make it fully diffable:
  - built-in device state as named fields instead of base64;
  - plug-in blobs in sidecar files named by content hash;
  - one note per line.
- **Versions:** in-app snapshots and branches ("try darker chorus"), A/B playback, and a semantic `diff_projects` ("bars 9–16: piano notes changed; Pad cutoff 2 kHz → 1.2 kHz").
- **Agents on branches:** an agent proposes *alternatives* on branches you audition and merge. This grows the Agent Changes list from a list of changes into alternative versions.
- No desktop DAW has this.

---

## Phase 3: Join the ecosystem

### 18. CLAP, multi-out instruments, MIDI-effect plug-ins. Effort: L each
- CLAP's SDK is MIT. Its per-note modulation fits SUBstation's per-note bends, and its thread-pool extension fits the parallel renderer.
- Multi-output instruments (drum plug-ins) and MIDI-effect plug-ins are standard expectations.

### 19. Windows MIDI Services and MIDI 2.0. Effort: M
- **Why now:** Windows MIDI Services reached general availability in Feb 2026.
- **Today:** the engine already plays Universal MIDI Packets; only WinMM is in the way.
- **What it brings:** multi-client ports and real MIDI 2.0 devices, and it fills a hole in the limitations page. Linux MIDI devices (ALSA) are a smaller sibling.

### 20. DAWproject import and export. Effort: M
- **What:** the open format from Bitwig and PreSonus, with an MIT-licensed spec, read by Bitwig, Studio One/Fender and Cubase.
- **Why:** it gives users a way in and out beyond Live import. Lock-in is a top complaint, and an exit door builds trust.
- **Ableton Link:** small and expected. Add it while working on interop.

### 21. Sampler and Drum Rack depth. Effort: L
- **Sampler:** multisampling (zones by key and velocity), manual slices, and filter and pitch envelopes.
- **Drum Rack:** choke groups, return chains and per-pad sends. (A sidechain from a single pad is done: each pad has its own taps.)
- **Piano roll:** pad names from the Drum Rack, and folding to the rows in use.

### 22. Big-project scalability, part two. Effort: L–XL
- **Disk streaming (done, off by default).** [docs/engine/streaming.md](docs/engine/streaming.md).
  - The problem: sources are float32 in RAM at the engine rate, about 2.2× a 16-bit WAV and about 20× an MP3. 32 stereo tracks × 5 minutes is about 3.7 GB.
  - Done: files past a threshold (30 s) stream: WAV and AIFF at the engine's rate as they are, anything else from a cache file decoded once (peak files beside them), every source read through a `SourceCursor` (live: never waiting, misses faded and counted; offline: waiting), disk threads reading by deadline into a pool, heads and clip and loop starts pinned, a pre-roll for Play, the warp cache's renders of streamed clips on disk, the readers off the audio thread through `read()`, the preferences and a *Disk* indicator. Decoding in place without the second copy, too. Every engine test runs streamed as well (`engine_streamed`), to the bit.
  - Still to do: on by default after a Canary release; measure the defaults (threshold, pool, horizon) on a spinning disk and a 100-track project; overlapped I/O, IoRing and `io_uring` (several reader threads stand in for them); level 0 of streamed peaks read from disk; storing resident sources at native rate or in 16 bits.
- **Anticipative rendering** (as REAPER does) for tracks that aren't live-monitored. It needs the separate processors from goal 13.
- **Sandbox defaults.**
  - Consider one sandbox per manufacturer as the default, because in *Together* mode one plug-in's crash silences every plug-in.
  - Batch the round trips of consecutive sandboxed plug-ins in one chain.
  - Let the number of workers in a sandbox follow the engine's audio threads instead of half the cores.
- **Benchmarks in CI** for open time, save time, rebuild time and undo memory.

---

## Phase 4: Push music forward

### 23. Microtonality as a first-class feature. Effort: M
- **What:** a project or track tuning from Scala `.scl` and `.kbm` files, applied to Synth, Sampler and Vital. Vital's engine already supports tunings; only loading them is missing.
- **MTS-ESP:** SUBstation as the source of the tuning, so Surge, Pianoteq, u-he and Arturia retune together. The client library is 0BSD.
- **Editing:** scale-degree display in the piano roll for non-12-EDO tunings.
- **Why:** mainstream DAWs ignore tuning, so this is cheap to own and very much "progression of music".

### 24. Generative, seeded sequencing. Effort: M
- **Notes:** per-note chance, Elektron-style trig conditions (1:4, FILL), random ranges and ratchets.
- **Arrangement:** sections with follow rules ("B or C, never twice").
- **Determinism:** everything is seeded, with a "Freeze variation" action, so renders stay bit-identical and agents can reason about them.
- **Patterns as text:** a clip type written in Tidal/Strudel mini-notation (`"bd*4 [~ sd] hh*8"`). It is a compact language for agents too. Write a clean-room parser: Strudel is AGPL.
- **OSC:** an OSC server mirroring the MCP tools, so Sonic Pi and TidalCycles can play SUBstation.

### 25. Stem separation and audio-to-MIDI. Effort: M–L
- **Why:** it is table stakes as of 2025–26 (Live 12.3, Logic, FL, Cubase 15, Fender), and it is great for learning, remixing and sampling.
- **Licences need care:**
  - **Spotify Basic Pitch** (audio → MIDI with bends): Apache-2.0 for code and model. Safe, and it feeds the harmony engine audio clips.
  - **Demucs/HTDemucs:** the code is MIT, but the weights' licence is ambiguous.
  - **Most BS-/Mel-RoFormer checkpoints:** CC BY-NC. Don't bundle them; offer them as a download you choose.
- **Runtime:** one ONNX Runtime (MIT) inference path for this and future models.

### 26. AI with consent and provenance. Effort: S–M, ongoing
- **Musicians' view:** they accept AI for utility uses (separation, tagging, mixing help) far more than for generating songs. In the 2026 Muse Group survey only about 18% are open to AI composing from scratch.
- **Lead with:** assistive and analytic AI, plus *agents the user chooses*.
- **Avoid bundling:** generation models.
- **Provenance flag:** add one on every clip, note and edit that an agent or model made, saved in the project and exportable. The Agent Changes list is already halfway there.
- **Optional extras:** text-to-sample (generated one-shots land in the browser, tagged and findable by similarity) and local MIDI infilling (Anticipatory Music Transformer: Apache-2.0, with caveats about its training data).

### 27. Live performance and modulation everywhere. Effort: XL
- **Session view / clip launcher.** It's big, and the arrangement is SUBstation's identity, so build it after everything above. An Ardour-style "cues" lane, or Bitwig-style clips launched inside the arrangement, may fit better than a whole second view.
- **Global modulators.** Add Bitwig/Cubase-style LFOs and envelopes on any parameter (rack modulators already exist), plus macro automation in the engine (a lane fanning out to its targets).
- **A small modular device** and a **spectral clip editor** come last.

### Deferred: macOS
macOS needs a Mac to build, run and listen on. It uses the same seams as Linux (`AudioBackend`, `MidiDriver`, `EditorWindow`, `platform/`), never `#ifdef`s through the engine:

- Real-time base: the time-constraint policy, and the workers joining the device's audio workgroup (`os_workgroup`) so Apple Silicon doesn't park them on efficiency cores. `ScopedRealtimePriority`, `AudioWorkgroup`, `Scheduler::setWorkgroup()` and `AudioBackend::workgroup()` are written but have never been built or run on a Mac.
- A Core Audio backend (`backends/CoreAudioBackend.cpp`, an `AudioDeviceIOProc` on the HAL device): devices, aggregate devices, following the default device, buffer size and rate, latencies with the safety offset, inputs, the workgroup handed to the `Scheduler`, device changes, the microphone permission.
- A CoreMIDI backend (`backends/MidiCoreMidi.cpp`): sources, hot-plug, host-time timestamps.
- Plug-ins: the VST3 SDK's `module_mac.mm` as the loader, universal or arm64 bundles only; editors in `EditorWindowMac.mm` (an `NSWindow`, resizing, Retina scale, the main window's shortcuts through an `NSEvent` monitor).
- The rest: FSEvents file watching for the browser, QoS classes for background threads, plug-in paths, the system's window frame and global menu bar, Ctrl → Cmd reviewed in the QML, a macOS arm64 CI job, a signed and notarized `.app` in a dmg, docs/building.md, and a round of commercial plug-ins.

---

## The order at a glance

| # | Goal | Why it's here | Effort |
|---|---|---|---|
| 1 | Sanitizers, RT trap, Windows CI per PR, fuzzing | everything after leans on it | S–M |
| 2 | Fix the timeout race, undo limit, CPU check, bridge errors, crash dumps | known crashes and leaks | S |
| 3 | Batch/incremental snapshot rebuilds + benchmark | the main scalability ceiling | M→L |
| 4 | Backups, rate changes and peaks off the GUI thread; better resampler | hitches in big projects | M |
| 5 | Correct limitations.md | users and agents read it | S |
| 6 | Markers, arrow keys, insert/delete time | everyday editing; feeds agents and a11y | S–M |
| 7 | MIDI files, stems, export options, collect-and-save, templates | delivery and interop | S–M |
| 8 | Mixer view + CPU per track | mixing big sessions | M |
| 9 | MIDI overdub, CC lanes, MIDI clip loops | beat-making and playing | L |
| 10 | Automation recording + MIDI learn | hands-on control | M–L |
| 11 | Tempo and time-signature map | biggest "real DAW" gap | XL |
| 12 | Warp markers, crossfades, audio bounce, clip gain | audio editing | L |
| 13 | Agents that listen (render + analysis + compare) and more tools | **the signature** | M–L |
| 14 | Accessibility: keyboard, screen readers, announcements, agent channel | **the mission** | S→L |
| 15 | Scale snap, chord track, explain mode, onboarding, translations | beginners | S→M |
| 16 | Headless CLI, stdio MCP, render hashes | agents anywhere | M |
| 17 | Diffable format, versions and branches, semantic diff | experimentation | M–L |
| 18 | CLAP, multi-out, MIDI-effect plug-ins | plug-in ecosystem | L |
| 19 | Windows MIDI Services / MIDI 2.0 | now generally available | M |
| 20 | DAWproject, Ableton Link | interop and trust | M |
| 21 | Multisample Sampler, Drum Rack depth | instruments | L |
| 22 | Disk streaming, anticipative rendering, sandbox defaults | huge projects | L–XL |
| 23 | Microtonality (Scala, MTS-ESP) | new music | M |
| 24 | Seeded generative sequencing, pattern clips, OSC | experimentation | M |
| 25 | Stem separation, audio-to-MIDI | remix and learning | M–L |
| 26 | Provenance and consent for AI | trust | S–M |
| 27 | Clip launching, global modulators, modular, spectral | performance and sound design | XL |

**On the order:** goals 13–15 are numbered after Phase 1, but each has a cheap first slice: a `render_audio` tool, labelling every control, scale snap. Run those slices in parallel with Phase 1 so the signature grows while the basics get filled in.

---

## Smaller items

Open items from the old TODO.md that don't belong to a goal above. "Maybe" means only if a benchmark or a user shows
it matters.

Engine and rendering
- Serial fallback from the measured cost instead of counting busy tracks.
- Maybe: an "urgent" slot, so a bus that becomes ready mid-run on the critical path goes before leaves still queued.
- Maybe: splitting a strip at a sidechain tap, so the consumer starts earlier.
- Maybe: a rack's chains on several workers.
- Freezing: maybe render only from the track's first sound; maybe re-render frozen tracks after a tempo change instead
  of warping them.

Arrangement and devices
- Maybe: returns in the scrolling lanes after the tracks, resizable, as in Ableton.
- Maybe: a built-in gate with a sidechain.
- Key and velocity zones on rack chains.
- Maybe: renaming a rack by hand, and plug-ins and built-in devices titled with their presets' names.
- Maybe: previewing the preset under the mouse while browsing (hot-swapping presets, as in Ableton).

Intelligence
- Maybe: the user weighs the similarity aspects (as Sononym does); a small learned embedding as another aspect.
- A timing model for Humanize › Timing (now random nudges).
- Chord recognition over a whole project.
- MCP: resources and change notifications (goal 13), a stdio transport (goal 16).

Linux
- `parallel_render_bench` live through PipeWire, and against Windows on the same machine.
- The worker group joined to PipeWire's data thread priority (it asks for 88; the workers run at SCHED_FIFO 70 or
  rtkit's SCHED_RR 20).
- The ALSA sequencer tests run against a real kernel sequencer (GitHub's runners have no `snd-seq`).
- Real plug-ins' editors (JUCE, VSTGUI, DPF) under a window manager, X11 and XWayland: focus, keys, resizing, scale.
- The system's window frame (`WindowFrame` is Windows only; `TitleBar.qml` drops its window buttons there).
- The editor tests in the Linux CI job; an AppImage (not Flatpak: its sandbox gets in the way of system plug-ins); a
  round of commercial plug-ins.
