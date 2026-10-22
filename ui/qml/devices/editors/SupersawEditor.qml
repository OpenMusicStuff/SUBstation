import QtQuick
import QtQuick.Controls
import SUBstation

// The Supersaw's editor: three pages, named in the device's title bar (Stacks,
// Filter, Voice). On every page the stacks are drawn (SupersawGraph: each
// saw where it plays, across its detune and up and down the stereo field, as
// large as it is loud, glowing and shimmering at its beat while sound comes
// out), each with its switch beside it: press a stack's lane to show its knobs,
// drag across for its Detune and up and down for its Width, double-click to
// reset them. Beside it two rows of knobs:
// - Stacks: the stack shown's Saws, Detune, Curve, Blend, Width and Level, its
//   Transpose and Fine; and the saws' Phase, Random Phase and Drift (every
//   stack's).
// - Filter: its type, Cutoff, Resonance, Key Track and Env (how far its
//   envelope moves the cutoff); its envelope's Attack, Decay, Sustain, Release;
//   Focus and its frequency (below it, each stack a single saw in the middle).
// - Voice: the amplitude envelope, Velocity and Volume; Mode, Glide, Bend
//   (Range), the Sub and its octave.
// Knobs that do nothing with the settings as they are (a stack's while it is
// off, Blend under three saws, the filter's while it is off, the Sub's octave
// without the Sub, Focus's frequency without Focus) are dimmed but still settable. Every control is bound to its
// parameter: undoably, touched when pressed, its menu on right-click. The page
// and the stack shown are view state (DeviceViews), kept while the frame is
// made again.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property var pageNames: [qsTr("Stacks"), qsTr("Filter"), qsTr("Voice")]
    readonly property int pages: pageNames.length
    property int page: DeviceViews.value(deviceId, "supersawPage", 0)
    onPageChanged: DeviceViews.setValue(deviceId, "supersawPage", page)
    // The stack shown (0..2), on the Stacks page and lit in the graph.
    readonly property int stack: DeviceViews.value(deviceId, "supersawStack", 0)
    function showStack(index) {
        DeviceViews.setValue(deviceId, "supersawStack", Math.max(0, Math.min(2, index)))
    }

    readonly property alias graph: graph

    // The layout: the switches, the graph, the grid (six cells of 62 px, 6 apart).
    readonly property real sideWidth: 16
    readonly property real graphWidth: 272
    readonly property real cellWidth: 62
    readonly property real cellSpacing: 6
    readonly property int columns: 6
    readonly property real gridWidth: columns * cellWidth + (columns - 1) * cellSpacing
    readonly property real graphX: 8 + sideWidth + 4
    readonly property real gridX: graphX + graphWidth + 10

    implicitWidth: gridX + gridWidth + 8
    implicitHeight: 6 + grid.implicitHeight + 6

    // The grid's two rows: [parameter id, caption, cells (1, or 2 for a list)] each.
    function rowsOf(p, s) {
        const k = "s" + (s + 1) + "_"
        switch (p) {
        case 0: return [
            [[k + "saws", qsTr("Saws")], [k + "detune", qsTr("Detune")], [k + "curve", qsTr("Curve")],
             [k + "blend", qsTr("Blend")], [k + "width", qsTr("Width")], [k + "level", qsTr("Level")]],
            [[k + "transpose", qsTr("Transpose")], [k + "fine", qsTr("Fine")], ["phase", qsTr("Phase")],
             ["random", qsTr("Rnd Phase")], ["drift", qsTr("Drift")]]]
        case 1: return [
            [["filter_type", qsTr("Filter"), 2], ["cutoff", qsTr("Cutoff")], ["resonance", qsTr("Resonance")],
             ["key_track", qsTr("Key Track")], ["filter_env", qsTr("Env")]],
            [["filter_attack", qsTr("Attack")], ["filter_decay", qsTr("Decay")], ["filter_sustain", qsTr("Sustain")],
             ["filter_release", qsTr("Release")], ["focus", qsTr("Focus")], ["focus_freq", qsTr("Focus Hz")]]]
        default: return [
            [["attack", qsTr("Attack")], ["decay", qsTr("Decay")], ["sustain", qsTr("Sustain")],
             ["release", qsTr("Release")], ["velocity", qsTr("Velocity")], ["volume", qsTr("Volume")]],
            [["mode", qsTr("Mode"), 2], ["glide", qsTr("Glide")], ["bend_range", qsTr("Bend")], ["sub", qsTr("Sub")],
             ["sub_octave", qsTr("Sub Oct")]]]
        }
    }
    readonly property var rows: rowsOf(page, stack)

    // What decides whether a knob does anything now.
    DeviceParamMap {
        id: settings
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["s1_on", "s2_on", "s3_on", "s1_saws", "s2_saws", "s3_saws", "filter_type", "sub", "focus"]
    }
    function settingOf(id) {
        const p = settings.get(id)
        return p && p.valid ? p.value : 0
    }
    function active(id) {
        const own = /^s([123])_/.exec(id)
        if (own) {
            const prefix = "s" + own[1] + "_"
            if (settingOf(prefix + "on") < 0.5)
                return false
            const saws = Math.round(settingOf(prefix + "saws"))
            if (id === prefix + "blend")
                return saws >= 3
            if (id === prefix + "detune" || id === prefix + "curve" || id === prefix + "width")
                return saws >= 2
            return true
        }
        if (id === "focus")
            return true
        if (id === "focus_freq")
            return settingOf("focus") >= 0.5
        if (editor.page === 1 && id !== "filter_type")
            return Math.round(settingOf("filter_type")) !== 0
        if (id === "sub_octave")
            return settingOf("sub") > 0
        return true
    }

    // A stack's switch: a dot, lit in its colour while on; click to switch, right-click for its menu.
    component Power: Item {
        id: power
        property string paramId
        property color colour: Theme.accent
        readonly property DeviceParam param: powerParam
        readonly property bool lit: param ? param.value >= 0.5 : false
        width: 14
        height: 14

        DeviceParam {
            id: powerParam
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            paramId: power.paramId
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: if (power.param) power.param.set(power.lit ? 0 : 1)
        }
        ParamArea {
            anchors.fill: parent
            param: power.param
        }
        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: "transparent"
            border.color: power.lit ? power.colour : Theme.textDisabled
            border.width: 1.5
            Rectangle {
                anchors.centerIn: parent
                width: 6
                height: 6
                radius: 3
                color: power.lit ? power.colour : "transparent"
                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }
        }
        HoverHandler {
            id: powerHover
        }
        ToolTip.visible: powerHover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("Switch the stack on or off")
    }

    // --- The stacks: their switches, and the graph -----------------------------------------------

    Repeater {
        model: 3
        Power {
            required property int index
            readonly property real lane: (graph.height - 2 * 3) / 3
            objectName: "power_" + index
            x: 8 + (editor.sideWidth - width) / 2
            y: graph.y + index * (lane + 3) + (lane - height) / 2
            paramId: "s" + (index + 1) + "_on"
            colour: index === 0 ? Theme.accent : index === 1 ? "#6fc8ff" : "#d28cff"
        }
    }

    SupersawGraph {
        id: graph
        objectName: "supersawGraph"
        x: editor.graphX
        y: 6
        width: editor.graphWidth
        height: editor.height - 12
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        stack: editor.stack
        onStackPressed: index => {
            editor.showStack(index)
            editor.page = 0
        }

        HoverHandler {
            id: graphHover
        }
        ToolTip.visible: graphHover.hovered && !graph.dragging
        ToolTip.delay: 700
        ToolTip.text: qsTr("Every saw where it plays: across, its detune; up and down, its place from left to right; "
                           + "as large as it is loud. Press a stack to show it, drag across for its Detune and up "
                           + "and down for its Width, double-click to reset them")
    }

    // --- The grid -----------------------------------------------------------------------------------

    Column {
        id: grid
        objectName: "knobs"
        x: editor.gridX
        y: 6
        spacing: 14

        Repeater {
            model: 2
            Row {
                id: row
                required property int index
                spacing: editor.cellSpacing
                height: probe.implicitHeight

                Repeater {
                    model: editor.rows[row.index] || []
                    DeviceParamKnob {
                        required property var modelData
                        // (Two cells for a list, its names whole; one for a knob.)
                        readonly property int span: (modelData[2] || 1) > 1 && param.isList ? modelData[2] : 1
                        objectName: "param_" + modelData[0]
                        trackId: editor.trackId
                        deviceId: editor.deviceId
                        paramId: modelData[0]
                        name: modelData[1]
                        cellWidth: span * editor.cellWidth + (span - 1) * editor.cellSpacing
                        opacity: editor.active(modelData[0]) ? 1 : 0.4
                        Behavior on opacity {
                            NumberAnimation {
                                duration: 120
                            }
                        }
                    }
                }
            }
        }
    }

    // (How tall a row of knobs is.)
    DeviceParamKnob {
        id: probe
        visible: false
        trackId: editor.trackId
        deviceId: editor.deviceId
        paramId: "volume"
        cellWidth: editor.cellWidth
    }
}
