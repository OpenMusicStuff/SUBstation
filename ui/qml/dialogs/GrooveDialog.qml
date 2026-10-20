import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// Edit › Grooves…: the project's grooves (Session.grooves), and the groove
// editor. On the left the library: grooves added from the built-in ones, the
// user's groove files (the preset library's Grooves folder: .gilgroove and
// Ableton's .agr) or a swing; duplicated, renamed, removed (what plays one keeps
// its own copy, so nothing changes sound), saved as a file, or made the
// project's default. On the right the groove chosen: a lane a row (the default
// lane first, for every key no other lane takes; a lane's keys typed as MIDI
// note numbers), a bar a column, each step a handle dragged late or early (its
// velocity below, dragged up or down; double-click: back to straight), the notes
// it was made from drawn faintly under its steps. Its cycle's bars and base grid
// are chosen above. Each drag is one undo step.
//
// Not modal: the song plays on while a groove is edited, and what plays it
// follows each change.
Dialog {
    id: dialog

    property string grooveId: ""
    readonly property var current: {
        void Session.grooves.revision
        return grooveId !== "" ? Session.grooves.groove(grooveId) : ({})
    }
    readonly property bool hasGroove: current.id !== undefined
    readonly property var lanes: current.lanes || []
    readonly property int steps: current.steps || 16
    readonly property int bars: current.bars || 1
    readonly property real stepBeats: current.step || 0.25
    readonly property var baseChoices: [{ name: "1/4", beats: 1 }, { name: "1/8", beats: 0.5 },
                                        { name: "1/8T", beats: 1 / 3 }, { name: "1/16", beats: 0.25 },
                                        { name: "1/16T", beats: 1 / 6 }, { name: "1/32", beats: 0.125 }]

    function choose(id) {
        if (id)
            grooveId = id
    }

    objectName: "grooveDialog"
    title: qsTr("Grooves")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: false
    closePolicy: Popup.CloseOnEscape
    width: Math.min(parent ? parent.width - 40 : 820, 820)
    height: Math.min(parent ? parent.height - 40 : 480, 480)
    standardButtons: Dialog.Close

    onOpened: {
        Session.grooves.refreshFiles()
        if (!hasGroove && Session.grooves.grooves.length > 0)
            grooveId = Session.grooves.grooves[0].id
    }
    // Its window takes the keys again afterwards (its shortcuts work).
    onClosed: if (Window.window) Window.window.requestActivate()

    Connections {
        target: Session.grooves
        // (The groove shown removed: the first left.)
        function onGroovesChanged() {
            if (dialog.grooveId !== "" && Session.grooves.find(dialog.grooveId) === "")
                dialog.grooveId = Session.grooves.grooves.length > 0 ? Session.grooves.grooves[0].id : ""
        }
    }

    contentItem: RowLayout {
        spacing: 10

        // --- The library ---

        ColumnLayout {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            spacing: 4

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.lane
                border.color: Theme.border

                ListView {
                    id: list
                    objectName: "grooveList"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: Session.grooves.grooves
                    delegate: ItemDelegate {
                        required property var modelData
                        objectName: "grooveItem_" + modelData.id
                        width: ListView.view.width
                        height: 22
                        text: modelData.name + (modelData.id === Session.grooves.defaultGrooveId ? qsTr("  (default)") : "")
                        highlighted: modelData.id === dialog.grooveId
                        onClicked: dialog.grooveId = modelData.id
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        visible: list.count === 0
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No grooves yet: add one, or extract one from a clip (the clip view's Groove › Extract)")
                        color: Theme.textDisabled
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                RoleButton {
                    id: addButton
                    objectName: "addGroove"
                    Layout.fillWidth: true
                    text: qsTr("Add ▾")
                    tooltip: qsTr("A built-in groove, one of your groove files, or a swing")
                    onClicked: addMenu.popup(addButton, 0, addButton.height)

                    Menu {
                        id: addMenu
                        objectName: "addMenu"

                        Menu {
                            id: builtinMenu
                            title: qsTr("Built-in")
                            Instantiator {
                                model: Session.grooves.builtins
                                delegate: MenuItem {
                                    required property var modelData
                                    objectName: "add_" + modelData.id
                                    text: modelData.name
                                    onTriggered: dialog.choose(Session.grooves.addBuiltin(modelData.id))
                                }
                                onObjectAdded: (index, object) => builtinMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => builtinMenu.removeItem(object)
                            }
                        }
                        Menu {
                            id: filesMenu
                            title: qsTr("From Groove Files")
                            Instantiator {
                                model: Session.grooves.files
                                delegate: MenuItem {
                                    required property var modelData
                                    text: modelData.name
                                    onTriggered: dialog.choose(Session.grooves.addFile(modelData.path))
                                }
                                onObjectAdded: (index, object) => filesMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => filesMenu.removeItem(object)
                            }
                            MenuItem {
                                enabled: false
                                visible: Session.grooves.files.length === 0
                                height: visible ? implicitHeight : 0
                                text: qsTr("(none in the library's Grooves folder)")
                            }
                        }
                        Menu {
                            title: qsTr("Swing")
                            Repeater {
                                model: [54, 58, 62, 66.7, 71]
                                MenuItem {
                                    required property var modelData
                                    text: qsTr("Swing %1 % (1/16)").arg(modelData)
                                    onTriggered: dialog.choose(Session.grooves.addSwing(modelData === 66.7 ? 200 / 3 : modelData, 2))
                                }
                            }
                        }
                    }
                }
                RoleButton {
                    objectName: "duplicateGroove"
                    Layout.fillWidth: true
                    enabled: dialog.hasGroove
                    text: qsTr("Duplicate")
                    tooltip: qsTr("A copy of the groove, to change apart from it")
                    onClicked: dialog.choose(Session.grooves.duplicate(dialog.grooveId))
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                RoleButton {
                    objectName: "removeGroove"
                    Layout.fillWidth: true
                    enabled: dialog.hasGroove
                    text: qsTr("Remove")
                    tooltip: qsTr("Remove it from the project: what plays it keeps its own copy, so nothing changes sound")
                    onClicked: Session.grooves.remove(dialog.grooveId)
                }
                RoleButton {
                    objectName: "saveGrooveFile"
                    Layout.fillWidth: true
                    enabled: dialog.hasGroove
                    text: qsTr("Save File")
                    tooltip: qsTr("Save it among your groove files (the preset library's Grooves folder),\nfor other projects")
                    onClicked: Session.grooves.saveFile(dialog.grooveId)
                }
            }
            RoleButton {
                objectName: "makeDefault"
                Layout.fillWidth: true
                enabled: dialog.hasGroove
                text: dialog.hasGroove && Session.grooves.defaultGrooveId === dialog.grooveId
                      ? qsTr("The Project's Default") : qsTr("Make the Project's Default")
                tooltip: qsTr("The project's default groove: what Groove devices and clips set to Project play,\nand what devices following the groove follow")
                onClicked: Session.grooves.setDefaultGroove(dialog.grooveId)
            }
        }

        // --- The groove editor ---

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            enabled: dialog.hasGroove

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                TextField {
                    id: nameField
                    objectName: "grooveName"
                    Layout.fillWidth: true
                    text: dialog.current.name || ""
                    selectByMouse: true
                    onEditingFinished: if (text !== "" && text !== dialog.current.name)
                                           Session.grooves.rename(dialog.grooveId, text)
                }
                Label {
                    text: qsTr("Bars")
                    color: Theme.textDim
                }
                ComboBox {
                    objectName: "grooveBars"
                    Layout.preferredWidth: 56
                    focusPolicy: Qt.NoFocus
                    model: [1, 2, 4]
                    currentIndex: Math.max(0, [1, 2, 4].indexOf(dialog.bars))
                    onActivated: index => Session.grooves.setBars(dialog.grooveId, [1, 2, 4][index])
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 700
                    ToolTip.text: qsTr("The groove's cycle: its steps repeat every so many bars")
                }
                Label {
                    text: qsTr("Base")
                    color: Theme.textDim
                }
                ComboBox {
                    objectName: "grooveBase"
                    Layout.preferredWidth: 68
                    focusPolicy: Qt.NoFocus
                    model: dialog.baseChoices.map(c => c.name)
                    currentIndex: {
                        const base = dialog.current.base || 0.25
                        for (let i = 0; i < dialog.baseChoices.length; ++i)
                            if (Math.abs(dialog.baseChoices[i].beats - base) < 1e-6)
                                return i
                        return 3
                    }
                    onActivated: index => Session.grooves.setBase(dialog.grooveId, dialog.baseChoices[index].beats)
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 700
                    ToolTip.text: qsTr("Base: the grid a note is found on (its nearest point takes the step's feel;\nbetween steps, the two are blended)")
                }
                Label {
                    // (A step none of the choices is, as an .agr's Base of 1/4 makes: as a note value.)
                    text: {
                        const index = dialog.current.stepIndex !== undefined ? dialog.current.stepIndex : -1
                        const step = dialog.stepBeats
                        return qsTr("Steps of %1").arg(index >= 0 ? Session.grooves.stepNames[index]
                                                                  : "1/" + Math.round(4 / step))
                    }
                    color: Theme.textDim
                }
            }

            ScrollView {
                id: lanesView
                Layout.fillWidth: true
                Layout.fillHeight: true
                // (Its size the layout's, not its content's: the content takes its width.)
                Layout.preferredWidth: 400
                Layout.preferredHeight: 200
                clip: true
                contentWidth: availableWidth

                Column {
                    width: lanesView.availableWidth
                    spacing: 8

                    Repeater {
                        // (By count: a drag's edits redraw the lanes, not make them again.)
                        model: dialog.lanes.length
                        GrooveLaneEditor {
                            required property int index
                            width: parent.width
                            grooveId: dialog.grooveId
                            lane: index
                            laneData: dialog.lanes[index] || ({ keys: [], steps: [] })
                            stepCount: dialog.steps
                            bars: dialog.bars
                            notes: dialog.current.notes || []
                            otherKeys: {
                                const keys = []
                                for (let i = 1; i < dialog.lanes.length; ++i)
                                    if (i !== index)
                                        keys.push(...dialog.lanes[i].keys)
                                return keys
                            }
                        }
                    }
                    RoleButton {
                        objectName: "addLane"
                        text: qsTr("+ Lane")
                        tooltip: qsTr("A lane of its own for some keys (the hats, the snare), starting as the default lane")
                        // (For the first of the hats, the snare, the kick no lane has yet.)
                    onClicked: {
                        const taken = []
                        for (const lane of dialog.lanes)
                            taken.push(...lane.keys)
                        const key = [42, 38, 36, 46, 44, 39, 37, 40, 35].find(k => taken.indexOf(k) < 0)
                        if (key !== undefined)
                            Session.grooves.addLane(dialog.grooveId, [key])
                    }
                    }
                }
            }
        }
    }
}
