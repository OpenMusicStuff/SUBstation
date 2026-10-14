import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// Preferences › MIDI (Session.midiPreferences, the old MidiPage): the MIDI
// inputs connected, each on (tracks can hear and record it) or off; one that
// could not be opened in red, why in its tooltip. Refresh looks for inputs
// plugged in or taken out since. Below, how late each input's driver hands
// its messages over (measured while the page shows), and whether MIDI input
// plays at once.
ColumnLayout {
    id: page

    readonly property var prefs: Session.midiPreferences
    readonly property alias inputList: inputs

    spacing: 6

    Label { text: qsTr("MIDI Inputs") }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitHeight: 160
        color: Theme.panel

        HoverHandler {
            id: listHover
        }
        ToolTip.visible: listHover.hovered && inputs.count === 0
        ToolTip.text: qsTr("MIDI tracks hear the inputs that are on (all of them, or the one they choose).")
        ToolTip.delay: 700

        ListView {
            id: inputs
            objectName: "midiInputs"
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: page.prefs.inputs
            ScrollBar.vertical: ScrollBar {}

            delegate: CheckBox {
                id: input

                required property var modelData
                required property int index

                objectName: "midiInput_" + index
                width: ListView.view.width
                focusPolicy: Qt.NoFocus
                text: modelData.name
                checked: modelData.enabled
                onToggled: page.prefs.setInputEnabled(modelData.name, checked)

                contentItem: Text {
                    leftPadding: input.indicator.width + input.spacing
                    text: input.text
                    font: input.font
                    // (the Python UI's red for an input that failed to open)
                    color: input.modelData.error ? "#ff6b5e" : Theme.text
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                ToolTip.visible: hovered
                ToolTip.text: modelData.error
                              || qsTr("MIDI tracks hear the inputs that are on (all of them, or the one they choose).")
                ToolTip.delay: 700
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        RoleButton {
            objectName: "midiRefresh"
            text: qsTr("Refresh")
            tooltip: qsTr("Look for MIDI inputs plugged in or taken out since.")
            onClicked: page.prefs.refresh()
        }
        Label {
            objectName: "midiStatus"
            Layout.fillWidth: true
            text: page.prefs.status
            wrapMode: Text.Wrap
            color: Theme.textDim
        }
    }

    CheckBox {
        objectName: "midiImmediate"
        focusPolicy: Qt.NoFocus
        text: qsTr("Play MIDI input at once (sooner, but its timing jitters by up to a buffer)")
        checked: Session.bridge.midiImmediate
        onToggled: {
            Session.bridge.midiImmediate = checked
            checked = Qt.binding(() => Session.bridge.midiImmediate)
        }
        ToolTip.visible: hovered
        ToolTip.text: qsTr("MIDI input normally plays one audio buffer after it arrives, so every note is "
                           + "delayed alike.\nAt once, a note plays as the next buffer starts: up to a buffer "
                           + "sooner, but as early as the buffer falls.")
        ToolTip.delay: 700
    }

    Label {
        id: delivery
        objectName: "midiDelivery"
        Layout.fillWidth: true
        text: qsTr("Play a few notes: how late the system hands them over shows here.")
        wrapMode: Text.Wrap
        color: Theme.textDim

        Timer {
            interval: 1000
            running: page.visible
            repeat: true
            onTriggered: {
                const text = page.prefs.takeDeliveryText()
                if (text !== "")
                    delivery.text = qsTr("Handed over after the driver took them in (last second):\n") + text
            }
        }
    }
}
