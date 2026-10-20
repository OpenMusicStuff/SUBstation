import QtQuick
import QtQuick.Controls
import SUBstation

// Progression ideas, beside the piano roll (Generate ▾ › Progressions…): chord
// progressions in a mood (Sad, Dark, Nostalgic), 4 or 8 bars, to try in the
// clip (ProgressionIdeas has the rules). Each idea is a row of chords, each
// with its Roman numeral in the key: click a row (or ▶) to write it into the
// clip, ↑ and ↓ to try the one above or below. Click a chord to lock it (New
// Ideas keeps it), right-click it for chords to swap it for. ★ keeps an idea
// as it is; ≈ puts ideas a chord or two away from it in place of the others.
// 7ths plays the triads with their sevenths, Bass adds a bass line.
//
//   ProgressionPanel {
//       ideas: progressionIdeas      // a ProgressionIdeas on the clip view's piano roll
//       onCloseRequested: ...         // × or Esc
//   }
//
// Whoever shows it calls open() as it does: ideas are made if there are none
// yet, and it takes the keys.
FocusScope {
    id: panel

    property ProgressionIdeas ideas

    signal closeRequested()

    function open() {
        if (ideas.count === 0)
            ideas.newIdeas()
        panel.forceActiveFocus()
    }

    implicitWidth: 304

    Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Up || event.key === Qt.Key_Down
        || event.key === Qt.Key_Return || event.key === Qt.Key_Enter
    Keys.onUpPressed: ideas.step(-1)
    Keys.onDownPressed: ideas.step(1)
    Keys.onReturnPressed: ideas.tryIdea(Math.max(0, ideas.current))
    Keys.onEnterPressed: ideas.tryIdea(Math.max(0, ideas.current))
    Keys.onEscapePressed: panel.closeRequested()

    Rectangle {
        anchors.fill: parent
        color: Theme.panelAlt
    }
    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }
    // A click anywhere on it gives it the keys (the buttons never take them).
    TapHandler {
        onTapped: panel.forceActiveFocus()
    }

    // --- The settings ---

    Column {
        id: settings
        x: 10
        y: 6
        width: parent.width - 18
        spacing: 6

        Item {
            width: parent.width
            height: 22

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Progressions")
                font: Theme.uiFont(10, true)
            }
            RoleButton {
                objectName: "closeProgressions"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22
                role: "flat"
                text: "×"
                font.pointSize: 13
                tooltip: qsTr("Close (Esc)")
                onClicked: panel.closeRequested()
            }
        }

        Row {
            spacing: 6

            ComboBox {
                objectName: "progressionMood"
                width: 100
                height: 24
                focusPolicy: Qt.NoFocus
                model: panel.ideas.moods
                currentIndex: panel.ideas.mood
                onActivated: index => panel.ideas.mood = index
                ToolTip.visible: hovered && !popup.visible
                ToolTip.text: qsTr("The mood: new ideas start from its own progressions")
                ToolTip.delay: 700
            }
            ComboBox {
                objectName: "progressionLength"
                width: 80
                height: 24
                focusPolicy: Qt.NoFocus
                model: panel.ideas.lengths.map(n => qsTr("%1 bars").arg(n))
                currentIndex: panel.ideas.lengths.indexOf(panel.ideas.length)
                onActivated: index => panel.ideas.length = panel.ideas.lengths[index]
                ToolTip.visible: hovered && !popup.visible
                ToolTip.text: qsTr("How many chords: a bar each")
                ToolTip.delay: 700
            }
            ToggleButton {
                objectName: "progressionSevenths"
                height: 24
                role: "activator"
                text: qsTr("7ths")
                tooltip: qsTr("Play the triads with their sevenths, as the key has them")
                checked: panel.ideas.sevenths
                onToggled: panel.ideas.sevenths = checked
            }
            ToggleButton {
                objectName: "progressionBass"
                height: 24
                role: "activator"
                text: qsTr("Bass")
                tooltip: qsTr("Write a bass line under the chords too")
                checked: panel.ideas.bass
                onToggled: panel.ideas.bass = checked
            }
        }

        Item {
            width: parent.width
            height: 24

            RoleButton {
                id: newIdeas
                objectName: "newIdeas"
                height: 24
                role: "small"
                text: qsTr("New Ideas")
                tooltip: qsTr("New ideas in place of those not kept (★); locked chords stay where they are")
                onClicked: panel.ideas.newIdeas()
            }
            Label {
                objectName: "progressionKey"
                anchors.left: newIdeas.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                color: Theme.textDim
                text: panel.ideas.keyName === "" ? ""
                    : panel.ideas.keySource === "assumed" ? qsTr("In %1 (no key yet)").arg(panel.ideas.keyName)
                    : panel.ideas.keySource === "inferred" ? qsTr("In %1 (inferred)").arg(panel.ideas.keyName)
                    : qsTr("In %1").arg(panel.ideas.keyName)

                HoverHandler {
                    id: keyHover
                }
                ToolTip.visible: keyHover.hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("The key new ideas are in: the song's (set it in the transport bar, after the swing),\nelse the one its MIDI is most likely in, else A minor")
            }
        }
    }

    // --- The ideas ---

    Flickable {
        id: list
        objectName: "progressionList"
        y: settings.y + settings.height + 8
        width: parent.width
        height: parent.height - y
        contentWidth: width
        contentHeight: rows.height + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: rows
            x: 8
            width: list.width - 16
            spacing: 4

            Repeater {
                model: panel.ideas

                delegate: Rectangle {
                    id: ideaRow

                    required property int index
                    required property var chords
                    required property bool kept
                    required property bool tried
                    required property string source

                    objectName: "idea" + index
                    width: rows.width
                    height: Math.max(chordFlow.height, buttons.height) + 8
                    radius: 3
                    color: tried ? Qt.alpha(Theme.accent, 0.25)
                        : index === panel.ideas.current ? Theme.surfaceHover : Theme.surface
                    border.color: tried ? Theme.accent : "transparent"

                    // The row tries the idea; its chords and buttons do their own.
                    MouseArea {
                        objectName: "ideaArea"
                        anchors.fill: parent
                        onClicked: {
                            panel.forceActiveFocus()
                            panel.ideas.tryIdea(ideaRow.index)
                        }
                    }

                    Flow {
                        id: chordFlow
                        x: 4
                        y: 4
                        width: parent.width - buttons.width - 14
                        spacing: 3

                        Repeater {
                            model: ideaRow.chords

                            delegate: Rectangle {
                                id: chip

                                required property var modelData
                                required property int index

                                objectName: "chord" + index
                                width: Math.floor((chordFlow.width - 3 * chordFlow.spacing) / 4)
                                height: 34
                                radius: 3
                                color: chipArea.containsMouse ? Theme.surfaceHover : Theme.panel
                                border.color: modelData.locked ? Theme.accent : Theme.border

                                Column {
                                    anchors.centerIn: parent
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: chip.modelData.name
                                        font: Theme.uiFont(9, true)
                                    }
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: chip.modelData.numeral
                                        font: Theme.uiFont(8)
                                        color: Theme.textDim
                                    }
                                }
                                Icon {
                                    x: parent.width - width - 2
                                    y: 2
                                    size: 9
                                    name: "lock_envelopes"
                                    checked: true
                                    color: Theme.accent
                                    visible: chip.modelData.locked
                                }

                                MouseArea {
                                    id: chipArea
                                    objectName: "chordArea"
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: mouse => {
                                        panel.forceActiveFocus()
                                        if (mouse.button === Qt.RightButton)
                                            swapPopup.openFor(ideaRow.index, chip.index, chip)
                                        else
                                            panel.ideas.toggleLock(ideaRow.index, chip.index)
                                    }
                                }
                                ToolTip.visible: chipArea.containsMouse
                                ToolTip.delay: 700
                                ToolTip.text: chip.modelData.locked
                                    ? qsTr("Locked: New Ideas keeps it here. Click to unlock;\nright-click to swap it for another chord")
                                    : qsTr("Click to lock it (New Ideas keeps it here);\nright-click to swap it for another chord")
                            }
                        }
                    }

                    Row {
                        id: buttons
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        IconButton {
                            objectName: "tryIdea"
                            width: 22
                            height: 22
                            iconName: "play"
                            checked: ideaRow.tried
                            tooltip: ideaRow.tried
                                ? qsTr("In the clip now\n%1").arg(ideaRow.source)
                                : qsTr("Try it in the clip (↑ ↓ step through them)\n%1").arg(ideaRow.source)
                            onClicked: {
                                panel.forceActiveFocus()
                                panel.ideas.tryIdea(ideaRow.index)
                            }
                        }
                        IconButton {
                            objectName: "keepIdea"
                            width: 22
                            height: 22
                            iconName: "star"
                            checked: ideaRow.kept  // (not checkable: the idea says)
                            tooltip: ideaRow.kept ? qsTr("Kept: New Ideas leaves it as it is. Click to let it go")
                                                            : qsTr("Keep it: New Ideas leaves it as it is")
                            onClicked: panel.ideas.toggleKept(ideaRow.index)
                        }
                        RoleButton {
                            objectName: "moreLikeIdea"
                            width: 22
                            height: 22
                            role: "tool"
                            text: "≈"
                            font.pointSize: 11
                            tooltip: qsTr("More like this: ideas a chord or two away from it\nin place of the others (but those kept)")
                            onClicked: {
                                panel.forceActiveFocus()
                                panel.ideas.moreLike(ideaRow.index)
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Swapping a chord: its colours, then the chords that lead best from the one before into the one after ---

    Popup {
        id: swapPopup
        objectName: "swapPopup"

        property int row: -1
        property int chord: -1
        property var choices: []
        readonly property var colours: choices.filter(c => c.colour)
        readonly property var others: choices.filter(c => !c.colour)

        function openFor(row, chord, item) {
            swapPopup.row = row
            swapPopup.chord = chord
            choices = panel.ideas.alternatives(row, chord)
            const at = item.mapToItem(panel, 0, item.height + 2)
            x = Math.max(4, Math.min(at.x, panel.width - width - 4))
            y = Math.min(at.y, panel.height - implicitHeight - 4)
            open()
        }
        function choose(index) {
            panel.ideas.swap(row, chord, index)
            close()
        }

        width: 176
        padding: 4
        background: Rectangle {
            color: Theme.panelAlt
            border.color: Theme.border
            radius: 4
        }

        contentItem: Column {
            spacing: 1

            Label {
                leftPadding: 6
                text: qsTr("Colours")
                font: Theme.uiFont(8)
                color: Theme.textDim
                visible: swapPopup.colours.length > 0
            }
            Repeater {
                model: swapPopup.colours
                delegate: SwapChoice {
                    required property var modelData
                    required property int index
                    objectName: "colour" + index
                    choice: modelData
                    onClicked: swapPopup.choose(index)
                }
            }
            Label {
                leftPadding: 6
                topPadding: 4
                text: qsTr("Instead")
                font: Theme.uiFont(8)
                color: Theme.textDim
                visible: swapPopup.others.length > 0
            }
            Repeater {
                model: swapPopup.others
                delegate: SwapChoice {
                    required property var modelData
                    required property int index
                    objectName: "instead" + index
                    choice: modelData
                    onClicked: swapPopup.choose(swapPopup.colours.length + index)
                }
            }
        }
    }

    component SwapChoice: ItemDelegate {
        property var choice

        width: swapPopup.width - 2 * swapPopup.padding
        height: 22
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        contentItem: Item {
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: choice.name
                font: Theme.uiFont(9, true)
            }
            Label {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: choice.numeral
                color: Theme.textDim
            }
        }
    }
}
