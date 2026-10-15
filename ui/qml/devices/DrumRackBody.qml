import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import SUBstation

// The body of a Drum Rack, laid out as Ableton's: a strip of buttons (show the
// chain list, show the selected pad's devices beside the rack, add a macro,
// take the last away), an overview of all 128 keys (lit where a pad is; click
// it to show those pads), sixteen pads, four by four (DrumPad; the arrows
// beside them or the wheel move them a row of keys), its macros unless
// collapsed (the ▾ in the device's title bar, beside the similar sounds'
// arrows: DeviceFrame), and, while shown, its chain list (RackDeviceBody's
// rows: a pad each). The device's menu has
// Save Pads as Similar References and Return Pads to References (menuActions);
// a pad's own menu locks it, saves or returns to its reference, renames,
// duplicates or deletes it.
Item {
    id: body

    required property string trackId
    required property string deviceId
    required property var panel
    required property DeviceInfo info
    readonly property DrumRack drums: drumRack
    readonly property RackChains chains: rackChains
    readonly property RackMacros macroCount: rackMacros
    readonly property alias pads: padRepeater
    readonly property alias overview: overview
    readonly property alias rows: rowRepeater
    readonly property alias chainList: list
    readonly property bool chainListShown: info.chainListShown
    readonly property bool macrosShown: info.rackMacrosShown
    readonly property real padWidth: 50
    readonly property real padSpacing: 2

    // What the device's right-click menu has before Fold.
    readonly property list<Action> menuActions: [
        Action {
            objectName: "saveRackReferences"
            text: qsTr("Save Pads as Similar References")
            enabled: drumRack.padCount > drumRack.lockedCount
            onTriggered: Session.similarSamples.saveRackReferences(body.trackId, body.deviceId)
        },
        Action {
            objectName: "returnRackToReferences"
            text: qsTr("Return Pads to References")
            enabled: {
                drumRack.pads  // (again as the pads change)
                return Session.similarSamples.rackCanReturn(body.trackId, body.deviceId)
            }
            onTriggered: Session.similarSamples.returnRackToReferences(body.trackId, body.deviceId)
        }
    ]

    readonly property real macrosLeft: similar.x + similar.width + 8
    readonly property real macrosRight: macrosShown ? macrosLeft + macroGrid.width : similar.x + similar.width
    implicitWidth: (chainListShown ? macrosRight + 12 + 214 : macrosRight) + 8
    implicitHeight: 6 + Math.max(macroGrid.implicitHeight, strip.implicitHeight, 100) + 6

    // A pad's menu.
    function showPadMenu(pad, x, y) {
        const menu = body.panel.menu
        const note = pad.note
        const chainId = pad.chainId
        const sampler = drumRack.samplerOf(note)
        let renaming = false
        menu.reset()
        menu.entry(qsTr("Load Sample…"), () => { fileDialog.note = note; fileDialog.open() })
        if (chainId !== "") {
            menu.entry(pad.modelData.locked ? qsTr("Unlock Pad") : qsTr("Lock Pad"), () => drumRack.toggleLock(note))
            menu.separator()
            menu.entry(qsTr("Save as Similar Reference"),
                       () => Session.similarSamples.saveReference(body.trackId, sampler), undefined, sampler !== "")
            menu.entry(qsTr("Return to Reference"),
                       () => Session.similarSamples.returnToReference(body.trackId, sampler), undefined,
                       sampler !== "" && Session.similarSamples.canReturn(body.trackId, sampler))
            menu.entry(qsTr("Previous Similar Sound"), () => Session.similarSamples.step(body.trackId, sampler, -1),
                       undefined, sampler !== "")
            menu.entry(qsTr("Next Similar Sound"), () => Session.similarSamples.step(body.trackId, sampler, 1),
                       undefined, sampler !== "")
            menu.separator()
            menu.entry(qsTr("Rename"), () => renaming = true)
            menu.entry(qsTr("Duplicate"), () => Session.editor.duplicateRackChain(body.trackId, chainId))
            menu.entry(qsTr("Delete"), () => drumRack.remove(note))
        }
        menu.afterClose = () => {
            if (renaming)
                pad.startRename()
        }
        menu.popup(pad, x, y)
    }

    DrumRack {
        id: drumRack
        session: Session
        trackId: body.trackId
        rackId: body.deviceId
    }
    RackChains {
        id: rackChains
        session: Session
        trackId: body.trackId
        rackId: body.deviceId
    }
    RackMacros {
        id: rackMacros
        session: Session
        trackId: body.trackId
        rackId: body.deviceId
    }

    Column {
        id: strip
        x: 6
        y: 6
        spacing: 2

        DeviceHeaderButton {
            objectName: "chainListButton"
            iconName: "chain_list"
            checkable: false
            checked: body.chainListShown
            tooltip: body.chainListShown ? qsTr("Hide the chain list") : qsTr("Show the chain list")
            onClicked: Session.deviceSelection.toggleChainList(body.deviceId)
        }
        DeviceHeaderButton {
            objectName: "devicesButton"
            iconName: "rack_devices"
            checkable: false
            checked: body.info.rackDevicesShown
            tooltip: body.info.rackDevicesShown ? qsTr("Hide the pad's devices") : qsTr("Show the pad's devices")
            onClicked: Session.deviceSelection.toggleRackDevices(body.deviceId)
        }
        DeviceHeaderButton {
            objectName: "addMacroButton"
            visible: body.macrosShown
            checkable: false
            text: "+"
            enabled: rackMacros.count < rackMacros.maximum
            tooltip: qsTr("Add a macro")
            onClicked: rackMacros.add()
        }
        DeviceHeaderButton {
            objectName: "removeMacroButton"
            visible: body.macrosShown
            checkable: false
            text: "−"
            enabled: rackMacros.count > 1
            tooltip: qsTr("Take the last macro away (with its mappings and automation)")
            onClicked: rackMacros.remove()
        }
    }

    // Every key, four to a row, the lowest at the bottom: lit where a pad is,
    // framed where the pads show. A click shows the pads there.
    Item {
        id: overview
        objectName: "overview"
        x: strip.x + strip.width + 6
        y: 6
        width: 16
        height: body.height - 12
        readonly property real rowHeight: height / 32

        Rectangle {
            anchors.fill: parent
            color: Theme.meterBg
            border.color: Theme.border
        }
        Repeater {
            model: 128
            Rectangle {
                required property int index
                readonly property bool lit: drumRack.filled.length > index && drumRack.filled[index]
                visible: lit
                x: 1 + (index % 4) * (overview.width - 2) / 4
                y: overview.height - (Math.floor(index / 4) + 1) * overview.rowHeight
                width: (overview.width - 2) / 4 - 1
                height: Math.max(1, overview.rowHeight - 1)
                color: Theme.accent
                opacity: 0.8
            }
        }
        Rectangle {
            objectName: "shownPads"
            x: 0
            width: overview.width
            y: overview.height - (drumRack.firstNote / 4 + 4) * overview.rowHeight
            height: 4 * overview.rowHeight
            color: "transparent"
            border.color: Theme.text
        }
        MouseArea {
            anchors.fill: parent
            onPressed: event => drumRack.firstNote = Math.round((overview.height - event.y) / overview.rowHeight - 2) * 4
            onPositionChanged: event => {
                if (pressed)
                    drumRack.firstNote = Math.round((overview.height - event.y) / overview.rowHeight - 2) * 4
            }
            onWheel: event => drumRack.scroll(event.angleDelta.y > 0 ? 1 : -1)
        }
        HoverHandler {
            id: overviewHover
        }
        ToolTip.visible: overviewHover.hovered
        ToolTip.text: qsTr("Every key: click to show its pads (%1 to %2 shown)")
                      .arg(drumRack.pads.length > 12 ? drumRack.pads[12].noteName : "")
                      .arg(drumRack.pads.length > 3 ? drumRack.pads[3].noteName : "")
        ToolTip.delay: 700
    }

    // The pads, four by four, the lowest key at the bottom left.
    Grid {
        id: padGrid
        objectName: "padGrid"
        x: overview.x + overview.width + 6
        y: 6
        columns: 4
        spacing: body.padSpacing

        Repeater {
            id: padRepeater
            model: drumRack.pads
            DrumPad {
                width: body.padWidth
                height: (body.height - 12 - 3 * body.padSpacing) / 4
                rack: drumRack
                body: body
                trackId: body.trackId
                rackId: body.deviceId
            }
        }
    }

    // The rows of keys up and down, and how many pads are locked.
    Column {
        id: similar
        objectName: "similar"
        x: padGrid.x + padGrid.width + 6
        y: 6
        spacing: 2

        DeviceHeaderButton {
            objectName: "padsUp"
            text: "▴"
            checkable: false
            enabled: drumRack.firstNote < 112  // (DrumRack::kLastFirstNote)
            tooltip: qsTr("The pads a row of keys up")
            onClicked: drumRack.scroll(1)
        }
        DeviceHeaderButton {
            objectName: "padsDown"
            text: "▾"
            checkable: false
            enabled: drumRack.firstNote > 0
            tooltip: qsTr("The pads a row of keys down")
            onClicked: drumRack.scroll(-1)
        }
        Text {
            objectName: "lockedCount"
            visible: drumRack.lockedCount > 0
            text: qsTr("%1 locked").arg(drumRack.lockedCount)
            color: Theme.textDim
            font: Theme.uiFont(7)
        }
    }

    Grid {
        id: macroGrid
        objectName: "macroGrid"
        visible: body.macrosShown
        x: body.macrosLeft
        y: Math.max(6, Math.round((body.height - height) / 2))
        columns: Math.max(1, Math.ceil(rackMacros.count / 2))
        columnSpacing: 4
        rowSpacing: 4

        Repeater {
            model: rackMacros.count
            RackMacroKnob {
                required property int index
                objectName: "macro" + index
                trackId: body.trackId
                rackId: body.deviceId
                macroIndex: index
                panel: body.panel
            }
        }
    }

    // The chain list: a row a pad.
    Rectangle {
        id: list
        objectName: "chainList"
        visible: body.chainListShown
        x: body.macrosRight + 12
        y: 6
        width: body.width - x - 8
        height: body.height - y - 6
        color: Theme.panel
        border.color: Theme.border

        Flickable {
            id: rowsView
            x: 1
            y: 1
            width: parent.width - 2
            height: parent.height - 2
            clip: true
            interactive: false
            contentWidth: width
            contentHeight: rowsColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: rowsView.contentHeight > rowsView.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            WheelHandler {
                acceptedModifiers: Qt.NoModifier
                onWheel: event => rowsView.contentY = Math.max(0, Math.min(Math.max(0, rowsView.contentHeight - rowsView.height),
                                                                           rowsView.contentY - event.angleDelta.y / 120 * 60))
            }

            Column {
                id: rowsColumn
                width: rowsView.width
                spacing: 1

                Repeater {
                    id: rowRepeater
                    model: body.chainListShown ? rackChains.chainIds : []
                    RackChainRow {
                        required property string modelData
                        objectName: "chainRow_" + modelData
                        width: rowsColumn.width
                        trackId: body.trackId
                        rackId: body.deviceId
                        chainId: modelData
                        selected: rackChains.shownChain === modelData
                        panel: body.panel
                    }
                }
                Text {
                    visible: rackChains.count === 0
                    width: rowsColumn.width
                    leftPadding: 2
                    text: qsTr("No pads yet.\nDrop samples on the pads.")
                    wrapMode: Text.Wrap
                    color: Theme.textDisabled
                    font: Theme.uiFont(8)
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        property int note: 36
        title: qsTr("Load Sample")
        nameFilters: [qsTr("Audio Files (*.wav *.wave *.flac *.mp3)")]
        onAccepted: drumRack.loadSample(note, FileUrls.localPath(selectedFile))
    }
}
