import QtQuick
import QtQuick.Controls
import SUBstation

// The Overdrive's editor, laid out as Ableton's, compact: the display
// (OverdriveGraph: the band that goes into the drive, Tone dashed over it, the
// input's spectrum behind; drag the band for Frequency and Width) with the
// band's readout under it, then Drive and Tone, Dynamics and Dry/Wet.
//
// Its animations are subtle: the display's (see OverdriveGraph.h), and the
// Drive knob's ring warming with how hard the sound is driven.
Item {
    id: editor

    required property string trackId
    required property string deviceId
    readonly property alias graph: graph

    readonly property int cell: 52
    readonly property int gap: 4
    readonly property int sectionGap: 8

    implicitWidth: 8 + graph.implicitWidth + sectionGap + 2 * cell + gap + 8 - 2
    implicitHeight: 6 + Math.max(2 * driveKnob.implicitHeight + gap, graph.implicitHeight + 14) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["freq", "width", "drive", "tone", "dynamics", "mix"]
    }

    Row {
        x: 8
        y: 6
        height: editor.height - 12
        spacing: editor.sectionGap

        Column {
            spacing: 2
            height: parent.height

            OverdriveGraph {
                id: graph
                objectName: "overdriveGraph"
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                width: implicitWidth
                height: Math.max(implicitHeight, parent.height - bandRow.height - parent.spacing)

                HoverHandler {
                    id: graphHover
                }
                ToolTip.visible: graphHover.hovered && !graph.dragging
                ToolTip.delay: 700
                ToolTip.text: qsTr("The band that goes into the drive: drag for Frequency across and Width up "
                                   + "and down, double-click to reset; Tone is dashed over it")
            }
            Row {
                id: bandRow
                spacing: 4
                height: 12

                EditorCaption {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Freq")
                }
                ParamBox {
                    objectName: "freq"
                    width: 58
                    height: 12
                    param: p.get("freq")
                    formatter: v => v >= 1000 ? (v / 1000).toFixed(2) + " kHz" : v.toFixed(0) + " Hz"
                    sampleText: "20.00 kHz"
                    tooltip: qsTr("Frequency: the middle of the band that goes into the drive")
                }
                EditorCaption {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Width")
                }
                ParamBox {
                    objectName: "width"
                    width: 52
                    height: 12
                    param: p.get("width")
                    step: 0.1
                    decimals: 1
                    formatter: v => v.toFixed(1) + " oct"
                    sampleText: "9.0 oct"
                    tooltip: qsTr("Width: how wide the band is, in octaves")
                }
            }
        }

        Grid {
            columns: 2
            columnSpacing: editor.gap
            rowSpacing: editor.gap

            EditorKnob {
                id: driveKnob
                objectName: "drive"
                width: editor.cell
                param: p.get("drive")
                title: qsTr("Drive")
                // Warms towards the accent as the sound is driven harder (eased by the graph).
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                               0.55 + 0.45 * Math.min(1, graph.driveActivity))
                tooltip: qsTr("Drive: how hard the band is pushed into the curve")
            }
            EditorKnob {
                objectName: "tone"
                width: editor.cell
                param: p.get("tone")
                title: qsTr("Tone")
                tooltip: qsTr("Tone: darker to brighter (a low-pass from 1 kHz to 20 kHz after the drive)")
            }
            EditorKnob {
                objectName: "dynamics"
                width: editor.cell
                param: p.get("dynamics")
                title: qsTr("Dynamics")
                tooltip: qsTr("Dynamics: how much of the input's level changes come back after the drive "
                              + "squashes them (0 %: squashed flat)")
            }
            EditorKnob {
                objectName: "mix"
                width: editor.cell
                param: p.get("mix")
                title: qsTr("Dry/Wet")
            }
        }
    }
}
