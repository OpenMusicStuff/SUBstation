import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import SUBstation

// The device view, along the bottom of the window (beside the info view, as
// Ableton's): the selected track's (or a return's, or the master's) chain of
// devices, left to right on the window's colour with a meter of what each puts
// out in the gap after it, built-in devices and plug-ins alike, racks with
// their macros and chains, and after a rack the chain it shows, in a bracket
// (DeviceChain, DeviceFrame); what may be dropped there (in an empty place as
// tall as a device, while a track is shown), or that no track is selected, or
// that it is frozen (Session.deviceSelection.hint). It scrolls sideways without
// a scroll bar: Shift+wheel, or Ctrl+Alt-drag anywhere on it (DeviceChainArea,
// which also takes the drops and starts the drags).
//
// What it is to the main window:
// - startChainRename(rackId, chainId): Ctrl+R on a rack's chain
//   (Session.renameTarget()): its row's name edited in place; false if the
//   chain isn't shown.
// - focusDevices(): the device view takes the focus (Delete, the clipboard
//   and Ctrl+G act on its devices).
// - statusMessage(text): what it has to say beyond what Session.statusMessage
//   says (the device selection's messages go there).
// - implicitHeight: the height it needs (the tallest device: a page of knobs
//   with page arrows; editors' graphs take that height); it never scrolls
//   vertically.
//
// Clicking beside the devices deselects them but keeps the focus here (Ctrl+V
// pastes into this track); right-click there to paste or load a preset file
// at that place. The device, sidechain, chain and macro menus and the dialogs
// (Save Preset, replacing one, the preset files, a macro's mappings) are the
// panel's own.
Rectangle {
    id: panel

    signal statusMessage(string message)

    readonly property alias area: area
    readonly property alias menu: menu
    readonly property alias chain: topChain
    readonly property alias hint: hint
    readonly property alias dropMarker: dropMarker
    readonly property alias loadMarker: loadMarker
    readonly property alias presetNameDialog: nameDialog
    readonly property alias presetReplaceDialog: replaceDialog
    readonly property alias macroMappings: macroMappings
    readonly property int panelMargin: 8  // PANEL_MARGIN: above and below the chain, and left and right of it
    // The tallest device: its border, a title bar, a page of knobs in two rows
    // (DeviceKnobPages: 14 px apart) with 6 px above and below them (a body's
    // margins: its content is as far from the title bar as from the bottom
    // edge), and `editorRoom` more for the editors' graphs (the knobs stay
    // centred). Every device is this tall; an editor's graphs take the height there is.
    readonly property int editorRoom: 24
    readonly property int deviceHeight: 2 + Math.max(16, titleMetrics.height) + 4 + 6 + 2 * probe.implicitHeight + 14 + 6
                                        + editorRoom

    // Ctrl+R on a rack's chain: its name edited in place, its rack's chain list
    // shown first if it is hidden (false: the rack isn't shown, or is folded).
    function startChainRename(rackId, chainId) {
        const row = area.chainRowOf(rackId, chainId)
        if (row) {
            area.scrollTo(rackId)
            row.startRename()
            return true
        }
        const rack = area.frameOf(rackId)
        if (!rack || rack.info.folded || rack.info.chainListShown)
            return false
        Session.deviceSelection.toggleChainList(rackId)  // (its rows are made: rename once they are)
        Qt.callLater(() => {
            const shown = area.chainRowOf(rackId, chainId)
            if (shown) {
                area.scrollTo(rackId)
                shown.startRename()
            }
        })
        return true
    }

    // The device view takes the focus: the Edit menu acts on its devices.
    function focusDevices() {
        if (Session.deviceSelection.trackId !== "")
            Session.selection.focusDevices()
        panel.forceActiveFocus()
    }

    // --- Menus ---------------------------------------------------------------------------------

    // A device's right-click menu: its own entries (a plug-in's editor and its
    // format's presets, a rack's Add Chain and what it shows, an editor's), Fold, the
    // clipboard's (for the selected devices), Move Left and Right, presets,
    // Group and Ungroup, Delete.
    function showDeviceMenu(frame, at, x, y) {
        const id = frame.deviceId
        const trackId = frame.trackId
        const info = frame.info
        const devices = Session.deviceSelection
        menu.reset()
        if (info.isPlugin) {
            menu.entry(qsTr("Show Editor"), () => Session.bridge.openPluginEditor(trackId, id), undefined, info.loaded)
            if (info.pluginFormat === "VST2") {
                menu.entry(qsTr("Load VST2 Preset…"), () => panel.loadVst2Preset(id), undefined, info.loaded)
                menu.entry(qsTr("Save VST2 Preset…"), () => panel.saveVst2Preset(id, info.name), undefined, info.loaded)
            } else {
                menu.entry(qsTr("Load VST3 Preset…"), () => panel.loadVst3Preset(id), undefined, info.loaded)
                menu.entry(qsTr("Save VST3 Preset…"), () => panel.saveVst3Preset(id, info.name), undefined, info.loaded)
            }
            menu.entry(qsTr("Reload Plug-in"), () => Session.bridge.reloadPlugin(trackId, id), undefined,
                       info.loaded || info.reloadable)
            menu.separator()
        }
        if (info.kind === "vital") {
            menu.entry(qsTr("Load Vital Preset…"), () => panel.loadVitalPreset(id))
            menu.entry(qsTr("Save Vital Preset…"), () => panel.saveVitalPreset(id, info.name))
            menu.separator()
        }
        if (info.isRack) {
            menu.entry(info.isDrumRack ? qsTr("Add Pad") : qsTr("Add Chain"), () => {
                if (Session.editor.tryAddRackChain(trackId, id) !== "")
                    Session.editor.setChainListShown(trackId, id, true)  // (where it shows)
            })
            menu.entry(info.chainListShown ? qsTr("Hide Chain List") : qsTr("Show Chain List"),
                       () => devices.toggleChainList(id))
            menu.entry(info.rackDevicesShown ? qsTr("Hide Devices") : qsTr("Show Devices"),
                       () => devices.toggleRackDevices(id))
            menu.separator()
        }
        const body = frame.body
        if (body && body.menuActions !== undefined) {
            for (let i = 0; i < body.menuActions.length; ++i)
                menu.addAction(body.menuActions[i])
        }
        menu.entry(info.folded ? qsTr("Unfold") : qsTr("Fold"), () => devices.toggleFold(id))
        menu.separator()
        menu.hinted(qsTr("Cut"), () => devices.cutSelected(), KeyMap.primary("cut"))
        menu.hinted(qsTr("Copy"), () => devices.copySelected(), KeyMap.primary("copy"))
        menu.hinted(qsTr("Paste"), () => devices.pasteAfter(id), KeyMap.primary("paste"), devices.hasClipboard)
        menu.hinted(qsTr("Duplicate"), () => devices.duplicateSelected(), KeyMap.primary("duplicate"))
        menu.separator()
        if (!info.instrument) {  // (an instrument doesn't move, and nothing goes before it)
            const index = devices.chainDevices(info.chainId).indexOf(id)
            menu.entry(qsTr("Move Left"), () => Session.editor.moveDevice(trackId, id, index - 1), undefined,
                       info.canMoveLeft)
            menu.entry(qsTr("Move Right"), () => Session.editor.moveDevice(trackId, id, index + 1), undefined,
                       info.canMoveRight)
            menu.separator()
        }
        menu.entry(qsTr("Save Preset…"), () => panel.savePreset(id))
        if (!info.isRack) {
            menu.entry(qsTr("Save as Default Preset"), () => devices.saveAsDefault(id))
            menu.entry(qsTr("Clear Default Preset"), () => devices.clearDefault(id), undefined, devices.hasDefault(id))
        }
        menu.separator()
        menu.hinted(qsTr("Group"), () => devices.groupDevice(id), KeyMap.primary("groupTracks"))
        if (info.isRack)
            menu.hinted(qsTr("Ungroup"), () => devices.ungroupRack(id), KeyMap.primary("ungroupTracks"))
        menu.separator()
        menu.entry(qsTr("Delete"), () => devices.deleteSelected())
        menu.popup(at, x, y)
    }

    // A device's sidechain menu: No Sidechain, the tracks it can come from
    // (those that would close a cycle greyed out) under a search field that
    // has the keyboard as it opens, then where it is taken, and a submenu for
    // each of the source's Drum Rack pads, where it is taken in the pad.
    function showSidechainMenu(frame, at, x, y) {
        const trackId = frame.trackId
        const id = frame.deviceId
        menu.reset()
        const entries = frame.info.sidechainMenu()
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (entry.separator) {
                menu.separator()
                continue
            }
            if (entry.search) {
                menu.search(entry.children,
                            source => Session.editor.trySetDeviceSidechain(trackId, id, source.source, source.tap))
                continue
            }
            if (entry.submenu !== undefined) {  // a Drum Rack pad's own taps
                const pad = menu.submenu(entry.submenu)
                for (const child of entry.children)
                    sidechainEntry(trackId, id, child, pad)
                continue
            }
            sidechainEntry(trackId, id, entry)
        }
        menu.popup(at, x, y)
    }

    function sidechainEntry(trackId, id, entry, target) {
        menu.entry(entry.text,
                   () => Session.editor.trySetDeviceSidechain(trackId, id, entry.source, entry.tap, entry.chain),
                   entry.checked, entry.enabled, target)
    }

    // A device's MIDI From menu: Own Track, then the MIDI tracks whose notes
    // it can play instead, under a search field that has the keyboard as it opens.
    function showMidiFromMenu(frame, at, x, y) {
        const info = frame.info
        menu.reset()
        const entries = info.midiFromMenu()
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            if (entry.search) {
                menu.search(entry.children, source => info.setMidiFrom(source.source))
                continue
            }
            menu.entry(entry.text, () => info.setMidiFrom(entry.source), entry.checked, entry.enabled)
        }
        menu.popup(at, x, y)
    }

    // A rack chain's menu; after it (whatever was chosen, but Rename, and
    // unless the chain went) the chain shows beside its rack.
    function showChainMenu(row, x, y) {
        const chain = row.chain
        const trackId = row.trackId
        const rackId = row.rackId
        const chainId = row.chainId
        let renaming = false
        menu.reset()
        menu.entry(qsTr("Rename"), () => renaming = true)
        menu.entry(qsTr("Duplicate"), () => Session.editor.duplicateRackChain(trackId, chainId))
        menu.entry(qsTr("Delete"), () => Session.editor.removeRackChains(trackId, [chainId]))
        menu.separator()
        menu.entry(qsTr("Add Chain"), () => Session.editor.tryAddRackChain(trackId, rackId))
        menu.separator()
        menu.entry(qsTr("Show Volume Automation"), () => chain.showVolumeAutomation())
        menu.entry(qsTr("Show Pan Automation"), () => chain.showPanAutomation())
        menu.afterClose = () => {
            if (renaming)
                row.startRename()
            else if (area.hasChain(rackId, chainId))
                Session.deviceSelection.clickChain(rackId, chainId)
        }
        menu.popup(row, x, y)
    }

    // A macro's menu (`cell`: its RackMacroKnob): its automation, Rename, Edit
    // Mappings… (the ranges), what it moves (to unmap), and adding a macro or
    // taking the last away.
    function showMacroMenu(cell, at, x, y) {
        const macro = cell.macro
        const trackId = macro.trackId
        const rackId = macro.rackId
        let renaming = false
        menu.reset()
        menu.automationEntries(macro)
        menu.separator()
        menu.entry(qsTr("Rename"), () => renaming = true)
        menu.entry(qsTr("Edit Mappings…"), () => macroMappings.show(macro, cell), undefined, macro.mapped)
        menu.separator()
        const entries = macro.unmapEntries()
        if (entries.length === 0)
            menu.entry(qsTr("Nothing mapped (right-click a parameter in the rack to map it)"), () => {}, undefined, false)
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            menu.entry(entry.text, () => Session.editor.unmapMacro(trackId, rackId, entry.deviceId, entry.paramId))
        }
        menu.separator()
        menu.entry(qsTr("Add Macro"), () => macro.addMacro(), undefined, macro.canAddMacro())
        menu.entry(qsTr("Remove Last Macro"), () => macro.removeLastMacro(), undefined, macro.canRemoveMacro())
        menu.afterClose = () => {
            if (renaming)
                cell.startRename()
        }
        menu.popup(at, x, y)
    }

    // Beside the devices (at x, y of the chain's area): paste there, or load a preset there.
    function showBesideMenu(x, y) {
        const devices = Session.deviceSelection
        if (devices.trackId === "")
            return
        const target = area.dropTarget(x, y)
        Session.selection.focusDevices()
        menu.reset()
        menu.entry(qsTr("Paste"), () => devices.pasteAt(target.chain, target.index), undefined, devices.hasClipboard)
        menu.separator()
        menu.entry(qsTr("Load Preset…"), () => panel.loadPresetFile(target.chain, target.index))
        menu.popup(area, x, y)
    }

    // --- Presets -------------------------------------------------------------------------------

    // The save button (and Save Preset…): the device saved to the library under
    // a name asked for (its name to start with), asking before replacing one.
    function savePreset(deviceId) {
        nameDialog.deviceId = deviceId
        nameField.text = Session.deviceSelection.presetName(deviceId)
        nameDialog.open()
    }

    // The name given: saved, unless it can't be a preset's (said why) or one
    // of that name is there (asked first).
    function savePresetNamed(deviceId, name) {
        const check = Session.deviceSelection.checkPresetName(deviceId, name)
        if (check.error !== undefined) {
            panel.statusMessage(check.error)
            return
        }
        if (check.exists) {
            replaceDialog.deviceId = deviceId
            replaceDialog.name = name
            replaceDialog.question = check.question
            replaceDialog.open()
            return
        }
        Session.deviceSelection.savePreset(deviceId, name)
    }

    // Load Preset… beside the devices: a preset file, as a new device at that place.
    function loadPresetFile(chain, index) {
        presetFileDialog.chain = chain
        presetFileDialog.index = index
        presetFileDialog.currentFolder = FileUrls.fileUrl(Session.deviceSelection.presetFolder())
        presetFileDialog.open()
    }

    function loadVst3Preset(deviceId) {
        vst3LoadDialog.deviceId = deviceId
        vst3LoadDialog.currentFolder = FileUrls.fileUrl(Session.deviceSelection.vst3PresetFolder(deviceId))
        vst3LoadDialog.open()
    }

    function saveVst3Preset(deviceId, name) {
        const folder = Session.deviceSelection.vst3PresetFolder(deviceId)
        vst3SaveDialog.deviceId = deviceId
        vst3SaveDialog.currentFolder = FileUrls.fileUrl(folder)
        vst3SaveDialog.selectedFile = FileUrls.fileUrl(folder + "/" + name + ".vstpreset")
        vst3SaveDialog.open()
    }

    // A VST2 plug-in's: an .fxp (a program) or .fxb (a bank), as its state is
    // (a plug-in that keeps a chunk saves a bank).
    function loadVst2Preset(deviceId) {
        vst2LoadDialog.deviceId = deviceId
        vst2LoadDialog.currentFolder = FileUrls.fileUrl(Session.deviceSelection.vst3PresetFolder(deviceId))
        vst2LoadDialog.open()
    }

    function saveVst2Preset(deviceId, name) {
        const folder = Session.deviceSelection.vst3PresetFolder(deviceId)
        const suffix = Session.deviceSelection.pluginPresetSuffix(deviceId)
        vst2SaveDialog.deviceId = deviceId
        vst2SaveDialog.nameFilters = suffix === "fxb" ? [qsTr("VST2 Bank (*.fxb)")] : [qsTr("VST2 Preset (*.fxp)")]
        vst2SaveDialog.currentFolder = FileUrls.fileUrl(folder)
        vst2SaveDialog.selectedFile = FileUrls.fileUrl(folder + "/" + name + "." + suffix)
        vst2SaveDialog.open()
    }

    function loadVitalPreset(deviceId) {
        vitalLoadDialog.deviceId = deviceId
        vitalLoadDialog.currentFolder = FileUrls.fileUrl(Session.deviceSelection.vitalPresetFolder())
        vitalLoadDialog.open()
    }

    function saveVitalPreset(deviceId, name) {
        const folder = Session.deviceSelection.vitalPresetFolder()
        vitalSaveDialog.deviceId = deviceId
        vitalSaveDialog.currentFolder = FileUrls.fileUrl(folder)
        vitalSaveDialog.selectedFile = FileUrls.fileUrl(folder + "/" + name + ".vital")
        vitalSaveDialog.open()
    }

    implicitHeight: 2 * panelMargin + deviceHeight
    color: Theme.window

    // A click beside the devices.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            if (mouse.button === Qt.LeftButton) {
                Session.deviceSelection.clickBeside()
            } else {
                const at = mapToItem(area, mouse.x, mouse.y)
                panel.showBesideMenu(at.x, at.y)
            }
        }
    }

    DeviceChainArea {
        id: area
        objectName: "chainArea"
        x: panel.panelMargin
        y: panel.panelMargin
        width: panel.width - 2 * panel.panelMargin
        height: panel.height - 2 * panel.panelMargin
        session: Session
        content: chainContent
        clip: true

        Row {
            id: chainContent
            objectName: "chainContent"
            x: -area.contentX
            height: area.height
            spacing: 8  // (the chain's, DeviceChainArea.kSpacing)

            DeviceChain {
                id: topChain
                objectName: "chain"
                height: chainContent.height
                visible: count > 0
                chainId: ""
                panel: panel
            }

            // The hint; while a track is shown, in an empty place as tall as a device, where what it asks
            // for drops (EMPTY_AREA in a line of DEVICE_HEADER, at least as wide as a device).
            Item {
                id: hintPlace
                readonly property bool boxed: Session.deviceSelection.trackId !== ""
                width: boxed ? Math.max(216, hint.implicitWidth + 2 * 16) : hint.implicitWidth
                height: chainContent.height
                visible: hint.text !== ""

                Rectangle {
                    anchors.fill: parent
                    visible: hintPlace.boxed
                    radius: Theme.radius
                    color: Theme.emptyArea
                    border.color: Theme.deviceHeader
                }
                Text {
                    id: hint
                    objectName: "hint"
                    x: Math.round((parent.width - width) / 2)
                    height: parent.height
                    visible: text !== ""
                    text: Session.deviceSelection.hint
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.textDim
                    font: Theme.font
                }
            }
        }

        // Where dragged devices would go: a line between devices; or the device a preset would load into,
        // outlined in the accent (its corners the frame's) over a faint wash of it.
        Item {
            x: -area.contentX
            Rectangle {
                id: dropMarker
                objectName: "dropMarker"
                visible: area.dropMarker.width > 0
                x: area.dropMarker.x
                y: area.dropMarker.y
                width: area.dropMarker.width
                height: area.dropMarker.height
                radius: 1
                color: Theme.accent
            }
            Rectangle {
                id: loadMarker
                objectName: "loadMarker"
                visible: area.loadMarker.width > 0
                x: area.loadMarker.x
                y: area.loadMarker.y
                width: area.loadMarker.width
                height: area.loadMarker.height
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                radius: Theme.radius
                border.width: 2
                border.color: Theme.accent
            }
        }
    }

    // Measured, not shown: a parameter's cell, for the tallest device's height.
    DeviceParamKnob {
        id: probe
        visible: false
    }
    FontMetrics {
        id: titleMetrics
        font: Theme.titleFont
    }

    PanelMenu {
        id: menu
        objectName: "panelMenu"
    }

    RackMacroMappings {
        id: macroMappings
    }

    Dialog {
        id: nameDialog
        objectName: "presetNameDialog"

        property string deviceId

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Save Preset")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        onAccepted: panel.savePresetNamed(deviceId, nameField.text)

        Column {
            spacing: 6

            Label {
                text: qsTr("Preset name:")
            }
            TextField {
                id: nameField
                objectName: "presetNameField"
                width: 260
                onAccepted: nameDialog.accept()
            }
        }
    }

    Dialog {
        id: replaceDialog
        objectName: "presetReplaceDialog"

        property string deviceId
        property string name
        property alias question: questionLabel.text

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Save Preset")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Session.deviceSelection.savePreset(deviceId, name)

        Column {
            Label {
                id: questionLabel
                width: Math.min(questionMetrics.advanceWidth + 2, 420)
                wrapMode: Text.Wrap

                TextMetrics {
                    id: questionMetrics
                    font: questionLabel.font
                    text: questionLabel.text
                }
            }
        }
    }

    FileDialog {
        id: presetFileDialog
        objectName: "presetFileDialog"

        property string chain
        property int index: -1

        title: qsTr("Load Preset")
        nameFilters: [qsTr("SUBstation Preset (*.gilpreset)"), qsTr("Vital Preset (*.vital)")]
        onAccepted: Session.deviceSelection.loadPresetFile(FileUrls.localPath(selectedFile), chain, index)
    }

    FileDialog {
        id: vst3LoadDialog
        objectName: "vst3LoadDialog"

        property string deviceId

        title: qsTr("Load VST3 Preset")
        nameFilters: [qsTr("VST3 Preset (*.vstpreset)")]
        onAccepted: Session.deviceSelection.loadVst3Preset(deviceId, FileUrls.localPath(selectedFile))
    }

    FileDialog {
        id: vst3SaveDialog
        objectName: "vst3SaveDialog"

        property string deviceId

        title: qsTr("Save VST3 Preset")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("VST3 Preset (*.vstpreset)")]
        onAccepted: Session.deviceSelection.saveVst3Preset(deviceId, FileUrls.localPath(selectedFile))
    }

    FileDialog {
        id: vst2LoadDialog
        objectName: "vst2LoadDialog"

        property string deviceId

        title: qsTr("Load VST2 Preset")
        nameFilters: [qsTr("VST2 Preset or Bank (*.fxp *.fxb)")]
        onAccepted: Session.deviceSelection.loadVst3Preset(deviceId, FileUrls.localPath(selectedFile))
    }

    FileDialog {
        id: vst2SaveDialog
        objectName: "vst2SaveDialog"

        property string deviceId

        title: qsTr("Save VST2 Preset")
        fileMode: FileDialog.SaveFile
        onAccepted: Session.deviceSelection.saveVst3Preset(deviceId, FileUrls.localPath(selectedFile))
    }

    FileDialog {
        id: vitalLoadDialog
        objectName: "vitalLoadDialog"

        property string deviceId

        title: qsTr("Load Vital Preset")
        nameFilters: [qsTr("Vital Preset (*.vital)")]
        onAccepted: Session.deviceSelection.loadVitalPreset(deviceId, FileUrls.localPath(selectedFile))
    }

    FileDialog {
        id: vitalSaveDialog
        objectName: "vitalSaveDialog"

        property string deviceId

        title: qsTr("Save Vital Preset")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Vital Preset (*.vital)")]
        onAccepted: Session.deviceSelection.saveVitalPreset(deviceId, FileUrls.localPath(selectedFile))
    }
}
