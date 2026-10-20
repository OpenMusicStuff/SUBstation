import QtQuick
import QtQuick.Controls
import SUBstation

// The Delay's editor, laid out as Ableton's: each side's time (Sync on: a grid
// of sixteenths and an offset; off: a time knob) and the link between them (the
// right side greyed out while linked), the feedback with its freeze under them;
// the filter on the echoes (FilterGraph: a curve to drag across for its
// frequency, up and down for its width, over a spectrum of the input) with its
// switch, frequency and width, and under it, where Ableton has its modulation,
// the Character: Swing (every other echo late), Drive (saturation in the loop),
// Duck (the echoes dip while the input plays) and Follow Groove (the swing the
// project's groove's); on the right the mode the time changes in and ping pong,
// over the dry/wet mix. Every control shows its parameter as it is now (its
// automation's value while that plays), sets it undoably, touches it when
// pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId
    readonly property alias graph: graph

    readonly property var divisions: ["1", "2", "3", "4", "5", "6", "8", "16"]  // the sixteenths, as the engine lists them
    readonly property int sideWidth: 56
    readonly property int linkWidth: 18
    readonly property int graphWidth: 226
    readonly property int rightWidth: 64
    readonly property int spacing: 10

    // The device's body: 2 * SIDE_WIDTH + LINK_WIDTH + GRAPH_WIDTH + RIGHT_WIDTH + 2 * SPACING + 16, less the
    // frame's border.
    implicitWidth: 2 * sideWidth + linkWidth + graphWidth + rightWidth + 2 * spacing + 16 - 2
    implicitHeight: 6 + Math.max(sides.implicitHeight + 4 + feedbackRow.implicitHeight, rightColumn.implicitHeight,
                                 140) + 6

    // The parameters, by id.
    readonly property var params: {
        const all = {}
        for (let i = 0; i < paramObjects.count; ++i) {
            const p = paramObjects.objectAt(i)
            if (p)
                all[p.paramId] = p
        }
        return all
    }
    Instantiator {
        id: paramObjects
        model: ["l_sync", "l_division", "l_time", "l_offset", "r_sync", "r_division", "r_time", "r_offset", "link",
                "feedback", "freeze", "filter", "freq", "width", "mode", "ping_pong", "mix", "swing", "drive", "duck",
                "follow_groove"]
        DeviceParam {
            required property string modelData
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            paramId: modelData
        }
    }

    // A Character control: its name over a box to drag or type in.
    component CharacterBox: Column {
        id: cell

        property string paramId: ""
        property string title: ""
        property string tooltip: ""

        spacing: 1

        EditorCaption {
            width: parent.width
            text: cell.title
        }
        ParamBox {
            objectName: cell.paramId
            width: parent.width
            param: editor.params[cell.paramId] || null
            step: 0.5
            decimals: 1
            defaultValue: 0.0
            formatter: v => v.toFixed(1) + " %"
            sampleText: "100.0 %"
            tooltip: cell.tooltip
        }
    }

    // One side's time: Sync, then the sixteenths and the offset (or, unsynced, the time).
    component Side: Column {
        id: side

        property string prefix: "l"
        property string title: ""
        readonly property bool synced: editor.params[prefix + "_sync"] ? editor.params[prefix + "_sync"].value >= 0.5
                                                                         : true

        width: editor.sideWidth
        spacing: 2

        EditorCaption {
            width: parent.width
            text: side.title
        }
        ParamButton {
            objectName: side.prefix + "Sync"
            width: parent.width
            param: editor.params[side.prefix + "_sync"] || null
            text: qsTr("Sync")
            tooltip: qsTr("Sync the time to the tempo, in sixteenths")
        }
        Item {
            width: parent.width
            height: Math.max(synced.implicitHeight, free.implicitHeight)
            implicitHeight: height

            Grid {
                id: synced
                objectName: side.prefix + "Synced"
                visible: side.synced
                width: parent.width
                columns: 2
                spacing: 2

                Repeater {
                    model: editor.divisions
                    ParamButton {
                        required property string modelData
                        required property int index
                        objectName: side.prefix + "Division" + modelData
                        width: (synced.width - 2) / 2
                        param: editor.params[side.prefix + "_division"] || null
                        choice: index
                        text: modelData
                        tooltip: qsTr("%1 sixteenth%2").arg(modelData).arg(modelData !== "1" ? "s" : "")
                    }
                }
            }
            ParamBox {
                objectName: side.prefix + "Offset"
                visible: side.synced
                y: synced.height + 2
                width: parent.width
                param: editor.params[side.prefix + "_offset"] || null
                step: 0.2
                decimals: 1
                defaultValue: 0.0
                formatter: v => v.toFixed(1) + " %"
                sampleText: "-33.0 %"
                tooltip: qsTr("Offset: lengthens or shortens the synced time (swing)")
            }
            Column {
                id: free
                visible: !side.synced
                width: parent.width
                topPadding: 4
                spacing: 1

                ParamKnob {
                    id: timeKnob
                    objectName: side.prefix + "Time"
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 34
                    param: editor.params[side.prefix + "_time"] || null
                }
                EditorReadout {
                    width: parent.width
                    text: timeKnob.param ? timeKnob.param.text : ""
                }
            }
        }
    }

    Row {
        id: content
        x: 8
        y: 6
        height: editor.height - 12
        spacing: editor.spacing

        // The sides' times, and under them the feedback with its freeze.
        Item {
            width: sides.width
            height: parent.height

            Row {
                id: sides

                Side {
                    id: leftSide
                    prefix: "l"
                    title: qsTr("Left")
                }
                Item {
                    width: editor.linkWidth
                    height: leftSide.height

                    ParamButton {
                        objectName: "link"
                        // Level with the sixteenths, as Ableton's is.
                        y: leftSide.height / 2 - height / 2
                        width: parent.width
                        param: editor.params["link"] || null
                        iconName: "link"
                        tooltip: qsTr("Link: the right side follows the left")
                    }
                }
                Side {
                    id: rightSide
                    objectName: "rightSide"
                    prefix: "r"
                    title: qsTr("Right")
                    enabled: !(editor.params["link"] && editor.params["link"].value >= 0.5)
                }
            }

            Row {
                id: feedbackRow
                anchors.bottom: parent.bottom
                width: sides.width
                spacing: 4

                ParamKnob {
                    id: feedback
                    objectName: "feedback"
                    anchors.verticalCenter: parent.verticalCenter
                    size: 26
                    param: editor.params["feedback"] || null
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: sides.width - feedback.width - freeze.width - 2 * feedbackRow.spacing

                    EditorCaption {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Feedback")
                    }
                    EditorReadout {
                        width: parent.width
                        horizontalAlignment: Text.AlignLeft
                        text: feedback.param ? feedback.param.text : ""
                    }
                }
                ParamButton {
                    id: freeze
                    objectName: "freeze"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    param: editor.params["freeze"] || null
                    iconName: "infinity"
                    tooltip: qsTr("Freeze: what is in the delay goes round for ever, new input is ignored")
                }
            }
        }

        // The filter on the echoes, and the Character under it.
        Column {
            width: editor.graphWidth
            height: parent.height
            spacing: 3

            FilterGraph {
                id: graph
                objectName: "filterGraph"
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                width: parent.width
                height: Math.max(implicitHeight, parent.height - filterRow.height - character.height - 2 * 3)

                HoverHandler {
                    id: graphHover
                }
                ToolTip.visible: graphHover.hovered && !graphHover.point.pressedButtons
                ToolTip.delay: 700
                ToolTip.text: qsTr("Drag across for the filter's frequency, up and down for its width")
            }
            Row {
                id: filterRow
                width: parent.width
                spacing: 3

                readonly property real boxWidth: (width - filterButton.width - widthLabel.width - 3 * spacing) / 2

                ParamButton {
                    id: filterButton
                    objectName: "filterOn"
                    width: 40
                    anchors.verticalCenter: parent.verticalCenter
                    param: editor.params["filter"] || null
                    text: qsTr("Filter")
                    tooltip: qsTr("The band-pass filter on the echoes")
                }
                ParamBox {
                    objectName: "freq"
                    width: filterRow.boxWidth
                    param: editor.params["freq"] || null
                    logScale: true
                    decimals: 0
                    defaultValue: 1000.0
                    formatter: v => param ? param.format(v) : ""
                    parser: text => param ? param.parse(text) : null
                    sampleText: "18.00 kHz"
                    tooltip: qsTr("Filter frequency")
                }
                EditorCaption {
                    id: widthLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Width")
                }
                ParamBox {
                    objectName: "width"
                    width: filterRow.boxWidth
                    param: editor.params["width"] || null
                    step: 0.05
                    decimals: 2
                    defaultValue: 8.0
                    sampleText: "8.00"
                    tooltip: qsTr("Filter width, in octaves")
                }
            }
            // Where Ableton has its modulation: the echoes' groove and grit.
            Column {
                id: character
                objectName: "character"
                width: parent.width
                spacing: 1

                Row {  // the title, a line either side
                    width: parent.width
                    spacing: 6

                    readonly property real lineWidth: (width - characterTitle.implicitWidth - 2 * spacing) / 2

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.lineWidth
                        height: 1
                        color: Theme.border
                    }
                    EditorCaption {
                        id: characterTitle
                        text: qsTr("Character")
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.lineWidth
                        height: 1
                        color: Theme.border
                    }
                }
                Row {
                    width: parent.width
                    spacing: 8

                    readonly property real cellWidth: (width - 3 * spacing) / 4

                    CharacterBox {
                        width: parent.cellWidth
                        paramId: "swing"
                        title: qsTr("Swing")
                        tooltip: qsTr("Swing: every other echo comes late, by up to half the time")
                    }
                    CharacterBox {
                        width: parent.cellWidth
                        paramId: "drive"
                        title: qsTr("Drive")
                        tooltip: qsTr("Drive: tape-like saturation in the feedback loop, each echo dirtier than the last")
                    }
                    CharacterBox {
                        width: parent.cellWidth
                        paramId: "duck"
                        title: qsTr("Duck")
                        tooltip: qsTr("Duck: the echoes dip while the input plays, and swell back in the gaps")
                    }
                    Column {
                        width: parent.cellWidth
                        spacing: 1

                        EditorCaption {
                            width: parent.width
                            text: qsTr("Groove")
                        }
                        ParamButton {
                            objectName: "followGroove"
                            width: parent.width
                            param: editor.params["follow_groove"] || null
                            text: qsTr("Follow")
                            tooltip: qsTr("Follow Groove: the swing is the project's default groove's (Edit › Default Groove):\nevery other echo as late as its offbeats")
                        }
                    }
                }
            }
        }

        // How a change of time sounds and ping pong, over the dry/wet mix.
        Item {
            id: rightColumn
            width: editor.rightWidth
            height: parent.height
            implicitHeight: top.implicitHeight + 6 + bottom.implicitHeight

            Column {
                id: top
                width: parent.width
                spacing: 2

                EditorCaption {
                    width: parent.width
                    text: qsTr("Mode")
                }
                Repeater {
                    model: [[qsTr("Repitch"), qsTr("Repitch: time changes glide, pitching the echoes as a tape delay does")],
                            [qsTr("Fade"), qsTr("Fade: time changes crossfade")],
                            [qsTr("Jump"), qsTr("Jump: time changes switch at once")]]
                    ParamButton {
                        required property var modelData
                        required property int index
                        objectName: "mode" + modelData[0]
                        width: parent.width
                        param: editor.params["mode"] || null
                        choice: index
                        text: modelData[0]
                        tooltip: modelData[1]
                    }
                }
                Item {
                    width: 1
                    height: 6
                }
                ParamButton {
                    objectName: "pingPong"
                    width: parent.width
                    param: editor.params["ping_pong"] || null
                    text: qsTr("Ping Pong")
                    tooltip: qsTr("The echoes bounce from left to right")
                }
            }
            Column {
                id: bottom
                anchors.bottom: parent.bottom
                width: parent.width
                spacing: 1

                EditorCaption {
                    width: parent.width
                    text: qsTr("Dry/Wet")
                }
                ParamKnob {
                    id: mix
                    objectName: "mix"
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 30
                    param: editor.params["mix"] || null
                }
                EditorReadout {
                    width: parent.width
                    text: mix.param ? mix.param.text : ""
                }
            }
        }
    }
}
