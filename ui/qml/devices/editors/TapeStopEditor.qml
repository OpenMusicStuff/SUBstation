import QtQuick
import QtQuick.Controls
import SUBstation

// The Tape Stop's editor, compact: a deck (two reels, the tape between them)
// over the stop's curve, worked out here as the engine works it out
// (TapeStop.cpp's speedAt: the speed falling from full to nothing along Curve),
// with Stop's button over its corner; beside it the times, the curve, the tone
// and the mix, three to a row.
//
// Its animation: the reels turn at the speed the tape is going (the display
// `speed`, through a DisplayValue), slowing to a halt as it stops and dimming
// while it is stopped; a dot rides the curve where the stop has got to; a
// change of the curve fades the new one in.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int deckWidth: 104
    readonly property int knobWidth: 56
    readonly property int knobSize: 28
    readonly property int spacing: 8

    implicitWidth: 8 + deckWidth + spacing + 3 * knobWidth + 2 * 4 + 8 - 2
    implicitHeight: 6 + Math.max(knobs.implicitHeight, 96) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["stop", "stop_time", "start_time", "curve", "tone", "mix"]
    }
    function value(id, otherwise) {
        const param = p.get(id)
        return param ? param.value : otherwise
    }

    DisplayValue {
        id: speedDisplay
        objectName: "speedDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "speed"
        width: 0
        height: 0
    }
    // The tape's speed as the engine has it (full while nothing comes, as it would run).
    readonly property real speed: speedDisplay.active ? Math.max(0, Math.min(1, speedDisplay.value))
                                                      : (editor.value("stop", 0) >= 0.5 ? 0 : 1)
    readonly property bool running: speedDisplay.active && editor.speed > 0.001

    // The speed `progress` (0..1) through a stop, along `curve` (-100..100): the engine's speedAt.
    function speedAt(progress, curve) {
        const k = Math.pow(2, Math.abs(curve) / 40)
        const t = Math.max(0, Math.min(1, progress))
        return curve >= 0 ? 1 - Math.pow(t, k) : Math.pow(1 - t, k)
    }
    // How far through a stop the tape is going at `speed`: speedAt's inverse.
    function progressAt(speed, curve) {
        const k = Math.pow(2, Math.abs(curve) / 40)
        const s = Math.max(0, Math.min(1, speed))
        return curve >= 0 ? Math.pow(1 - s, 1 / k) : 1 - Math.pow(s, 1 / k)
    }
    readonly property real curve: value("curve", 0)

    // The reels' turn, in degrees: on at the speed while the tape runs.
    property real angle: 0
    Timer {
        id: spin
        interval: 16
        repeat: true
        running: editor.running && editor.visible
        property double last: 0
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            const dt = Math.min(0.1, (now - last) / 1000)
            last = now
            editor.angle = (editor.angle + 240 * editor.speed * dt) % 360
        }
    }

    // A reel: a hub, its spokes and the tape wound on it.
    component Reel: Item {
        id: reel

        property real wound: 0.6  // how much of the reel the tape fills
        width: 34
        height: 34
        rotation: editor.angle

        Rectangle {  // the tape on it
            anchors.centerIn: parent
            width: 12 + (reel.width - 12) * reel.wound
            height: width
            radius: width / 2
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
        }
        Rectangle {  // the flange
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: Theme.textDim
        }
        Repeater {
            model: 3
            Rectangle {
                required property int index
                x: reel.width / 2 - 1
                y: 4
                width: 2
                height: reel.height / 2 - 4
                color: Theme.textDim
                transform: Rotation {
                    origin.x: 1
                    origin.y: reel.height / 2 - 4
                    angle: index * 120
                }
            }
        }
        Rectangle {  // the hub
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: Theme.accent
        }
    }

    Rectangle {
        id: deck
        objectName: "deck"
        x: 8
        y: 6
        width: editor.deckWidth
        height: editor.height - 12
        color: Theme.lane
        border.color: Theme.border
        clip: true

        // The reels and the tape between them, dimmed while the tape is stopped.
        Item {
            id: reels
            objectName: "reels"
            x: 1
            y: 3
            width: parent.width - 2
            height: 36
            opacity: 0.45 + 0.55 * editor.speed
            Behavior on opacity {
                NumberAnimation {
                    duration: 150
                }
            }

            Rectangle {  // the tape
                x: left.x + left.width / 2
                y: left.y + left.height - 1
                width: right.x - left.x
                height: 1
                color: Theme.accent
            }
            Reel {
                id: left
                x: 12
                wound: 0.75
            }
            Reel {
                id: right
                x: reels.width - width - 12
                wound: 0.35
            }
        }

        // The curve: the speed through a stop, left to right, a dot where the tape is.
        Item {
            id: graph
            objectName: "curveGraph"
            x: 4
            y: reels.y + reels.height + 4
            width: parent.width - 8
            height: parent.height - y - 4

            Rectangle {  // nothing
                y: graph.height - 1
                width: parent.width
                height: 1
                color: Theme.border
            }
            Repeater {
                model: Math.max(0, Math.floor(graph.width / 2))
                Rectangle {
                    required property int index
                    readonly property real level: editor.speedAt((index + 0.5) / (graph.width / 2), editor.curve)
                    x: 2 * index
                    y: graph.height - height
                    width: 1
                    height: Math.max(1, level * graph.height)
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
                    Behavior on height {
                        NumberAnimation {
                            duration: 140
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }
            Rectangle {
                id: dot
                objectName: "speedDot"
                readonly property real progress: editor.progressAt(editor.speed, editor.curve)
                x: progress * (graph.width - width)
                y: (1 - editor.speed) * (graph.height - height)
                width: 5
                height: 5
                radius: 2.5
                color: Theme.accent
                border.color: Theme.accentText
                Behavior on x {
                    NumberAnimation {
                        duration: 60
                    }
                }
                Behavior on y {
                    NumberAnimation {
                        duration: 60
                    }
                }
            }
        }

        HoverHandler {
            id: hover
        }
        ToolTip.visible: hover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("The tape's speed through a stop: Curve bends it, the reels turn as fast as it goes")
    }

    Grid {
        id: knobs
        x: deck.x + deck.width + editor.spacing
        y: 6
        columns: 3
        columnSpacing: 4
        rowSpacing: 2

        EditorKnob {
            objectName: "stop_time"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("stop_time")
            title: qsTr("Stop Time")
            tooltip: qsTr("Stop Time: how long the tape takes to slow to a halt")
        }
        EditorKnob {
            objectName: "start_time"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("start_time")
            title: qsTr("Start Time")
            tooltip: qsTr("Start Time: how long it takes to spin up again once Stop is let go")
        }
        EditorKnob {
            objectName: "curve"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("curve")
            title: qsTr("Curve")
            formatter: v => (v > 0 ? "+" : "") + v.toFixed(0) + " %"
            tooltip: qsTr("Curve: above 0 the speed holds, then drops; below, it drops at once and crawls to the end")
        }
        EditorKnob {
            objectName: "tone"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("tone")
            title: qsTr("Tone")
            tooltip: qsTr("Tone: how much darker it gets as it slows")
        }
        EditorKnob {
            objectName: "mix"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("mix")
            title: qsTr("Dry/Wet")
        }
        // Stop itself, lit while stopped: pressed to stop, pressed again to start.
        Item {
            width: editor.knobWidth
            height: knobs.children[0].height

            ParamButton {
                id: stopButton
                objectName: "stop"
                anchors.centerIn: parent
                width: parent.width - 4
                height: 24
                param: p.get("stop")
                text: qsTr("Stop")
                tooltip: qsTr("Stop: the tape slows to a halt; pressed again, it spins up (automate it to stop on the beat)")
            }
        }
    }
}
