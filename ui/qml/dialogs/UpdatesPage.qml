import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// Preferences › Updates (Session.updates): the update channel (Stable, Nightly
// or Canary, each with what it offers), Automatically check for updates,
// Install updates automatically after closing the app, and what that adds up
// to ("Selected: Stable", Updater.summary). Then this build's version, what
// the last look found (Updater.status), Check Now, and for an update found:
// Download and Install, its progress, Restart to Install (closing SUBstation as
// Quit does, unsaved changes asked about), and its release page. Changes apply
// at once and are kept.
ColumnLayout {
    id: page

    readonly property var updates: Session.updates

    spacing: 8

    Label {
        text: qsTr("Update channel")
        font.bold: true
    }

    ButtonGroup {
        id: channelGroup
    }

    Repeater {
        model: page.updates.channels

        RowLayout {
            id: channelRow

            required property var modelData

            Layout.fillWidth: true
            spacing: 6

            RadioButton {
                objectName: "updateChannel_" + channelRow.modelData.value
                text: channelRow.modelData.label
                font.bold: true
                ButtonGroup.group: channelGroup
                checked: page.updates.channel === channelRow.modelData.value
                onToggled: {
                    if (checked)
                        page.updates.channel = channelRow.modelData.value
                    checked = Qt.binding(() => page.updates.channel === channelRow.modelData.value)
                }
            }
            Label {
                Layout.fillWidth: true
                text: channelRow.modelData.description
                color: Theme.textDim
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.bottomMargin: 4
        implicitHeight: 1
        color: Theme.border
    }

    CheckBox {
        objectName: "updatesAutoCheck"
        text: qsTr("Automatically check for updates")
        checked: page.updates.autoCheck
        onToggled: {
            page.updates.autoCheck = checked
            checked = Qt.binding(() => page.updates.autoCheck)
        }
    }

    CheckBox {
        objectName: "updatesAutoInstall"
        text: qsTr("Install updates automatically after closing the app")
        enabled: page.updates.canInstall
        checked: page.updates.autoInstall
        onToggled: {
            page.updates.autoInstall = checked
            checked = Qt.binding(() => page.updates.autoInstall)
        }
    }

    // What the choices add up to.
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 4
        implicitHeight: summary.implicitHeight + 2 * 8
        color: Theme.surface
        border.color: Theme.border
        radius: 2

        ColumnLayout {
            id: summary

            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Label {
                objectName: "updatesSummaryTitle"
                text: "ⓘ  " + page.updates.summaryTitle
                font.bold: true
            }
            Label {
                objectName: "updatesSummary"
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                wrapMode: Text.Wrap
                color: Theme.textDim
                text: page.updates.summary
            }
        }
    }

    Item {
        implicitHeight: 4
    }

    Label {
        objectName: "updatesStatus"
        Layout.fillWidth: true
        Layout.maximumWidth: 520
        wrapMode: Text.Wrap
        color: page.updates.state === "error" ? "#ff6b5e" : Theme.text
        text: page.updates.status
    }

    ProgressBar {
        objectName: "updatesProgress"
        Layout.fillWidth: true
        visible: page.updates.state === "downloading"
        value: page.updates.progress
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        RoleButton {
            objectName: "updatesCheckNow"
            text: qsTr("Check Now")
            enabled: page.updates.state !== "checking" && page.updates.state !== "downloading"
                     && page.updates.state !== "ready"
            tooltip: qsTr("Look for a newer version on the channel chosen.")
            onClicked: page.updates.checkNow()
        }
        RoleButton {
            objectName: "updatesDownload"
            text: qsTr("Download and Install")
            visible: page.updates.canInstall && page.updates.availableVersion !== ""
                     && (page.updates.state === "available" || page.updates.state === "error")
            tooltip: qsTr("Download the update; it installs when you say, or when SUBstation closes.")
            onClicked: page.updates.download()
        }
        RoleButton {
            objectName: "updatesRestart"
            text: qsTr("Restart to Install")
            visible: page.updates.state === "ready"
            tooltip: qsTr("Close SUBstation (asking about unsaved changes), install the update and start again.")
            onClicked: page.updates.installNow()
        }
        RoleButton {
            objectName: "updatesReleasePage"
            text: qsTr("Release Notes")
            visible: page.updates.releaseUrl !== ""
            tooltip: page.updates.releaseUrl
            onClicked: Qt.openUrlExternally(page.updates.releaseUrl)
        }
        Item { Layout.fillWidth: true }
    }

    Label {
        objectName: "updatesVersion"
        Layout.fillWidth: true
        color: Theme.textDim
        text: qsTr("This is SUBstation %1 (%2). %3").arg(page.updates.currentVersion)
                  .arg(page.updates.buildChannel).arg(page.updates.lastChecked)
    }

    Item { Layout.fillHeight: true }
}
