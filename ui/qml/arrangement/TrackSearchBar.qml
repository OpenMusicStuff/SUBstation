import QtQuick
import QtQuick.Controls
import SUBstation

// The find bar over the arrangement and the clip view (Q): finds tracks by
// their names, then by what they are (their labels and what those were made
// from: an instrument's preset, the files they play; Session.trackSearch).
// Typing goes to the best match; Enter (or Down) to the next, after the last the
// first again; Shift+Enter (or Up) to the one before; Esc (or ✕) closes it
// (closed(): the view under it takes the keyboard back). Each match is
// matchChosen(trackId): the main window shows it (Main.qml's showTrackMatch).
Rectangle {
    id: bar

    readonly property alias field: field

    signal closed()
    signal matchChosen(string trackId)

    // Q: shows (or, shown, takes the keyboard again), what was typed selected to type over.
    function open() {
        visible = true
        field.forceActiveFocus()
        field.selectAll()
    }
    function close() {
        if (!visible)
            return
        visible = false
        closed()
    }
    function reveal(trackId) {
        if (trackId !== "")
            matchChosen(trackId)
    }
    function step(forward) {
        reveal(forward ? Session.trackSearch.next() : Session.trackSearch.previous())
    }

    visible: false
    width: content.implicitWidth + 12
    height: Theme.controlHeight + 10
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radius

    Row {
        id: content
        x: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        TextField {
            id: field
            objectName: "trackSearchField"
            width: 220
            placeholderText: qsTr("Find track  (Q)")
            Component.onCompleted: text = Session.trackSearch.query
            onTextEdited: {
                Session.trackSearch.query = text
                bar.reveal(Session.trackSearch.currentTrackId)
            }
            Keys.onReturnPressed: event => bar.step(!(event.modifiers & Qt.ShiftModifier))
            Keys.onEnterPressed: event => bar.step(!(event.modifiers & Qt.ShiftModifier))
            Keys.onDownPressed: bar.step(true)
            Keys.onUpPressed: bar.step(false)
            Keys.onEscapePressed: bar.close()
        }

        // "2 of 5", or that nothing matches.
        Label {
            objectName: "trackSearchCount"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(implicitWidth, 64)
            color: Theme.textDim
            text: Session.trackSearch.count > 0
                  ? qsTr("%1 of %2").arg(Session.trackSearch.current + 1).arg(Session.trackSearch.count)
                  : field.text.trim() !== "" ? qsTr("No match") : ""
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "✕"
            font.pixelSize: 10
            color: closeArea.containsMouse ? Theme.text : Theme.textDim

            MouseArea {
                id: closeArea
                objectName: "trackSearchClose"
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                onClicked: bar.close()
            }
        }
    }
}
