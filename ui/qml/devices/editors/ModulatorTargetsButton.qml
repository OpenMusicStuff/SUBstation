import QtQuick
import SUBstation

// A modulator editor's cell (beside its knobs, as one of them) whose button
// unfolds what it moves (ModulatorTargets) and folds it again: view state per
// device (DeviceViews, "targets"), not undone. Its caption over the button and
// how many targets there are under it line up with the knobs' captions and
// readouts.
Column {
    id: cell

    required property string deviceId
    required property bool shown
    property int count: 0
    property real size: 38  // the knobs' (the button a little smaller)
    readonly property alias button: button

    width: Math.max(size + 16, 52)
    spacing: 1

    EditorCaption {
        width: parent.width
        text: qsTr("Targets")
    }
    Item {  // a knob's height, the button in its middle: the count under it in line with the knobs' readouts
        width: parent.width
        height: cell.size

        RoleButton {
            id: button
            objectName: "targetsButton"
            anchors.centerIn: parent
            width: cell.size - 6
            height: cell.size - 6
            role: "small"
            checkable: false
            lit: cell.shown
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
            font.pointSize: 13
            text: cell.shown ? "‹" : "›"
            tooltip: cell.shown ? qsTr("Hide what it moves") : qsTr("Show what it moves")
            onClicked: DeviceViews.setValue(cell.deviceId, "targets", !cell.shown)
        }
    }
    EditorReadout {
        width: parent.width
        color: cell.count > 0 ? Theme.modulation : Theme.textDim
        text: cell.count === 0 ? qsTr("none") : cell.count === 1 ? qsTr("1 target") : qsTr("%1 targets").arg(cell.count)
        elide: Text.ElideRight
    }
}
