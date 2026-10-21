import QtQuick
import SUBstation

// The grip in the gap between the info view and the devices (as Ableton's; the
// gaps between devices hold their meters): two short dark lines, upright in
// the middle of the gap's height. Put it in the gap.
Item {
    implicitWidth: 5

    Repeater {
        model: 2
        Rectangle {
            required property int index
            x: index * 3
            y: parent.height * 0.21
            width: 2
            height: parent.height * 0.58
            color: Theme.border
        }
    }
}
