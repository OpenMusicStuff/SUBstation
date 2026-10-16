import QtQuick
import QtQuick.Controls
import SUBstation

// The Glitch's editor, compact: its Grid and Variation over sixteen cells, the
// last sixteen steps of the grid, each coloured by what it did (Stutter,
// Reverse, Tape or Gate; dark for nothing); beside them Chance over the mix,
// then the four kinds' weights two by two, each knob in its kind's colour.
//
// Its animations, subtle: the step playing is outlined, the outline sliding to
// it from the last; a step that glitches pops in its colour and shivers a
// pixel while it plays, then settles and fades as it ages; a scan line runs
// down the cells while one plays; the kind playing swells its knob a little.
// All of it from the displays `step` (the step and what it does) and `level`
// (DisplayValues), and still while nothing comes in.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int stripWidth: 152
    readonly property int knobWidth: 50
    readonly property int knobSize: 28
    readonly property int spacing: 8
    readonly property int stepCount: 16

    implicitWidth: 8 + stripWidth + spacing + 3 * knobWidth + 2 * 4 + 8 - 2
    implicitHeight: 6 + Math.max(knobs.implicitHeight, 96) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["grid", "chance", "stutter", "reverse", "tape", "gate", "variation", "mix"]
    }

    // Each kind's colour (1 Stutter, 2 Reverse, 3 Tape, 4 Gate; 0 nothing).
    // (Fixed hues, apart from each other and from the theme's accent on Chance and Dry/Wet.)
    readonly property var kindColors: [Theme.border, Qt.hsla(0.75, 0.6, 0.66, 1), Qt.hsla(0.53, 0.65, 0.58, 1),
                                       Qt.hsla(0.33, 0.5, 0.55, 1), Qt.hsla(0.95, 0.62, 0.64, 1)]
    function kindColor(kind) {
        return editor.kindColors[Math.max(0, Math.min(4, kind))]
    }

    // The step playing and what it does: its number (0..15) plus 16 times its kind, one value.
    DisplayValue {
        id: stepDisplay
        objectName: "stepDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "step"
        width: 0
        height: 0
        onValueChanged: editor.note()
    }
    DisplayValue {
        id: levelDisplay
        objectName: "levelDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "level"
        smoothing: 0.3
        width: 0
        height: 0
    }

    // The step playing (0..15), or -1 while nothing comes in; and what it does.
    readonly property int playingStep: stepDisplay.active ? Math.round(stepDisplay.value) % editor.stepCount : -1
    readonly property int playingKind: stepDisplay.active ? Math.floor(Math.round(stepDisplay.value) / editor.stepCount) : 0
    // What each of the sixteen steps did when it last played (-1: not seen).
    property var cells: Array(stepCount).fill(-1)
    // (From the display itself: its signal can come before the bindings above have followed it.)
    function note() {
        if (!stepDisplay.active)
            return
        const value = Math.round(stepDisplay.value)
        const step = value % editor.stepCount, kind = Math.floor(value / editor.stepCount)
        if (editor.cells[step] === kind)
            return
        const next = editor.cells.slice()
        next[step] = kind
        editor.cells = next
    }

    Column {
        id: left
        x: 8
        y: 6
        width: editor.stripWidth
        spacing: 4

        Row {
            id: top
            width: parent.width
            spacing: 4

            EditorCaption {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Grid")
            }
            ParamChoice {
                objectName: "grid"
                width: 54
                param: p.get("grid")
                tooltip: qsTr("Grid: the note value each step lasts; each may glitch as it starts")
            }
            EditorCaption {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Var")
            }
            ParamBox {
                objectName: "variation"
                width: 34
                param: p.get("variation")
                step: 1
                decimals: 0
                defaultValue: 0
                sampleText: "99"
                tooltip: qsTr("Variation: another roll of the dice: the same steps glitch otherwise")
            }
        }

        Rectangle {
            id: strip
            objectName: "steps"
            width: parent.width
            height: Math.max(40, editor.height - 12 - top.height - left.spacing)
            color: Theme.lane
            border.color: Theme.border
            clip: true

            readonly property int columns: 8
            readonly property real cellWidth: (width - 4 - (columns - 1) * 2) / columns
            readonly property real cellHeight: (height - 4 - 2) / 2

            // The step playing: an outline sliding to it from the last, fading out when nothing comes in.
            Rectangle {
                id: cursor
                objectName: "cursor"
                readonly property int step: Math.max(0, editor.playingStep)
                x: 1 + (step % strip.columns) * (strip.cellWidth + 2)
                y: 1 + Math.floor(step / strip.columns) * (strip.cellHeight + 2)
                width: strip.cellWidth + 2
                height: strip.cellHeight + 2
                radius: 3
                color: "transparent"
                border.color: Theme.text
                opacity: editor.playingStep >= 0 ? 0.7 : 0
                Behavior on x {
                    NumberAnimation {
                        duration: 70
                        easing.type: Easing.OutQuad
                    }
                }
                Behavior on y {
                    NumberAnimation {
                        duration: 70
                        easing.type: Easing.OutQuad
                    }
                }
                Behavior on opacity {
                    NumberAnimation {
                        duration: 200
                    }
                }
            }

            Repeater {
                model: editor.stepCount
                Rectangle {
                    id: cell
                    required property int index
                    readonly property int kind: editor.cells[index]
                    readonly property bool lit: editor.playingStep === index
                    // How many steps ago it played (0 now): older ones fade.
                    readonly property int age: editor.playingStep < 0 ? 0
                                               : (editor.playingStep - index + editor.stepCount) % editor.stepCount
                    objectName: "cell" + index
                    x: 2 + (index % strip.columns) * (strip.cellWidth + 2) + shiver
                    y: 2 + Math.floor(index / strip.columns) * (strip.cellHeight + 2)
                    width: strip.cellWidth
                    height: strip.cellHeight
                    radius: 2
                    color: kind > 0 ? editor.kindColor(kind) : Qt.rgba(Theme.textDim.r, Theme.textDim.g, Theme.textDim.b, 0.12)
                    opacity: lit ? 1 : (kind > 0 ? 0.85 - 0.04 * age : 0.8)
                    scale: lit && kind > 0 ? 1.1 : 1
                    Behavior on color {
                        ColorAnimation {
                            duration: 120
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 180
                        }
                    }
                    Behavior on scale {
                        NumberAnimation {
                            duration: 140
                            easing.type: Easing.OutBack
                        }
                    }

                    // A pixel's shiver while it glitches.
                    property real shiver: 0
                    SequentialAnimation {
                        running: cell.lit && cell.kind > 0 && editor.visible
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { target: cell; property: "shiver"; to: 1; duration: 45 }
                        NumberAnimation { target: cell; property: "shiver"; to: -1; duration: 70 }
                        NumberAnimation { target: cell; property: "shiver"; to: 0; duration: 45 }
                        PauseAnimation { duration: 120 }
                    }
                }
            }

            // A scan line running down the cells while a step glitches.
            Rectangle {
                id: scan
                objectName: "scan"
                width: strip.width
                height: 1
                color: Theme.text
                opacity: editor.playingKind > 0 ? 0.18 + 0.2 * Math.min(1, levelDisplay.smoothed) : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: 160
                    }
                }
                NumberAnimation on y {
                    from: 0
                    to: strip.height
                    duration: 900
                    loops: Animation.Infinite
                    running: editor.playingKind > 0 && editor.visible
                }
            }

            HoverHandler {
                id: stripHover
            }
            ToolTip.visible: stripHover.hovered
            ToolTip.delay: 700
            ToolTip.text: qsTr("The last sixteen steps, coloured by what each did: Stutter, Reverse, Tape or Gate")
        }
    }

    // A kind's weight: its knob in its colour, swelling a little while its kind plays.
    component KindKnob: EditorKnob {
        required property int kind
        width: editor.knobWidth
        size: editor.knobSize
        color: editor.kindColor(kind)
        scale: editor.playingKind === kind ? 1.06 : 1
        Behavior on scale {
            NumberAnimation {
                duration: 140
                easing.type: Easing.OutQuad
            }
        }
    }

    // Chance over Dry/Wet, then the kinds two by two.
    Grid {
        id: knobs
        x: left.x + left.width + editor.spacing
        y: 6
        rows: 2
        flow: Grid.TopToBottom
        columnSpacing: 4
        rowSpacing: 2

        EditorKnob {
            objectName: "chance"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("chance")
            title: qsTr("Chance")
            tooltip: qsTr("Chance: how likely each step is to glitch")
        }
        EditorKnob {
            objectName: "mix"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("mix")
            title: qsTr("Dry/Wet")
        }
        KindKnob {
            objectName: "stutter"
            kind: 1
            param: p.get("stutter")
            title: qsTr("Stutter")
            tooltip: qsTr("Stutter: how often a glitch repeats the step's start (a half to an eighth of it)")
        }
        KindKnob {
            objectName: "reverse"
            kind: 2
            param: p.get("reverse")
            title: qsTr("Reverse")
            tooltip: qsTr("Reverse: how often a glitch plays the step before backwards")
        }
        KindKnob {
            objectName: "tape"
            kind: 3
            param: p.get("tape")
            title: qsTr("Tape")
            tooltip: qsTr("Tape: how often a glitch slows the step to a stop")
        }
        KindKnob {
            objectName: "gate"
            kind: 4
            param: p.get("gate")
            title: qsTr("Gate")
            tooltip: qsTr("Gate: how often a glitch chops the step, every other part silent")
        }
    }
}
