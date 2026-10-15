import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SUBstation

// Preferences › Plug-ins (Session.plugins, the old PluginsPage): where VST3 and
// VST2 plug-ins are looked for (the standard folders, dimmed and always searched;
// the user's own, red if they don't exist any more) and rescanning them.
// Folders added or removed apply at once: only the new files are read, and a
// removed folder's plug-ins leave the browser. The scan's status shows the
// count and the files that could not be read (why, in its tooltip). Then where
// plug-ins run (Session.pluginHosting): within SUBstation or in sandboxes, and
// Reload Plug-ins, which moves those loaded already to where it says now.
ColumnLayout {
    id: page

    readonly property var plugins: Session.plugins
    readonly property var hosting: Session.pluginHosting
    readonly property int hostingIndex: hosting.modes.findIndex(m => m.value === hosting.mode)
    readonly property alias folderList: folders

    // Adds a folder (and scans it); the folder (added, or listed already) is current.
    function addFolder(folder) {
        if (!folder)
            return
        plugins.addFolder(folder)
        folders.currentIndex = plugins.folders.find(folder)
    }

    // Takes the current folder off the list (and scans again, so its plug-ins go).
    function removeFolder() {
        const row = folders.currentItem
        if (!row || !row.removable)
            return
        plugins.removeFolder(row.path)
        folders.currentIndex = -1
    }

    spacing: 6

    Label { text: qsTr("Plug-in Folders") }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitHeight: 160
        color: Theme.panel

        ListView {
            id: folders
            objectName: "pluginFolders"
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            currentIndex: -1
            model: page.plugins.folders
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: folder

                required property int index
                required property string path
                required property var model
                required property bool standard
                required property bool exists
                required property bool removable
                required property string toolTip

                width: ListView.view.width
                text: model.display
                highlighted: ListView.isCurrentItem
                onClicked: folders.currentIndex = index

                contentItem: Text {
                    text: folder.text
                    font: folder.font
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                    color: folder.highlighted ? Theme.accentText
                                              : folder.standard ? Theme.textDim
                                                                : (folder.exists ? Theme.text : "#ff6b5e")
                }

                ToolTip.visible: hovered && toolTip !== ""
                ToolTip.text: toolTip
                ToolTip.delay: 700
            }
        }
    }

    RowLayout {
        spacing: 6

        RoleButton {
            objectName: "addPluginFolder"
            text: qsTr("Add Folder…")
            onClicked: folderDialog.open()
        }
        RoleButton {
            objectName: "removePluginFolder"
            text: qsTr("Remove")
            enabled: folders.currentItem !== null && folders.currentItem.removable
            onClicked: page.removeFolder()
        }
        Item {
            Layout.fillWidth: true
        }
    }

    Item {
        implicitHeight: 2
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        RoleButton {
            objectName: "rescanPlugins"
            text: qsTr("Rescan Plug-ins")
            tooltip: qsTr("Read every plug-in file again, also those that could not be read before.")
            enabled: !page.plugins.scanning
            onClicked: page.plugins.rescan()
        }
        Label {
            id: scanStatus
            objectName: "scanStatus"
            Layout.fillWidth: true
            Layout.maximumWidth: 520
            text: page.plugins.statusText
            wrapMode: Text.Wrap
            color: Theme.textDim

            HoverHandler {
                id: statusHover
            }
            ToolTip.visible: statusHover.hovered && !page.plugins.scanning && page.plugins.failuresText !== ""
            ToolTip.text: page.plugins.failuresText
            ToolTip.delay: 700
        }
    }

    Item {
        implicitHeight: 6
    }

    Label { text: qsTr("Plug-in Hosting") }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        ChoiceBox {
            objectName: "pluginHosting"
            Layout.fillWidth: true
            tooltip: qsTr("Where plug-ins run: in SUBstation, or in sandboxes of their own")
            choices: page.hosting.modes
            chosenIndex: page.hostingIndex
            onChosen: index => page.hosting.mode = page.hosting.modes[index].value
        }
        RoleButton {
            objectName: "reloadPlugins"
            text: qsTr("Reload Plug-ins")
            tooltip: qsTr("Load every plug-in again, where Plug-in Hosting says now.")
            onClicked: page.hosting.reloadPlugins()
        }
    }

    Label {
        objectName: "pluginHostingDescription"
        Layout.fillWidth: true
        Layout.maximumWidth: 520
        wrapMode: Text.Wrap
        color: Theme.textDim
        text: (page.hostingIndex >= 0 ? page.hosting.modes[page.hostingIndex].toolTip + " " : "")
              + qsTr("Plug-ins loaded already stay where they are until reloaded.")
    }

    Label {
        objectName: "pluginHostingStatus"
        Layout.fillWidth: true
        text: page.hosting.status
        color: Theme.textDim
    }

    FolderDialog {
        id: folderDialog
        objectName: "pluginFolderDialog"
        title: qsTr("Add Plug-in Folder")
        currentFolder: FileUrls.homeUrl()
        onAccepted: page.addFolder(FileUrls.localPath(selectedFolder))
    }
}
