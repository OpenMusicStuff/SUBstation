import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// Preferences › Keyboard (KeyMap): the profile the keys start from, and every
// menu command's keys, to change. A key's × takes it from its command; + then
// a key pressed gives the command that key too (Esc: never mind), taking it
// from the command that had it, which the line under the list says; Reset
// gives a command the profile's keys again, Reset All every command. Changes
// apply at once and are kept for the next run.
ColumnLayout {
    id: page

    // The command whose next key is being waited for ("": none).
    property string recording: ""
    // What the last change did (a key taken from another command).
    property string message: ""
    readonly property alias commandList: list
    readonly property alias searchField: search

    // The commands the search finds (in their names, their menus or their keys).
    readonly property var shownCommands: {
        const words = search.text.trim().toLowerCase()
        const bindings = KeyMap.bindings
        if (words === "")
            return KeyMap.commands
        return KeyMap.commands.filter(command => {
            const keys = (bindings[command.id] || []).map(key => Theme.shortcutText(key)).join(" ")
            const text = (command.name + " " + command.category + " " + keys).toLowerCase()
            return text.indexOf(words) >= 0
        })
    }

    // Waits for the command's next key (pressed on the recorder).
    function record(id) {
        recording = id
        message = ""
        recorder.forceActiveFocus()
    }

    // Gives the command the key; says which command had it.
    function assign(id, key) {
        recording = ""
        const owner = KeyMap.assign(id, key)
        message = owner !== "" ? qsTr("%1 was %2's: now it is %3's.").arg(Theme.shortcutText(key))
                                                                       .arg(KeyMap.commandName(owner))
                                                                       .arg(KeyMap.commandName(id))
                               : ""
    }

    spacing: 6

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Label { text: qsTr("Profile") }
        ChoiceBox {
            objectName: "keyProfile"
            Layout.fillWidth: true
            choices: KeyMap.profiles.map(profile => ({ label: profile.name, value: profile.id }))
            chosenIndex: KeyMap.profiles.findIndex(profile => profile.id === KeyMap.profile)
            onChosen: index => KeyMap.profile = KeyMap.profiles[index].id
        }
        RoleButton {
            objectName: "resetAllKeys"
            text: qsTr("Reset All")
            tooltip: qsTr("Every command's keys as the profile has them.")
            onClicked: {
                page.recording = ""
                page.message = ""
                KeyMap.resetAll()
            }
        }
    }

    TextField {
        id: search
        objectName: "keySearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search commands and keys")
        selectByMouse: true
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitWidth: 520
        implicitHeight: 320
        color: Theme.panel

        ListView {
            id: list
            objectName: "keyCommands"
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: page.shownCommands
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: row

                required property var modelData
                required property int index
                readonly property string commandId: modelData.id
                readonly property var keys: KeyMap.bindings[commandId] || []
                readonly property bool waiting: page.recording === commandId
                readonly property bool customized: KeyMap.customizedCommands.indexOf(commandId) >= 0

                objectName: "keyRow_" + commandId
                width: ListView.view.width
                height: 26

                Rectangle {
                    anchors.fill: parent
                    color: row.waiting ? Theme.surfaceHover : (row.index % 2 ? Theme.panelAlt : "transparent")
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 4

                    Text {
                        Layout.preferredWidth: 60
                        text: row.modelData.category
                        color: Theme.textDim
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.name
                        color: Theme.text
                        font.bold: row.customized
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: row.keys
                        delegate: RoleButton {
                            required property string modelData

                            objectName: "key_" + row.commandId + "_" + modelData
                            role: "small"
                            text: Theme.shortcutText(modelData) + "  ×"
                            tooltip: qsTr("Remove this key")
                            onClicked: {
                                page.message = ""
                                KeyMap.unassign(row.commandId, modelData)
                            }
                        }
                    }
                    Text {
                        visible: row.waiting
                        text: qsTr("Press a key… (Esc: cancel)")
                        color: Theme.accent
                    }
                    RoleButton {
                        objectName: "addKey_" + row.commandId
                        role: "small"
                        text: "+"
                        tooltip: qsTr("Add a key: click, then press it")
                        visible: !row.waiting
                        onClicked: page.record(row.commandId)
                    }
                    RoleButton {
                        objectName: "resetKeys_" + row.commandId
                        role: "small"
                        text: qsTr("Reset")
                        tooltip: qsTr("The profile's keys for this command")
                        visible: row.customized
                        onClicked: {
                            page.message = ""
                            KeyMap.reset(row.commandId)
                        }
                    }
                }
            }
        }
    }

    Label {
        objectName: "keyMessage"
        Layout.fillWidth: true
        text: page.message
        visible: text !== ""
        wrapMode: Text.Wrap
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Keys without Ctrl or Alt don't act in a text field, nor while the computer MIDI keyboard plays them.")
        wrapMode: Text.Wrap
        color: Theme.textDim
    }

    // Takes the key pressed while a command waits for one: before the window's
    // shortcuts and the dialog's Esc.
    Item {
        id: recorder
        objectName: "keyRecorder"
        implicitWidth: 0
        implicitHeight: 0

        onActiveFocusChanged: if (!activeFocus) page.recording = ""
        Keys.onShortcutOverride: event => event.accepted = page.recording !== ""
        Keys.onPressed: event => {
            if (page.recording === "")
                return
            event.accepted = true
            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                page.recording = ""
                return
            }
            const key = KeyMap.keyFor(event.key, event.modifiers)
            if (key !== "")
                page.assign(page.recording, key)
        }
    }
}
