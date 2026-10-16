import QtQuick
import QtQuick.Controls
import SUBstation

// The Glitch's editor, compact: its Grid and Variation over sixteen cells, the
// last sixteen steps of the grid, each coloured by what it did (Stutter,
// Reverse, Tape or Gate; dark for nothing); beside them, centred top to
// bottom, Chance over the mix, then the four kinds' weights two by two, each
// knob in its kind's colour.
//
// Its animations, loud while a step glitches and still otherwise: the step
// playing is outlined, the outline sliding to it from the last. A glitch
// starting jolts the cells sideways, flashes the strip in its kind's colour
// and throws its name across it in split red and cyan; while it plays, the
// cells are torn by bands across the strip, static flickers over them, two
// scan lines run down them, and its cell pops, shivers, splits into slices
// knocked sideways with cyan and magenta ghosts behind it, and moves as its
// kind does (Stutter: echoes rippling out; Reverse: a sweep running back;
// Tape: sinking dark from the top; Gate: blinking); the kind's knob swells and
// flickers. After, the cells settle and fade as they age. All of it from the
// displays `step` (the step and what it does) and `level` (DisplayValues);
// the disorder changes with `tick`, a clock running only while one glitches.
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
    readonly property var kindNames: ["", "STUTTER", "REVERSE", "TAPE", "GATE"]
    function kindColor(kind) {
        return editor.kindColors[Math.max(0, Math.min(4, kind))]
    }
    function tinted(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
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
    readonly property bool glitching: playingKind > 0 && visible
    readonly property real level: Math.min(1, levelDisplay.smoothed)
    // What each of the sixteen steps did when it last played (-1: not seen).
    property var cells: Array(stepCount).fill(-1)
    // Glitches started (each one jolts and flashes), and the kind of the last.
    property int onsets: 0
    property int onsetKind: 0
    property int lastStep: -1
    // (From the display itself: its signal can come before the bindings above have followed it.)
    function note() {
        if (!stepDisplay.active) {
            editor.lastStep = -1  // (a step coming back after a silence starts again)
            return
        }
        const value = Math.round(stepDisplay.value)
        const step = value % editor.stepCount, kind = Math.floor(value / editor.stepCount)
        if (step !== editor.lastStep) {
            editor.lastStep = step
            if (kind > 0) {
                editor.onsetKind = kind
                ++editor.onsets
            }
        }
        if (editor.cells[step] === kind)
            return
        const next = editor.cells.slice()
        next[step] = kind
        editor.cells = next
    }

    // The disorder's clock: on while a step glitches.
    property int tick: 0
    Timer {
        interval: 45
        repeat: true
        running: editor.glitching
        onTriggered: ++editor.tick
    }
    // A number in 0..1 from `a` and `b`, the same for the same two (a hash, not Math.random(), so bindings follow tick).
    function noise(a, b) {
        const x = Math.sin(a * 127.1 + b * 311.7) * 43758.5453
        return x - Math.floor(x)
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

            // A glitch starting: the strip flashes in its kind's colour.
            Rectangle {
                id: flash
                objectName: "flash"
                anchors.fill: parent
                color: editor.kindColor(editor.onsetKind)
                opacity: 0
            }
            Connections {
                target: editor
                function onOnsetsChanged() {
                    onset.restart()
                }
            }
            ParallelAnimation {
                id: onset
                SequentialAnimation {
                    NumberAnimation { target: flash; property: "opacity"; to: 0.32; duration: 30 }
                    NumberAnimation { target: flash; property: "opacity"; to: 0; duration: 380; easing.type: Easing.OutQuad }
                }
                SequentialAnimation {  // the jolt
                    NumberAnimation { target: board; property: "jolt"; to: 4; duration: 25 }
                    NumberAnimation { target: board; property: "jolt"; to: -3; duration: 35 }
                    NumberAnimation { target: board; property: "jolt"; to: 2; duration: 35 }
                    NumberAnimation { target: board; property: "jolt"; to: 0; duration: 45 }
                }
                SequentialAnimation {  // its name across the strip
                    NumberAnimation { target: label; property: "opacity"; to: 0.9; duration: 20 }
                    PauseAnimation { duration: 160 }
                    NumberAnimation { target: label; property: "opacity"; to: 0; duration: 340 }
                }
            }

            // The cells, and the cursor over them, jolted together.
            Item {
                id: board
                objectName: "board"
                property real jolt: 0
                x: jolt
                width: strip.width
                height: strip.height

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
                        readonly property bool wild: lit && kind > 0 && editor.visible
                        // How many steps ago it played (0 now): older ones fade.
                        readonly property int age: editor.playingStep < 0 ? 0
                                                   : (editor.playingStep - index + editor.stepCount) % editor.stepCount
                        readonly property color tone: editor.kindColor(kind)
                        objectName: "cell" + index
                        x: 2 + (index % strip.columns) * (strip.cellWidth + 2) + shiver
                        y: 2 + Math.floor(index / strip.columns) * (strip.cellHeight + 2)
                        width: strip.cellWidth
                        height: strip.cellHeight
                        radius: 2
                        color: kind > 0 ? tone : Qt.rgba(Theme.textDim.r, Theme.textDim.g, Theme.textDim.b, 0.12)
                        opacity: lit ? (blink ? 0.35 : 1) : (kind > 0 ? 0.85 - 0.04 * age : 0.8)
                        scale: wild ? 1.1 : 1
                        z: lit ? 2 : 0
                        Behavior on color {
                            ColorAnimation {
                                duration: 120
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
                            running: cell.wild
                            loops: Animation.Infinite
                            alwaysRunToEnd: true
                            NumberAnimation { target: cell; property: "shiver"; to: 1; duration: 45 }
                            NumberAnimation { target: cell; property: "shiver"; to: -1; duration: 70 }
                            NumberAnimation { target: cell; property: "shiver"; to: 0; duration: 45 }
                            PauseAnimation { duration: 120 }
                        }
                        // Gate: blinking.
                        property bool blink: false
                        Timer {
                            interval: 70
                            repeat: true
                            running: cell.wild && cell.kind === 4
                            onTriggered: cell.blink = !cell.blink
                            onRunningChanged: if (!running) cell.blink = false
                        }

                        // Cyan and magenta ghosts behind it, knocked about.
                        Rectangle {
                            objectName: "ghostCyan"
                            z: -1
                            x: -1 - 3 * editor.noise(cell.index, editor.tick)
                            y: 1 - 2 * editor.noise(cell.index + 7, editor.tick)
                            width: parent.width
                            height: parent.height
                            radius: 2
                            color: Qt.rgba(0.2, 0.95, 1, 0.55)
                            visible: cell.wild
                        }
                        Rectangle {
                            objectName: "ghostMagenta"
                            z: -1
                            x: 1 + 3 * editor.noise(cell.index + 3, editor.tick)
                            y: -1 + 2 * editor.noise(cell.index + 11, editor.tick)
                            width: parent.width
                            height: parent.height
                            radius: 2
                            color: Qt.rgba(1, 0.2, 0.8, 0.55)
                            visible: cell.wild
                        }
                        // Sliced, the slices knocked sideways.
                        Repeater {
                            model: 4
                            Rectangle {
                                required property int index
                                objectName: "slice"
                                x: (editor.noise(cell.index * 5 + index, editor.tick) - 0.5) * 8
                                y: index * cell.height / 4
                                width: cell.width
                                height: cell.height / 4
                                color: Qt.lighter(cell.tone, 1 + 0.5 * editor.noise(index, editor.tick))
                                visible: cell.wild && editor.noise(cell.index + index * 13, editor.tick) > 0.35
                            }
                        }
                        // Stutter: echoes rippling out.
                        Repeater {
                            model: 2
                            Rectangle {
                                id: echo
                                required property int index
                                anchors.fill: parent
                                radius: 2
                                color: "transparent"
                                border.color: cell.tone
                                border.width: 2
                                visible: cell.wild && cell.kind === 1
                                opacity: 0
                                SequentialAnimation on scale {
                                    running: echo.visible
                                    loops: Animation.Infinite
                                    PauseAnimation { duration: echo.index * 110 }
                                    NumberAnimation { from: 1; to: 1.7; duration: 220; easing.type: Easing.OutQuad }
                                }
                                SequentialAnimation on opacity {
                                    running: echo.visible
                                    loops: Animation.Infinite
                                    PauseAnimation { duration: echo.index * 110 }
                                    NumberAnimation { from: 0.9; to: 0; duration: 220 }
                                }
                            }
                        }
                        // Reverse: a sweep running back.
                        Rectangle {
                            id: sweep
                            width: 2
                            height: parent.height
                            color: "white"
                            visible: cell.wild && cell.kind === 2
                            NumberAnimation on x {
                                running: sweep.visible
                                loops: Animation.Infinite
                                from: cell.width
                                to: -2
                                duration: 240
                            }
                        }
                        // Tape: sinking dark from the top.
                        Rectangle {
                            id: sink
                            width: parent.width
                            radius: 2
                            color: Qt.rgba(0, 0, 0, 0.55)
                            visible: cell.wild && cell.kind === 3
                            height: 0
                            NumberAnimation on height {
                                running: sink.visible
                                from: 0
                                to: cell.height * 0.85
                                duration: 700
                                easing.type: Easing.InQuad
                            }
                        }
                    }
                }
            }

            // Static flickering over the cells while a step glitches, as loud as the input.
            Repeater {
                model: 36
                Rectangle {
                    required property int index
                    objectName: "static"
                    x: editor.noise(index, editor.tick) * strip.width
                    y: editor.noise(index + 101, editor.tick) * strip.height
                    width: 1 + Math.round(2 * editor.noise(index + 202, editor.tick))
                    height: 1
                    color: editor.noise(index + 303, editor.tick) > 0.5 ? "white" : editor.kindColor(editor.playingKind)
                    opacity: 0.25 + 0.5 * editor.level
                    visible: editor.glitching
                }
            }
            // Tears: bands across the strip, knocked sideways.
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    objectName: "tear"
                    x: (editor.noise(index + 404, editor.tick) - 0.5) * 24
                    y: editor.noise(index + 505, editor.tick) * strip.height
                    width: strip.width
                    height: 1 + Math.round(3 * editor.noise(index + 606, editor.tick))
                    color: index % 2 === 0 ? editor.kindColor(editor.playingKind) : Theme.text
                    opacity: 0.2 + 0.35 * editor.noise(index + 707, editor.tick)
                    visible: editor.glitching && editor.noise(index + 808, editor.tick) > 0.25
                }
            }

            // Two scan lines running down the cells while a step glitches.
            Rectangle {
                id: scan
                objectName: "scan"
                width: strip.width
                height: 1
                color: Theme.text
                opacity: editor.playingKind > 0 ? 0.25 + 0.3 * editor.level : 0
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
                    running: editor.glitching
                }
            }
            Rectangle {
                width: strip.width
                height: 2
                color: editor.kindColor(editor.playingKind)
                opacity: editor.playingKind > 0 ? 0.35 : 0
                NumberAnimation on y {
                    from: strip.height
                    to: 0
                    duration: 380
                    loops: Animation.Infinite
                    running: editor.glitching
                }
            }

            // The kind's name thrown across the strip as it starts, split red and cyan.
            Item {
                id: label
                objectName: "kindLabel"
                readonly property string text: editor.kindNames[Math.max(0, Math.min(4, editor.onsetKind))]
                anchors.fill: parent
                opacity: 0

                Repeater {
                    model: [[Qt.rgba(1, 0.15, 0.35, 0.8), -2], [Qt.rgba(0.15, 0.9, 1, 0.8), 2], [Theme.text, 0]]
                    Text {
                        required property var modelData
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: modelData[1] + (editor.noise(modelData[1], editor.tick) - 0.5) * 4
                        text: label.text
                        color: modelData[0]
                        font.family: "monospace"
                        font.pixelSize: Math.min(22, strip.height * 0.35)
                        font.bold: true
                        font.letterSpacing: 2
                    }
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

    // A kind's weight: its knob in its colour, swelling and flickering while its kind plays.
    component KindKnob: EditorKnob {
        required property int kind
        readonly property bool playing: editor.playingKind === kind
        width: editor.knobWidth
        size: editor.knobSize
        color: editor.kindColor(kind)
        scale: playing ? 1.08 : 1
        opacity: playing ? 0.75 + 0.25 * editor.noise(kind, editor.tick) : 1
        Behavior on scale {
            NumberAnimation {
                duration: 140
                easing.type: Easing.OutBack
            }
        }
    }

    // Chance over Dry/Wet, then the kinds two by two; centred top to bottom.
    Grid {
        id: knobs
        objectName: "knobs"
        x: left.x + left.width + editor.spacing
        y: Math.max(6, Math.round((editor.height - height) / 2))
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
