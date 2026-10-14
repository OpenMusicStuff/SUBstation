import QtQuick
import QtQuick.Controls
import SUBstation

// The Compressor's editor, laid out as Ableton's Compressor in its Activity
// view (without the sidechain section): Ratio, Attack and Release down the
// left; the display (ReductionGraph: the levels scrolling by, the gain
// reduction hanging from the top, the threshold as a blue line to drag) with
// Thresh, the GR and Output switches (what it draws: view state) and Out (the
// Makeup gain) along its top, Knee and Lookahead along its bottom; and down the
// right, Makeup (automatic makeup gain), Peak, RMS and Expand, and Dry/Wet.
// Every control shows its parameter as it is now, sets it undoably, touches it
// when pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias graph: graph
    readonly property int sideWidth: 60
    readonly property int displayWidth: 268
    readonly property int barHeight: 20  // the display's top and bottom strips

    implicitWidth: 8 + sideWidth + 8 + displayWidth + 8 + sideWidth + 8
    implicitHeight: 6 + Math.max(3 * ratioKnob.implicitHeight, sideColumn.implicitHeight,
                                  2 * barHeight + graph.implicitHeight) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["threshold", "ratio", "attack", "release", "knee", "makeup", "mix", "mode", "lookahead", "auto_makeup"]
    }

    // --- Ratio, Attack, Release ----------------------------------------------------------------

    Column {
        id: knobColumn
        x: 8
        y: 6
        width: editor.sideWidth
        height: editor.height - 12
        spacing: Math.max(0, Math.floor((height - 3 * ratioKnob.implicitHeight) / 2))

        EditorKnob {
            id: ratioKnob
            objectName: "param_ratio"
            width: parent.width
            size: 24
            param: p.get("ratio")
            title: qsTr("Ratio")
            formatter: v => v.toFixed(2) + " : 1"
            knob.formatter: v => v.toFixed(2) + " : 1"
            tooltip: qsTr("Ratio: above the threshold, how many dB in make one dB out")
        }
        EditorKnob {
            objectName: "param_attack"
            width: parent.width
            size: 24
            param: p.get("attack")
            title: qsTr("Attack")
            tooltip: qsTr("Attack: how quickly the gain comes down once the level is over the threshold")
        }
        EditorKnob {
            objectName: "param_release"
            width: parent.width
            size: 24
            param: p.get("release")
            title: qsTr("Release")
            tooltip: qsTr("Release: how quickly the gain comes back once the level falls")
        }
    }

    // --- The display ---------------------------------------------------------------------------

    Rectangle {
        id: display
        objectName: "compressorDisplay"
        x: knobColumn.x + knobColumn.width + 8
        y: 6
        width: editor.displayWidth
        height: editor.height - 12
        radius: 3
        color: Theme.meterBg
        border.color: Theme.border

        // Along the top: Thresh, the GR and Output switches, Out.
        Item {
            id: top
            x: 6
            width: parent.width - 12
            height: editor.barHeight

            Text {
                id: thresholdCaption
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Thresh")
                color: Theme.textDim
                font: Theme.uiFont(8)
            }
            ParamBox {
                objectName: "param_threshold"
                x: thresholdCaption.width + 4
                anchors.verticalCenter: parent.verticalCenter
                width: 56
                param: p.get("threshold")
                tooltip: qsTr("Threshold: where compression starts (the blue line: drag it in the display too)")
            }
            RoleButton {
                id: grButton
                objectName: "showReduction"
                x: Math.round(parent.width / 2) - width - 2
                anchors.verticalCenter: parent.verticalCenter
                height: 12
                role: "small"
                font.pointSize: 6.5
                topPadding: 0
                bottomPadding: 0
                leftPadding: 4
                rightPadding: 4
                text: qsTr("GR")
                checkable: false
                checked: graph.showReduction
                tooltip: qsTr("Show the gain reduction (orange)")
                onClicked: DeviceViews.setValue(editor.deviceId, "showReduction", !graph.showReduction)
            }
            RoleButton {
                objectName: "showOutput"
                x: Math.round(parent.width / 2) + 2
                anchors.verticalCenter: parent.verticalCenter
                height: 12
                role: "small"
                font.pointSize: 6.5
                topPadding: 0
                bottomPadding: 0
                leftPadding: 4
                rightPadding: 4
                text: qsTr("Output")
                checkable: false
                checked: graph.showOutput
                tooltip: qsTr("Show the output's level (outlined)")
                onClicked: DeviceViews.setValue(editor.deviceId, "showOutput", !graph.showOutput)
            }
            ParamBox {
                id: outBox
                objectName: "param_makeup"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 52
                param: p.get("makeup")
                tooltip: qsTr("Out: the gain after compression (makeup gain)")
            }
            Text {
                anchors.right: outBox.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Out")
                color: Theme.textDim
                font: Theme.uiFont(8)
            }
        }

        ReductionGraph {
            id: graph
            objectName: "reductionGraph"
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            x: 1
            y: editor.barHeight
            width: parent.width - 2
            height: parent.height - 2 * editor.barHeight
            showReduction: DeviceViews.value(editor.deviceId, "showReduction", true)
            showOutput: DeviceViews.value(editor.deviceId, "showOutput", false)

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
            ToolTip.text: qsTr("The last 2.5 s: the input (grey), the output (outlined, with Output on)\n"
                               + "and the gain reduction (orange, from the top, with GR on).\n"
                               + "Drag up or down for the threshold (the blue line), Shift finely;\n"
                               + "double-click to reset it, right-click the line for its menu")
        }

        // Along the bottom: Knee and Lookahead.
        Item {
            x: 6
            y: parent.height - editor.barHeight
            width: parent.width - 12
            height: editor.barHeight

            Text {
                id: kneeCaption
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Knee")
                color: Theme.textDim
                font: Theme.uiFont(8)
            }
            ParamBox {
                objectName: "param_knee"
                x: kneeCaption.width + 4
                anchors.verticalCenter: parent.verticalCenter
                width: 50
                param: p.get("knee")
                tooltip: qsTr("Knee: how gradually compression comes in around the threshold")
            }
            Text {
                id: lookaheadCaption
                x: Math.round(parent.width / 2) + 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Look.")
                color: Theme.textDim
                font: Theme.uiFont(8)
            }
            ParamChoice {
                objectName: "param_lookahead"
                x: lookaheadCaption.x + lookaheadCaption.width + 4
                anchors.verticalCenter: parent.verticalCenter
                width: 54
                param: p.get("lookahead")
                tooltip: qsTr("Lookahead: the compressor sees what comes this much ahead,\n"
                              + "to catch transients (adds as much latency)")
            }
        }
    }

    // --- Makeup, the modes, Dry/Wet ------------------------------------------------------------

    Item {
        id: sideColumn
        x: display.x + display.width + 8
        y: 6
        width: editor.sideWidth
        height: editor.height - 12
        implicitHeight: modes.y + modes.height + 6 + mix.implicitHeight

        ParamButton {
            objectName: "param_auto_makeup"
            width: parent.width
            param: p.get("auto_makeup")
            text: qsTr("Makeup")
            tooltip: qsTr("Makeup: add back about what compression takes off, automatically")
        }
        Column {
            id: modes
            y: 28
            width: parent.width
            spacing: 2

            Repeater {
                model: [qsTr("Peak"), qsTr("RMS"), qsTr("Expand")]
                ParamButton {
                    required property int index
                    required property string modelData
                    objectName: "mode" + index
                    width: parent.width
                    param: p.get("mode")
                    choice: index
                    text: modelData
                    tooltip: [qsTr("Peak: react to the signal's peaks (tight control)"),
                              qsTr("RMS: react to the average level (gentler, more musical)"),
                              qsTr("Expand: turn down what is below the threshold instead")][index]
                }
            }
        }
        EditorKnob {
            id: mix
            objectName: "param_mix"
            anchors.bottom: parent.bottom
            width: parent.width
            size: 28
            param: p.get("mix")
            title: qsTr("Dry/Wet")
            tooltip: qsTr("Dry/Wet: how much of the compressed signal is heard")
        }
    }
}
