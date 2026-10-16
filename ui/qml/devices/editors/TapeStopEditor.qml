import QtQuick
import QtQuick.Controls
import SUBstation

// The Tape Stop's editor, laid out as Kilohearts' Tape Stop: a deck across the
// top, two big reels (cut off by its bottom edge) either side of the Stop
// button (a play triangle while the tape runs, a square while it is stopped);
// under it the knobs in a row, Stop Time and Start Time at the ends, Curve,
// Tone and Dry/Wet between them.
//
// Its animation: the reels turn at the speed the tape is going (the display
// `speed`, through a DisplayValue), slowing to a halt as it stops, the tape
// between them sagging and the deck dimming while it is stopped; the button
// glows while the tape runs.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int knobWidth: 56
    readonly property int knobSize: 28
    readonly property int knobGap: 4
    readonly property int knobCount: 5
    readonly property int contentWidth: knobCount * knobWidth + (knobCount - 1) * knobGap

    implicitWidth: 8 + contentWidth + 8 - 2
    implicitHeight: 6 + 60 + 4 + knobs.implicitHeight + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["stop", "stop_time", "start_time", "curve", "tone", "mix"]
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
    readonly property bool stopped: p.get("stop") ? p.get("stop").value >= 0.5 : false
    // The tape's speed as the engine has it (while nothing comes, as the switch says it would run).
    readonly property real speed: speedDisplay.active ? Math.max(0, Math.min(1, speedDisplay.value))
                                                      : (editor.stopped ? 0 : 1)
    readonly property bool running: speedDisplay.active && editor.speed > 0.001

    // The reels' turn, in degrees: on at the speed while the tape runs.
    property real angle: 0
    Timer {
        interval: 16
        repeat: true
        running: editor.running && editor.visible
        property double last: 0
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            const dt = Math.min(0.1, (now - last) / 1000)
            last = now
            editor.angle = (editor.angle + 200 * editor.speed * dt) % 360
        }
    }

    // A reel: a grey flange with three holes, turning; the tape wound on it under them.
    component Reel: Item {
        id: reel

        property real wound: 0.6  // how much of the reel the tape fills
        rotation: editor.angle

        Rectangle {  // the flange
            anchors.fill: parent
            radius: width / 2
            color: Qt.rgba(Theme.textDim.r, Theme.textDim.g, Theme.textDim.b, 0.55)
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.25)
        }
        Rectangle {  // the tape wound on it, seen through the holes
            anchors.centerIn: parent
            width: reel.width * (0.3 + 0.55 * reel.wound)
            height: width
            radius: width / 2
            color: Qt.rgba(Theme.lane.r * 0.6, Theme.lane.g * 0.6, Theme.lane.b * 0.6, 1)
            opacity: 0.6
        }
        Repeater {  // the holes
            model: 3
            Rectangle {
                required property int index
                readonly property real a: (index * 120 - 90) * Math.PI / 180
                width: reel.width * 0.3
                height: width
                radius: width / 2
                x: reel.width / 2 + Math.cos(a) * reel.width * 0.27 - width / 2
                y: reel.height / 2 + Math.sin(a) * reel.width * 0.27 - height / 2
                color: Theme.lane
            }
        }
        Rectangle {  // the hub
            anchors.centerIn: parent
            width: reel.width * 0.16
            height: width
            radius: width / 2
            color: Theme.lane
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.35)
            Rectangle {  // its key
                anchors.horizontalCenter: parent.horizontalCenter
                y: 1
                width: 2
                height: parent.height / 2 - 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.5)
            }
        }
    }

    Rectangle {
        id: deck
        objectName: "deck"
        x: 8
        y: 6
        width: editor.contentWidth
        height: editor.height - 12 - knobs.height - 4
        color: Theme.lane
        border.color: Theme.border
        clip: true

        readonly property real reelSize: Math.min(height * 1.25, width * 0.36)

        Item {
            id: reels
            objectName: "reels"
            anchors.fill: parent
            opacity: 0.55 + 0.45 * editor.speed
            Behavior on opacity {
                NumberAnimation {
                    duration: 150
                }
            }

            Reel {
                id: left
                width: deck.reelSize
                height: width
                x: deck.width * 0.04
                y: deck.height * 0.18
                wound: 0.8
            }
            Reel {
                id: right
                width: deck.reelSize
                height: width
                x: deck.width - width - deck.width * 0.04
                y: deck.height * 0.18
                wound: 0.35
            }
            // The tape from reel to reel under the button, sagging as it slows.
            Rectangle {
                x: left.x + left.width / 2
                width: right.x + right.width / 2 - x
                y: deck.height - 3 - 2 * (1 - editor.speed)
                height: 2
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.5)
                Behavior on y {
                    NumberAnimation {
                        duration: 120
                    }
                }
            }
        }

        // Stop: a play triangle while the tape runs, a square while it is stopped; pressed, it switches.
        Item {
            id: stopButton
            objectName: "stop"
            readonly property bool lit: editor.stopped
            readonly property DeviceParam param: p.get("stop")
            readonly property real glyph: Math.min(30, deck.height * 0.5)
            anchors.centerIn: parent
            width: glyph + 14
            height: glyph + 10

            ParamArea {
                anchors.fill: parent
                param: stopButton.param
            }
            MouseArea {
                id: press
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onPressed: if (stopButton.param) stopButton.param.touch()
                onClicked: if (stopButton.param) stopButton.param.set(stopButton.lit ? 0 : 1)
            }

            // The play triangle: rows narrowing to its point.
            Item {
                id: play
                objectName: "playGlyph"
                anchors.centerIn: parent
                width: stopButton.glyph * 0.9
                height: stopButton.glyph
                opacity: stopButton.lit ? 0 : 1
                scale: press.pressed ? 0.92 : (press.containsMouse ? 1.06 : 1)
                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: 90
                    }
                }
                Repeater {
                    model: Math.max(1, Math.round(play.height))
                    Rectangle {
                        required property int index
                        y: index
                        height: 1
                        width: play.width * (1 - Math.abs(2 * (index + 0.5) / play.height - 1))
                        color: Theme.accent
                    }
                }
            }
            // The stopped square.
            Rectangle {
                objectName: "stopGlyph"
                anchors.centerIn: parent
                width: stopButton.glyph * 0.75
                height: width
                radius: 2
                color: Theme.textDim
                opacity: stopButton.lit ? 1 : 0
                scale: press.pressed ? 0.92 : (press.containsMouse ? 1.06 : 1)
                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                    }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: 90
                    }
                }
            }

            ToolTip.visible: press.containsMouse
            ToolTip.delay: 700
            ToolTip.text: qsTr("Stop: the tape slows to a halt; pressed again, it spins up (automate it to stop on the beat)")
        }
    }

    // The knobs in a row under the deck: Stop Time and Start Time at the ends.
    Row {
        id: knobs
        x: deck.x
        y: deck.y + deck.height + 4
        spacing: editor.knobGap

        EditorKnob {
            objectName: "stop_time"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("stop_time")
            title: qsTr("Stop Time")
            tooltip: qsTr("Stop Time: how long the tape takes to slow to a halt")
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
        EditorKnob {
            objectName: "start_time"
            width: editor.knobWidth
            size: editor.knobSize
            param: p.get("start_time")
            title: qsTr("Start Time")
            tooltip: qsTr("Start Time: how long it takes to spin up again once Stop is let go")
        }
    }
}
