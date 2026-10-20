import QtQuick
import QtQuick.Controls
import SUBstation

// The Arpeggiator's editor: its Style (a list), Hold and Follow Groove over the pattern it
// plays, drawn: sixteen steps of a three-note chord over its octaves, walked as
// the engine walks it (Arpeggiator.cpp's indexFor), the odd steps late by the
// swing and each as long as the gate; beside it its Rate (Sync on: a note
// value; off: Hz), Gate and Swing, and under them the Octaves and the velocity
// (as played, or fixed at a value).
//
// Its animation: the step playing lights up as it plays (the display `step`,
// through a DisplayValue), a glow sliding to it from the last; a change of the
// pattern fades the new steps in.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int patternWidth: 200
    readonly property int knobWidth: 56
    readonly property int controlsWidth: 3 * knobWidth + 2 * 6
    readonly property int captionWidth: 42
    readonly property int spacing: 10
    readonly property int chordSize: 3  // the chord the pattern is drawn for

    implicitWidth: 8 + patternWidth + spacing + controlsWidth + 8 - 2
    implicitHeight: 6 + Math.max(styleRow.height + 3 + 60, controls.implicitHeight) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["style", "sync", "rate", "synced", "gate", "octaves", "swing", "velocity", "fixed_velocity", "hold",
              "follow_groove"]
    }
    function value(id, otherwise) {
        const param = p.get(id)
        return param ? param.value : otherwise
    }
    readonly property bool synced: value("sync", 1) >= 0.5

    // The step playing (0..15), or -1 while none sounds.
    DisplayValue {
        id: playing
        objectName: "stepDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "step"
        width: 0
        height: 0
    }
    readonly property int playingStep: playing.active ? Math.round(playing.value) : -1

    // Step k's note of the pattern's n (the engine's indexFor); for Chord, the octave.
    function indexFor(style, k, n) {
        if (n <= 1)
            return 0
        switch (style) {
        case 1: return n - 1 - k % n  // Down
        case 2: case 3: {  // Up-Down, Down-Up
            const period = 2 * n - 2
            const at = k % period
            const up = at < n ? at : period - at
            return style === 2 ? up : n - 1 - up
        }
        case 4: case 5: {  // Converge, Diverge
            const at = k % n
            const from = style === 4 ? at : n - 1 - at
            return from % 2 === 0 ? from / 2 : n - 1 - (from - 1) / 2
        }
        case 7: return (k * 7 + 3 * Math.floor(k / n)) % n  // Random: a shuffle, to show it jumps about
        default: return k % n  // Up, Played
        }
    }
    // The steps drawn: {step, row, rows, chord}: the row (0 the lowest) of the pattern's `rows`.
    readonly property var pattern: {
        const style = Math.round(value("style", 0))
        const octaves = Math.round(value("octaves", 1))
        const n = editor.chordSize * octaves
        const out = []
        for (let k = 0; k < 16; ++k) {
            if (style === 8)  // Chord: the chord, an octave further each step
                out.push({ step: k, row: (k % octaves) * editor.chordSize, rows: n, chord: true })
            else
                out.push({ step: k, row: editor.indexFor(style, k, n), rows: n, chord: false })
        }
        return out
    }

    Column {
        id: patternColumn
        x: 8
        y: 6
        width: editor.patternWidth
        spacing: 3

        Row {
            id: styleRow
            width: parent.width
            spacing: 4

            EditorCaption {
                id: styleCaption
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Style")
            }
            ParamChoice {
                objectName: "style"
                width: parent.width - 3 * parent.spacing - styleCaption.width - 40 - 44
                param: p.get("style")
                tooltip: qsTr("Style: the order the held notes play in")
            }
            ParamButton {
                objectName: "hold"
                width: 40
                param: p.get("hold")
                text: qsTr("Hold")
                tooltip: qsTr("Hold: the notes play on after they are let go, until a new chord comes")
            }
            ParamButton {
                objectName: "followGroove"
                width: 44
                param: p.get("follow_groove")
                text: qsTr("Groove")
                tooltip: qsTr("Follow Groove: synced and playing, its steps where the project's default groove\nputs them (Edit › Default Groove), as loud as it has them; Swing then does nothing")
            }
        }

        Rectangle {
            id: grid
            objectName: "patternView"
            width: parent.width
            height: Math.max(60, editor.height - 12 - grid.y)
            color: Theme.lane
            border.color: Theme.border
            clip: true

            readonly property real stepWidth: (width - 2) / 16

            Repeater {  // the beats
                model: 4
                Rectangle {
                    required property int index
                    x: 1 + index * 4 * grid.stepWidth
                    width: 1
                    height: grid.height
                    color: Theme.gridBeat
                }
            }
            // The step playing: a glow behind it, sliding from the last, fading out when the notes stop.
            Rectangle {
                id: glow
                objectName: "playingGlow"
                readonly property int step: Math.max(0, editor.playingStep)
                x: 1 + step * grid.stepWidth
                width: grid.stepWidth
                height: grid.height
                color: Theme.accent
                opacity: editor.playingStep >= 0 ? 0.18 : 0
                Behavior on x {
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
                model: editor.pattern
                Item {
                    id: step
                    required property var modelData
                    readonly property real rowHeight: (grid.height - 8) / modelData.rows
                    readonly property real swing: modelData.step % 2 === 1 ? 0.5 * editor.value("swing", 0) / 100 : 0
                    readonly property bool lit: editor.playingStep === modelData.step
                    x: 1 + (modelData.step + swing) * grid.stepWidth
                    width: Math.max(2, Math.min(2, editor.value("gate", 50) / 100) * grid.stepWidth - 1)
                    height: grid.height
                    opacity: 0
                    Component.onCompleted: opacity = 1
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 160
                        }
                    }

                    Repeater {
                        model: step.modelData.chord ? editor.chordSize : 1
                        Rectangle {
                            required property int index
                            y: grid.height - 4 - (step.modelData.row + index + 1) * step.rowHeight
                            width: step.width
                            height: Math.max(2, step.rowHeight - 1)
                            radius: 1
                            color: step.lit ? Qt.lighter(Theme.accent, 1.35) : Theme.accent
                            opacity: step.lit || editor.playingStep < 0 ? 0.9 : 0.6
                            scale: step.lit ? 1.08 : 1
                            Behavior on opacity {
                                NumberAnimation {
                                    duration: 120
                                }
                            }
                            Behavior on scale {
                                NumberAnimation {
                                    duration: 120
                                    easing.type: Easing.OutQuad
                                }
                            }
                        }
                    }
                }
            }

            HoverHandler {
                id: patternHover
            }
            ToolTip.visible: patternHover.hovered
            ToolTip.delay: 700
            ToolTip.text: qsTr("Sixteen steps of a three-note chord as it plays them, over its octaves")
        }
    }

    // The timing over how far and how hard.
    Column {
        id: controls
        x: patternColumn.x + patternColumn.width + editor.spacing
        y: 6
        width: editor.controlsWidth
        spacing: 4

        Row {
            id: timing
            spacing: 6

            Column {
                width: editor.knobWidth
                spacing: 2

                ParamButton {
                    objectName: "sync"
                    width: parent.width
                    param: p.get("sync")
                    text: qsTr("Sync")
                    tooltip: qsTr("Sync: steps of a note value at the tempo (on the song's grid while it plays); off, in Hz")
                }
                EditorCaption {
                    width: parent.width
                    text: qsTr("Rate")
                }
                ParamChoice {
                    objectName: "synced"
                    visible: editor.synced
                    width: parent.width
                    param: p.get("synced")
                    tooltip: qsTr("Rate: the note value each step lasts")
                }
                ParamKnob {
                    id: rateKnob
                    objectName: "rate"
                    visible: !editor.synced
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 26
                    param: p.get("rate")
                }
                EditorReadout {
                    visible: !editor.synced
                    width: parent.width
                    text: rateKnob.param ? rateKnob.param.text : ""
                }
            }
            EditorKnob {
                objectName: "gate"
                width: editor.knobWidth
                param: p.get("gate")
                title: qsTr("Gate")
                tooltip: qsTr("Gate: how long each note lasts, as a share of a step (past 100% they overlap)")
            }
            EditorKnob {
                objectName: "swing"
                width: editor.knobWidth
                param: p.get("swing")
                title: qsTr("Swing")
                tooltip: qsTr("Swing: every other step comes late, by up to half a step")
            }
        }

        // How far: the octaves.
        Row {
            width: parent.width
            spacing: 2

            EditorCaption {
                width: editor.captionWidth
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignLeft
                text: qsTr("Octaves")
            }
            Repeater {
                model: 4
                ParamButton {
                    required property int index
                    objectName: "octaves" + (index + 1)
                    width: (editor.controlsWidth - editor.captionWidth - 8) / 4
                    param: p.get("octaves")
                    // (Octaves runs 1..4: its value, not an index, chooses the button.)
                    choice: index + 1
                    text: String(index + 1)
                    tooltip: qsTr("The pattern over %n octave(s)", "", index + 1)
                }
            }
        }
        // How hard: as played, or fixed at a value.
        Row {
            width: parent.width
            spacing: 2

            EditorCaption {
                width: editor.captionWidth
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignLeft
                text: qsTr("Velocity")
            }
            Repeater {
                model: [[qsTr("Played"), qsTr("Each note as hard as its key was played")],
                        [qsTr("Fixed"), qsTr("Every note at the same velocity")]]
                ParamButton {
                    required property var modelData
                    required property int index
                    objectName: "velocity" + modelData[0]
                    width: 42
                    param: p.get("velocity")
                    choice: index
                    text: modelData[0]
                    tooltip: modelData[1]
                }
            }
            ParamBox {
                objectName: "fixedVelocity"
                width: editor.controlsWidth - editor.captionWidth - 2 * 42 - 3 * 2
                enabled: editor.value("velocity", 0) >= 0.5
                param: p.get("fixed_velocity")
                step: 1
                decimals: 0
                defaultValue: 100
                sampleText: "127"
                tooltip: qsTr("The velocity every note plays at, while Fixed")
            }
        }
    }
}
