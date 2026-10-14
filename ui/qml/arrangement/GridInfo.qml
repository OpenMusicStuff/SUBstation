import QtQuick
import QtQuick.Controls
import SUBstation

// The corner above the headers, as Ableton's: on the ruler's ground (its lit
// top edge, its dark base), Automation (A: every track's automation shown, lit
// while it is) and Lock Envelopes; under them the grid's size. A click
// anywhere else in it toggles snapping.
Rectangle {
    id: info

    required property Arrangement arrangement

    color: Theme.window

    Rectangle {
        width: parent.width
        height: 1
        color: Qt.lighter(Theme.window, 1.5)
    }
    Rectangle {
        width: parent.width
        height: 2
        anchors.bottom: parent.bottom
        color: Theme.border
    }
    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onPressed: info.arrangement.snap = !info.arrangement.snap
    }

    Row {
        x: 12
        y: 5
        spacing: 4

        ToggleButton {
            objectName: "automationMode"
            width: 30
            height: 16
            role: "tool"
            iconName: "automation_mode"
            iconSize: 11
            tooltip: qsTr("Automation (A): every track's automation shown, or none")
            checkable: false
            checked: info.arrangement.automationMode
            onClicked: Session.editor.toggleAllAutomation()
        }
        ToggleButton {
            objectName: "lockEnvelopes"
            width: 30
            height: 16
            role: "tool"
            iconName: "lock_envelopes"
            iconSize: 11
            tooltip: qsTr("Lock Envelopes: automation stays in place when clips move (off: it moves with them)")
            checkable: false
            checked: Session.project.automationLocked
            onClicked: Session.editor.setAutomationLocked(!Session.project.automationLocked)
        }
    }

    Text {
        objectName: "gridLabel"
        x: 12
        y: 25
        width: parent.width - 20
        text: info.arrangement.gridLabel
        color: info.arrangement.snap ? Theme.textDim : Theme.textDisabled
        font: Theme.smallFont
        elide: Text.ElideRight
    }

    ToolTip.visible: area.containsMouse
    ToolTip.text: qsTr("Grid (Ctrl+1 narrower, Ctrl+2 wider). Click to toggle snapping (Ctrl+4).")
    ToolTip.delay: 700
}
