import QtQuick
import SUBstation

// A small button on a device's title bar: 16 px square, the "device-header"
// look (no background but under the mouse, the accent while checked), its icon
// 5 px smaller or its text ("‹", "›") in 11 pt, both in the look's colour (dim
// at rest, the text's under the mouse, the accent's text while lit).
RoleButton {
    role: "device-header"
    implicitWidth: 16
    implicitHeight: 16
    width: 16
    height: 16
    iconSize: 11
    iconColor: look.text
    font.pointSize: 11
}
