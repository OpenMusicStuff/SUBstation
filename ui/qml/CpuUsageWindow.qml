import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// View › CPU Usage (Ctrl+Alt+U), in a window of its own: Session.cpuUsage,
// what each track and device costs the audio thread, in % of one CPU core,
// updated twice a second. The tracks are listed as in the arrangement (or the
// costliest first), each followed by its devices; a bar shows each one's
// share, its number the average lately and its peak. Clicking a row selects
// its track. The engine only measures while this window is open.
Window {
    id: window

    readonly property var usage: Session.cpuUsage

    // Its bar's colour: green, then yellow from 5%, red from 15% of a core.
    function levelColor(cpu) {
        return cpu >= 15 ? Theme.meterHigh : cpu >= 5 ? Theme.meterMid : Theme.meterLow
    }
    function percent(cpu) {
        return cpu >= 10 ? cpu.toFixed(0) + "%" : cpu.toFixed(1) + "%"
    }

    objectName: "cpuUsageWindow"
    width: 560
    height: 640
    minimumWidth: 380
    minimumHeight: 240
    title: qsTr("CPU Usage")
    color: Theme.panel

    onVisibleChanged: usage.active = visible

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                objectName: "cpuAudioLoad"
                text: qsTr("Audio thread: %1 of its time").arg(window.percent(Session.bridge.cpuLoad * 100))
                color: Theme.text
                font: Theme.listFont
            }
            Label {
                objectName: "cpuTotal"
                text: qsTr("Tracks: %1 of a core").arg(window.percent(window.usage.total))
                color: Theme.textDim
                font: Theme.listFont
            }
            Item {
                Layout.fillWidth: true
            }
            CheckBox {
                objectName: "cpuSortByCpu"
                text: qsTr("Costliest first")
                checked: window.usage.sortByCpu
                onToggled: {
                    window.usage.sortByCpu = checked
                    checked = Qt.binding(() => window.usage.sortByCpu)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.window
            border.color: Theme.border

            ListView {
                id: list
                objectName: "cpuUsageList"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: window.usage
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: row

                    required property string trackId
                    required property string name
                    required property string color
                    required property int depth
                    required property bool isTrack
                    required property double cpu
                    required property double peak

                    width: ListView.view.width
                    height: 22

                    Rectangle {
                        anchors.fill: parent
                        color: area.containsMouse ? Theme.panelAlt : row.isTrack ? Theme.panel : "transparent"
                    }
                    Rectangle {
                        id: swatch
                        x: 6 + row.depth * 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: row.isTrack ? 8 : 0
                        height: 12
                        radius: 2
                        visible: row.isTrack
                        color: row.color !== "" ? row.color : Theme.textDim
                    }
                    Text {
                        anchors.left: swatch.right
                        anchors.leftMargin: row.isTrack ? 6 : 0
                        anchors.right: bar.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.name
                        elide: Text.ElideRight
                        font.family: Theme.listFont.family
                        font.pointSize: Theme.listFont.pointSize
                        font.bold: row.isTrack
                        color: row.isTrack ? Theme.text : Theme.textDim
                    }
                    // The share, against a quarter of a core (more fills it).
                    Rectangle {
                        id: bar
                        anchors.right: numbers.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        height: 8
                        color: Theme.meterBg
                        Rectangle {
                            height: parent.height
                            width: parent.width * Math.min(1, row.cpu / 25)
                            color: window.levelColor(row.cpu)
                        }
                        Rectangle {  // the peak
                            x: Math.min(parent.width - 2, parent.width * Math.min(1, row.peak / 25))
                            width: 2
                            height: parent.height
                            visible: row.peak > row.cpu + 0.05
                            color: window.levelColor(row.peak)
                        }
                    }
                    Text {
                        id: numbers
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 104
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("%1  (peak %2)").arg(window.percent(row.cpu)).arg(window.percent(row.peak))
                        font: Theme.smallFont
                        color: row.cpu >= 15 ? Theme.meterHigh : Theme.text
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: Session.selection.selectTrack(row.trackId)
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font: Theme.smallFont
            color: Theme.textDim
            text: qsTr("% of one CPU core. A track counts its clips and devices; a group only what it adds. A plug-in in a sandbox counts the time the audio thread waits for it. A device resting in silence costs nothing. Measured only while this window is open.")
        }
    }
}
