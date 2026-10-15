import QtQuick
import QtQuick.Controls
import SUBstation

// The Vocal Rider's editor: its display (VocalRiderGraph: the last 5 s of the vocal's level and what comes out, the
// music on the sidechain, the target as a blue line to drag, and under them the ride itself) with Target and the
// ride now along its top; beside it Range Min, Range Max, Speed and Output over Vocal Sens., Music Sens. and the
// weighting (Vocal or Flat). Every control shows its parameter as it is now, sets it undoably,
// touches it when pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias graph: graph
    readonly property int margin: 8
    readonly property int gap: 8
    readonly property int graphWidth: 300
    readonly property int barHeight: 20
    readonly property int cellWidth: Math.max(56, cellTexts.needed)

    implicitWidth: margin + graphWidth + gap + 4 * cellWidth + 3 * 4 + margin
    implicitHeight: 6 + Math.max(2 * minCell.implicitHeight + 6, barHeight + graph.implicitHeight, 118) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["target", "min_gain", "max_gain", "speed", "sensitivity", "music_sens", "weighting", "output"]
    }

    EditorBoxWidth {
        id: targetBox
        params: [p.get("target")]
    }
    EditorTextsWidth {
        id: cellTexts
        knobs: [minCell, maxCell, speedCell, sensCell, musicCell, outputCell]
    }

    component Knob: EditorKnob {
        width: editor.cellWidth
        size: 26
    }

    Rectangle {
        id: display
        objectName: "riderDisplay"
        x: editor.margin
        y: 6
        width: editor.graphWidth
        height: editor.height - 12
        radius: 3
        color: Theme.meterBg
        border.color: Theme.border

        Item {
            x: 6
            width: parent.width - 12
            height: editor.barHeight

            Text {
                id: targetCaption
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Target")
                color: Theme.textDim
                font: Theme.uiFont(8)
            }
            ParamBox {
                objectName: "param_target"
                x: targetCaption.width + 4
                anchors.verticalCenter: parent.verticalCenter
                width: targetBox.needed
                sampleText: targetBox.sample
                param: p.get("target")
                tooltip: qsTr("Target: the level the vocal is ridden to (the blue line: drag it in the display too)")
            }
            Text {
                objectName: "rideReadout"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Ride %1").arg((graph.gain >= 0.05 ? "+" : graph.gain <= -0.05 ? "−" : "")
                                          + Math.abs(graph.gain).toFixed(1) + " dB")
                color: Theme.accent
                font: Theme.uiFont(8)
            }
        }

        VocalRiderGraph {
            id: graph
            objectName: "vocalRiderGraph"
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            x: 1
            y: editor.barHeight
            width: parent.width - 2
            height: parent.height - editor.barHeight - 1

            onParamMenuRequested: id => {
                lineMenu.param = p.get(id)
                lineMenu.show()
            }

            ParamMenu {
                id: lineMenu
            }
            HoverHandler {
                id: graphHover
            }
            ToolTip.visible: graphHover.hovered && !graph.dragging
            ToolTip.delay: 700
            ToolTip.text: qsTr("The last 5 s: the vocal's level (grey), what comes out (outlined) and the music on\n"
                               + "the sidechain (yellow). Under them the ride: up for gain, down for cut.\n"
                               + "Drag up or down for the target (the blue line), Shift finely;\n"
                               + "double-click to reset it, right-click the line for its menu")
        }
    }

    Grid {
        x: display.x + display.width + editor.gap
        y: 6
        columns: 4
        columnSpacing: 4
        rowSpacing: 6

        Knob {
            id: minCell
            objectName: "param_min_gain"
            param: p.get("min_gain")
            title: qsTr("Range Min")
            tooltip: qsTr("Range Min: the most it turns the vocal down")
        }
        Knob {
            id: maxCell
            objectName: "param_max_gain"
            param: p.get("max_gain")
            title: qsTr("Range Max")
            tooltip: qsTr("Range Max: the most it turns the vocal up")
        }
        Knob {
            id: speedCell
            objectName: "param_speed"
            param: p.get("speed")
            title: qsTr("Speed")
            tooltip: qsTr("Speed: how quickly it rides: short follows every word, long only the phrases")
        }
        Knob {
            id: outputCell
            objectName: "param_output"
            param: p.get("output")
            title: qsTr("Output")
            tooltip: qsTr("Output: the level after the ride")
        }
        Knob {
            id: sensCell
            objectName: "param_sensitivity"
            param: p.get("sensitivity")
            title: qsTr("Vocal Sens.")
            tooltip: qsTr("Vocal Sens.: the level the vocal counts from. Under it (breaths, bleed, silence)\n"
                          + "the ride holds, so nothing quiet is brought up")
        }
        Knob {
            id: musicCell
            objectName: "param_music_sens"
            param: p.get("music_sens")
            title: qsTr("Music Sens.")
            tooltip: qsTr("Music Sens.: with the music on the sidechain, how far the target follows its level\n"
                          + "(100%: the vocal rides to the music's level)")
        }
        Column {
            width: editor.cellWidth
            spacing: 2

            Repeater {
                model: [qsTr("Vocal"), qsTr("Flat")]
                ParamButton {
                    required property int index
                    required property string modelData
                    objectName: "weighting" + index
                    width: parent.width
                    height: 16
                    param: p.get("weighting")
                    choice: index
                    text: modelData
                    tooltip: index === 0 ? qsTr("Vocal: hears the level as a listener does (K-weighted, no rumble)")
                                         : qsTr("Flat: hears every frequency alike")
                }
            }
        }
    }
}
