import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// Preferences › Agents (Session.mcp): the MCP server agents read the project
// through. On or off (as Options › MCP Server), its port, where it is
// (http://127.0.0.1:<port>/mcp) and the command that adds it to Claude Code,
// to copy; why it couldn't start, in red. Changes apply at once and are kept.
ColumnLayout {
    id: page

    readonly property var mcp: Session.mcp

    spacing: 10

    CheckBox {
        objectName: "mcpEnabled"
        text: qsTr("Let agents read the project (MCP server)")
        checked: page.mcp.enabled
        onToggled: {
            page.mcp.enabled = checked
            checked = Qt.binding(() => page.mcp.enabled)
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim
        text: qsTr("Agents such as Claude Code connect to it on this computer only. They can read the tracks, " +
                   "what each one is, the devices, clips, chords and key; they change nothing.")
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: 12
        rowSpacing: 6

        Label { text: qsTr("Port") }
        TextField {
            id: port
            objectName: "mcpPort"
            Layout.preferredWidth: 90
            text: page.mcp.port
            validator: IntValidator { bottom: 1; top: 65535 }
            onEditingFinished: {
                if (acceptableInput)
                    page.mcp.port = parseInt(text)
                text = Qt.binding(() => page.mcp.port)
            }
        }
        Item { Layout.fillWidth: true }

        Label { text: qsTr("Address") }
        TextField {
            objectName: "mcpUrl"
            Layout.fillWidth: true
            Layout.columnSpan: 2
            readOnly: true
            selectByMouse: true
            text: page.mcp.url
        }

        Label { text: qsTr("Claude Code") }
        TextField {
            id: command
            objectName: "mcpCommand"
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            text: page.mcp.claudeCodeCommand
        }
        Button {
            objectName: "mcpCopyCommand"
            text: qsTr("Copy")
            onClicked: {
                command.selectAll()
                command.copy()
                command.deselect()
            }
        }
    }

    Label {
        objectName: "mcpStatus"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: page.mcp.error !== "" ? "#ff6b5e" : Theme.textDim
        text: page.mcp.error !== "" ? qsTr("Couldn't start on this port: %1").arg(page.mcp.error)
              : page.mcp.running ? qsTr("Running.") : qsTr("Off.")
    }

    Item { Layout.fillHeight: true }
}
