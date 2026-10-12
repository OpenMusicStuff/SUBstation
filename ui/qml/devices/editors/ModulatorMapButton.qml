import QtQuick
import SUBstation

// A modulator's Map button (its editor's, beside Sync): lit and pulsing while
// it waits (the next parameter touched on the track is mapped to it,
// ModulationLearn); Escape, or the button again, gives up.
RoleButton {
    id: button

    required property ModulatorMappings mappings

    objectName: "map"
    role: "small"
    checkable: false
    lit: mappings.learning
    text: mappings.learning ? qsTr("Touch…") : qsTr("Map")
    tooltip: mappings.learning ? qsTr("Touch a parameter on this track to modulate it (Esc to cancel)")
                               : qsTr("Map: then touch any parameter on this track to modulate it")
    onClicked: mappings.toggleLearning()

    Shortcut {
        sequence: "Escape"
        enabled: button.mappings.learning
        onActivated: button.mappings.stopLearning()
    }

    // While it waits, it pulses.
    SequentialAnimation on opacity {
        running: button.mappings.learning
        loops: Animation.Infinite
        alwaysRunToEnd: true
        NumberAnimation { from: 1; to: 0.45; duration: 420; easing.type: Easing.InOutSine }
        NumberAnimation { from: 0.45; to: 1; duration: 420; easing.type: Easing.InOutSine }
    }
}
