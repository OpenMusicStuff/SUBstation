import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// Agent Changes, on the right of the window (View › Agent Changes,
// Ctrl+Alt+A): Session.agentChanges, what agents changed through the MCP
// server, apart from Edit › Undo. Each row is one change (one tool call), in
// the order made, the latest in view: what it did and when, dimmed and struck through once
// rewound. Its button rewinds it (or restores it); Rewind All rewinds every
// change in effect, the latest first. A change the user changed the same
// since can't be rewound: the status line says why (as an undo refused).
Rectangle {
    id: panel

    readonly property var changes: Session.agentChanges

    // Its ✕: the window hides it (View › Agent Changes).
    signal closeRequested()

    // Rewinds (or restores) a change, saying why not in the status line.
    function toggle(changeId, applied) {
        const why = applied ? changes.rewind(changeId) : changes.restore(changeId)
        if (why !== "")
            Session.statusMessage(why)
    }

    objectName: "agentChanges"
    color: Theme.window
    implicitWidth: 300

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 6
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 4
            spacing: 6

            Label {
                text: qsTr("AGENT CHANGES")
                font: Theme.listHeadingFont
                color: Theme.textDim
            }
            Item {
                Layout.fillWidth: true
            }
            RoleButton {
                objectName: "rewindAllAgentChanges"
                role: "small"
                text: qsTr("Rewind All")
                enabled: panel.changes.appliedCount > 0
                tooltip: qsTr("Rewind All\nTakes back every change agents made that is still in effect, the latest first. Your own edits stay as they are.")
                onClicked: {
                    const why = panel.changes.rewindAll()
                    if (why !== "")
                        Session.statusMessage(why)
                }
            }
            RoleButton {
                objectName: "closeAgentChanges"
                role: "flat"
                text: "✕"
                tooltip: qsTr("Hide Agent Changes (Ctrl+Alt+A)")
                onClicked: panel.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.panel

            Label {
                objectName: "noAgentChanges"
                anchors.fill: parent
                anchors.margins: 12
                visible: list.count === 0
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.textDim
                text: qsTr("What agents change through the MCP server (Preferences › Agents) shows here, each change apart from Edit › Undo, to rewind on its own.")
            }

            ListView {
                id: list
                objectName: "agentChangeList"
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: panel.changes
                ScrollBar.vertical: ScrollBar {}

                // (The latest in view as it comes.)
                onCountChanged: positionViewAtEnd()

                delegate: Item {
                    id: row

                    required property int changeId
                    required property string text
                    required property string time
                    required property bool applied

                    width: ListView.view.width
                    implicitHeight: changeText.implicitHeight + timeText.implicitHeight + 10

                    Rectangle {
                        anchors.fill: parent
                        color: area.containsMouse ? Theme.panelAlt : "transparent"
                    }
                    Text {
                        id: changeText
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: button.left
                        anchors.rightMargin: 6
                        y: 5
                        text: row.text
                        font.family: Theme.listFont.family
                        font.pointSize: Theme.listFont.pointSize
                        font.strikeout: !row.applied
                        color: row.applied ? Theme.text : Theme.textDim
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    Text {
                        id: timeText
                        anchors.left: changeText.left
                        anchors.right: changeText.right
                        anchors.top: changeText.bottom
                        text: row.applied ? row.time : qsTr("%1 · rewound").arg(row.time)
                        font: Theme.smallFont
                        color: Theme.textDim
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                    RoleButton {
                        id: button
                        objectName: "agentChangeButton"
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        role: "small"
                        text: row.applied ? qsTr("Rewind") : qsTr("Restore")
                        tooltip: row.applied ? qsTr("Rewind\nTakes this change back, leaving everything else as it is.")
                                             : qsTr("Restore\nPuts this change back.")
                        onClicked: panel.toggle(row.changeId, row.applied)
                    }
                }
            }
        }
    }
}
