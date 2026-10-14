import QtQuick
import QtQuick.Controls
import SUBstation

// What a modulator (an LFO, an Envelope) moves, in its editor (its Map button,
// ModulatorMapButton, is the editor's): how many, × for all, then a row per
// target: its device and parameter, the range it moves over as a bar (drag
// either end, or the middle for both; double-click for the whole range; its
// ends' values in the parameter's units as you drag), where it is now as the
// modulator plays, Invert, and × to unmap it. The rows are a ListModel kept in
// step with the mappings in place (sync()), so a row being dragged outlives the
// edits its drag makes. Right-click a parameter anywhere for Modulate With too.
Item {
    id: targets

    required property string trackId
    required property string deviceId
    // The modulator's value now (0..1; < 0: nothing yet), for each row's marker.
    property real output: -1
    readonly property alias mappings: mappings
    readonly property alias list: list

    implicitWidth: 172
    implicitHeight: 120

    ModulatorMappings {
        id: mappings
        session: Session
        trackId: targets.trackId
        deviceId: targets.deviceId
        onRowsChanged: targets.sync()
    }
    Component.onCompleted: sync()

    // The rows, as the mappings have them: changed in place while they are the same targets in the same order.
    ListModel {
        id: rows
    }
    function sync() {
        const now = mappings.rows
        let same = rows.count === now.length
        for (let i = 0; same && i < now.length; ++i) {
            const row = rows.get(i)
            same = row.deviceId === now[i].deviceId && row.paramId === now[i].paramId
        }
        if (!same) {
            rows.clear()
            for (let i = 0; i < now.length; ++i)
                rows.append(now[i])
            return
        }
        for (let i = 0; i < now.length; ++i) {
            for (const role of ["device", "param", "low", "high", "setLow", "setHigh", "invert", "lowText", "highText"]) {
                if (rows.get(i)[role] !== now[i][role])
                    rows.setProperty(i, role, now[i][role])
            }
        }
    }

    Row {
        id: headerRow
        width: parent.width
        spacing: 4

        Text {
            width: parent.width - clearButton.width - parent.spacing
            height: clearButton.height
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignLeft
            text: mappings.count === 0 ? qsTr("nothing yet") : mappings.count === 1 ? qsTr("1 target")
                                                                                       : qsTr("%1 targets").arg(mappings.count)
            color: Theme.textDim
            font: Theme.uiFont(7)
            elide: Text.ElideRight
        }
        RoleButton {
            id: clearButton
            objectName: "clearTargets"
            width: 18
            height: 18
            role: "small"
            text: "×"
            enabled: mappings.count > 0
            tooltip: qsTr("Unmap everything it moves")
            onClicked: mappings.clear()
        }
    }

    ListView {
        id: list
        objectName: "targets"
        anchors.top: headerRow.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        spacing: 3
        boundsBehavior: Flickable.StopAtBounds
        model: rows
        ScrollIndicator.vertical: ScrollIndicator {}

        delegate: Rectangle {
            id: row

            required property int index
            required property string deviceId
            required property string paramId
            required property string device
            required property string param
            required property real low
            required property real high
            required property real setLow   // (the ends as set: past 0..1, a knob's handle's amount held)
            required property real setHigh
            required property bool invert

            width: ListView.view.width
            height: 32
            radius: 3
            color: Qt.rgba(1, 1, 1, rowHover.hovered ? 0.07 : 0.04)

            HoverHandler {
                id: rowHover
            }

            Text {
                id: name
                x: 5
                y: 2
                width: parent.width - x - invert.width - remove.width - 8
                text: row.device + "  ›  " + row.param
                color: Theme.text
                font: Theme.uiFont(7)
                elide: Text.ElideMiddle

                HoverHandler {
                    id: nameHover
                }
                ToolTip.visible: nameHover.hovered && name.truncated
                ToolTip.delay: 600
                ToolTip.text: name.text
            }
            RoleButton {
                id: invert
                objectName: "invert"
                anchors.right: remove.left
                anchors.rightMargin: 1
                y: 1
                width: 22
                height: 13
                role: "small"
                checkable: false
                lit: row.invert
                text: "⇅"
                font.pointSize: 7
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                tooltip: qsTr("Invert: the modulator upside down")
                onClicked: mappings.setInverted(row.deviceId, row.paramId, !row.invert)
            }
            RoleButton {
                id: remove
                objectName: "remove"
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 1
                width: 14
                height: 13
                role: "small"
                text: "×"
                font.pointSize: 7
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                tooltip: qsTr("Unmap it")
                onClicked: mappings.remove(row.deviceId, row.paramId)
            }

            // The range: min and max, normalized (min > max: upside down), dragged at either end or in the middle.
            Item {
                id: range
                objectName: "range"
                x: 5
                y: 17
                width: parent.width - 10
                height: 12

                property real low: row.low
                property real high: row.high
                readonly property real from: Math.min(low, high)
                readonly property real to: Math.max(low, high)
                property string dragging: ""   // "low", "high", "both" or ""
                property string gesture: ""
                property real grabbed: 0       // where a "both" drag took hold, normalized
                property real grabbedLow: 0
                property real grabbedHigh: 0

                Rectangle {  // its track
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 4
                    radius: 2
                    color: Theme.knobTrack
                }
                Rectangle {  // what it moves over
                    anchors.verticalCenter: parent.verticalCenter
                    x: range.from * range.width
                    width: Math.max(2, (range.to - range.from) * range.width)
                    height: 4
                    radius: 2
                    color: Theme.modulation
                    opacity: 0.55
                }
                Rectangle {  // where it is now
                    readonly property real now: {
                        if (targets.output < 0)
                            return -1
                        const v = row.invert ? 1 - targets.output : targets.output
                        if (range.dragging !== "")
                            return range.low + v * (range.high - range.low)
                        return Math.max(0, Math.min(1, row.setLow + v * (row.setHigh - row.setLow)))
                    }
                    visible: now >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    x: now * range.width - width / 2
                    width: 3
                    height: 10
                    radius: 1.5
                    color: "white"
                }
                Repeater {  // its ends: min (filled) and max (hollow)
                    model: ["low", "high"]
                    Rectangle {
                        required property string modelData
                        readonly property real at: modelData === "low" ? range.low : range.high
                        anchors.verticalCenter: parent.verticalCenter
                        x: at * range.width - width / 2
                        width: 8
                        height: 8
                        radius: 4
                        color: modelData === "low" ? Theme.modulation : Theme.panel
                        border.color: Theme.modulation
                        border.width: 1.5
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -3
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.SizeHorCursor

                    function valueAt(x) {
                        return Math.max(0, Math.min(1, (x - 3) / range.width))
                    }

                    onPressed: mouse => {
                        const at = valueAt(mouse.x)
                        const toLow = Math.abs(at - range.low) * range.width
                        const toHigh = Math.abs(at - range.high) * range.width
                        range.gesture = "modulation-range:" + Date.now() + ":" + Math.random()  // (one per drag)
                        if (Math.min(toLow, toHigh) <= 7)
                            range.dragging = toLow <= toHigh ? "low" : "high"
                        else if (at > range.from && at < range.to)
                            range.dragging = "both"
                        else
                            range.dragging = Math.abs(at - range.low) < Math.abs(at - range.high) ? "low" : "high"
                        range.grabbed = at
                        range.grabbedLow = range.low
                        range.grabbedHigh = range.high
                        if (range.dragging !== "both")
                            positionChanged(mouse)
                    }
                    onPositionChanged: mouse => {
                        if (!pressed || range.dragging === "")
                            return
                        const at = valueAt(mouse.x)
                        let low = range.low, high = range.high
                        if (range.dragging === "low") {
                            low = at
                        } else if (range.dragging === "high") {
                            high = at
                        } else {
                            const shift = Math.max(-Math.min(range.grabbedLow, range.grabbedHigh),
                                                   Math.min(1 - Math.max(range.grabbedLow, range.grabbedHigh),
                                                            at - range.grabbed))
                            low = range.grabbedLow + shift
                            high = range.grabbedHigh + shift
                        }
                        range.low = low
                        range.high = high
                        mappings.setRange(row.deviceId, row.paramId, low, high, range.gesture)
                    }
                    onReleased: {
                        range.dragging = ""
                        range.low = Qt.binding(() => row.low)
                        range.high = Qt.binding(() => row.high)
                    }
                    onDoubleClicked: mappings.setRange(row.deviceId, row.paramId, 0, 1)
                }

                ToolTip.visible: range.dragging !== "" || rangeHover.hovered
                ToolTip.delay: range.dragging !== "" ? 0 : 600
                ToolTip.text: mappings.valueText(row.deviceId, row.paramId, range.low) + "  →  " +
                              mappings.valueText(row.deviceId, row.paramId, range.high)
                HoverHandler {
                    id: rangeHover
                }
            }
        }
    }

    // Nothing mapped yet: how to.
    Text {
        visible: mappings.count === 0
        anchors.top: headerRow.bottom
        anchors.topMargin: 10
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("Click Map, then touch a knob on this track.\nOr right-click any parameter › Modulate With.")
        color: Theme.textDisabled
        font: Theme.uiFont(7)
    }
}
