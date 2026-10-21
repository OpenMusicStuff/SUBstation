import QtQuick
import SUBstation

// A rack's chain shown beside the rack: its devices, in a quiet bracket (the
// rack's title bar's colour, so it turns teal with its rack selected), where
// they are selected, dragged and dropped onto as on the track's own chain
// (racks in it show theirs further along); "Drop devices here" while it has
// none, in the middle of a place as wide as a device.
Item {
    id: view

    objectName: "chainView"
    readonly property string panelRole: "chain"
    property string chainId
    property var panel
    // The bracket's colour (DEVICE_HEADER; its rack's title bar's, from DeviceChain).
    property color bracketColor: Theme.deviceHeader
    readonly property alias devices: devices

    // (Its last device's meter inside the bracket too, in a gap as wide as the chain's spacing.)
    implicitWidth: devices.count > 0 ? 6 + devices.width + 8 + 6 : Math.max(216, 6 + hint.implicitWidth + 6)

    // EMPTY_AREA, its corners the bracket's.
    Rectangle {
        anchors.fill: parent
        color: Theme.emptyArea
    }

    // "[ ]": an upright 2 px line at each side, a 5 px foot at its top and bottom pointing in.
    Repeater {
        model: 2
        Item {
            id: side
            required property int index
            x: side.index === 0 ? 0 : view.width - width
            width: 5
            height: view.height

            Rectangle {
                x: side.index === 0 ? 0 : parent.width - width
                width: 2
                height: parent.height
                color: view.bracketColor
            }
            Rectangle {
                width: 5
                height: 2
                color: view.bracketColor
            }
            Rectangle {
                y: parent.height - height
                width: 5
                height: 2
                color: view.bracketColor
            }
        }
    }

    DeviceChain {
        id: devices
        x: 6
        height: view.height
        visible: count > 0
        chainId: view.chainId
        panel: view.panel
    }

    Text {
        id: hint
        objectName: "chainHint"
        x: Math.round((view.width - width) / 2)
        height: view.height
        visible: devices.count === 0
        verticalAlignment: Text.AlignVCenter
        text: qsTr("Drop devices here")
        color: Theme.textDim
        font: Theme.font
    }
}
