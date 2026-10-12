import QtQuick
import QtQuick.Controls
import SUBstation

// The Transient Shaper's editor: what it does to a hit (TransientShapeGraph:
// the hit before and after, morphing as the settings change, with Attack's and
// Sustain's handles to drag, a glowing head running along it at each transient
// heard), what it is doing to the sound (TransientGraph: the last 2 s scrolling
// by, the shaped regions glowing, the gain in a lane under it, sparks at each
// transient), then its knobs: Attack and Sustain (bipolar, in the colours the
// displays draw them in) over their times, Threshold, Output and Dry/Wet, and
// Lookahead and Clip. Every control shows its parameter as it is now (its
// automation's value while that plays), sets it undoably, touches it when
// pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias shape: shape
    readonly property alias history: history
    readonly property alias handleMenu: handleMenu  // (Attack's or Sustain's, from the shape's handles)
    // The knobs' cells: as wide as their captions and every text their readouts take, measured in their font
    // as a Text lays them out (EditorTextsWidth), 56 px at least; the last column as its caption and controls.
    readonly property int cellWidth: Math.max(56, cellTexts.needed)
    readonly property int sideWidth: Math.ceil(Math.max(cellWidth, lookaheadCaption.implicitWidth, lookahead.needs,
                                                        clipButton.implicitWidth))

    implicitWidth: knobGrid.x + 3 * cellWidth + sideWidth + 3 * knobGrid.columnSpacing + 8
    implicitHeight: 6 + Math.max(knobGrid.implicitHeight, shape.implicitHeight, history.implicitHeight) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["attack", "sustain", "attack_time", "release", "threshold", "output", "mix", "clip", "lookahead"]
    }

    EditorTextsWidth {
        id: cellTexts
        knobs: [attackCell, sustainCell, thresholdCell, attackTimeCell, releaseCell, mixCell, outputCell]
    }

    // Attack and Sustain signed: which way they shape reads at a glance.
    function signedText(id, v) {
        const param = p.get(id)
        const text = param ? param.format(v) : ""
        return v > 0.05 && text !== "" && text[0] !== "+" ? "+" + text : text
    }

    TransientShapeGraph {
        id: shape
        objectName: "transientShape"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        x: 8
        y: 6
        width: implicitWidth
        height: Math.max(implicitHeight, editor.height - 12)

        // A handle right-clicked: its amount's menu.
        onParamMenuRequested: id => {
            handleMenu.param = p.get(id)
            handleMenu.show()
        }

        ParamMenu {
            id: handleMenu
        }
        HoverHandler {
            id: shapeHover
        }
        ToolTip.visible: shapeHover.hovered && !shape.dragging && shape.hovered === ""
        ToolTip.delay: 700
        ToolTip.text: qsTr("A hit before (dashed) and after the shaping, from its onset to 600 ms.\n"
                           + "Drag the orange handle up and down for Attack, across for Attack Time;\n"
                           + "the blue one up and down for Sustain, across for Release (Shift: finely).\n"
                           + "Double-click a handle to reset it, right-click it for its menu")
    }

    TransientGraph {
        id: history
        objectName: "transientHistory"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        x: shape.x + shape.width + 6
        y: 6
        width: implicitWidth
        height: Math.max(implicitHeight, editor.height - 12)

        HoverHandler {
            id: historyHover
        }
        ToolTip.visible: historyHover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("The last 2 s: the input (faint) and the output, glowing where the shaping turned it\n"
                           + "up or down (orange: Attack, blue: Sustain), the gain under it, sparks at each "
                           + "transient.\nAt the right: the output's level")
    }

    Grid {
        id: knobGrid
        x: history.x + history.width + 10
        y: Math.max(6, Math.round((editor.height - implicitHeight) / 2))
        columns: 4
        columnSpacing: 4
        rowSpacing: 6

        EditorKnob {
            id: attackCell
            objectName: "attack"
            width: editor.cellWidth
            param: p.get("attack")
            title: qsTr("Attack")
            color: Theme.accent
            knob.bipolar: true
            formatter: v => editor.signedText("attack", v)
            knob.formatter: v => editor.signedText("attack", v)
            tooltip: qsTr("Attack: turns onsets up (punchier) or down (softer). At +100 % an onset rises twice "
                          + "as far; at −100 % only as fast as Attack Time lets it")
        }
        EditorKnob {
            id: sustainCell
            objectName: "sustain"
            width: editor.cellWidth
            param: p.get("sustain")
            title: qsTr("Sustain")
            color: Theme.soloOn
            knob.bipolar: true
            formatter: v => editor.signedText("sustain", v)
            knob.formatter: v => editor.signedText("sustain", v)
            tooltip: qsTr("Sustain: turns tails up (fuller, longer) or down (tighter, drier). At +100 % a tail "
                          + "falls only as fast as Release; at −100 % twice as fast")
        }
        EditorKnob {
            id: thresholdCell
            objectName: "threshold"
            width: editor.cellWidth
            param: p.get("threshold")
            title: qsTr("Threshold")
            tooltip: qsTr("Threshold: under this level nothing is shaped (fading out over 12 dB), so a noise "
                          + "floor or bleed isn't lifted")
        }
        EditorKnob {
            id: outputCell
            objectName: "output"
            width: editor.cellWidth
            param: p.get("output")
            title: qsTr("Output")
            knob.bipolar: true
            tooltip: qsTr("Output: the level of what comes out, after Dry/Wet")
        }
        EditorKnob {
            id: attackTimeCell
            objectName: "attack_time"
            width: editor.cellWidth
            param: p.get("attack_time")
            title: qsTr("Attack Time")
            color: Theme.accent
            tooltip: qsTr("Attack Time: how long an onset counts as one. Short shapes only the click of a hit; "
                          + "long, its whole body. Drag the orange handle across")
        }
        EditorKnob {
            id: releaseCell
            objectName: "release"
            width: editor.cellWidth
            param: p.get("release")
            title: qsTr("Release")
            color: Theme.soloOn
            tooltip: qsTr("Release: how long a tail is held up against. Drag the blue handle across")
        }
        EditorKnob {
            id: mixCell
            objectName: "mix"
            width: editor.cellWidth
            param: p.get("mix")
            title: qsTr("Dry/Wet")
            tooltip: qsTr("Dry/Wet: how much of the shaped sound is heard (parallel shaping below 100 %)")
        }
        // Lookahead and Clip, in a cell as tall as a knob's: the list under its caption, Clip level with the
        // bottom of the knobs' readouts.
        Item {
            objectName: "transientSide"
            width: editor.sideWidth
            height: mixCell.height

            EditorCaption {
                id: lookaheadCaption
                objectName: "lookaheadCaption"
                width: parent.width
                text: qsTr("Lookahead")
            }
            ParamChoice {
                id: lookahead
                objectName: "lookahead"
                // What it needs: its longest choice and the arrow.
                readonly property real needs: choiceNames.needed + button.leftPadding + button.rightPadding

                y: lookaheadCaption.height + 1
                width: parent.width
                param: p.get("lookahead")
                tooltip: qsTr("Lookahead: the shaper hears what comes this much ahead, so its gain moves before\n"
                              + "an onset reaches the output (adds as much latency)")

                EditorTextsWidth {
                    id: choiceNames
                    font: lookahead.button.font
                    texts: lookahead.names
                }
            }
            ParamButton {
                id: clipButton
                objectName: "clip"
                y: parent.height - height
                width: parent.width
                param: p.get("clip")
                text: qsTr("Clip")
                tooltip: qsTr("Clip: what goes over −6 dBFS bends smoothly towards full scale, so a boosted onset\n"
                              + "never goes over")
            }
        }
    }
}
