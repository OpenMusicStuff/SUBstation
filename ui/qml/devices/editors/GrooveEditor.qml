import QtQuick
import QtQuick.Controls
import SUBstation

// The Groove device's editor: the groove it plays (Swing, the project's
// default, a groove of the project's library, or its own copy of one: its
// source, chosen above the steps), drawn (GrooveSteps: its default lane over its
// cycle, each step where it plays and as loud), and the knobs it plays it with:
// Swing and Step (the Swing source's), Timing, Velocity, Length, Random and
// Quantize, with Allow Early and Latency. Apply writes what it does into the
// track's notes and switches it off; Add to Grooves puts the groove it plays in
// the project's library (it then plays that).
//
// Its animation: the step playing lights up as it plays (the display `step`,
// through a DisplayValue), a glow sliding to it from the last.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int stepsWidth: 220
    readonly property int knobWidth: 52
    readonly property int controlsWidth: 4 * knobWidth + 3 * 6
    readonly property int spacing: 10

    implicitWidth: 8 + stepsWidth + spacing + controlsWidth + 8
    implicitHeight: 6 + controls.implicitHeight + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["swing", "step", "timing", "velocity", "length", "random", "quantize", "early", "latency"]
    }

    // What it plays: {kind, grooveId, name, groove} (Session.grooves.deviceGroove).
    readonly property var plays: {
        void Session.grooves.revision
        return Session.grooves.deviceGroove(editor.trackId, editor.deviceId)
    }
    readonly property string kind: plays.kind || "swing"
    readonly property bool swingSource: kind === "swing"

    // The sources to choose from: Swing, Project, the library's grooves (and its own, while it has one).
    readonly property var sources: {
        const list = [{ kind: "swing", grooveId: "", name: qsTr("Swing") },
                      { kind: "project", grooveId: "", name: qsTr("Project: %1").arg(Session.grooves.defaultName) }]
        for (const g of Session.grooves.grooves)
            list.push({ kind: "library", grooveId: g.id, name: g.name })
        if (kind === "own")
            list.push({ kind: "own", grooveId: "", name: qsTr("Its own groove") })
        return list
    }
    readonly property int sourceIndex: {
        for (let i = 0; i < sources.length; ++i) {
            const s = sources[i]
            if (s.kind === kind && (kind !== "library" || s.grooveId === plays.grooveId))
                return i
        }
        return 0
    }

    // The step of its cycle playing, or -1 while none sounds.
    DisplayValue {
        id: playing
        objectName: "stepDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "step"
        width: 0
        height: 0
    }
    readonly property int playingStep: playing.active ? Math.round(playing.value) : -1

    Column {
        id: stepsColumn
        x: 8
        y: 6
        width: editor.stepsWidth
        spacing: 3

        Row {
            width: parent.width
            spacing: 4

            EditorCaption {
                id: sourceCaption
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Groove")
            }
            ComboBox {
                id: source
                objectName: "source"
                width: parent.width - parent.spacing - sourceCaption.width
                height: 18
                focusPolicy: Qt.NoFocus
                model: editor.sources
                textRole: "name"
                currentIndex: editor.sourceIndex
                onActivated: index => {
                    const chosen = editor.sources[index]
                    Session.grooves.setDeviceSource(editor.trackId, editor.deviceId, chosen.kind, chosen.grooveId)
                }
                ToolTip.visible: hovered && !popup.visible
                ToolTip.delay: 700
                ToolTip.text: qsTr("The groove it plays: Swing (the Swing and Step knobs), the project's default groove,\nor one of the project's grooves (Edit › Grooves…)")
            }
        }

        GrooveSteps {
            id: steps
            objectName: "stepsView"
            width: parent.width
            height: Math.max(40, controls.implicitHeight - 3 * 3 - 18 - 18)
            groove: editor.plays.groove || ({})
            lit: editor.playingStep

            HoverHandler {
                id: stepsHover
            }
            ToolTip.visible: stepsHover.hovered
            ToolTip.delay: 700
            ToolTip.text: qsTr("The groove over its cycle: each step where it plays, and how loud")
        }

        Row {
            width: parent.width
            spacing: 4

            RoleButton {
                objectName: "apply"
                width: (parent.width - parent.spacing) / 2
                height: 18
                role: "small"
                text: qsTr("Apply")
                tooltip: qsTr("Write what the groove does into the track's notes, and switch the device off")
                onClicked: Session.grooves.applyDevice(editor.trackId, editor.deviceId)
            }
            RoleButton {
                objectName: "addToGrooves"
                width: (parent.width - parent.spacing) / 2
                height: 18
                role: "small"
                text: qsTr("Add to Grooves")
                tooltip: qsTr("Put the groove it plays in the project's grooves, to edit it\nand play it elsewhere (it then plays that one)")
                onClicked: Session.grooves.addFromDevice(editor.trackId, editor.deviceId)
            }
        }
    }

    // What it moves notes with.
    Column {
        id: controls
        x: stepsColumn.x + stepsColumn.width + editor.spacing
        y: 6
        width: editor.controlsWidth
        spacing: 4

        Row {
            spacing: 6

            EditorKnob {
                objectName: "swing"
                width: editor.knobWidth
                enabled: editor.swingSource
                param: p.get("swing")
                title: qsTr("Swing")
                tooltip: qsTr("Swing: every other step late, landing this far through its pair\n(50 %: straight; 66.7 %: a triplet feel)")
            }
            Column {
                width: editor.knobWidth
                spacing: 2
                enabled: editor.swingSource
                opacity: enabled ? 1 : 0.55

                EditorCaption {
                    width: parent.width
                    text: qsTr("Step")
                }
                ParamChoice {
                    objectName: "step"
                    width: parent.width
                    param: p.get("step")
                    tooltip: qsTr("Step: the notes Swing moves (every other one of these)")
                }
            }
            EditorKnob {
                objectName: "timing"
                width: editor.knobWidth
                param: p.get("timing")
                title: qsTr("Timing")
                tooltip: qsTr("Timing: how far notes move as the groove has them (100 %: as it has them; up to 200 %)")
            }
            EditorKnob {
                objectName: "velocity"
                width: editor.knobWidth
                param: p.get("velocity")
                title: qsTr("Velocity")
                tooltip: qsTr("Velocity: how much the groove's accents change velocities (negative: turned round)")
            }
        }
        Row {
            spacing: 6

            EditorKnob {
                objectName: "length"
                width: editor.knobWidth
                param: p.get("length")
                title: qsTr("Length")
                tooltip: qsTr("Length: how far notes take the lengths the groove's steps have")
            }
            EditorKnob {
                objectName: "random"
                width: editor.knobWidth
                param: p.get("random")
                title: qsTr("Random")
                tooltip: qsTr("Random: a jitter on each note's timing, up to a quarter step either way,\nthe same every time it plays")
            }
            EditorKnob {
                objectName: "quantize"
                width: editor.knobWidth
                param: p.get("quantize")
                title: qsTr("Quantize")
                tooltip: qsTr("Quantize: notes pulled onto the groove's grid before it moves them")
            }
            Column {
                width: editor.knobWidth
                spacing: 4
                anchors.verticalCenter: parent.verticalCenter

                ParamButton {
                    objectName: "early"
                    width: parent.width
                    height: 18
                    param: p.get("early")
                    text: qsTr("Early")
                    tooltip: qsTr("Allow Early: clips' notes move early as well as late (up to half a step),\nhanded over ahead of their time")
                }
                ParamButton {
                    objectName: "latency"
                    width: parent.width
                    height: 18
                    param: p.get("latency")
                    text: qsTr("Latency")
                    tooltip: qsTr("Latency: the track plays half a step later, so notes played live\nmove early too (delay compensation keeps it in time)")
                }
            }
        }
    }
}
