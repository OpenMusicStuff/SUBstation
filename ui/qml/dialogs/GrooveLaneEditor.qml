import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// One lane of the groove editor (GrooveDialog): its keys (the default lane:
// every key no other lane takes), then its steps over the cycle, a bar a column:
// each step a handle at its place on the grid moved by its timing (dragged late
// or early, up to half a step), its velocity below (dragged up or down from the
// middle line). Double-click puts a step back. The notes the groove was made
// from that this lane plays are drawn faintly under its steps. Each drag is one
// undo step (Session.grooves.setStep with a merge key).
//
// Its delegates are made once per step count, not per change, so a drag goes on
// while what it changes is redrawn.
Column {
    id: laneEditor

    required property string grooveId
    required property int lane
    required property var laneData
    required property int stepCount
    required property int bars
    property var notes: []
    property var otherKeys: []  // the other lanes' keys (the default lane plays the rest)

    readonly property bool defaultLane: lane === 0
    readonly property var steps: laneData && laneData.steps ? laneData.steps : []
    readonly property real stepWidth: (width - 2) / Math.max(1, stepCount)
    readonly property real cycleBeats: bars * 4
    readonly property real stepBeats: cycleBeats / Math.max(1, stepCount)
    // The notes it plays, from what the groove was made of.
    readonly property var laneNotes: notes.filter(n => defaultLane ? otherKeys.indexOf(n.key) < 0
                                                                   : laneData.keys.indexOf(n.key) >= 0)

    function stepAt(k) {
        return k < steps.length ? steps[k] : { timing: 0, velocity: 0, ms: 0, length: 1 }
    }
    // A step's grid point, and where it plays (x).
    function gridX(k) {
        return 1 + (k + 0.5) * stepWidth
    }
    function playX(k) {
        return gridX(k) + stepAt(k).timing * stepWidth
    }
    // The step whose handle is nearest x.
    function nearest(x) {
        let best = 0
        let distance = Infinity
        for (let k = 0; k < stepCount; ++k) {
            const d = Math.abs(playX(k) - x)
            if (d < distance) {
                distance = d
                best = k
            }
        }
        return best
    }

    spacing: 2

    RowLayout {
        width: parent.width
        spacing: 6

        Label {
            text: laneEditor.defaultLane ? qsTr("Every key") : qsTr("Keys")
            font: Theme.uiFont(9, true)
        }
        TextField {
            objectName: "laneKeys" + laneEditor.lane
            visible: !laneEditor.defaultLane
            Layout.preferredWidth: 140
            text: laneEditor.laneData.keys ? laneEditor.laneData.keys.join(" ") : ""
            selectByMouse: true
            placeholderText: qsTr("MIDI notes: 36 38 42")
            onEditingFinished: {
                const keys = text.split(/[\s,]+/).filter(t => t !== "").map(t => parseInt(t)).filter(k => k >= 0 && k <= 127)
                if (keys.length > 0)
                    Session.grooves.setLaneKeys(laneEditor.grooveId, laneEditor.lane, keys)
            }
            ToolTip.visible: hovered
            ToolTip.delay: 700
            ToolTip.text: qsTr("The keys this lane plays (MIDI note numbers; 36: C1, the kick on a Drum Rack)")
        }
        Label {
            visible: laneEditor.defaultLane
            text: qsTr("(but the other lanes' keys)")
            color: Theme.textDim
        }
        Item {
            Layout.fillWidth: true
        }
        RoleButton {
            objectName: "resetLane" + laneEditor.lane
            role: "small"
            text: qsTr("Straight")
            tooltip: qsTr("Every step of the lane back on the grid, at its velocity and length")
            onClicked: Session.grooves.resetLane(laneEditor.grooveId, laneEditor.lane)
        }
        RoleButton {
            objectName: "removeLane" + laneEditor.lane
            visible: !laneEditor.defaultLane
            role: "small"
            text: "×"
            tooltip: qsTr("Remove the lane: its keys play the default lane")
            onClicked: Session.grooves.removeLane(laneEditor.grooveId, laneEditor.lane)
        }
    }

    // --- Timing ---

    Rectangle {
        id: timingArea
        objectName: "timingArea" + laneEditor.lane
        width: parent.width
        height: 46
        color: Theme.lane
        border.color: Theme.border
        clip: true

        Repeater {  // the beats (the bars stronger), through their grid points
            model: Math.round(laneEditor.cycleBeats)
            Rectangle {
                required property int index
                x: laneEditor.gridX(index / laneEditor.stepBeats)
                width: 1
                height: timingArea.height
                color: index % 4 === 0 ? Theme.gridBar : Theme.gridBeat
            }
        }
        Repeater {  // the notes it was made from, faintly
            model: laneEditor.laneNotes
            Rectangle {
                required property var modelData
                x: 1 + (modelData.beat / laneEditor.stepBeats + 0.5) * laneEditor.stepWidth - 1
                y: 4
                width: 2
                height: timingArea.height - 8
                color: Theme.text
                opacity: 0.08 + 0.12 * Math.min(1, modelData.velocity / 127)
            }
        }
        Repeater {
            model: laneEditor.stepCount
            Item {
                required property int index
                readonly property real timing: laneEditor.stepAt(index).timing
                anchors.fill: parent

                Rectangle {  // from the grid to where it plays
                    x: Math.min(laneEditor.gridX(index), laneEditor.playX(index))
                    y: timingArea.height / 2
                    width: Math.abs(laneEditor.playX(index) - laneEditor.gridX(index))
                    height: 1
                    color: Theme.accent
                    opacity: 0.6
                }
                Rectangle {
                    x: laneEditor.gridX(index)
                    y: timingArea.height - 5
                    width: 1
                    height: 4
                    color: Theme.textDisabled
                }
                Rectangle {
                    objectName: "handle" + index
                    x: laneEditor.playX(index) - width / 2
                    y: timingArea.height / 2 - height / 2
                    width: Math.max(3, Math.min(8, laneEditor.stepWidth * 0.6))
                    height: 14
                    radius: 2
                    color: Math.abs(parent.timing) > 1e-6 ? Theme.accent : Theme.textDim
                }
            }
        }
        MouseArea {
            id: timingMouse
            objectName: "timingMouse" + laneEditor.lane
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            property int step: -1
            property string mergeKey: ""
            function set(x) {
                const timing = Math.max(-0.5, Math.min(0.5, (x - laneEditor.gridX(step)) / laneEditor.stepWidth))
                Session.grooves.setStep(laneEditor.grooveId, laneEditor.lane, step, "timing", timing, mergeKey)
            }
            onPressed: mouse => {
                step = laneEditor.nearest(mouse.x)
                mergeKey = "grooveTiming:" + Date.now()
            }
            onPositionChanged: mouse => {
                if (pressed && step >= 0)
                    set(mouse.x)
            }
            onDoubleClicked: mouse => {
                const k = laneEditor.nearest(mouse.x)
                Session.grooves.setStep(laneEditor.grooveId, laneEditor.lane, k, "timing", 0)
            }
            onReleased: step = -1
        }
        ToolTip.visible: timingMouse.containsMouse && !timingMouse.pressed
        ToolTip.delay: 900
        ToolTip.text: qsTr("Drag a step late or early (double-click: back on the grid)")
    }

    // --- Velocity ---

    Rectangle {
        id: velocityArea
        objectName: "velocityArea" + laneEditor.lane
        width: parent.width
        height: 34
        color: Theme.lane
        border.color: Theme.border
        clip: true

        Rectangle {  // no change
            y: velocityArea.height / 2
            width: velocityArea.width
            height: 1
            color: Theme.gridBeat
        }
        Repeater {
            model: laneEditor.stepCount
            Rectangle {
                required property int index
                readonly property real velocity: laneEditor.stepAt(index).velocity
                readonly property real half: velocityArea.height / 2 - 2
                x: laneEditor.gridX(index) - width / 2
                y: velocity >= 0 ? velocityArea.height / 2 - velocity * half : velocityArea.height / 2
                width: Math.max(2, Math.min(6, laneEditor.stepWidth * 0.5))
                height: Math.max(1, Math.abs(velocity) * half)
                color: Theme.accent
                opacity: 0.8
            }
        }
        MouseArea {
            id: velocityMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            property int step: -1
            property string mergeKey: ""
            function set(y) {
                const half = velocityArea.height / 2 - 2
                const velocity = Math.max(-1, Math.min(1, (velocityArea.height / 2 - y) / half))
                Session.grooves.setStep(laneEditor.grooveId, laneEditor.lane, step, "velocity", velocity, mergeKey)
            }
            onPressed: mouse => {
                step = Math.max(0, Math.min(laneEditor.stepCount - 1, Math.floor((mouse.x - 1) / laneEditor.stepWidth)))
                mergeKey = "grooveVelocity:" + Date.now()
                set(mouse.y)
            }
            onPositionChanged: mouse => {
                if (pressed && step >= 0)
                    set(mouse.y)
            }
            onDoubleClicked: mouse => {
                const k = Math.max(0, Math.min(laneEditor.stepCount - 1, Math.floor((mouse.x - 1) / laneEditor.stepWidth)))
                Session.grooves.setStep(laneEditor.grooveId, laneEditor.lane, k, "velocity", 0)
            }
            onReleased: step = -1
        }
        ToolTip.visible: velocityMouse.containsMouse && !velocityMouse.pressed
        ToolTip.delay: 900
        ToolTip.text: qsTr("A step's velocity: dragged up, louder; down, softer (double-click: no change)")
    }
}
