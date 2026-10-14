import QtQuick
import QtQuick.Controls
import SUBstation

// The Envelope's editor: its shape to draw on (ModulatorGraph: played from each
// trigger, flashing as one comes, the key against its threshold once a
// sidechain is chosen), then its controls (Sync, Random and Map over its knobs: the
// length in ms or synced in notes, Depth, Velocity, Threshold, Smooth, and the
// Targets button beside them), then, unfolded by that button (view state:
// DeviceViews), what it moves (ModulatorTargets). What triggers it is what it is given: the notes it
// hears (its track's, or MIDI From's), and the hits of a sidechain once one is
// chosen (the title bar's buttons); Threshold counts only then.
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
    readonly property bool keyed: info.sidechainOn
    readonly property bool targetsShown: DeviceViews.value(deviceId, "targets", false)

    implicitWidth: 250 + 8 + controls.width + (targetsShown ? 8 + targets.width : 0) + 6
    implicitHeight: Math.max(96, 5 + controls.implicitHeight + 5)

    DeviceParamMap {
        id: params
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["threshold", "sync", "length", "synced", "depth", "velocity", "smooth", "random"]
    }
    DeviceInfo {
        id: info
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
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
                    width: 52
                    height: 18
                    param: params.get("sync")
                    text: qsTr("Sync")
                    tooltip: qsTr("Its length in notes, at the tempo")
                }
                ParamButton {
                    objectName: "random"
                    width: 52
                    height: 18
                    param: params.get("random")
                    text: qsTr("Random")
                    tooltip: qsTr("Random: a new random shape from every trigger, instead of the curve drawn")
                }
                ModulatorMapButton {
                    id: mapButton
                    width: 52
                    height: 18
                    mappings: targets.mappings
                }
            }
            Grid {
                columns: 3
                rowSpacing: 2
                columnSpacing: 0

                Item {
                    objectName: "lengthStack"
                    width: lengthKnob.width
                    height: lengthKnob.implicitHeight

                    EditorKnob {
                        id: lengthKnob
                        objectName: "length"
                        width: 56
                        visible: !syncedKnob.visible
                        size: 38
                        param: params.get("length")
                        title: qsTr("Length")
                        tooltip: qsTr("How long its curve plays from a trigger")
                    }
                    EditorKnob {
                        id: syncedKnob
                        objectName: "synced"
                        width: 56
                        visible: params.get("sync") ? params.get("sync").value >= 0.5 : false
                        size: 38
                        step: 1
                        param: params.get("synced")
                        title: qsTr("Length")
                        tooltip: qsTr("How long its curve plays from a trigger, in notes")
                    }
                }
                EditorKnob {
                    objectName: "depth"
                    width: 56
                    size: 38
                    param: params.get("depth")
                    title: qsTr("Depth")
                    tooltip: qsTr("How far up its targets' ranges it reaches")
                }
                EditorKnob {
                    objectName: "velocity"
                    width: 56
                    size: 38
                    param: params.get("velocity")
                    title: qsTr("Velocity")
                    tooltip: qsTr("How much softer notes play it lower")
                }
                EditorKnob {
                    objectName: "threshold"
                    width: 56
                    enabled: editor.keyed
                    size: 38
                    param: params.get("threshold")
                    title: qsTr("Threshold")
                    tooltip: editor.keyed ? qsTr("How loud a hit in the sidechain must be to trigger it")
                                          : qsTr("Choose a sidechain (the title bar) to trigger it from its hits")
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
