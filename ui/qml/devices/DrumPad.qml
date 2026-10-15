import QtQuick
import QtQuick.Controls
import SUBstation

// One of a Drum Rack's pads (DrumRackBody): its name (its sample's, or as
// named) and its key; lit while the device view shows its devices beside the
// rack (and while an audio drag is over it). Drop an audio file or a clip from
// the arrangement on it to load it (DrumPadItem); a pad with a chain takes
// devices dropped on it into its chain, as a chain's row does. A click shows
// its devices and plays its key while held; right-click for its menu (the
// body's). Its mute (the activator), solo and lock (similar sounds leave its
// sample be) are along its bottom. Renamed in place (startRename(): Ctrl+R).
DrumPadItem {
    id: pad

    required property var modelData  // DrumRack.pads' entry
    required property DrumRack rack
    required property var body
    property string trackId
    property string rackId
    readonly property string chainId: modelData.chainId
    readonly property bool filled: chainId !== ""
    // Devices dropped on a pad with a chain go into it (DeviceChainArea's roles).
    readonly property string panelRole: filled ? "chainRow" : ""
    readonly property alias nameLabel: name
    readonly property alias activator: activator
    readonly property alias solo: solo
    readonly property alias lock: lock
    readonly property alias renameField: renameField

    objectName: "pad_" + modelData.note
    drumRack: rack
    note: modelData.note

    function startRename() {
        if (!filled || renameField.visible)
            return
        renameField.text = modelData.name
        renameField.visible = true
        renameField.selectAll()
        renameField.forceActiveFocus()
    }

    function finishRename() {
        if (!renameField.visible)
            return
        const typed = renameField.text.trim()
        renameField.visible = false
        if (typed !== "" && filled)
            Session.editor.renameChain(pad.trackId, pad.chainId, typed)
    }

    Rectangle {
        id: face
        anchors.fill: parent
        color: pad.dropping ? Theme.surfaceHover : pad.modelData.selected ? Theme.surfaceHover
               : pad.filled ? Theme.surface : Theme.meterBg
        border.color: pad.dropping || pad.modelData.selected ? Theme.accent : Theme.border
        border.width: pad.dropping ? 2 : 1
        opacity: pad.filled && pad.modelData.mute ? 0.6 : 1.0
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property bool playing: false
        onPressed: event => {
            if (event.button === Qt.RightButton) {
                pad.body.showPadMenu(pad, event.x, event.y)
                return
            }
            pad.rack.select(pad.note)
            pad.rack.play(pad.note, 100)
            playing = true
        }
        onReleased: {
            if (playing)
                pad.rack.play(pad.note, 0)
            playing = false
        }
        onCanceled: {
            if (playing)
                pad.rack.play(pad.note, 0)
            playing = false
        }
        onDoubleClicked: event => {
            if (event.button === Qt.LeftButton && pad.filled)
                pad.startRename()
        }
        // The wheel moves the pads a row of keys.
        onWheel: event => pad.rack.scroll(event.angleDelta.y > 0 ? 1 : -1)
    }

    Text {
        id: name
        objectName: "name"
        x: 3
        y: 2
        width: parent.width - 6
        text: pad.modelData.name
        elide: Text.ElideRight
        color: Theme.text
        font: Theme.uiFont(8)
        visible: !renameField.visible
    }
    Text {
        objectName: "noteName"
        x: 3
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        text: pad.modelData.noteName
        color: Theme.textDim
        font: Theme.uiFont(7)
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        spacing: 1
        visible: pad.filled && pad.height >= 22

        ToggleButton {
            id: lock
            objectName: "lock"
            width: 11
            height: 11
            role: "small"
            checkable: false
            checked: pad.modelData.locked
            iconName: "lock_envelopes"
            iconSize: 8
            tooltip: pad.modelData.locked ? qsTr("Locked: the rack's similar sounds leave its sample be (click to unlock)")
                                          : qsTr("Lock its sample: the rack's similar sounds leave it be")
            onClicked: pad.rack.toggleLock(pad.note)
        }
        ToggleButton {
            id: activator
            objectName: "activator"
            width: 11
            height: 11
            role: "activator"
            checkable: false
            checked: !pad.modelData.mute
            tooltip: qsTr("Pad Activator (unmute)")
            onClicked: pad.rack.toggleMute(pad.note)
        }
        ToggleButton {
            id: solo
            objectName: "solo"
            width: 11
            height: 11
            role: "solo"
            text: "S"
            font.pointSize: 6
            checkable: false
            checked: pad.modelData.solo
            tooltip: qsTr("Solo: only the soloed pads are heard")
            onClicked: pad.rack.toggleSolo(pad.note)
        }
    }

    TextField {
        id: renameField
        objectName: "renameField"
        visible: false
        x: 1
        y: 1
        width: parent.width - 2
        height: Math.min(parent.height - 2, 16)
        font: Theme.uiFont(8)
        padding: 1
        onAccepted: pad.finishRename()
        onEditingFinished: pad.finishRename()
        Keys.onEscapePressed: pad.finishRename()
    }

    HoverHandler {
        id: hover
    }
    ToolTip.visible: hover.hovered && !mouse.pressed && pad.filled && !renameField.visible
    ToolTip.text: pad.modelData.sample !== "" ? pad.modelData.name + "\n" + pad.modelData.sample : pad.modelData.name
    ToolTip.delay: 700
}
