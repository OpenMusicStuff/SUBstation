import QtQuick
import SUBstation

// A groove drawn (Session.grooves' map of one: GrooveLibrary.h): one lane's
// steps over its cycle, each a bar where it plays (moved from its place on the
// grid by its timing, the grid's place a faint tick) as tall as it is loud. The
// beats and bars are lines; a step lit (`lit`, the step of the cycle playing)
// glows. With no groove, a flat grid.
//
//   GrooveSteps { groove: Session.grooves.groove(id); lit: playingStep }
Rectangle {
    id: view

    property var groove: ({})
    property int lane: 0
    property int lit: -1

    readonly property var steps: {
        const lanes = groove && groove.lanes ? groove.lanes : []
        return lane < lanes.length ? lanes[lane].steps : []
    }
    readonly property int count: Math.max(1, steps.length)
    readonly property real cycleBeats: groove && groove.bars ? groove.bars * 4 : 4
    readonly property real stepBeats: groove && groove.step ? groove.step : 0.25
    readonly property real stepWidth: (width - 2) / count

    color: Theme.lane
    border.color: Theme.border
    clip: true

    Repeater {  // the beats, the bars stronger
        model: Math.round(view.cycleBeats)
        Rectangle {
            required property int index
            x: 1 + index / view.cycleBeats * (view.width - 2)
            width: 1
            height: view.height
            color: index % 4 === 0 ? Theme.gridBar : Theme.gridBeat
        }
    }
    Rectangle {
        id: glow
        objectName: "playingGlow"
        x: 1 + Math.max(0, view.lit) * view.stepWidth
        width: view.stepWidth
        height: view.height
        color: Theme.accent
        opacity: view.lit >= 0 ? 0.18 : 0
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
        model: view.steps
        Item {
            required property var modelData
            required property int index
            readonly property bool isLit: view.lit === index
            readonly property real loud: Math.max(0.1, Math.min(1, 0.6 + 0.4 * modelData.velocity))
            x: 1 + index * view.stepWidth
            width: view.stepWidth
            height: view.height

            Rectangle {  // where the grid has it
                x: 0
                y: view.height - 4
                width: 1
                height: 3
                color: Theme.textDisabled
            }
            Rectangle {
                x: Math.max(-parent.x + 1, modelData.timing * view.stepWidth)
                y: view.height - 4 - height
                width: Math.max(2, Math.min(3, view.stepWidth * 0.3))
                height: Math.max(2, (view.height - 10) * parent.loud)
                radius: 1
                color: parent.isLit ? Qt.lighter(Theme.accent, 1.35) : Theme.accent
                opacity: parent.isLit ? 1 : view.lit < 0 ? 0.75 : 0.5
                Behavior on height {
                    NumberAnimation {
                        duration: 120
                    }
                }
            }
        }
    }
}
