# The session

`Session` ([app/src/session](../../app/src/session)) is one open project and everything that works on it: the main
window's logic, without its drawing. It owns the project, the undo stack, the editor, the selection, the engine
bridge, the browser and the plug-in index, and wires them together. Its parts hold the logic behind the views that
isn't drawing: the arrangement's clipboard and its Edit commands, the device view's selection and actions, renders in
the background, the computer MIDI keyboard, the audio and MIDI preferences.

The UI reaches all of it through the session: QML as the `Session` singleton (`import SUBstation`; registered by
`sub::ui::registerSession` in [ui/src/app/AppTypes.cpp](../../ui/src/app/AppTypes.cpp)), the UI's C++ items through a
`session` property (`Q_PROPERTY(sub::app::Session* session ...)`, set from QML as `session: Session`) and its
accessors. The UI shows dialogs and asks the session to act; the session itself never asks the user anything.

## Files

| File | What it holds |
|---|---|
| [Session.h](../../app/src/session/Session.h) | `Session`: its parts, properties, invokables and signals; its header comment is the overview. `Session.cpp`: making the parts and wiring them, the title, start-up and shut-down |
| [SessionFiles.cpp](../../app/src/session/SessionFiles.cpp) | New, open, save, the template, the recent projects, the last folder, recovering backups, Export Audio |
| [Backups.h](../../app/src/session/Backups.h) / [Backups.cpp](../../app/src/session/Backups.cpp) | `Backups`: unsaved changes backed up every minute (written on a thread of its own), the marks running sessions keep, what a crash left |
| [SessionEditing.cpp](../../app/src/session/SessionEditing.cpp) | The Create and Edit commands on what is selected (dispatched to the arrangement's actions, or the device view's while it has the focus), adding to the selected track, freezing |
| [SessionTransport.cpp](../../app/src/session/SessionTransport.cpp) | Play, record (with the count-in), stop, locate |
| [Selection.h](../../app/src/session/Selection.h) | `Selection`: what is selected in the arrangement |
| [ArrangementActions.h](../../app/src/session/ArrangementActions.h) | `ArrangementActions`: the clipboard for clips, automation and tracks, and what acts on the arrangement's selection |
| [DeviceSelection.h](../../app/src/session/DeviceSelection.h) | `DeviceSelection`: the device view's selection, clipboard, presets and drops |
| [GrooveLibrary.h](../../app/src/session/GrooveLibrary.h) | `GrooveLibrary`: the project's grooves for the UI (the groove editor's edits, what a Groove device and a clip play, the default groove, the user's groove files); `grooveFilesDir`, `grooveToMap` |
| [RenderProgress.h](../../app/src/session/RenderProgress.h), [Renders.h](../../app/src/session/Renders.h) | The render progress the UI shows as a modal dialog; the renders in the background (`ExportAudioRender`, `FreezeTracksRender`, `ReverseClipsRender`) |
| [ComputerKeyboard.h](../../app/src/session/ComputerKeyboard.h) | The computer MIDI keyboard |
| [ModulationLearn.h](../../app/src/session/ModulationLearn.h) | A modulator's Map: the next parameter touched on its track is mapped to it |
| [AudioPreferences.h](../../app/src/session/AudioPreferences.h), [MidiPreferences.h](../../app/src/session/MidiPreferences.h) | Preferences › Audio and › MIDI, without their widgets |
| [PluginHostingPreferences.h](../../app/src/session/PluginHostingPreferences.h) | Preferences › Plug-ins › Plug-in Hosting, without its widgets: where plug-ins run |
| [DisplayPreferences.h](../../app/src/session/DisplayPreferences.h) | Preferences › Look and Feel › Maximum display frame rate, without its widgets: how often the live displays refresh |
| [SessionSupport.h](../../app/src/session/SessionSupport.h) | Small helpers the parts share (plug-in refs from QML maps, clip refs for QML) |
| [files/FileManager.h](../../app/src/files/FileManager.h), [files/HotSwap.h](../../app/src/files/HotSwap.h) | The File Manager and hot swaps ([below](#the-file-manager-and-hot-swaps-filemanager-hotswap)); what they work from: [files/ProjectFiles.h](../../app/src/files/ProjectFiles.h) (the files a project plays, and what plays each) and [files/MissingFiles.h](../../app/src/files/MissingFiles.h) (finding missing files again) |

## What it owns

`Session(engine, options)` makes, in order: the `Project`, the `QUndoStack` (its history the latest `kUndoLimit`,
500, steps: older ones go as new ones come; set while it is empty, as Qt requires), the `ProjectEditor`, the `Selection`, the
`EngineBridge` (on the engine it is given, which outlives it), the `PluginHostingPreferences` and the
`DisplayPreferences` (which the bridge follows from the start), the `PluginIndex`, the `SoundSimilarity` (which
analyses the browser's files: [intelligence.md](../intelligence.md)), the `Harmony` (the song's chords and key), the
`Humanizer`, the `TrackLabels` (what each track is, in words), the `TrackSearch` (finding tracks by their names and labels), the `BrowserController` (which starts the plug-in
scan, and points the sound similarity at its index), the `PresetIndex`, the `RenderProgress`, and the parts `ArrangementActions`, `DeviceSelection`,
`GrooveLibrary`, `ComputerKeyboard`, `ModulationLearn`, `HotSwap`, `FileManager`, `AudioPreferences`, `MidiPreferences`, the `Updater`, and last the `McpServer` (its tools
read the parts above). They are its children, and QML
reads them as constant properties:

| Property | Type | What it is |
|---|---|---|
| `project`, `undoStack`, `editor`, `selection`, `bridge` | `Project`, `QUndoStack`, `ProjectEditor`, `Selection`, `EngineBridge` | [model.md](model.md), [engine-bridge.md](engine-bridge.md) |
| `browser`, `plugins` | `BrowserController`, `PluginIndex` | the browser ([browser.md](../browser.md)); the plug-ins, also Preferences › Plug-ins ([plugin-scanner.md](plugin-scanner.md)) |
| `similarity` | `SoundSimilarity` | finding similar sounds ([intelligence.md](../intelligence.md)); the browser's Find Similar uses it |
| `similarSamples` | `SimilarSamples` | the Sampler's and the Drum Rack's similar-sound arrows: stepping Samplers through the sounds most like their references ([intelligence.md](../intelligence.md#swapping-in-similar-samples)) |
| `audioAnalysis` | `AudioAnalysis` | each audio file's tempo, key and chords, heard in the background ([intelligence.md](../intelligence.md#tempo-key-and-chords-from-audio-the-application-side)): dropped audio warped at its tempo, chords along audio clips |
| `harmony` | `Harmony` | the song's chords and key, from its MIDI and its audio clips' chords ([intelligence.md](../intelligence.md#harmony-the-application-side)); the piano roll shows them and Generate writes from them |
| `trackLabels` | `TrackLabels` | what each track is, in a few words, and what that was made from ([intelligence.md](../intelligence.md#track-labels-the-application-side)); the tooltip over a track's name, and what the MCP server tells agents of each track |
| `mcp` | `McpServer` | the MCP server agents read and edit the project through ([mcp.md](mcp.md)); its `statusMessage` is the session's |
| `agentChanges` | `AgentHistory` | what agents changed through it, apart from the undo stack: each change rewound and restored on its own (View › Agent Changes); `undo()` and `redo()` refuse to undo over an agent's change ([mcp.md](mcp.md#the-agents-history)) |
| `trackSearch` | `TrackSearch` | the arrangement's find bar (Q): the tracks matching a query, names first, then labels ([intelligence.md](../intelligence.md#finding-tracks)) |
| `arrangement` | `ArrangementActions` | [below](#the-arrangements-actions-arrangementactions) |
| `deviceSelection` | `DeviceSelection` | [below](#the-device-view-deviceselection) |
| `grooves` | `GrooveLibrary` | [below](#the-projects-grooves-groovelibrary) |
| `render` | `RenderProgress` | [below](#renders-in-the-background) |
| `cpuUsage` | `CpuUsage` | View › CPU Usage: each track's and device's share of a core, in a list model; while `active` the engine times them (`EngineBridge::setMeasuringCpu`, `Engine::takeCpu`) |
| `computerKeyboard` | `ComputerKeyboard` | [below](#the-computer-midi-keyboard) |
| `modulationLearn` | `ModulationLearn` | a modulator (an LFO, an Envelope) waiting for the next parameter touched on its track (`ProjectEditor::parameterTouched`) to be mapped to it: `start`, `stop`, `toggle`, `isLearning`, `active`; its own controls don't count, it says why for another track's or a switch, and it stops once it maps one, its modulator goes or the project is replaced ([ui/device-view.md](../ui/device-view.md#modulators)) |
| `files`, `hotSwap` | `FileManager`, `HotSwap` | [below](#the-file-manager-and-hot-swaps-filemanager-hotswap) |
| `audioPreferences`, `midiPreferences` | `AudioPreferences`, `MidiPreferences` | [below](#preferences) |
| `pluginHosting` | `PluginHostingPreferences` | [below](#preferences) |
| `displayPreferences` | `DisplayPreferences` | [below](#preferences) |
| `updates` | `Updater` | Preferences › Updates: [updates.md](updates.md); an update found also goes out on `statusMessage` |

`Session::Options` is for the tests: `scanner` (the plug-in scanner's program; empty: `substation-scan` beside the
application), `scanPlugins` (scan at once), `browserIndex` (keep the browser's index and the sounds' fingerprints on
disk) and `analyseSounds` (fingerprint the browser's files in the background).

`setOwnerWindow(handle)` (from `ui/main.cpp`) gives the bridge the main window's native handle, which plug-in editors
and ASIO drivers' dialogs belong to. `start()` (queued by `ui/main.cpp` once the window shows) starts audio
(`EngineBridge::startAudio`: the render threads, the MIDI inputs, the saved device). `shutdown()` (after the event
loop, or when the session goes) aborts a render that runs, stops the transport and the preview, closes the plug-in
editors, ends a hot swap and a search for missing files, stops the browser's threads, then the sound similarity's (saving the fingerprints), closes the device and shuts the bridge down (the plug-ins unload while the
application is still whole).

## Wiring

The session connects its parts to each other in `Session::wire()`:

- **The editor's hooks**: `setParamInfo` ← `EngineBridge::deviceParamInfo`, made a `ParamSpec` (`ParamSpec::fromInfo`),
  for macros; `setOwnValue` ← `EngineBridge::ownValue`; `setDeviceDefaults` ← `defaultDevice` (the user's default
  presets, [io/Presets.h](../../app/src/io/Presets.h)); `setTransients` ← `EngineBridge::transients` (audio files'
  transients, for audio clips' grooves and Extract Groove). The agents' editor gets the same.
- **Messages**: the bridge's, the browser's, the plug-in index's, the arrangement's, the device view's, the computer
  keyboard's, the grooves', the humanizer's, the File Manager's and the hot swap's `statusMessage`, and the editor's `refused`, all go
  out on the session's `statusMessage`.
- **Plug-ins**: the plug-in index's results go to `EngineBridge::setKnownPlugins` (so projects find plug-ins that
  moved); the editor's `pluginAdded` opens the plug-in's editor on the next turn of the event loop (the add, a drop,
  has finished, and the track may be selected just after), if its track is still there; a plug-in editor's
  `pluginParamEdited` becomes `ProjectEditor::setDeviceParam` with the merge key
  `"plugin edit|<device id>|<param id>|<gesture>"` (one undo step per knob drag), `pluginParamTouched` becomes
  `touchParameter`; `pluginStateDirty` calls `QUndoStack::resetClean()` (the project has changes no edit shows).
- **The selection**: when it changes, only the selected track's plug-in editors show
  (`EngineBridge::showPluginEditors`) and its plug-ins load first (`prioritizePlugins`). The selection drops what no
  longer exists whenever tracks, clips or automation change (`Selection::prune`), and a parameter changed by hand (or
  taken hold of: `parameterTouched`) shows its automation lane, if it can be automated.
- **Recording**: `takesRecorded` → `ProjectEditor::addRecordings(takes, recordQuantize(), recordQuantizeEnds())`, one undo step; the new
  clips are selected.
- **The browser**: previews go to the bridge (`previewFile`, `stopPreview`), but while a hot swap runs and the
  transport plays (what is selected is heard in the song); what it activates (a double-click, Enter) goes on the
  selected track (`addFileAtInsert`, `addDeviceToSelectedTrack`, `addPluginToSelectedTrack`,
  `addPresetToSelectedTrack`); its presets are the `PresetIndex`'s, listed again when the library changes. While a hot
  swap runs, an audio file the user chooses there (`fileChosen`: clicked, or reached with the arrow keys) is swapped
  in (`HotSwap::swap`), and one activated is kept instead of added (`HotSwap::commit`).
- **State for the UI**: `automationOverridden` follows `EngineBridge::automationStateChanged`; `pluginsLoaded`,
  `pluginsTotal` and `pluginsLoadingText` follow `pluginsLoading`; `clean` and `title` follow the undo stack's clean
  state and the project's path.
- **Renders**: the arrangement's reversing starts its renders through the session (`startRender`), so there is one
  render at a time.

## The QML-facing API, in outline

The full list, with what each does, is in [Session.h](../../app/src/session/Session.h).

| Area | Properties and invokables |
|---|---|
| Window | `title` ("<project name>[*] - SUBstation"), `clean`, `statusTimeout` (8000 ms), `aboutTitle`, `aboutText`, `requestClose()` |
| Transport | `togglePlay()` (Space: plays from the insert marker; again, stops and goes back to where playback started), `toggleRecord()` (F9: records the armed tracks from the insert marker after the count-in, or from the playhead while playing; again, stops recording while playing goes on), `stop()` (the stop button; stopped, it goes to the start), `locate(beat)`, `countInBars` (0, 1, 2 or 4) with `countInChoices`, `recordQuantize` with `recordQuantizeChoices`, `automationOverridden` |
| Create | `insertAudioTrack()`, `insertMidiTrack()` (after the selected track, in its group; selected), `insertReturnTrack()`, `insertMidiClip(gridStep)`, `groupSelected()`, `ungroupSelected()` (tracks, or devices while the device view has the focus), `deleteSelectedTracks()` |
| Adding | `addDeviceToSelectedTrack(kind)`, `addPluginToSelectedTrack({format, uid, name, vendor, path, instrument})`, `addPresetToSelectedTrack(path)` (an instrument with no MIDI track selected: on a new MIDI track), `addFileAtInsert(path)`, `presetSaved(path)` |
| Edit | `cut()`, `copy()`, `paste()`, `duplicate()`, `whatIsCopied(verb)`, `deleteSelection()`, `split()`, `selectAll()`, `consolidate()`, `reverseClips()`, `toggleClipActivation()`, `soloSelectedTracks()`, `armSelectedTracks()`, `renameTarget(browserListFocused)` |
| Freezing | `toggleFreeze()` (Ctrl+Shift+F), `flattenSelectedTracks()`, `freezeActions(ids)` (a track menu's texts and states), `freezeTracks(ids)`, `unfreezeTracks(ids)`, `flattenTracks(ids)` |
| Files | `newProject()`, `openProject(path)`, `saveProject()`, `saveProjectAs(path)`, `suggestedSavePath()`, `recentProjects`, `recentProjectAvailable(path)`, `recentMenuItems()`, `clearRecentProjects()`, `recoverableBackups()`, `recoverBackup(path)`, `dismissRecovery()`, `backupMenuItems()`, `backupsFolder()`, `lastFolder`, `projectFilter`, `projectExtension`, `confirmDiscardText`; `saveAsTemplate()`, `clearTemplate()`, `hasTemplate`, `templatePath()`; `importLiveSet(path)`, `liveSetFilter` |
| Export | `exportRangeChoices()` ("arrangement", "loop" while the loop is on, "selection" while there is a time selection), `exportProblem(range)`, `suggestedExportPath(fileType)`, `exportBitDepthChoices`, `defaultExportBitDepth` (24), `exportFileTypeChoices` (WAV, MP3), `exportBitrateChoices`, `defaultExportBitrate` (320), `exportAudio(path, range, bitDepth, fileType, bitrate)` |
| Plug-ins loading | `pluginsLoaded`, `pluginsTotal`, `pluginsLoadingText` ("Loading plug-ins: 1 of 3") |

Signals: `statusMessage(text)` (the status line), `warning(text)` and `information(text)` (what were message boxes:
the UI shows them), `saveAsRequested()`, `projectOpened()` (the arrangement shows all of the project), and the
properties' notifications.

The Edit commands act on what has the focus (`Selection::focus()`): the device view's selected devices while it has
it (`DeviceSelection`), else the arrangement: selected breakpoints, a lane range's automation, a clip range, or the
selected tracks (`ArrangementActions`). `whatIsCopied()` says which ("devices", "automation", "clips", "tracks", or ""
with a status message saying why not, for breakpoints, returns and the master).

`renameTarget()` (Ctrl+R) says what to rename, for the UI to edit in place: the preset current in the browser's list
(if the list has the focus), the rack chain last clicked in the device view, or the track (return) last clicked:
`{kind: "preset", path, name}`, `{kind: "chain", trackId, rackId, chainId, name}` or `{kind: "track", trackId, name}`;
`{}` if none (a status message says so). The UI then calls `browser.renamePreset`, `editor.renameChain` or
`editor.renameTrack`.

## Files and unsaved changes

The session never asks the user anything; the UI does, and then calls it:

- **New, Open, Open Recent and Quit**: if the project isn't `clean`, the UI asks `confirmDiscardText` ("Save changes to
  the current project?": Save, Discard, Cancel). On Save it saves (below) and goes on only if that returned true. For
  Open Recent it first calls `recentProjectAvailable(path)`: a file that is gone is taken off the list, with a
  `warning`, and nothing opens.
- **Save** (Ctrl+S): `saveProject()` saves to the project's file and returns true; for a project never saved it emits
  `saveAsRequested()` and returns false, and the UI asks where (starting at `suggestedSavePath()`:
  `<last folder>/Untitled.gilproj`) and calls `saveProjectAs(path)`. Saving stores the plug-ins' states first
  (`EngineBridge::storePluginStates`), writes the file ([serialization.md](serialization.md)), marks the undo stack
  clean, remembers the folder and the file (the recent projects), and says "Saved <file>". A file that can't be
  written is a `warning`.
- **Open**: `openProject(path)` loads the file (one that can't be read: a `warning`, and the open project stays as it
  was), clears the undo stack, the selection and the insert marker, remembers the folder and the file, emits
  `projectOpened()` and says "Opened <file>", and how many of the files it plays are missing ("Opened song.gilproj: 2
  files are missing (the File Manager finds them)"; the UI shows the File Manager then). Its plug-ins load after it
  shows ([engine-bridge.md](engine-bridge.md#opening-a-project)).
- **New**: `newProject()` opens the template if one was saved (`hasTemplate`), else empties the project; then it
  clears the undo stack and the selection. A project from the template is everything the template holds, untitled (no
  file: Save asks where); a template that can't be read is a `warning`, and the project starts empty. The
  application opens a new project this way as it starts, unless it was given a project to open.
- **Import Ableton Live Set**: `importLiveSet(path)` reads and translates the set
  ([live-import.md](live-import.md)), with the plug-in index's plug-ins, and loads it as `loadInto` loads a
  project file (a set that can't be read: a `warning`, and the project stays). It then goes on as opening does
  (`loadedFrom`: a session of its own, the folder remembered, the files looked at, `projectOpened()`), names the
  untitled project after the set (`title()`, `suggestedSavePath()`) and leaves it unsaved (`QUndoStack::resetClean`:
  not `clean` until saved, so New, Open and Quit ask), says "Imported song.als: 12 tracks, 80 clips" and, if anything
  didn't come across as it was, lists it in an `information`. Running out of memory reading it is a `warning` too.
- **Templates**: `saveAsTemplate()` stores the plug-ins' states, then writes the project to `templatePath()`
  (`Template.gilproj` in the local data folder, or `SUBSTATION_TEMPLATE`) without making it the project's file: the
  project keeps its file and its unsaved changes. Its files are kept by their absolute paths
  ([serialization.md](serialization.md)). `clearTemplate()` deletes it: new projects start empty again. Both say so in
  the status line and emit `templateChanged()`.
- **Close**: `requestClose()` is false while a render runs: it cancels the render instead, and the window stays (as
  the render dialog's Cancel would). Otherwise the UI asks about unsaved changes as above.
- **Recent projects**: at most `kMaxRecent` (10), the latest first, each stored absolute with links resolved, in the
  system's form; the same file written another way (`pathIdentity`: in another case too, where the system ignores
  case) replaces it. `recentMenuItems()` gives the
  Open Recent menu's entries (`"&1  song.gilproj"`, with `&` doubled in names). A list of one, which QSettings can hand back
  as a single string, is read as a list too.
- **Backups** ([Backups.h](../../app/src/session/Backups.h); `Options::backups`, off in the tests): while the session
  isn't `clean`, the project is written every `Backups::kIntervalMs` (a minute) after a change (the undo stack's
  `indexChanged`, an agent's change), to a folder of the project's own under `Backups::defaultFolder()` (`Backups` in the local data folder, or `SUBSTATION_BACKUPS`): "<name>
  <8 hex digits of its path's MD5>" for a saved project, "Untitled <session id>" for one never saved. A backup is the
  project as Save writes it (relative paths to the project's own file), named by when it was made
  (`2026-10-16 14-03-22.gilproj`), plus `backup_of` (the project's file, "" for none) and `backup_name`. The newest
  `kKeep` (20) of a folder stay. The timer's backups don't hold up the window (`backUpInBackground()`): the plug-ins'
  states are gathered first without waiting for them (`setGatherBeforeBackup`: the bridge's `gatherPluginStates`,
  which asks again only for those something changed: [engine-bridge.md](engine-bridge.md#plug-in-states)), then the
  project is turned into JSON on the GUI thread (`projectToJson`), and encoded, written and the oldest pruned on a
  thread of its own (a `QThreadPool` of one); `backedUp(path)` comes back on the GUI thread, and the session's mark
  is written then. One at a time; one under way when the project is saved (or the session finishes) is let go of
  (written, if it was being, but not marked). A write that fails is tried again at the next interval. `backupNow()`
  does it all at once, the plug-ins' states stored as Save stores them (the tests'). Saving, opening, New and
  importing mark the project as its file has it (`Backups::saved()`: nothing to back up or recover); an import,
  untitled and unsaved, is backed up as it changes.
  Each running session keeps a mark in `sessions/`: `<id>.lock` (a `QLockFile` it holds, with no stale time: only a
  session whose process is gone lets go) and `<id>.json` (`backup`: its latest backup while unsaved, else "";
  `project`, `name`). `start()` (from `Session::start()`) takes the marks whose locks are free (sessions that ended
  without `finish()`: they crashed), keeps those with a backup as `recoverableBackups()` (the newest first), removes
  the marks and emits `recoveryAvailable()`; the UI asks "Recover them from its backup of …?". `recoverBackup(path)`
  loads a backup, gives the project the file it was of (`Project::setPath`: Save writes there; none for one never
  saved, named after `backup_name`), starts a session of its own as opening does, leaves it unsaved
  (`resetClean`) and says "Recovered song from its backup of …: save it to keep it". `shutdown()` calls `finish()`,
  removing the session's mark (its backups stay). `backupMenuItems()` is File › Backups (the open project's backups,
  the newest first), `backupsFolder()` its Show Backups Folder.

## The selection (Selection)

`Selection` ([Selection.h](../../app/src/session/Selection.h)) is what is selected in the arrangement: the selected
track (or tracks, a return track, or the master), the insert (start) marker, and a time selection: a beat range over
one or more adjacent tracks. Selecting is always on the grid: selecting a clip selects the area it covers.

- A time selection made in the clips' band (or by clicking clips) is a *clip range*: `clips()` holds the clips it
  touches (for the clip view), and Delete cuts out just that range. Made lower in the lanes it is a *lane range*;
  made in automation lanes, `lanes()` holds them (owner, key), and Delete clears their automation in the range.
- A time selection acts on `rangeTrackIds()` (its `TimeRange::trackIds`): the tracks it covers and what is in the
  groups among them. `rangeRows()` are the tracks whose rows it covers, top to bottom, where the arrangement shows it
  (`setTimeRange(..., rows)`; by default all of its tracks): a selection made over a group's row acts on the group's
  tracks too, but shows only over the rows it was made over.
- Automation breakpoints can be selected too (`points()`: owner, key, indices); Delete deletes them.
- Several tracks can be selected (`selectTrack(id, focusTrack, mode, order)`: `Mode::Toggle` for Ctrl-click,
  `Mode::Range` for Shift-click from the last one clicked); `trackIds()`. `trackId()` is the one the device view
  shows, the last one clicked.
- `focus()` (`Focus::Clips`, `Track`, `Devices`, `Automation`) is what Delete, Cut and Copy act on.
- `prune(project)` drops what no longer exists (the session calls it whenever tracks, clips or automation change).

## The arrangement's actions (ArrangementActions)

`ArrangementActions` (`Session.arrangement`, [ArrangementActions.h](../../app/src/session/ArrangementActions.h)) acts on
the arrangement's selection. The arrangement view calls it (a paste at a clicked beat on a clicked track, its context
menus, drops), and the session dispatches the Edit menu's commands to it.

- **One clipboard** holds the last thing copied or cut (Ctrl+C / Ctrl+X): clip content from a clip range
  (`ClipboardContent`), automation from a lane range (`CopiedAutomation`), or tracks (`CopiedTracks`; their plug-ins as
  they were: their states are stored first). Ctrl+V pastes it (`paste()`, or `paste(atBeat, trackId)`;
  `pasteAutomationAt(owner, key)`). A cut that was refused (nothing taken out) leaves the clipboard as it was.
  `hasClipboard`, `clipboardKind`.
- An area command the editor refuses (a stretch of only some of a frozen group, a paste into a frozen track of what
  wasn't copied from it: see [model.md](model.md#freezing)) changes nothing, the selection included; the editor's
  `refused` says why in the status line. What it selects afterwards keeps the rows the selection showed over.
- `deleteArea()`, `duplicateArea()`, `copyArea()`, `cutArea()`, `copyAutomation()`, `cutAutomation()`,
  `copyTracks(ids)`, `cutTracks(ids)`, `duplicateTracks(ids)`, `consolidate()`, `splitAt(beat)`, `canConsolidate()`,
  `canReverse()`, `insertMidiClip(trackId, beat, gridStep)`, `insertTrackAfter(trackId, midi)`.
- **The selected MIDI clips' notes** (a clip's menu's *MIDI Notes*): `selectedMidiClips()` (the MIDI clips among
  `Selection::clips()`, by track, then time), and on every note of each, one undo step through
  `ProjectEditor::setClipsNotes` (the notes placed with `notes::place`, as the piano roll's tools place them):
  `transposeNotes(semitones)` (all together, held by `notes::clampMove` so none leaves the keyboard; none can move:
  the status line says so), `legatoNotes()` (`notes::legato` in each clip, to its own next notes and its end) and
  `quantizeNotes(step)` (`notes::quantized` on the song's grid: each note where it plays on the timeline, and back).
- **Deactivating** (0, `toggleActivation()`): the clips in the selected area, audio and MIDI, deactivated
  (`ProjectEditor::setRangeActive`: just the stretch of each inside it, one undo step), or activated again if they
  all are; frozen tracks' clips stay as they are (their frozen audio holds them). `activates()` says which (none:
  nothing to change), for the menu; `canToggleActivation()` and `areaDeactivated()` are its QML forms.
- **Reversing** (R, `reverseSelection()`) writes a reversed copy of each file once (as Ableton does): less than
  `kReverseInPlaceSeconds` (30 s) of audio in all is written at once; more in the background (`ReverseClipsRender`),
  followed in the session's render progress: Cancel makes none, and nothing is reversed. Then
  `ProjectEditor::reverseRange` turns the clips to the copies, one undo step.
- **Drops** on the lanes: `dropSources(paths)` (what a drag of files would add: name and length, for the drop's
  preview), `dropFiles(paths, trackId, beat)`, `dropDevices(kinds, plugins, trackId)` (`""`: below the tracks, a new
  track), `dropPresets(paths, trackId)`, `dropMovedDevices(fromTrack, ids, toTrack)`.
- `clipViewRequested(refs, leadTrackId, leadClipId)` asks the UI to open clips in the clip view (a MIDI clip inserted,
  a clip double-clicked).

## The device view (DeviceSelection)

`DeviceSelection` (`Session.deviceSelection`, [DeviceSelection.h](../../app/src/session/DeviceSelection.h)) is the
device view without its widgets. It shows the selected track's chain (`trackId`: a track, a return or the master), and
after each rack, unless it is folded or hides its devices, the chain it shows (the one last clicked in its chain list:
`shownChain()`, else its first), and so on inside: `shownDevices`. A frozen track (or one in a frozen group) shows none
(`hint`). A press on a clip in the arrangement holds the track shown (`holdTrack()`) while the selection moves to the
clip, so the clip can be dragged onto a Sampler or a Drum Rack's pad there; `letGoTrack()` (the release) follows the
selection again, `letGoTrack(true)` (the clip dragged out) selects the track shown instead (`Selection::selectTrack`,
the clips staying selected). `toggleChainList(rack)` and `toggleRackDevices(rack)` show or hide a rack's chain list (hidden by default)
and its chain's devices (shown by default): view state (`ProjectEditor::setChainListShown`, `setRackDevicesShown`).
Clicking a chain (`clickChain`) shows its rack's devices again.

- **Selecting**: a click selects a device, Ctrl adds or removes it, Shift selects the range from the last one clicked
  (`anchor`); a selection is of one chain's devices. A press on a device already selected keeps the others (to drag
  them all), and if no drag follows, the release selects just it (`press()`, `release()`). Selecting devices focuses
  the device view (`Selection::focusDevices`: Delete and the clipboard act on them); selecting anything else
  deselects them. `selected` is in chain order. `modifiers` arguments are `Qt.KeyboardModifiers` as an int.
- **The clipboard** is the device view's own: the devices copied or cut (plug-ins in their state then), and which of
  them were folded: their copies are folded too. `copySelected`, `cutSelected`, `paste()`, `pasteAt(chain, index)`,
  `pasteAfter(id)`, `duplicateSelected`.
- **Racks**: `groupSelected`, `groupDevice`, `ungroupSelected`, `ungroupRack`, `toggleFold`, `clickChain`.
- **Presets**: `savePreset(id, name)` (after `checkPresetName(id, name)`: an error, or the question before replacing a
  preset of that name), `saveAsDefault`, `clearDefault`, `hasDefault`, `insertPreset`, `loadPresetInto`,
  `presetLoadsInto`, `loadPresetFile` (*Load Preset…*, starting in `presetFolder()`). A plug-in's own `.vstpreset`
  files (a VST2's `.fxp` and `.fxb`) are loaded (`loadVst3Preset`: an undoable `SetDeviceStateCommand`) and saved here
  too, starting in `vst3PresetFolder()`; `pluginPresetSuffix()` says what a save is called after. A Vital's own `.vital` presets load (`loadVitalPreset`: as `loadPresetInto`) and save
  (`saveVitalPreset`) here, starting in `vitalPresetFolder()`. The folders last used are remembered
  ([Settings](#settings)).
- **Drops**: `dropMoved(ids, chain, index)`, `dropDevices(kinds, plugins, chain, index)`,
  `dropPresets(paths, chain, index, intoDeviceId)` (onto a device of the preset's kind: loaded into it), `dragEnded()`.
- Texts the view shows: `kEffectsHint`, `kInstrumentHint`, `kInstrumentRefused`.

## The project's grooves (GrooveLibrary)

`GrooveLibrary` (`Session.grooves`, [GrooveLibrary.h](../../app/src/session/GrooveLibrary.h)) is the project's grooves
([model.md](model.md#grooves)) for the UI: the groove dialog (*Edit › Grooves…*), the Groove device's editor, the
clip view's Groove, the piano roll's Groove menu, *Edit › Default Groove* and the transport's Swing
([ui/README.md](../ui/README.md)). It reads the project and edits it through the editor, each edit undoable.

- **Properties**: `grooves` (the library in order, `[{id, name}]`: `groovesChanged`), `builtins` (the built-in
  grooves, `[{id, name}]`), `files` (the user's groove files, `[{path, name}]` by name: `filesChanged`), `stepNames`
  ("1/8", "1/8T", "1/16", "1/16T", "1/32"), and the default groove: `defaultKind` ("": none, "swing", "library",
  "own"), `defaultSwing` (50: none), `defaultGrooveId` ("": none) and `defaultName` ("None", "Swing 58.0 % (1/16)",
  a groove's name), notified by `defaultChanged` (the library changing too: a name). `revision` counts the project's
  changes that can change what plays a groove (devices, their parameters and states, clips, the library, the
  default, a reset; `revisionChanged`): bindings on `deviceGroove()` and `clipGroove()` read it to be worked out again.
- **A groove, for QML** (`grooveToMap`): `{id, name, step (beats), stepIndex (of stepNames; -1: none of them), bars,
  base (beats), steps (in a cycle), lanes: [{keys, steps: [{timing, ms, velocity, length}]}], notes: [{beat, key,
  velocity}]}`: `groove(id)` (a library groove's), `builtinGroove(id)`, `swingGroove(percent, stepIndex)`.
- **What plays one**: `deviceGroove(track, device)` is `{kind, grooveId, name, groove}` (Swing's from the device's
  Swing and Step, a library groove's and its own from the copy in its state, Project's the default's);
  `clipGroove(track, clip)` is `{kind ("": none), grooveId, name, swing, stepIndex, timing, velocity, random,
  quantize (fractions), groove}`. `find(idOrName)`: the library's groove named so, by id, else by name ignoring case
  ("": none).
- **The Groove device**: `setDeviceSource(track, device, kind, grooveId)` (`ProjectEditor::setDeviceGrooveSource`),
  `applyDevice(track, device)` (`applyDeviceGroove`; nothing to move: the status line says so), `addFromDevice(track,
  device)` (the groove it plays into the library, named "<track> Groove", and the device then playing it: one undo
  step, "Add to Grooves").
- **Into the library**, each giving the new groove's id ("": none): `addBuiltin(id)`, `addFile(path)` (a `.gilgroove`
  or `.agr`, `loadGrooveFile`; one that can't be read: the status line says why), `addSwing(percent, stepIndex)`
  (named as `describe()` shows it), `extract(track, clip, stepIndex, bars, perKeyLanes)`
  (`ProjectEditor::extractGroove`; nothing to take it from: the status line says so). And `duplicate(id)`,
  `rename(id, name)`, `remove(id)`.
- **The user's groove files** are the preset library's `Grooves` folder (`grooveFilesDir()`: `libraryDir()/Grooves`),
  `.gilgroove` and `.agr` files, which the browser lists with the presets
  ([serialization.md](serialization.md#groove-files-gilgroove-agr)). `saveFile(id)` writes a library groove there,
  named as it is (a file of that name replaced), and says so; `refreshFiles()` lists them again (the dialog does as
  it opens).
- **The groove editor's edits**, each through `ProjectEditor::setGroove` on the groove changed:
  `setStep(id, lane, step, field, value, mergeKey)` (`field`: "timing" (-0.5..0.5 of a step), "ms", "velocity"
  (-1..1), "length" (0.25..2); one merge key per drag, one undo step), `addLane(id, keys)` (its steps the default
  lane's), `setLaneKeys(id, lane, keys)` (taken from the other lanes first), `removeLane(id, lane)` (neither for the
  default lane), `resetLane(id, lane)` (every step straight), `setBars(id, bars)` (1, 2 or 4: the steps there were
  repeated, or cut), `setBase(id, beats)`.
- **The default groove**: `setDefaultSwing(percent, mergeKey)` (50: none), `setDefaultGroove(id)` ("": none),
  `clearDefault()`.

## The File Manager and hot swaps (FileManager, HotSwap)

The files a project plays are worked out from the project each time, never stored:
[ProjectFiles.h](../../app/src/files/ProjectFiles.h)'s `projectFiles(project)` walks the tracks, the returns and the
master, each one's audio clips (the file a clip plays: a reversed clip's reversed copy), then its take lanes' audio
clips (`Track::takes`), then its devices, in racks too: a built-in device's state names a file under `"sample"`
(`deviceFile`, `withDeviceFile`; the Sampler's). It groups them as the system compares paths (`pathIdentity`) and
keeps each file's uses (`FileUses`: `ClipRef`s, `TakeClipRef`s and `DeviceRef`s). Frozen tracks' own audio isn't
among them. `changeableUses` leaves out what is on frozen tracks (or in frozen groups), which a replacement may not
change; take lanes' clips stay in (they don't play, so frozen audio doesn't hold them).

Putting one file in another's place is the editor's (`EditorFiles.cpp`, [model.md](model.md)):
`ProjectEditor::replaceFile(uses, path, seconds, text, mergeKey)` changes the clips (each as `edits::replaceFile` has
it, then `edits::fitToTempo` trims one running into the next), the take lanes holding them (whole lists, as
`Project::setTakeLanes` takes them) and the devices' states in one `ReplaceFilesCommand`;
`relinkFiles({old: new})` does it for files found somewhere else (the same audio: `edits::relinkFile`, also what a
reversed clip was reversed from; frozen tracks take it).

**`FileManager`** (`Session.files`, [FileManager.h](../../app/src/files/FileManager.h)) is the panel's logic:

- `files` is a `FileListModel` (roles `path`, `name`, `folder`, `missing`, `uses`, `frozen`; `get(row)` gives them by
  name: `rowMap()`, [ListModels.h](../../app/src/ListModels.h)): the missing first, then
  by name, filtered by `filter` (every word in the name or the folder). `fileCount`, `missingCount` and `summary`
  ("12 files, 2 missing") count every file. It follows the project after each change (a zero-length timer:
  `update()` once per turn of the event loop; `update()` at once from `openProject`). Whether a file is there is a
  `QFileInfo::isFile()` kept per file: looked at again on `refresh()` (the panel showing), and for a file the engine
  decoded or failed to (`sourceReady`, `sourceFailed`): the engine's memory is no proof a file is on disk.
- `search()` and `searchFolder(folder)` run a `missing::MissingFileSearch` on a thread of its own: it collects the
  files with a missing file's name from the browser index's latest snapshot (in memory) and from folders walked on
  disk (the project's, or the one chosen; 16 deep, no names starting with "." or "$", no symbolic links to folders),
  then `missing::match` picks for each the candidate sharing the most folders with it, from the file up
  (`bestMatch`), and where the files found went tells where the others did (`moveAlong`). Its result comes back as a
  queued call; a search started meanwhile, `cancelSearch()`, or another project drop it. What was found for files
  still missing goes through `relinkFiles` ("Locate Missing Files"), and the status line says how many.
- `locate(path, found)`: one missing file pointed at by the user, and the others `moveAlong` finds from it.
- `replace(path, with)`: an audio file in its place wherever it plays (`changeableUses`: frozen tracks' are left, and
  said so), its length from `EngineBridge::fileInfo`.
- `hotSwap(path)`, `selectClips(path)`, `findSimilar(path)`, `showInFolder(path)`, `actions(path)` (a row's menu),
  `dialogFolder(path)` (where Replace… and Locate… start), `reveal(path)` (Show in File Manager from an audio clip's
  menu: `revealRequested`, the UI shows the panel and the row; a filter hiding the file is cleared).

**`HotSwap`** (`Session.hotSwap`, [HotSwap.h](../../app/src/files/HotSwap.h)) links the browser to a file's uses:
`startClip(ref)` (an audio clip's menu) takes every clip and sampler of the clip's track playing its file, and
`startFile(path)` (the File Manager) every one in the project (but frozen tracks', either way), and ask the browser for the sounds most like it (`BrowserController::findSimilar`; for
a clip, like the part of its file it plays). While `active`, `swap(path)` (the browser's `fileChosen`) puts each file
chosen in their place through `replaceFile` with a merge key of the hot swap's own, so all of it is one undo step,
worked out from the clips as they were before it began; back on the file it began with, the merged command is
obsolete and leaves the stack. `commit(path)` (the browser's double-click or Enter) swaps and ends; `stop()` ends.
It never outlives what the user is doing with it: the undo stack moving but for its own swaps (any other edit, an undo,
a redo) ends it, and so do `Project::reset` and none of what it swaps being left; the browser panel ends it when a drag starts
there (a press elsewhere leaves it running), so a sample dragged out while
one runs is added, never swapped in. `path`, `name`, `originPath` and `usesText` are for the browser's bar and the
File Manager's lit button.

## Renders in the background

Exporting, freezing and reversing long files render on threads of their own while the window goes on (it repaints, its
meters move) but takes no edits: the render is of the project as it was when it started. One render runs at a time
(`startRender` refuses another).

The renders ([Renders.h](../../app/src/session/Renders.h)) are state machines driven by their tasks' signals
(`RenderTask::progressChanged`, `ended`: [engine-bridge.md](engine-bridge.md#renders-in-the-background-rendertask))
and by Cancel. There are **no nested event loops**: nothing waits in a local loop for a render to end, so nothing can
re-enter the session meanwhile. Each render:

1. shows itself in the session's `RenderProgress` (`begin(title)`);
2. waits, if it must, until every device is ready to render offline (a project's plug-ins still loading, samples) and
   every audio file is decoded (a project just opened, a new sample rate: their clips aren't heard until then): a busy
   bar meanwhile, saying what it waits for, polling `EngineBridge::devicesReady()` and `sourcesLoading()` every 30 ms
   (the bridge goes on loading them meanwhile);
3. starts its task(s) and follows each, its part of the progress bar, until it ends;
4. finishes it (`finishExport`, `finishFreezes`, `finishReversed`), says what came of it (`statusMessage`, `warning`),
   ends the progress, and deletes itself.

| Render | What it does |
|---|---|
| `ExportAudioRender` | the arrangement (the loop, the time selection) into a WAV or MP3 file, once the devices are ready (`Session::exportAudio`; playback stops first) |
| `FreezeTracksRender` | several tracks in one render (`startFreeze(tracks)`: a file for each, "Freezing A and B…"), then frozen in one undo step: all or nothing (cancelled, or failing, none is frozen and the renders are deleted; a render the editor then refuses, its track gone meanwhile, is deleted too) |
| `ReverseClipsRender` | reversed copies of long files, written side by side and followed one after another; cancelled, none is used |

`RenderProgress` (`Session.render`, [RenderProgress.h](../../app/src/session/RenderProgress.h)) is what the UI binds its
modal dialog to: `active` (show it), `title`, `label`, `progress` (0..1), `busy` (a busy bar while plug-ins or samples
load), `cancelled` ("Cancelling…": Cancel is disabled until the render has stopped). `cancel()` is the dialog's Cancel
(and Esc, and closing it); Cancel never takes the keyboard focus, so Space, meant for the transport, doesn't cancel.
`requestClose()` cancels a render too, and `shutdown()` aborts one (waiting for its threads; nothing changes and its
files go).

## The computer MIDI keyboard

`ComputerKeyboard` (`Session.computerKeyboard`, [ComputerKeyboard.h](../../app/src/session/ComputerKeyboard.h)), as in
Ableton: while it is on (M: `toggle()`, `enabled`), the letter keys play notes into the MIDI tracks that hear it, as a
MIDI input called "Computer Keyboard" (`kComputerKeyboard`; every track on All Ins hears it too).

- The middle row is the white keys from C (A, S, D, F, G, H, J, K, L, ;, '), the row above it the black keys between
  them (W, E, T, Y, U, O, P). Z and X move the octave down and up (`shiftOctave`; `octave`, `octaveLabel`: the note A
  plays, C3 to start with). Notes play at velocity 100.
- It is an application event filter on Qt Gui's key events, so it sees the keys wherever they go. The keys reach it
  before the window's shortcuts (S, A and Z do other things while it is off: it accepts their `ShortcutOverride`, so
  they come as key presses), but not while typing in a text input (the focus object answers `Qt::ImEnabled`), nor with
  modifiers held. `takesKey(key)` tells the UI which keys it takes.
- Notes still held when it is turned off, or when the application loses the keyboard, are released.
- Its notes go to `EngineBridge::sendMidi` (dropped while no audio device runs).

## Preferences

**Preferences › Audio** is `AudioPreferences` (`Session.audioPreferences`,
[AudioPreferences.h](../../app/src/session/AudioPreferences.h)): the driver type, the device, its outputs (ASIO and PipeWire,
whose devices name their channels: `listsChannels()`), sample
rate, buffer size, exclusive mode (WASAPI), the audio threads, the driver's own settings (*Hardware Setup*), and a
status line. Changes apply at once: what a device offers (its sample rates, buffer sizes and channels) is only known
while it is open. Each change opens the device with the new settings (another driver or device: falling back to the
device's own settings if it won't take them), and only settings that opened are saved (`AudioSettings`, so the program
opens the same next time).

The QML dialog calls `open()` when it shows and `close()` when it goes. Each combo box binds to a list of choices
(`[{label, value, enabled, toolTip}]`: `driverChoices`, `deviceChoices`, `outputChoices`, `sampleRateChoices`,
`bufferChoices`, `threadChoices`) and its current index (`driverIndex`...), and calls the matching `choose...(index)`
with the index the user picked (the same index again does nothing). The drivers are `audioDrivers()`: WASAPI and ASIO on Windows,
where a build without the ASIO SDK lists it disabled, saying why; the engine's elsewhere (PipeWire, System). `showControlPanel()` opens the ASIO driver's own settings (it may run a message loop:
the UI disables the dialog meanwhile). `status` is rich text: what runs, an error first in red, and a warning (in red too) when
the system refused the audio threads beside the driver's own real-time priority (`EngineBridge::realtimeWorkers()`,
with what to do about it: on Linux rtkit or `rtprio` in limits.conf), and another when the device has dropped out
since it opened (`AudioDeviceStatus::dropouts`: how many; a larger buffer may help; looked at once a second while the dialog
shows, `kDropoutsInterval`, since dropouts come without an event). The latency shown is the
output's, and the input's too where inputs are open (or the driver is ASIO).

**Preferences › MIDI** is `MidiPreferences` (`Session.midiPreferences`): the MIDI inputs connected
(`inputs: [{name, enabled, error}]`), each on (tracks can hear and record it) or off (`setInputEnabled`, saved);
`refresh()` finds inputs plugged in or taken out since; `status`.

**Preferences › Plug-ins** is the plug-in index itself (`Session.plugins`): the standard and the user's folders, adding
and removing folders, rescanning ([plugin-scanner.md](plugin-scanner.md#in-the-application)); and where plug-ins run,
`PluginHostingPreferences` (`Session.pluginHosting`,
[PluginHostingPreferences.h](../../app/src/session/PluginHostingPreferences.h)): `mode` ("within", "together" (the
default), "manufacturer", "plugin", "instance": [engine/sandboxes.md](../engine/sandboxes.md#hosting-modes)), saved
as `plugins/hosting` and applied to the bridge at once (plug-ins loaded from then on); `modes` (`[{value, label,
toolTip}]`, the choices); `reloadPlugins()` (every plug-in again, where the mode says now); `status` (what runs where:
"3 plug-ins in 2 sandboxes"). It is made before the bridge loads anything, and gives the engine the host program
(`substation-plugin-host` next to the application; `Session::Options::pluginHost`, or `SUBSTATION_PLUGIN_HOST`). Without
a saved mode the default is "together", or `SUBSTATION_PLUGIN_HOSTING`'s (the tests set "within": their plug-ins run in
their own process unless a test asks for sandboxes).

**Preferences › Look and Feel › Maximum display frame rate** is `DisplayPreferences` (`Session.displayPreferences`,
[DisplayPreferences.h](../../app/src/session/DisplayPreferences.h)); the theme beside it is the UI's own (`Theme`). How
often the live displays refresh: the playhead, the meters, a recording's waveform, the scope, the devices' editors.
`maxRate` is one of `kRates` (30, 60, 120, 144, 240 Hz; 60 by default, so a tick every 16 ms as it always was), saved
as `ui/max_display_rate` (another number: the nearest of them); `rateChoices` (`[{label, value}]`), `rateIndex` and
`chooseRate(index)` for the page's `ChoiceBox`. The displays tick at `rate`, the lower of it and `screenRate`, the main
window's screen's refresh rate (`followWindow(window)`, from `ui/main.cpp`: its screen now, and as the window moves to
another or the rate changes; 0, not known, caps nothing; `setScreenRate` for the tests), every `intervalMs`: 1000 /
rate ms rounded leaning short (`intervalFor`: 33, 16, 8, 7 and 4 ms; 59.94 Hz 16 ms). `status` says what it comes to
("Displays refresh at 75 Hz, the screen's rate."). Applied at once: the bridge polls the playhead and the meters every
`intervalMs` (`EngineBridge::setDisplayInterval`, [engine-bridge.md](engine-bridge.md#polling)), and the UI's
`DisplayClock` follows `changed` ([ui/device-view.md](../ui/device-view.md)). An application preference, not the
project's: no undo.

## Settings

Besides the audio and MIDI preferences ([engine-bridge.md](engine-bridge.md#settings-storage)), the session and its
parts keep these QSettings keys:

| Key | Holds | Code |
|---|---|---|
| `files/recent` | the recent projects, the latest first (at most 10) | `Session::kRecentKey` |
| `files/last_dir` | where the file dialogs start (default `~/Music`) | `Session::kLastFolderKey` |
| `transport/count_in_bars` | the count-in (0, 1, 2 or 4 bars) | `Session::kCountInKey` |
| `ui/max_display_rate` | Preferences › Look and Feel › Maximum display frame rate, in Hz (30, 60, 120, 144 or 240; 60 without one) | `DisplayPreferences::kKey` |
| `updates/channel`, `updates/auto_check`, `updates/auto_install`, `updates/last_check` | Preferences › Updates, and when it last looked | `Updater` ([updates.md](updates.md#settings)) |
| `presets/dir` | where *Load Preset…* last loaded a preset file from (default: the library) | `DeviceSelection::presetFolder` |
| `vital/preset_dir` | where a Vital preset (`.vital`) was last loaded from or saved to (default: Vital's presets folder, `Documents/Vital` or `~/.local/share/vital`, if it exists) | `DeviceSelection::vitalPresetFolder` |
| `plugins/preset_dir` | where a plug-in's own `.vstpreset` was last loaded from or saved to (default: `Documents/VST3 Presets/<vendor>/<plug-in>`, if it exists) | `DeviceSelection::vst3PresetFolder` |

## Gotchas

- The session never shows a dialog: what were message boxes are `warning` and `information`, and questions (unsaved
  changes, replacing a preset, where to save) are the UI's, which then calls the session.
- `saveProject()` returns false for a project never saved: that isn't a failure, the UI is asking where
  (`saveAsRequested`).
- Renders hold the project as it was when they started; the UI's modal dialog keeps the user from editing meanwhile.
  Code that starts a render must go through `startRender` (one at a time).
- `Selection::trackId()` and the device view's ids may name things gone after an undo: look them up again
  (`Project::findTrack`, `findDevice`).

## Tests

- [test_selection.cpp](../../tests/app/test_selection.cpp): the selection's rules.
- [test_session_edit.cpp](../../tests/app/test_session_edit.cpp): the Edit and Create commands, the clipboard,
  reversing, the selected MIDI clips' notes at once.
- [test_session_devices.cpp](../../tests/app/test_session_devices.cpp): the device view's selection, clipboard, racks,
  drops and presets.
- [test_backups.cpp](../../tests/app/test_backups.cpp): backups per project and how many stay, only unsaved changes
  backed up, the timer's written in the background once the plug-ins' states are gathered (one at a time, let go of
  when saved meanwhile), a crashed session's backup offered (not a running one's, not a saved one's), the session
  recovering one.
- [test_session_files.cpp](../../tests/app/test_session_files.cpp): files, the title, the recent projects, Export
  Audio's choices, count-in and record quantization, the preferences (the display rate too: kept, applied to the
  bridge, capped by the screen's).
- [test_session_renders.cpp](../../tests/app/test_session_renders.cpp): renders in the background, Cancel, closing
  meanwhile, freezing and flattening, plug-ins loading after a project opens, renders waiting for them and for files
  still decoding.
- [test_session_engine.cpp](../../tests/app/test_session_engine.cpp): what the engine hears of the edits.
- [test_session_keyboard.cpp](../../tests/app/test_session_keyboard.cpp): the computer MIDI keyboard.
- [test_grooves.cpp](../../tests/app/test_grooves.cpp): the grooves' model and editing under `GrooveLibrary`;
  [test_ui_grooves.cpp](../../tests/app/test_ui_grooves.cpp) drives it through Edit › Grooves….
- [test_file_manager.cpp](../../tests/app/test_file_manager.cpp): the project's files, replacing and relinking them,
  matching missing files and the search in the background (in the project's folder, a folder chosen, the browser's
  places), Locate, the File Manager's list, and hot swaps through the browser.
- The UI's tests ([testing.md](../testing.md#the-ui-test_ui_-on-a-display)) drive all of it through the QML.

Test fixture: [SessionFixture.h](../../tests/app/support/SessionFixture.h) ([testing.md](../testing.md#testsappsupport)).
