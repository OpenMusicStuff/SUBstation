import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SUBstation

// The main window: the title bar on top (the menus, the title, the status line
// with a project's plug-ins loading, and on Windows the window's buttons: the
// system's caption is gone), the transport bar under it; below them the
// browser (left) beside the arrangement (or the clip view covering it while
// clips are open in it), and the File Manager at the right while it shows
// (View › File Manager); along the window's bottom, as in Ableton, the
// info view (what the control under the mouse is: the tooltips are said there)
// and the device view across the rest of the width.
// Every menu action and shortcut calls the session (the main window's logic:
// transport, files, the Edit and Create commands on what is selected, renders,
// preferences); this file shows what it says and asks the user what it needs
// (files, unsaved changes).
//
// The arrangement and the device view are reached only through their
// interfaces, each call guarded:
//   arrangement: zoom(factor), zoomToArrangement(), narrowGrid(), widenGrid(),
//                openClipView(), renameTrack(trackId) -> bool, focusLanes();
//                snap, follow, ioShown (read/write), gridStep, gridLevel; statusMessage(text),
//                preferencesRequested(page)
//   devicePanel: startChainRename(rackId, chainId) -> bool, focusDevices();
//                statusMessage(text)
ApplicationWindow {
    id: window

    // The views (for the tests, and whoever needs them).
    readonly property alias transportBar: transportBar
    readonly property alias browserPanel: browser
    readonly property alias fileManagerPanel: fileManager
    readonly property alias arrangementView: arrangement
    readonly property alias devicePanelView: devicePanel
    readonly property alias infoView: infoView
    readonly property alias clipView: clipView
    readonly property alias arrangementArea: arrangementArea
    readonly property alias trackSearchBar: trackSearchBar
    readonly property alias pluginKeys: pluginKeys
    readonly property alias titleBar: titleBar
    // Set once the close was confirmed (no render running, nothing unsaved or the user said so).
    property bool closeConfirmed: false
    // What runs after a Save that asks where (the unsaved-changes question's Save).
    property var afterSave: null

    // MainWindow.show_message: the title bar's status line, for Session.statusTimeout ms.
    function showMessage(text) {
        titleBar.message = text
        messageTimer.restart()
    }

    // A command's key as its Action shows it (KeyMap: the first of its keys; "": none).
    function keyOf(id) {
        const keys = KeyMap.bindings[id]
        return keys && keys.length > 0 ? keys[0] : ""
    }

    // --- The views' interfaces (guarded: a placeholder has none) ---------------------------

    function focusLanes() {
        if (arrangement.focusLanes)
            arrangement.focusLanes()
        else
            window.contentItem.forceActiveFocus()  // (not a view that has gone, at least)
    }

    // The grid a new MIDI clip snaps to (beats; 0: snapping is off).
    function insertGridStep() {
        return arrangement.snap === true && arrangement.gridStep !== undefined ? arrangement.gridStep : 0
    }

    function zoom(factor) {
        if (arrangementArea.clipViewShown)
            clipView.zoom(factor)  // (what it shows: the waveforms, or the piano roll)
        else if (arrangement.zoom)
            arrangement.zoom(factor)
    }

    // Zoom to Arrangement (Z): the whole song, or in the clip view the clips shown.
    function zoomToFit() {
        if (arrangementArea.clipViewShown)
            clipView.zoomToFit()
        else if (arrangement.zoomToArrangement)
            arrangement.zoomToArrangement()
    }

    // --- Files ---------------------------------------------------------------------------------

    // Runs `then` unless the user cancels: the project has no unsaved changes,
    // or they were saved (Save) or may go (Discard).
    function confirmDiscard(then) {
        if (Session.clean) {
            then()
            return
        }
        unsavedDialog.then = then
        unsavedDialog.open()
    }

    // Ctrl+S: to the project's file; a project never saved asks where first
    // (Session.saveAsRequested). `then` runs once it was saved.
    function save(then) {
        afterSave = then || null
        const saved = Session.saveProject()  // (never saved: saveAsRequested has asked where meanwhile)
        const after = afterSave
        afterSave = null
        if (saved && after)
            after()
    }

    // Save As…: asks where (starting at "<last folder>/Untitled.gilproj"); `then` runs once saved.
    function saveAs(then) {
        saveDialog.then = then || null
        saveDialog.currentFolder = FileUrls.folderUrl(Session.suggestedSavePath())
        saveDialog.selectedFile = FileUrls.fileUrl(Session.suggestedSavePath())
        saveDialog.open()
    }

    function saveChosen(path) {
        const then = saveDialog.then
        saveDialog.then = null
        if (path !== "" && Session.saveProjectAs(path) && then)
            then()
    }

    function newProject() {
        confirmDiscard(() => Session.newProject())
    }

    // Open…: asks about unsaved changes, then which file.
    function openProject() {
        confirmDiscard(() => {
            openDialog.currentFolder = FileUrls.fileUrl(Session.lastFolder)
            openDialog.open()
        })
    }

    // Import Ableton Live Set…: asks about unsaved changes, then which set.
    function importLiveSet() {
        confirmDiscard(() => {
            liveSetDialog.currentFolder = FileUrls.fileUrl(Session.lastFolder)
            liveSetDialog.open()
        })
    }

    function importChosen(path) {
        if (path !== "")
            Session.importLiveSet(path)
    }

    function openChosen(path) {
        if (path !== "")
            Session.openProject(path)
    }

    // File › Backups, as it opens: the open project's backups (newest first), a
    // separator and Show Backups Folder; with none, "No Backups Yet".
    function fillBackupsMenu() {
        while (backupsMenu.count > 0) {
            const item = backupsMenu.takeItem(0)
            if (item)
                item.destroy()
        }
        const items = Session.backupMenuItems()
        if (items.length === 0)
            backupsMenu.addItem(backupEntry.createObject(backupsMenu.contentItem, {text: qsTr("No Backups Yet"), enabled: false}))
        for (const entry of items)
            backupsMenu.addItem(backupEntry.createObject(backupsMenu.contentItem, {text: entry.label, path: entry.path, tip: entry.toolTip}))
        backupsMenu.addItem(menuSeparator.createObject(backupsMenu.contentItem))
        backupsMenu.addItem(backupEntry.createObject(backupsMenu.contentItem, {text: qsTr("&Show Backups Folder"), showFolder: true}))
    }

    // A recent project chosen: one whose file is gone is taken off the list (with a warning).
    function openRecent(path) {
        if (Session.recentProjectAvailable(path))
            confirmDiscard(() => Session.openProject(path))
    }

    // Open Recent, as it opens: the projects ("&1  name"), a separator and Clear List;
    // with none, "No Recent Projects".
    function fillRecentMenu() {
        while (recentMenu.count > 0) {
            const item = recentMenu.takeItem(0)
            if (item)
                item.destroy()
        }
        const items = Session.recentMenuItems()
        if (items.length === 0) {
            recentMenu.addItem(recentEntry.createObject(recentMenu.contentItem, {text: qsTr("No Recent Projects"), enabled: false}))
            return
        }
        for (const entry of items)
            recentMenu.addItem(recentEntry.createObject(recentMenu.contentItem, {text: entry.label, path: entry.path, tip: entry.toolTip}))
        recentMenu.addItem(menuSeparator.createObject(recentMenu.contentItem))
        recentMenu.addItem(recentEntry.createObject(recentMenu.contentItem, {text: qsTr("&Clear List"), clearList: true}))
    }

    // File › Export Audio…: the range, the file type and its bit depth or
    // bitrate, then where (or why there is nothing to export).
    function exportChosen(range, bitDepth, fileType, bitrate) {
        const problem = Session.exportProblem(range)
        if (problem !== "") {
            messages.information(problem)
            return
        }
        exportFileDialog.range = range
        exportFileDialog.bitDepth = bitDepth
        exportFileDialog.fileType = fileType || "wav"
        exportFileDialog.bitrate = bitrate || Session.defaultExportBitrate
        const suggested = Session.suggestedExportPath(exportFileDialog.fileType)
        exportFileDialog.currentFolder = FileUrls.folderUrl(suggested)
        exportFileDialog.selectedFile = FileUrls.fileUrl(suggested)
        exportFileDialog.open()
    }

    function exportFileChosen(path) {
        if (path !== "")
            Session.exportAudio(path, exportFileDialog.range, exportFileDialog.bitDepth, exportFileDialog.fileType,
                                exportFileDialog.bitrate)
    }

    // --- Edit ----------------------------------------------------------------------------------

    // Ctrl+R: the preset current in the browser's list (if the list has the
    // focus), the rack chain last clicked (device view), or the track last
    // clicked, renamed where it shows.
    function rename() {
        const target = Session.renameTarget(browser.listFocused)
        let started = false
        if (target.kind === "preset")
            started = browser.startRename(target.path)
        else if (target.kind === "chain")
            started = devicePanel.startChainRename ? devicePanel.startChainRename(target.rackId, target.chainId) : false
        else if (target.kind === "track")
            started = arrangement.renameTrack ? arrangement.renameTrack(target.trackId) : false
        else
            return  // (the session said why)
        if (!started)
            showMessage(qsTr("Select a track, a rack chain or a preset to rename."))
    }

    // Find Similar (from an audio clip's menu too): the browser shows the sounds.
    Connections {
        target: Session.browser

        function onSimilarChanged() {
            if (Session.browser.similarTo !== "")
                browserAction.checked = true
        }
    }

    // A hot swap (an audio clip's menu, the File Manager) works in the browser: it shows.
    Connections {
        target: Session.hotSwap

        function onChanged() {
            if (Session.hotSwap.active)
                browserAction.checked = true
        }
    }

    // Show in File Manager (an audio clip's menu): it shows, the file's row selected.
    Connections {
        target: Session.files

        function onRevealRequested(path) {
            fileManagerAction.checked = true
            Qt.callLater(() => fileManager.reveal(path))
        }
    }

    // Ctrl+F (from a plug-in's editor too): the browser shows, searching everything.
    function findInBrowser() {
        window.requestActivate()
        browserAction.checked = true
        Session.browser.focusSearch()
    }

    // Q: the find bar, finding tracks by their names and labels. From the clip
    // view, the beats its clips span are the search's area: a match with a clip
    // there opens in the clip view, the others in the arrangement.
    function findTrack() {
        if (!trackSearchBar.visible)  // (where the search began: Ctrl+Enter goes back there)
            trackSearchOrigin = {
                trackId: Session.selection.trackId,
                clipView: arrangementArea.clipViewShown,
                clipIds: clipView.clipIds,
                leadTrackId: clipView.trackId,
                leadClipId: clipView.leadClipId
            }
        if (arrangementArea.clipViewShown)
            Session.trackSearch.setArea(clipView.trackId, clipView.clipIds)
        else
            Session.trackSearch.clearArea()
        trackSearchBar.open()
    }

    // The track selected (and the clips open in the clip view) when the find bar opened.
    property var trackSearchOrigin: null

    // Ctrl+Enter in the find bar: back to the track selected before it opened,
    // and the clip view as it was.
    function returnFromTrackSearch() {
        const origin = trackSearchOrigin
        if (!origin)
            return
        if (origin.trackId !== "" && arrangement.revealTrack)
            arrangement.revealTrack(origin.trackId)
        if (origin.clipView && origin.clipIds.length > 0)
            arrangementArea.openClips(origin.clipIds, origin.leadTrackId, origin.leadClipId)
        else
            arrangementArea.clipViewShown = false
    }

    // A match of the find bar: selected, unfolded and scrolled to in the
    // arrangement; and its clip in the search's area in the clip view, if it has
    // one (else the arrangement shows, its clip nearest the playhead selected
    // and scrolled to). The find bar keeps the keyboard.
    function showTrackMatch(trackId) {
        if (arrangement.revealTrack)
            arrangement.revealTrack(trackId)
        const clipId = Session.trackSearch.clipInArea(trackId)
        if (clipId !== "") {
            arrangementArea.openClips([clipId], trackId, clipId)
        } else {
            arrangementArea.clipViewShown = false
            if (arrangement.revealNearestClip)
                arrangement.revealNearestClip(trackId)
        }
        trackSearchBar.field.forceActiveFocus()
    }

    // Shift+Tab: the clip view goes back to the arrangement; or the selected clips open in it.
    function toggleClipView() {
        if (arrangementArea.clipViewShown)
            arrangementArea.closeClipView()
        else if (arrangement.openClipView)
            arrangement.openClipView()
    }

    width: 1440
    height: 860
    visible: true
    title: Session.title

    Component.onCompleted: {
        windowState.restore()
        const splitterState = windowState.splitterState()
        if (splitterState)
            splitter.restoreState(splitterState)
        const deviceSplitterState = windowState.deviceSplitterState()
        if (deviceSplitterState)
            rows.restoreState(deviceSplitterState)
        Qt.callLater(window.focusLanes)  // (the lanes take the keyboard once the window shows)
    }

    // Closing: a render running is cancelled instead (the window stays, as its
    // dialog's Cancel); else unsaved changes are asked about. The window's
    // place and the splitters are kept for next time.
    onClosing: close => {
        if (!closeConfirmed) {
            if (!Session.requestClose()) {
                close.accepted = false
                return
            }
            if (!Session.clean) {
                close.accepted = false
                confirmDiscard(() => {
                    window.closeConfirmed = true
                    window.close()
                })
                return
            }
        }
        windowState.save(splitter.saveState(), rows.saveState())
    }

    WindowState {
        id: windowState
        window: window
    }

    // The window's shortcuts while a plug-in's editor has the focus (its own or a sandbox's).
    PluginEditorKeys {
        id: pluginKeys
        target: window
        session: Session
    }

    Connections {
        target: Session

        function onStatusMessage(message) {
            window.showMessage(message)
        }
        function onWarning(message) {
            messages.warning(message)
        }
        function onInformation(message) {
            messages.information(message)
        }
        function onSaveAsRequested() {
            window.saveAs(window.afterSave)
            window.afterSave = null
        }
        // SUBstation closed last time without saving: its backup offered.
        function onRecoveryAvailable() {
            const backups = Session.recoverableBackups()
            if (backups.length === 0)
                return
            recoveryDialog.path = backups[0].path
            recoveryDialog.show(qsTr("SUBstation closed unexpectedly while %1 had unsaved changes.\n\n"
                                     + "Recover them from its backup of %2?\nUntil you save, the recovered project is unsaved.")
                                .arg(backups[0].name).arg(backups[0].time))
        }
        function onProjectOpened() {
            if (arrangement.zoomToArrangement)
                arrangement.zoomToArrangement()
            if (Session.files.missingCount > 0)  // (it finds them)
                fileManagerAction.checked = true
        }
    }

    // Restart to Install: SUBstation closes as Quit closes it (unsaved changes asked about), then the update installs.
    Connections {
        target: Session.updates

        function onQuitRequested() {
            window.close()
        }
    }

    Connections {
        target: Session.arrangement

        function onClipViewRequested(refs, leadTrackId, leadClipId) {
            arrangementArea.openClips(refs, leadTrackId, leadClipId)
        }
    }

    // What the views have to say, for the status line.
    Connections {
        target: arrangement
        ignoreUnknownSignals: true

        function onStatusMessage(message) {
            window.showMessage(message)
        }
    }
    Connections {
        target: devicePanel
        ignoreUnknownSignals: true

        function onStatusMessage(message) {
            window.showMessage(message)
        }
    }

    menuBar: TitleBar {
        id: titleBar
        title: Session.title

        MenuBar {
            objectName: "menuBar"

            Menu {
                title: qsTr("&File")
                Action { objectName: "newProject"; text: qsTr("&New Project"); shortcut: window.keyOf(objectName); onTriggered: window.newProject() }
                Action { objectName: "openProject"; text: qsTr("&Open…"); shortcut: window.keyOf(objectName); onTriggered: window.openProject() }
                Menu {
                    id: recentMenu
                    objectName: "recentMenu"
                    title: qsTr("Open &Recent")
                    onAboutToShow: window.fillRecentMenu()
                    MenuItem { text: qsTr("No Recent Projects"); enabled: false }
                }
                // The open project's backups (made every minute while it has unsaved changes).
                Menu {
                    id: backupsMenu
                    objectName: "backupsMenu"
                    title: qsTr("&Backups")
                    onAboutToShow: window.fillBackupsMenu()
                    MenuItem { text: qsTr("No Backups Yet"); enabled: false }
                }
                Action { objectName: "importLiveSet"; shortcut: window.keyOf(objectName); text: qsTr("&Import Ableton Live Set…"); onTriggered: window.importLiveSet() }
                MenuSeparator {}
                Action { objectName: "save"; text: qsTr("&Save"); shortcut: window.keyOf(objectName); onTriggered: window.save() }
                Action { objectName: "saveAs"; text: qsTr("Save &As…"); shortcut: window.keyOf(objectName); onTriggered: window.saveAs() }
                Action { objectName: "saveAsTemplate"; shortcut: window.keyOf(objectName); text: qsTr("Save as &Template"); onTriggered: Session.saveAsTemplate() }
                Action { objectName: "clearTemplate"; shortcut: window.keyOf(objectName); text: qsTr("C&lear Template"); enabled: Session.hasTemplate; onTriggered: Session.clearTemplate() }
                MenuSeparator {}
                Action { objectName: "exportAudio"; text: qsTr("&Export Audio…"); shortcut: window.keyOf(objectName); onTriggered: exportDialog.open() }
                MenuSeparator {}
                Action { objectName: "quit"; text: qsTr("&Quit"); shortcut: window.keyOf(objectName); onTriggered: window.close() }
            }
            Menu {
                title: qsTr("&Edit")
                Action {
                    id: undoAction
                    objectName: "undo"
                    text: Session.undoStack.undoText !== "" ? qsTr("&Undo %1").arg(Session.undoStack.undoText) : qsTr("&Undo")
                    enabled: Session.undoStack.canUndo
                    shortcut: window.keyOf(objectName)
                    onTriggered: Session.undo()  // (refused while an agent changed the same since)
                }
                Action {
                    id: redoAction
                    objectName: "redo"
                    text: Session.undoStack.redoText !== "" ? qsTr("&Redo %1").arg(Session.undoStack.redoText) : qsTr("&Redo")
                    enabled: Session.undoStack.canRedo
                    shortcut: window.keyOf(objectName)
                    onTriggered: Session.redo()
                }
                MenuSeparator {}
                Action { objectName: "cut"; text: qsTr("Cu&t"); shortcut: window.keyOf(objectName); onTriggered: Session.cut() }
                Action { objectName: "copy"; text: qsTr("&Copy"); shortcut: window.keyOf(objectName); onTriggered: Session.copy() }
                Action { objectName: "paste"; text: qsTr("&Paste"); shortcut: window.keyOf(objectName); onTriggered: Session.paste() }
                Action { objectName: "duplicate"; text: qsTr("D&uplicate"); shortcut: window.keyOf(objectName); onTriggered: Session.duplicate() }
                Action { objectName: "rename"; text: qsTr("&Rename"); shortcut: window.keyOf(objectName); onTriggered: window.rename() }
                Action { objectName: "split"; text: qsTr("&Split"); shortcut: window.keyOf(objectName); onTriggered: Session.split() }
                Action { objectName: "consolidate"; text: qsTr("C&onsolidate"); shortcut: window.keyOf(objectName); onTriggered: Session.consolidate() }
                Action { objectName: "reverseClips"; text: qsTr("Re&verse Clips"); shortcut: window.keyOf(objectName); onTriggered: Session.reverseClips() }
                Action { objectName: "toggleClipActivation"; text: qsTr("Deact&ivate / Activate Clips"); shortcut: window.keyOf(objectName); onTriggered: Session.toggleClipActivation() }
                MenuSeparator {}
                Action { objectName: "freeze"; text: qsTr("&Freeze / Unfreeze Track"); shortcut: window.keyOf(objectName); onTriggered: Session.toggleFreeze() }
                Action { objectName: "flatten"; shortcut: window.keyOf(objectName); text: qsTr("Flatten Track"); onTriggered: Session.flattenSelectedTracks() }
                Action { id: deleteAction; objectName: "delete"; text: qsTr("&Delete"); shortcut: window.keyOf(objectName); onTriggered: Session.deleteSelection() }
                Action { objectName: "selectAll"; text: qsTr("Select &All"); shortcut: window.keyOf(objectName); onTriggered: Session.selectAll() }
                MenuSeparator {}
                Menu {
                    id: defaultGrooveMenu
                    objectName: "defaultGroove"
                    title: qsTr("Default &Groove")
                    // The project's default groove (Session.grooves): none, a swing, or one of its
                    // grooves. Groove devices and clips set to Project play it; devices following
                    // the groove follow it. Checked from it (also after a click changed it). (Its
                    // entries aren't commands: no names, no keys.)
                    Action {
                        text: qsTr("None")
                        checkable: true
                        checked: Session.grooves.defaultKind === ""
                        onTriggered: {
                            Session.grooves.clearDefault()
                            checked = Qt.binding(() => Session.grooves.defaultKind === "")
                        }
                    }
                    Instantiator {
                        model: [54, 58, 62, 200 / 3, 71]
                        delegate: Action {
                            required property var modelData
                            readonly property bool chosen: Session.grooves.defaultKind === "swing"
                                                           && Math.abs(Session.grooves.defaultSwing - modelData) < 0.05
                            text: qsTr("Swing %1 %").arg(Number(modelData).toFixed(modelData % 1 === 0 ? 0 : 1))
                            checkable: true
                            checked: chosen
                            onTriggered: {
                                Session.grooves.setDefaultSwing(modelData)
                                checked = Qt.binding(() => chosen)
                            }
                        }
                        onObjectAdded: (index, object) => defaultGrooveMenu.insertAction(1 + index, object)
                        onObjectRemoved: (index, object) => defaultGrooveMenu.removeAction(object)
                    }
                    MenuSeparator {}
                    Instantiator {
                        model: Session.grooves.grooves
                        delegate: Action {
                            required property var modelData
                            readonly property bool chosen: Session.grooves.defaultGrooveId === modelData.id
                            text: modelData.name
                            checkable: true
                            checked: chosen
                            onTriggered: {
                                Session.grooves.setDefaultGroove(modelData.id)
                                checked = Qt.binding(() => chosen)
                            }
                        }
                        onObjectAdded: (index, object) => defaultGrooveMenu.addAction(object)  // (after the swings)
                        onObjectRemoved: (index, object) => defaultGrooveMenu.removeAction(object)
                    }
                }
                Action { objectName: "grooves"; text: qsTr("Gr&ooves…"); shortcut: window.keyOf(objectName); onTriggered: grooveDialog.open() }
                MenuSeparator {}
                // Enabled while automation is overridden somewhere.
                Action {
                    objectName: "reEnableAutomation"
                    shortcut: window.keyOf(objectName)
                    text: qsTr("Re-Enable Automation")
                    enabled: Session.automationOverridden
                    onTriggered: Session.bridge.reEnableAutomation()
                }
                Action { objectName: "soloSelectedTracks"; text: qsTr("Solo Selected Tracks"); shortcut: window.keyOf(objectName); onTriggered: Session.soloSelectedTracks() }
                Action { objectName: "armSelectedTracks"; text: qsTr("Arm Selected Tracks"); shortcut: window.keyOf(objectName); onTriggered: Session.armSelectedTracks() }
                MenuSeparator {}
                Action { objectName: "playStop"; text: qsTr("Play / Stop"); shortcut: window.keyOf(objectName); onTriggered: Session.togglePlay() }
                Action { objectName: "record"; text: qsTr("Record"); shortcut: window.keyOf(objectName); onTriggered: Session.toggleRecord() }
                Menu {
                    id: quantizeMenu
                    objectName: "recordQuantization"
                    title: qsTr("Record &Quantization")
                    // Where recorded MIDI notes start: as played, or on this grid
                    // (record/quantize in the preferences; checked from it).
                    ActionGroup {
                        id: quantizeGroup
                    }
                    Instantiator {
                        model: Session.recordQuantizeChoices
                        delegate: Action {
                            required property var modelData
                            required property int index

                            // The check follows the session's choice (also after a click changed it).
                            function rebind() {
                                checked = Qt.binding(() => Math.abs(modelData.value - Session.recordQuantize) < 1e-9)
                            }

                            objectName: "quantize_" + index
                            text: modelData.label
                            checkable: true
                            checked: Math.abs(modelData.value - Session.recordQuantize) < 1e-9
                            ActionGroup.group: quantizeGroup
                            onTriggered: {
                                Session.recordQuantize = modelData.value
                                const actions = quantizeGroup.actions
                                for (let i = 0; i < actions.length; ++i)
                                    actions[i].rebind()
                            }
                        }
                        onObjectAdded: (index, object) => quantizeMenu.insertAction(index, object)
                        onObjectRemoved: (index, object) => quantizeMenu.removeAction(object)
                    }
                    MenuSeparator {}
                    Action {
                        objectName: "quantize_ends"
                        text: qsTr("Quantize Note Ends")
                        checkable: true
                        checked: Session.recordQuantizeEnds
                        enabled: Session.recordQuantize > 0
                        onTriggered: {
                            Session.recordQuantizeEnds = checked
                            checked = Qt.binding(() => Session.recordQuantizeEnds)  // (the click broke it)
                        }
                    }
                }
                Action { objectName: "goToStart"; text: qsTr("Go to Start"); shortcut: window.keyOf(objectName); onTriggered: Session.locate(0) }
                Action {
                    id: loopAction
                    objectName: "loop"
                    text: qsTr("Loop")
                    shortcut: window.keyOf(objectName)
                    checkable: true
                    checked: Session.project.loopEnabled
                    onTriggered: {
                        Session.editor.setLoopEnabled(checked)
                        checked = Qt.binding(() => Session.project.loopEnabled)
                    }
                }
                Action { objectName: "findInBrowser"; text: qsTr("Find in Browser"); shortcut: window.keyOf(objectName); onTriggered: window.findInBrowser() }
            }
            Menu {
                title: qsTr("&Create")
                Action { objectName: "insertAudioTrack"; text: qsTr("Insert Audio &Track"); shortcut: window.keyOf(objectName); onTriggered: Session.insertAudioTrack() }
                Action { objectName: "insertMidiTrack"; text: qsTr("Insert &MIDI Track"); shortcut: window.keyOf(objectName); onTriggered: Session.insertMidiTrack() }
                Action { objectName: "insertReturnTrack"; text: qsTr("Insert &Return Track"); shortcut: window.keyOf(objectName); onTriggered: Session.insertReturnTrack() }
                Action {
                    id: insertMidiClipAction
                    objectName: "insertMidiClip"
                    text: qsTr("Insert MIDI &Clip")
                    shortcut: window.keyOf(objectName)
                    onTriggered: Session.insertMidiClip(window.insertGridStep())
                }
                MenuSeparator {}
                // (In the device view: its selected devices.)
                Action { objectName: "groupTracks"; text: qsTr("&Group Tracks"); shortcut: window.keyOf(objectName); onTriggered: Session.groupSelected() }
                Action { objectName: "ungroupTracks"; text: qsTr("&Ungroup Tracks"); shortcut: window.keyOf(objectName); onTriggered: Session.ungroupSelected() }
                MenuSeparator {}
                Action { objectName: "deleteSelectedTracks"; shortcut: window.keyOf(objectName); text: qsTr("Delete Selected Tracks"); onTriggered: Session.deleteSelectedTracks() }
            }
            Menu {
                title: qsTr("&View")
                Action { id: browserAction; objectName: "browser"; text: qsTr("&Browser"); shortcut: window.keyOf(objectName); checkable: true; checked: true }
                // The files the project plays, on the right (as Ableton's): missing ones found, any replaced, hot-swapped.
                Action { id: fileManagerAction; objectName: "fileManager"; text: qsTr("&File Manager"); shortcut: window.keyOf(objectName); checkable: true; checked: false }
                // What agents changed through the MCP server, apart from Undo: each to rewind on its own.
                Action { id: agentChangesAction; objectName: "agentChangesView"; text: qsTr("A&gent Changes"); shortcut: window.keyOf(objectName); checkable: true; checked: false }
                // What each track and device costs the audio thread, in a window of its own (measured while open).
                Action { id: cpuUsageAction; objectName: "cpuUsage"; text: qsTr("CPU &Usage"); shortcut: window.keyOf(objectName); checkable: true; checked: false }
                Action { id: deviceViewAction; objectName: "deviceView"; text: qsTr("&Device View"); shortcut: window.keyOf(objectName); checkable: true; checked: true }
                // (Hidden, or with the device view hidden, the tooltips pop up again.)
                Action { id: infoViewAction; objectName: "infoView"; shortcut: window.keyOf(objectName); text: qsTr("&Info View"); checkable: true; checked: true }
                Action { objectName: "clipView"; text: qsTr("&Clip View"); shortcut: window.keyOf(objectName); onTriggered: window.toggleClipView() }
                Action { objectName: "findTrack"; text: qsTr("Find &Track"); shortcut: window.keyOf(objectName); onTriggered: window.findTrack() }
                Action { objectName: "automation"; text: qsTr("&Automation"); shortcut: window.keyOf(objectName); onTriggered: Session.editor.toggleAllAutomation() }
                // The track headers' In/Out column (Audio From and To, monitoring), as Ableton's.
                Action {
                    objectName: "inOut"
                    text: qsTr("&In/Out")
                    shortcut: window.keyOf(objectName)
                    checkable: true
                    checked: arrangement.ioShown
                    onTriggered: {
                        arrangement.ioShown = checked
                        checked = Qt.binding(() => arrangement.ioShown)
                    }
                }
                // The piano roll's chord lane and its notes out of the key in red, and audio clips' chords in the arrangement.
                Action {
                    objectName: "showHarmony"
                    text: qsTr("C&hords and Key")
                    shortcut: window.keyOf(objectName)
                    checkable: true
                    checked: Session.harmony.shown
                    onTriggered: {
                        Session.harmony.shown = checked
                        checked = Qt.binding(() => Session.harmony.shown)
                    }
                }
                // Plug-in editors are Win32 windows: closing the foremost is for Windows only.
                MenuItem {
                    visible: pluginKeys.supported
                    height: visible ? implicitHeight : 0
                    action: Action {
                        objectName: "closePluginEditor"
                        text: qsTr("Close Plug-in &Editor")
                        shortcut: window.keyOf(objectName)
                        enabled: pluginKeys.supported
                        onTriggered: pluginKeys.closeForemostPluginEditor()
                    }
                }
                // Ctrl+Shift+W, as a browser's Ctrl+Shift+T: the selected track's editor closed last opens again,
                // or with none closed its first plug-in's (from a plug-in's editor too: PluginEditorKeys passes it on).
                MenuItem {
                    visible: pluginKeys.supported
                    height: visible ? implicitHeight : 0
                    action: Action {
                        objectName: "reopenPluginEditor"
                        text: qsTr("&Reopen Closed Plug-in Editor")
                        shortcut: window.keyOf(objectName)
                        enabled: pluginKeys.supported
                        onTriggered: Session.bridge.reopenClosedPluginEditor(Session.selection.trackId)
                    }
                }
                MenuSeparator {}
                Action { id: zoomInAction; objectName: "zoomIn"; text: qsTr("Zoom &In"); shortcut: window.keyOf(objectName); onTriggered: window.zoom(1.4) }
                Action { id: zoomOutAction; objectName: "zoomOut"; text: qsTr("Zoom &Out"); shortcut: window.keyOf(objectName); onTriggered: window.zoom(1 / 1.4) }
                Action {
                    objectName: "zoomToArrangement"
                    text: qsTr("Zoom to &Arrangement")
                    shortcut: window.keyOf(objectName)
                    onTriggered: window.zoomToFit()
                }
                MenuSeparator {}
                Action { objectName: "narrowGrid"; text: qsTr("Narrow Grid"); shortcut: window.keyOf(objectName); onTriggered: if (arrangement.narrowGrid) arrangement.narrowGrid() }
                Action { objectName: "widenGrid"; text: qsTr("Widen Grid"); shortcut: window.keyOf(objectName); onTriggered: if (arrangement.widenGrid) arrangement.widenGrid() }
                Action {
                    id: snapAction
                    objectName: "snapToGrid"
                    text: qsTr("Snap to Grid")
                    shortcut: window.keyOf(objectName)
                    checkable: true
                    checked: arrangement.snap !== false
                    onTriggered: {
                        if (arrangement.snap !== undefined)
                            arrangement.snap = checked
                        checked = Qt.binding(() => arrangement.snap !== false)
                    }
                }
            }
            Menu {
                title: qsTr("&Options")
                Action { objectName: "preferences"; text: qsTr("&Preferences…"); shortcut: window.keyOf(objectName); onTriggered: preferences.open() }
                Action { objectName: "rescanPlugins"; shortcut: window.keyOf(objectName); text: qsTr("&Rescan Plug-ins"); onTriggered: Session.browser.rescanPlugins() }
                Action {
                    objectName: "computerMidiKeyboard"
                    text: qsTr("Computer &MIDI Keyboard")
                    shortcut: window.keyOf(objectName)
                    checkable: true
                    checked: Session.computerKeyboard.enabled
                    onTriggered: {
                        Session.computerKeyboard.enabled = checked
                        checked = Qt.binding(() => Session.computerKeyboard.enabled)
                    }
                }
                // Agents read the project through it (Preferences › Agents: its port, its address).
                Action {
                    objectName: "mcpServer"
                    shortcut: window.keyOf(objectName)
                    text: qsTr("M&CP Server")
                    checkable: true
                    checked: Session.mcp.enabled
                    onTriggered: {
                        Session.mcp.enabled = checked
                        checked = Qt.binding(() => Session.mcp.enabled)
                    }
                }
                MenuSeparator {}
                Action {
                    objectName: "lockEnvelopes"
                    shortcut: window.keyOf(objectName)
                    text: qsTr("&Lock Envelopes")
                    checkable: true
                    checked: Session.project.automationLocked
                    onTriggered: {
                        Session.editor.setAutomationLocked(checked)
                        checked = Qt.binding(() => Session.project.automationLocked)
                    }
                }
            }
            Menu {
                title: qsTr("&Help")
                Action {
                    objectName: "checkForUpdates"
                    text: qsTr("Check for &Updates…")
                    shortcut: window.keyOf(objectName)
                    onTriggered: {
                        preferences.currentPage = 6  // (Updates)
                        preferences.open()
                        Session.updates.checkNow()
                    }
                }
                Action { objectName: "about"; shortcut: window.keyOf(objectName); text: qsTr("&About SUBstation"); onTriggered: aboutDialog.open() }
            }
        }
    }

    // The actions' other keys (an Action has one): a Shortcut for each command
    // (its objectName the command's, + "Keys"), triggering its Action.
    Instantiator {
        model: KeyMap.commands
        delegate: Shortcut {
            required property var modelData
            property QtObject action: null

            objectName: modelData.id + "Keys"
            sequences: {
                const keys = KeyMap.bindings[modelData.id]
                return keys ? keys.slice(1) : []
            }
            enabled: action !== null && action.enabled && sequences.length > 0
            onActivated: action.trigger()
            Component.onCompleted: action = KeyMap.findAction(window, modelData.id)
        }
    }

    header: TransportBar {
        id: transportBar
        arrangement: window.arrangementView
        onPreferencesRequested: preferences.open()
    }

    // The browser and the arrangement over the bottom row.
    SplitView {
        id: rows
        objectName: "rows"
        anchors.fill: parent
        orientation: Qt.Vertical

        SplitView {
            id: splitter
            objectName: "splitter"
            orientation: Qt.Horizontal
            SplitView.fillHeight: true
            SplitView.minimumHeight: 120

            BrowserPanel {
                id: browser
                visible: browserAction.checked
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 120
            }

            // The arrangement, or the clip view covering it while clips are open
            // in it (a double-click on a clip, Shift+Tab), as before.
            Item {
                id: arrangementArea
                objectName: "arrangementArea"
                SplitView.fillWidth: true

                property bool clipViewShown: false

                // Opens clips in the clip view (the arrangement's clipViewRequested).
                function openClips(refs, leadTrackId, leadClipId) {
                    clipView.trackId = leadTrackId
                    clipView.leadClipId = leadClipId
                    clipView.clipIds = refs
                    clipViewShown = true
                }

                // Back to the arrangement (the clip view's Esc or ×, Shift+Tab); the lanes take the keyboard.
                function closeClipView() {
                    if (!clipViewShown)
                        return
                    clipViewShown = false
                    window.focusLanes()
                }

                ArrangementView {
                    id: arrangement
                    objectName: "arrangement"
                    anchors.fill: parent
                    visible: !arrangementArea.clipViewShown
                    onPreferencesRequested: page => {
                        preferences.currentPage = page
                        preferences.open()
                    }
                }

                ClipView {
                    id: clipView
                    objectName: "clipView"
                    anchors.fill: parent
                    visible: arrangementArea.clipViewShown
                    onCloseRequested: arrangementArea.closeClipView()
                    onLocateRequested: beat => Session.locate(beat)
                    onStatusMessage: message => window.showMessage(message)
                }

                // The find bar (Q), over the top right of the arrangement's lanes, or of the clip view.
                TrackSearchBar {
                    id: trackSearchBar
                    objectName: "trackSearchBar"
                    x: Math.max(0, (arrangement.lanesWidth ?? parent.width) - width - 12)
                    y: arrangementArea.clipViewShown ? 60 : (arrangement.rulerHeight ?? 0) + 8  // (below the rulers)
                    z: 1
                    onMatchChosen: trackId => window.showTrackMatch(trackId)
                    onReturnRequested: window.returnFromTrackSearch()
                    onClosed: {
                        if (arrangementArea.clipViewShown)
                            clipView.focusContent()
                        else
                            window.focusLanes()
                    }
                }
            }

            // The File Manager, at the right (View › File Manager).
            FileManagerPanel {
                id: fileManager
                visible: fileManagerAction.checked
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 160
                onCloseRequested: fileManagerAction.checked = false
            }

            // Agent Changes, at the right (View › Agent Changes).
            AgentChangesPanel {
                id: agentChanges
                visible: agentChangesAction.checked
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 160
                onCloseRequested: agentChangesAction.checked = false
            }
        }

        // The bottom row, across the window (as Ableton's): the info view at the
        // left, the device view along the rest, as tall as its tallest device needs.
        Rectangle {
            id: bottomRow
            objectName: "bottomRow"
            visible: deviceViewAction.checked
            color: Theme.window
            SplitView.preferredHeight: devicePanel.implicitHeight
            SplitView.minimumHeight: devicePanel.implicitHeight
            SplitView.maximumHeight: devicePanel.implicitHeight

            InfoView {
                id: infoView
                objectName: "infoView"
                visible: infoViewAction.checked
                z: 1  // (its grip, in the gap, over the device view)
                x: devicePanel.panelMargin
                y: devicePanel.panelMargin
                width: 230
                height: devicePanel.deviceHeight
            }

            DevicePanel {
                id: devicePanel
                objectName: "devicePanel"
                x: infoView.visible ? infoView.x + infoView.width : 0
                width: parent.width - x
                height: parent.height
            }
        }
    }

    // The status line's message goes after a while.
    Timer {
        id: messageTimer
        objectName: "messageTimer"
        interval: Session.statusTimeout
        onTriggered: titleBar.message = ""
    }

    // --- Dialogs -------------------------------------------------------------------------------

    UnsavedChangesDialog {
        id: unsavedDialog
        property var then: null
        onAnswered: value => {
            const then = unsavedDialog.then
            unsavedDialog.then = null
            if (value === "save")
                window.save(then)
            else if (value === "discard")
                then()
        }
    }

    // What a crash left: recovered, or not now (its backups stay: File › Backups, the backups folder).
    MessageBox {
        id: recoveryDialog
        objectName: "recoveryDialog"
        property string path
        title: qsTr("Recover Unsaved Work")
        icon: "question"
        choices: [{text: qsTr("&Recover"), value: "recover"}, {text: qsTr("Not Now"), value: "no"}]
        onAnswered: value => {
            if (value === "recover")
                window.confirmDiscard(() => Session.recoverBackup(recoveryDialog.path))
            else
                Session.dismissRecovery()
        }
    }

    // Session.warning and Session.information, one after another.
    MessageBox {
        id: messages
        objectName: "messageBox"

        property var queue: []

        function warning(text) {
            queue.push({text: text, icon: "warning"})
            showNext()
        }
        function information(text) {
            queue.push({text: text, icon: "information"})
            showNext()
        }
        function showNext() {
            if (visible || queue.length === 0)
                return
            const next = queue.shift()
            icon = next.icon
            show(next.text)
        }

        onClosed: Qt.callLater(showNext)
    }

    AboutDialog {
        id: aboutDialog
    }

    PreferencesDialog {
        id: preferences
    }

    ExportDialog {
        id: exportDialog
        onExportChosen: (range, bitDepth, fileType, bitrate) => window.exportChosen(range, bitDepth, fileType, bitrate)
    }

    RenderDialog {
        id: renderDialog
    }

    GrooveDialog {
        id: grooveDialog
    }

    CpuUsageWindow {
        visible: cpuUsageAction.checked
        onClosing: cpuUsageAction.checked = false
        onTrackClicked: trackId => {
            if (arrangement.revealTrack)
                arrangement.revealTrack(trackId)
        }
    }

    FileDialog {
        id: openDialog
        objectName: "openDialog"
        title: qsTr("Open Project")
        fileMode: FileDialog.OpenFile
        nameFilters: [Session.projectFilter]
        onAccepted: window.openChosen(FileUrls.localPath(selectedFile))
    }

    FileDialog {
        id: liveSetDialog
        objectName: "liveSetDialog"
        title: qsTr("Import Ableton Live Set")
        fileMode: FileDialog.OpenFile
        nameFilters: [Session.liveSetFilter]
        onAccepted: window.importChosen(FileUrls.localPath(selectedFile))
    }

    FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        property var then: null
        title: qsTr("Save Project As")
        fileMode: FileDialog.SaveFile
        nameFilters: [Session.projectFilter]
        defaultSuffix: Session.projectExtension.replace(/^\./, "")
        onAccepted: window.saveChosen(FileUrls.localPath(selectedFile))
        onRejected: then = null
    }

    FileDialog {
        id: exportFileDialog
        objectName: "exportFileDialog"
        property string range: "arrangement"
        property int bitDepth: 24
        property string fileType: "wav"
        property int bitrate: 320
        title: qsTr("Export Audio")
        fileMode: FileDialog.SaveFile
        nameFilters: fileType === "mp3" ? [qsTr("MP3 Audio (*.mp3)")] : [qsTr("WAV Audio (*.wav)")]
        defaultSuffix: fileType
        onAccepted: window.exportFileChosen(FileUrls.localPath(selectedFile))
    }

    Component {
        id: backupEntry

        MenuItem {
            property string path
            property string tip
            property bool showFolder: false
            onTriggered: {
                if (showFolder)
                    Session.browser.showInFolder(Session.backupsFolder())
                else if (path !== "")
                    window.confirmDiscard(() => Session.recoverBackup(path))
            }
            ToolTip.visible: hovered && tip !== ""
            ToolTip.text: tip
            ToolTip.delay: 700
        }
    }

    Component {
        id: recentEntry

        MenuItem {
            property string path
            property string tip
            property bool clearList: false
            onTriggered: {
                if (clearList)
                    Session.clearRecentProjects()
                else if (path !== "")
                    window.openRecent(path)
            }
            ToolTip.visible: hovered && tip !== ""
            ToolTip.text: tip
            ToolTip.delay: 700
        }
    }

    Component {
        id: menuSeparator

        MenuSeparator {}
    }
}
