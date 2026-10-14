import QtQuick
import QtQuick.Controls
import SUBstation

// The LFO's editor: its cycle to draw on (ModulatorGraph: the playhead riding
// it, what it puts out at the right), then its controls (Sync, Retrig, Random and Map
// over its knobs: the rate in Hz or synced in notes, Depth, Offset, Phase,
// Smooth, and the Targets button beside them), then, unfolded by that button
// (view state, kept per device: DeviceViews), what it moves (ModulatorTargets: each target's
// range, Invert and ×).
Item {
    id: editor

    required property string trackId
    required property string deviceId
    readonly property alias graph: graph
    readonly property alias targets: targets
    readonly property alias curveMenu: curveMenu
    readonly property alias mapButton: mapButton
    readonly property alias targetsButton: targetsCell.button
    readonly property DeviceParamMap p: params
    // What it moves shows (view state, not undone).
    readonly property bool targetsShown: DeviceViews.value(deviceId, "targets", false)

    // The device's body: GRAPH + CONTROLS (+ TARGETS), 8 px apart, and an inset at the right.
    implicitWidth: 250 + 8 + controls.width + (targetsShown ? 8 + targets.width : 0) + 6
    implicitHeight: Math.max(96, 5 + controls.implicitHeight + 5)

    DeviceParamMap {
        id: params
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["rate", "sync", "synced", "depth", "offset", "phase", "retrigger", "smooth", "random"]
    }

    Row {
        anchors.fill: parent
        anchors.rightMargin: 6
        spacing: 8

        ModulatorGraph {
            id: graph
            objectName: "modulatorGraph"
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            width: parent.width - parent.spacing - controls.width - (targets.visible ? parent.spacing + targets.width : 0)
            height: parent.height
            onContextMenuRequested: (position, point) => curveMenu.showAt(position, point)
        }

        Column {
            id: controls
            width: 3 * 56
            y: 5
            spacing: 4

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4

                ParamButton {
                    objectName: "sync"
                    width: 39
                    height: 18
                    param: params.get("sync")
                    text: qsTr("Sync")
                    tooltip: qsTr("The rate in notes, at the tempo (following the song while it plays)")
                }
                ParamButton {
                    objectName: "retrigger"
                    width: 39
                    height: 18
                    param: params.get("retrigger")
                    text: qsTr("Retrig")
                    tooltip: qsTr("Each note starts the cycle again")
                }
                ParamButton {
                    objectName: "random"
                    width: 39
                    height: 18
                    param: params.get("random")
                    text: qsTr("Rand")
                    tooltip: qsTr("Random: a new random shape every cycle, instead of the curve drawn")
                }
                ModulatorMapButton {
                    id: mapButton
                    width: 39
                    height: 18
                    mappings: targets.mappings
                }
            }
            Grid {
                columns: 3
                rowSpacing: 2
                columnSpacing: 0

                Item {
                    objectName: "rateStack"
                    width: rateKnob.width
                    height: rateKnob.implicitHeight

                    EditorKnob {
                        id: rateKnob
                        objectName: "rate"
                        width: 56
                        visible: !syncedKnob.visible
                        size: 38
                        param: params.get("rate")
                        title: qsTr("Rate")
                        tooltip: qsTr("Cycles a second")
                    }
                    EditorKnob {
                        id: syncedKnob
                        objectName: "synced"
                        width: 56
                        visible: params.get("sync") ? params.get("sync").value >= 0.5 : true
                        size: 38
                        step: 1
                        param: params.get("synced")
                        title: qsTr("Rate")
                        tooltip: qsTr("A cycle's length in notes")
                    }
                }
                EditorKnob {
                    objectName: "depth"
                    width: 56
                    size: 38
                    param: params.get("depth")
                    title: qsTr("Depth")
                    tooltip: qsTr("How far it swings round the middle of its targets' ranges")
                }
                EditorKnob {
                    objectName: "offset"
                    width: 56
                    size: 38
                    param: params.get("offset")
                    title: qsTr("Offset")
                    tooltip: qsTr("Moves it up or down its targets' ranges")
                }
                EditorKnob {
                    objectName: "phase"
                    width: 56
                    size: 38
                    param: params.get("phase")
                    title: qsTr("Phase")
                    tooltip: qsTr("Where in its cycle it starts")
                }
                EditorKnob {
                    objectName: "smooth"
                    width: 56
                    size: 38
                    param: params.get("smooth")
                    title: qsTr("Smooth")
                    tooltip: qsTr("Eases its steps and jumps")
                }
                ModulatorTargetsButton {
                    id: targetsCell
                    objectName: "targetsCell"
                    width: 56
                    deviceId: editor.deviceId
                    shown: editor.targetsShown
                    count: targets.mappings.count
                }
            }
        }

        ModulatorTargets {
            id: targets
            objectName: "modulatorTargets"
            visible: editor.targetsShown
            trackId: editor.trackId
            deviceId: editor.deviceId
            width: 164
            y: 5
            height: parent.height - 10
            output: graph.output
        }
    }

    // The curve's right-click menu: delete the point, shapes to start from, flip, reverse, reset.
    DynamicMenu {
        id: curveMenu

        function build(point) {
            clear()
            if (point >= 0) {
                entry(qsTr("Delete Point"), () => graph.removePoint(point))
                separator()
            }
            const shapes = submenu(qsTr("Shapes"))
            for (let i = 0; i < graph.shapes.length; ++i) {
                const index = i
                entry(graph.shapes[i], () => graph.applyShape(index), undefined, true, shapes)
            }
            entry(qsTr("Flip Curve"), () => graph.flip())
            entry(qsTr("Reverse Curve"), () => graph.reverse())
            separator()
            entry(qsTr("Reset Curve"), () => graph.resetCurve())
        }

        function showAt(position, point) {
            build(point)
            popup(graph, position.x, position.y)
        }
    }
}
