import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import SUBstation

// Vital's editor: its whole synth in nine pages, named in the device's title
// bar (Osc, Sample, Filter, Env, LFO, Random, FX, Matrix, Voice), each a drawn
// panel beside a grid of knobs.
//
// The column on the left is the patch's: a small screen with the preset's name
// and author over the sound as it plays (VitalScope, faint), the page's choices
// (which oscillator, filter, envelope, LFO or random LFO), and the four macros,
// named as the preset names them, on every page.
//
// The panel shows the section chosen, drawn as Vital plays it, under a header
// with its switch (a lit dot), its name and what it routes to:
// - Osc: the wavetable's frames stacked (VitalWavetableGraph; drag up and down
//   through them), its name, Wavetable… (load a .wav wavetable, Vital's
//   .vitaltable, any audio vocoded or pitched, or a basic shape), the output;
//   under it the spectral morph's, the warp's and the unison stack's types.
// - Sample and Random: the output, live (VitalScope); the sample's switches, the
//   random LFO's style and timing under it.
// - Filter: its response (VitalFilterGraph; drag the cutoff and resonance),
//   its model and style under it.
// - Env: its stages (VitalEnvelopeGraph; drag their ends and curves), and the
//   newest voice moving through them.
// - LFO: its shape (VitalLfoGraph) with the newest voice's phase, Shape… (a
//   basic shape), its trigger mode, timing and smoothing under it.
// - FX: Vital's nine effects as tiles, each lit while on (its dot switches it),
//   the one clicked shown in the grid.
// - Voice: the output's spectrum, and how many voices play.
// The grid is ten cells across in two rows (a list takes two cells, its names
// whole); the knobs that do nothing with the section's settings as they are
// (the formant's without the Formant model, a rate while synced to the tempo)
// are dimmed but still settable. Matrix is the modulation matrix: a source and
// a destination chosen and added (the first free of Vital's 64 slots), then
// each modulation in a row: its amount, Bipolar, Stereo and Bypass, and × to
// remove it.
//
// Every control is bound to Vital's parameter: shown as Vital shows its value,
// set undoably, touched when pressed (so its automation shows), right-click for
// its menu. The matrix's and the wavetables' edits go through the editor too
// (VitalPatchInfo), one undo step each. Vital's own presets load and save from
// the device's menu (Load Vital Preset…, Save Vital Preset…), or drop a .vital
// file on it. The page and each page's choice are view state (DeviceViews),
// kept while the frame is made again.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property var pageNames: [qsTr("Osc"), qsTr("Sample"), qsTr("Filter"), qsTr("Env"), qsTr("LFO"),
        qsTr("Random"), qsTr("FX"), qsTr("Matrix"), qsTr("Voice")]
    readonly property int pages: pageNames.length
    property int page: DeviceViews.value(deviceId, "vitalPage", 0)
    onPageChanged: DeviceViews.setValue(deviceId, "vitalPage", page)

    readonly property alias patch: patch

    // The layout: the side column, the panel, the grid (ten cells of 54 px, 6 apart), as wide as before.
    readonly property real sideWidth: 112
    readonly property real panelWidth: 254
    readonly property real cellWidth: 54
    readonly property real cellSpacing: 6
    readonly property int columns: 10
    readonly property real gridWidth: columns * cellWidth + (columns - 1) * cellSpacing
    readonly property real panelX: 8 + sideWidth + 10
    readonly property real gridX: panelX + panelWidth + 10
    readonly property real headerHeight: 16
    readonly property real footerHeight: 16

    implicitWidth: gridX + gridWidth + 8
    implicitHeight: 6 + grid.implicitHeight + 6

    // Each page's choices (the side column's buttons; the FX page's are its tiles), and which is shown.
    readonly property var effects: [
        {key: "chorus", name: qsTr("Chorus")}, {key: "compressor", name: qsTr("Comp")},
        {key: "delay", name: qsTr("Delay")}, {key: "distortion", name: qsTr("Distortion")},
        {key: "eq", name: qsTr("EQ")}, {key: "filter_fx", name: qsTr("Filter")},
        {key: "flanger", name: qsTr("Flanger")}, {key: "phaser", name: qsTr("Phaser")},
        {key: "reverb", name: qsTr("Reverb")}]
    function choicesOf(p) {
        const numbered = n => Array.from({length: n}, (_, i) => String(i + 1))
        switch (p) {
        case 0: return numbered(3)
        case 2: return numbered(2)
        case 3: return numbered(6)
        case 4: return numbered(8)
        case 5: return numbered(4)
        case 6: return effects.map(e => e.name)
        default: return []
        }
    }
    readonly property var choices: choicesOf(page)
    readonly property int choice: Math.min(DeviceViews.value(deviceId, "vitalChoice" + page, 0),
                                           Math.max(0, choices.length - 1))
    function choose(index) {
        DeviceViews.setValue(deviceId, "vitalChoice" + page, index)
    }
    readonly property string number: String(choice + 1)

    // The section the panel shows: its parameters' prefix, its name, its switch (or none).
    readonly property string prefix: {
        switch (page) {
        case 0: return "osc_" + number + "_"
        case 1: return "sample_"
        case 2: return "filter_" + number + "_"
        case 3: return "env_" + number + "_"
        case 4: return "lfo_" + number + "_"
        case 5: return "random_" + number + "_"
        case 6: return effects[choice].key + "_"
        default: return ""
        }
    }
    readonly property string sectionName: {
        switch (page) {
        case 0: return qsTr("Oscillator %1").arg(number)
        case 1: return qsTr("Sample")
        case 2: return qsTr("Filter %1").arg(number)
        case 3: return qsTr("Envelope %1").arg(number)
        case 4: return qsTr("LFO %1").arg(number)
        case 5: return qsTr("Random %1").arg(number)
        case 6: return qsTr("Effects")
        case 8: return qsTr("Voice")
        default: return ""
        }
    }
    // The panel's title: short.
    readonly property string title: {
        switch (page) {
        case 0: return qsTr("OSC %1").arg(number)
        case 1: return qsTr("SAMPLE")
        case 2: return qsTr("FILTER %1").arg(number)
        case 3: return qsTr("ENV %1").arg(number)
        case 4: return qsTr("LFO %1").arg(number)
        case 5: return qsTr("RANDOM %1").arg(number)
        case 6: return qsTr("EFFECTS")
        case 8: return qsTr("VOICE")
        default: return ""
        }
    }
    readonly property bool hasSwitch: page <= 2

    // The grid's two rows: [parameter id, caption, cells (1, or 2 for a list)] each.
    function rowsOf(p, c) {
        const n = String(c + 1)
        const osc = "osc_" + n + "_"
        switch (p) {
        case 0: return [
            [[osc + "level", qsTr("Level")], [osc + "pan", qsTr("Pan")], [osc + "transpose", qsTr("Transp")],
             [osc + "tune", qsTr("Tune")], [osc + "wave_frame", qsTr("Frame")], [osc + "phase", qsTr("Phase")],
             [osc + "random_phase", qsTr("Random")], [osc + "unison_voices", qsTr("Unison")],
             [osc + "unison_detune", qsTr("Detune")], [osc + "unison_blend", qsTr("Blend")]],
            [[osc + "spectral_morph_amount", qsTr("Morph")], [osc + "spectral_morph_spread", qsTr("M.Spread")],
             [osc + "distortion_amount", qsTr("Warp")], [osc + "distortion_spread", qsTr("W.Spread")],
             [osc + "distortion_phase", qsTr("W.Phase")], [osc + "detune_range", qsTr("Range")],
             [osc + "detune_power", qsTr("Det Pwr")], [osc + "stereo_spread", qsTr("Stereo")],
             [osc + "frame_spread", qsTr("Frm Sprd")]]]
        case 1: return [
            [["sample_level", qsTr("Level")], ["sample_pan", qsTr("Pan")], ["sample_transpose", qsTr("Transpose")],
             ["sample_tune", qsTr("Tune")]],
            []]
        case 2: return filterRows("filter_" + n + "_", false)
        case 3: {
            const env = "env_" + n + "_"
            return [
                [[env + "delay", qsTr("Delay")], [env + "attack", qsTr("Attack")], [env + "hold", qsTr("Hold")],
                 [env + "decay", qsTr("Decay")], [env + "sustain", qsTr("Sustain")], [env + "release", qsTr("Release")]],
                [[env + "attack_power", qsTr("Atk Curve")], [env + "decay_power", qsTr("Dec Curve")],
                 [env + "release_power", qsTr("Rel Curve")]]]
        }
        case 4: {
            const lfo = "lfo_" + n + "_"
            return [
                [[lfo + "frequency", qsTr("Rate")], [lfo + "tempo", qsTr("Tempo")], [lfo + "phase", qsTr("Phase")],
                 [lfo + "stereo", qsTr("Stereo")], [lfo + "delay_time", qsTr("Delay")], [lfo + "fade_time", qsTr("Fade In")]],
                [[lfo + "smooth_time", qsTr("Smooth")], [lfo + "keytrack_transpose", qsTr("Transpose")],
                 [lfo + "keytrack_tune", qsTr("Tune")]]]
        }
        case 5: {
            const random = "random_" + n + "_"
            return [
                [[random + "frequency", qsTr("Rate")], [random + "tempo", qsTr("Tempo")],
                 [random + "stereo", qsTr("Stereo")]],
                [[random + "keytrack_transpose", qsTr("Transpose")], [random + "keytrack_tune", qsTr("Tune")]]]
        }
        case 6: return effectRows(effects[c].key)
        case 8: return [
            [["volume", qsTr("Volume")], ["polyphony", qsTr("Voices")], ["velocity_track", qsTr("Velocity")],
             ["pitch_bend_range", qsTr("Bend")], ["voice_tune", qsTr("Tune")], ["voice_transpose", qsTr("Transp")],
             ["portamento_time", qsTr("Glide")], ["portamento_slope", qsTr("Slope")], ["mod_wheel", qsTr("Mod Whl")],
             ["pitch_wheel", qsTr("Pitch Whl")]],
            [["voice_priority", qsTr("Priority"), 2], ["voice_override", qsTr("Override")], ["legato", qsTr("Legato")],
             ["portamento_force", qsTr("Always")], ["portamento_scale", qsTr("Oct Scale")],
             ["stereo_routing", qsTr("Stereo")], ["stereo_mode", qsTr("St. Mode")],
             ["oversampling", qsTr("Quality")], ["voice_amplitude", qsTr("Amplitude")]]]
        default: return [[], []]
        }
    }
    function filterRows(f, withLists) {
        const lists = withLists ? [[f + "model", qsTr("Model"), 2], [f + "style", qsTr("Style"), 2]] : []
        return [
            [[f + "cutoff", qsTr("Cutoff")], [f + "resonance", withLists ? qsTr("Res") : qsTr("Resonance")],
             [f + "drive", qsTr("Drive")], [f + "blend", qsTr("Blend")],
             [f + "keytrack", withLists ? qsTr("Key Trk") : qsTr("Key Track")], [f + "mix", qsTr("Mix")]].concat(lists),
            [[f + "blend_transpose", withLists ? qsTr("Comb") : qsTr("Comb Offset")],
             [f + "formant_x", withLists ? qsTr("Fmt X") : qsTr("Formant X")],
             [f + "formant_y", withLists ? qsTr("Fmt Y") : qsTr("Formant Y")],
             [f + "formant_transpose", withLists ? qsTr("Shift") : qsTr("Fmt Shift")],
             [f + "formant_resonance", qsTr("Fmt Res")],
             [f + "formant_spread", withLists ? qsTr("Spread") : qsTr("Fmt Spread")]]]
    }
    function effectRows(key) {
        const e = key + "_"
        switch (key) {
        case "chorus": return [
            [[e + "dry_wet", qsTr("Mix")], [e + "voices", qsTr("Voices")], [e + "sync", qsTr("Timing"), 2],
             [e + "frequency", qsTr("Rate")], [e + "tempo", qsTr("Tempo")], [e + "mod_depth", qsTr("Depth")],
             [e + "feedback", qsTr("Feedback")]],
            [[e + "delay_1", qsTr("Delay 1")], [e + "delay_2", qsTr("Delay 2")], [e + "cutoff", qsTr("Cutoff")],
             [e + "spread", qsTr("Spread")]]]
        case "compressor": return [
            [[e + "enabled_bands", qsTr("Bands"), 2], [e + "attack", qsTr("Attack")], [e + "release", qsTr("Release")],
             [e + "mix", qsTr("Mix")], [e + "low_gain", qsTr("Low Gain")], [e + "band_gain", qsTr("Mid Gain")],
             [e + "high_gain", qsTr("High Gain")], [e + "low_upper_threshold", qsTr("Low Up")],
             [e + "low_upper_ratio", qsTr("Low Ratio")]],
            [[e + "low_lower_threshold", qsTr("Low Low")], [e + "low_lower_ratio", qsTr("Low Exp")],
             [e + "band_upper_threshold", qsTr("Mid Up")], [e + "band_upper_ratio", qsTr("Mid Ratio")],
             [e + "band_lower_threshold", qsTr("Mid Low")], [e + "band_lower_ratio", qsTr("Mid Exp")],
             [e + "high_upper_threshold", qsTr("High Up")], [e + "high_upper_ratio", qsTr("Hi Ratio")],
             [e + "high_lower_threshold", qsTr("High Low")], [e + "high_lower_ratio", qsTr("High Exp")]]]
        case "delay": return [
            [[e + "dry_wet", qsTr("Mix")], [e + "style", qsTr("Style"), 2], [e + "feedback", qsTr("Feedback")],
             [e + "filter_cutoff", qsTr("Cutoff")], [e + "filter_spread", qsTr("Spread")]],
            [[e + "sync", qsTr("Timing"), 2], [e + "frequency", qsTr("Rate")], [e + "tempo", qsTr("Tempo")],
             [e + "aux_sync", qsTr("Timing 2"), 2], [e + "aux_frequency", qsTr("Rate 2")],
             [e + "aux_tempo", qsTr("Tempo 2")]]]
        case "distortion": return [
            [[e + "type", qsTr("Type"), 2], [e + "drive", qsTr("Drive")], [e + "mix", qsTr("Mix")]],
            [[e + "filter_order", qsTr("Filter"), 2], [e + "filter_cutoff", qsTr("Cutoff")],
             [e + "filter_resonance", qsTr("Resonance")], [e + "filter_blend", qsTr("Blend")]]]
        case "eq": return [
            [[e + "low_mode", qsTr("Low"), 2], [e + "low_cutoff", qsTr("Low Freq")], [e + "low_gain", qsTr("Low Gain")],
             [e + "low_resonance", qsTr("Low Res")], [e + "band_mode", qsTr("Band"), 2],
             [e + "band_cutoff", qsTr("Band Freq")], [e + "band_gain", qsTr("Band Gain")],
             [e + "band_resonance", qsTr("Band Res")]],
            [[e + "high_mode", qsTr("High"), 2], [e + "high_cutoff", qsTr("High Freq")],
             [e + "high_gain", qsTr("High Gain")], [e + "high_resonance", qsTr("High Res")]]]
        case "filter_fx": return filterRows("filter_fx_", true)
        case "flanger": return [
            [[e + "dry_wet", qsTr("Mix")], [e + "sync", qsTr("Timing"), 2], [e + "frequency", qsTr("Rate")],
             [e + "tempo", qsTr("Tempo")]],
            [[e + "center", qsTr("Center")], [e + "feedback", qsTr("Feedback")], [e + "mod_depth", qsTr("Depth")],
             [e + "phase_offset", qsTr("Offset")]]]
        case "phaser": return [
            [[e + "dry_wet", qsTr("Mix")], [e + "sync", qsTr("Timing"), 2], [e + "frequency", qsTr("Rate")],
             [e + "tempo", qsTr("Tempo")]],
            [[e + "center", qsTr("Center")], [e + "feedback", qsTr("Feedback")], [e + "mod_depth", qsTr("Depth")],
             [e + "phase_offset", qsTr("Offset")], [e + "blend", qsTr("Blend")]]]
        case "reverb": return [
            [[e + "dry_wet", qsTr("Mix")], [e + "size", qsTr("Size")], [e + "decay_time", qsTr("Decay")],
             [e + "delay", qsTr("Delay")], [e + "pre_low_cutoff", qsTr("Pre Low")],
             [e + "pre_high_cutoff", qsTr("Pre High")]],
            [[e + "low_shelf_cutoff", qsTr("Low Freq")], [e + "low_shelf_gain", qsTr("Low Gain")],
             [e + "high_shelf_cutoff", qsTr("High Freq")], [e + "high_shelf_gain", qsTr("High Gain")],
             [e + "chorus_amount", qsTr("Chorus")], [e + "chorus_frequency", qsTr("Chorus Rate")]]]
        default: return [[], []]
        }
    }
    readonly property var rows: rowsOf(page, choice)
    // The page's cells: as wide as its widest row lets them be (the house's 84 px at most).
    readonly property int rowUnits: Math.max(1, ...rows.map(r => r.reduce((n, cell) => n + (cell[2] || 1), 0)))
    readonly property real pageCellWidth: Math.min(84, Math.floor((gridWidth - (rowUnits - 1) * cellSpacing) / rowUnits))
    // Vital's units, shorter, so a readout fits its cell ("0.500 secs": "0.500 s").
    function compact(text) {
        return text.replace(/ secs?$/, " s").replace(/ semitones?$/, " st").replace(/ cents?$/, " ct")
    }

    // What decides whether a knob does anything now: the filter's model, the section's timing (an LFO's, an
    // effect's), the delay's second timing. (Each its own DeviceParam, following the page.)
    DeviceParam {
        id: modelSetting
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        paramId: editor.page === 2 || editor.prefix === "filter_fx_" ? editor.prefix + "model" : ""
    }
    DeviceParam {
        id: timingSetting
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        paramId: editor.page === 4 || editor.page === 5 || (editor.page === 6 && editor.prefix !== "filter_fx_")
                 ? editor.prefix + "sync" : ""
    }
    DeviceParam {
        id: auxTimingSetting
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        paramId: editor.prefix === "delay_" ? "delay_aux_sync" : ""
    }
    // Whether a knob does anything with the section's settings as they are (else it is dimmed).
    function active(id) {
        const model = modelSetting.valid ? Math.round(modelSetting.value) : -1
        if (/_formant_(x|y|transpose|resonance|spread)$/.test(id))
            return model === 5
        if (/_blend_transpose$/.test(id))
            return model === 6
        const timing = Math.round((id.includes("_aux_") ? auxTimingSetting : timingSetting).value)
        if (editor.page === 4 || editor.page === 5) {
            if (id.endsWith("_frequency"))
                return timing === 0
            if (id.endsWith("_tempo"))
                return timing >= 1 && timing <= 3
            if (id.includes("_keytrack_"))
                return timing === 4
        }
        if (editor.page === 6 && editor.prefix !== "filter_fx_") {
            if (id.endsWith("_frequency"))
                return timing === 0
            if (id.endsWith("_tempo"))
                return timing !== 0
        }
        return true
    }

    // Why the last wavetable didn't load ("": it did).
    property string loadError: ""

    function showShapeMenu(from) {
        shapeMenu.clear()
        const oscillator = page === 0
        const index = choice
        if (oscillator) {
            shapeMenu.entry(qsTr("Load Wavetable…"), () => browseWavetable(index, 0))
            shapeMenu.entry(qsTr("Load Audio, Vocoded…"), () => browseWavetable(index, 1))
            shapeMenu.entry(qsTr("Load Audio, Pitched…"), () => browseWavetable(index, 2))
            shapeMenu.separator()
        }
        for (let i = oscillator ? 0 : 1; i < patch.shapeNames.length; ++i) {  // (an LFO's Init is its triangle)
            const shape = i
            shapeMenu.entry(patch.shapeNames[i], () => oscillator ? patch.setWavetableShape(index, shape)
                                                                  : patch.setLfoShape(index, shape))
        }
        shapeMenu.popup(from, 0, from.height)
    }
    function browseWavetable(oscillator, style) {
        wavetableDialog.oscillator = oscillator
        wavetableDialog.style = style
        wavetableDialog.open()
    }

    VitalPatchInfo {
        id: patch
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
    }

    DeviceParamMap {
        id: macros
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["macro_control_1", "macro_control_2", "macro_control_3", "macro_control_4"]
    }

    // --- Small parts ------------------------------------------------------------------------------

    // A section's switch: a dot, lit in the accent while on; click to switch, right-click for its menu.
    component Power: Item {
        id: power
        property string paramId
        readonly property DeviceParam param: powerParam
        property string tooltip: ""
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
            border.color: power.lit ? Theme.accent : Theme.textDisabled
            border.width: 1.5
            Rectangle {
                anchors.centerIn: parent
                width: 6
                height: 6
                radius: 3
                color: power.lit ? Theme.accent : "transparent"
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
        ToolTip.visible: tooltip !== "" && powerHover.hovered
        ToolTip.delay: 700
        ToolTip.text: tooltip
    }

    // A list in a panel's header or footer: its caption dim, its value's name (elided) and an arrow, on the
    // panel's well; a click lists its names (one undo step each), right-click its menu.
    component Choice: Item {
        id: chooser
        property string paramId
        readonly property DeviceParam param: chooserParam
        objectName: visible && paramId !== "" ? "param_" + paramId : ""
        property string caption: ""
        property string tooltip: ""
        // Its names (a list's), or each of its whole values as the device shows it.
        readonly property var names: {
            if (!param || !param.valid)
                return []
            if (param.labels.length > 0)
                return param.labels
            const all = []
            for (let v = Math.round(param.minimum); v <= Math.round(param.maximum); ++v)
                all.push(param.format(v))
            return all
        }
        readonly property int index: param ? (param.labels.length > 0 ? param.index
                                                                       : Math.round(param.value - param.minimum)) : 0
        height: editor.footerHeight

        DeviceParam {
            id: chooserParam
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            paramId: chooser.paramId
        }
        Rectangle {
            anchors.fill: parent
            radius: 2
            color: chooserArea.containsMouse ? Qt.lighter(Theme.meterBg, 1.6) : Theme.meterBg
            border.color: Qt.rgba(1, 1, 1, 0.07)
        }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.right: arrow.left
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                id: captionText
                visible: chooser.caption !== ""
                text: chooser.caption
                color: Theme.textDim
                font: Theme.uiFont(7)
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                width: parent.width - (captionText.visible ? captionText.width + parent.spacing : 0)
                text: chooser.names[chooser.index] || ""
                elide: Text.ElideRight
                color: Theme.text
                font: Theme.uiFont(8)
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Icon {
            id: arrow
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            name: "fold"
            size: 7
            color: Theme.textDim
        }
        MouseArea {
            id: chooserArea
            anchors.fill: parent
            hoverEnabled: true
            onPressed: if (chooser.param) chooser.param.touch()
            onClicked: chooserMenu.show()
        }
        ParamArea {
            anchors.fill: parent
            param: chooser.param
        }
        DynamicMenu {
            id: chooserMenu
            function show() {
                clear()
                for (let i = 0; i < chooser.names.length; ++i) {
                    const index = i
                    const value = chooser.param.labels.length > 0 ? index : Math.round(chooser.param.minimum) + index
                    entry(chooser.names[i], () => chooser.param.set(value), chooser.index === i)
                }
                popup(chooser, 0, chooser.height)
            }
        }
        ToolTip.visible: tooltip !== "" && chooserArea.containsMouse
        ToolTip.delay: 700
        ToolTip.text: tooltip
    }

    // A switch in a panel's footer, its name lit while on.
    component Toggle: ParamButton {
        id: toggle
        property string paramId
        objectName: visible && paramId !== "" ? "param_" + paramId : ""
        height: editor.footerHeight
        font.pointSize: 7
        param: toggleParam
        DeviceParam {
            id: toggleParam
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            paramId: toggle.paramId
        }
    }

    // --- The side column: the patch's screen, the page's choices, the macros -----------------------

    Item {
        id: side
        objectName: "side"
        x: 8
        y: 6
        width: editor.sideWidth
        height: editor.height - 12

        Rectangle {
            id: screen
            objectName: "presetScreen"
            width: parent.width
            height: 36
            radius: 3
            gradient: Gradient {
                GradientStop { position: 0; color: "#1b1e25" }
                GradientStop { position: 1; color: "#0f1115" }
            }
            border.color: Qt.rgba(1, 1, 1, 0.07)
            clip: true

            VitalScope {
                anchors.fill: parent
                mode: "badge"
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
            }
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                Text {
                    objectName: "presetName"
                    width: parent.width
                    text: patch.presetName !== "" ? patch.presetName : qsTr("Init")
                    elide: Text.ElideRight
                    color: Theme.text
                    font: Theme.uiFont(9, true)
                }
                Text {
                    width: parent.width
                    text: patch.author !== "" ? patch.author : qsTr("Vital patch")
                    elide: Text.ElideRight
                    color: Theme.textDim
                    font: Theme.uiFont(7)
                }
            }
            HoverHandler {
                id: nameHover
            }
            ToolTip.visible: nameHover.hovered
            ToolTip.delay: 700
            ToolTip.text: qsTr("The patch: load or save Vital presets (.vital) from the device's menu, or drop one here")
        }

        Grid {
            objectName: "choices"
            anchors.top: screen.bottom
            anchors.topMargin: 6
            visible: editor.page !== 6
            columns: 4
            spacing: 3

            Repeater {
                model: editor.page !== 6 ? editor.choices : []
                RoleButton {
                    required property string modelData
                    required property int index
                    objectName: "choice_" + index
                    role: "small"
                    text: modelData
                    font.pointSize: 8
                    width: (editor.sideWidth - 9) / 4
                    height: 18
                    checkable: false
                    checked: editor.choice === index
                    onClicked: editor.choose(index)
                }
            }
        }

        // The macros, named as the preset names them: two by two.
        Grid {
            id: macroGrid
            anchors.bottom: parent.bottom
            width: parent.width
            columns: 2
            rowSpacing: 2
            Repeater {
                model: 4
                Column {
                    id: macro
                    required property int index
                    readonly property string paramId: "macro_control_" + (index + 1)
                    readonly property DeviceParam param: macros.get(paramId)
                    objectName: "param_" + paramId
                    width: editor.sideWidth / 2
                    spacing: 0
                    ParamKnob {
                        anchors.horizontalCenter: parent.horizontalCenter
                        size: 20
                        param: macro.param
                        color: Theme.modulation
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: patch.macroNames.length > macro.index ? patch.macroNames[macro.index]
                                                                    : qsTr("Macro %1").arg(macro.index + 1)
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font: Theme.uiFont(7)
                    }
                }
            }
        }
    }

    DynamicMenu {
        id: shapeMenu
    }

    FileDialog {
        id: wavetableDialog

        property int oscillator: 0
        property int style: 0

        title: style === 0 ? qsTr("Load Wavetable") : qsTr("Load Audio as Wavetable")
        nameFilters: style === 0 ? [qsTr("Wavetables (*.wav *.vitaltable)"),
                                    qsTr("Audio Files (*.wav *.wave *.flac *.mp3 *.aif *.aiff *.ogg)")]
                                 : [qsTr("Audio Files (*.wav *.wave *.flac *.mp3 *.aif *.aiff *.ogg)")]
        onAccepted: editor.loadError = patch.loadWavetable(oscillator, FileUrls.localPath(selectedFile), style)
    }

    // --- The panel: the section drawn, under its header ----------------------------------------------

    Item {
        id: panel
        objectName: "panel"
        x: editor.panelX
        y: 6
        width: editor.panelWidth
        height: editor.height - 12
        visible: editor.page !== 7

        // The header: the switch, the section's name, what it is and where it goes.
        Item {
            id: header
            width: parent.width
            height: editor.headerHeight

            Power {
                id: power
                objectName: "power"
                visible: editor.hasSwitch
                anchors.verticalCenter: parent.verticalCenter
                paramId: editor.prefix + "on"
                tooltip: qsTr("Switch %1 on or off").arg(editor.sectionName)
            }
            Text {
                id: title
                anchors.left: editor.hasSwitch ? power.right : parent.left
                anchors.leftMargin: editor.hasSwitch ? 4 : 1
                anchors.verticalCenter: parent.verticalCenter
                text: editor.title
                color: editor.hasSwitch && !power.lit ? Theme.textDim : Theme.text
                font.family: Theme.uiFont(8, true).family
                font.pointSize: 8
                font.bold: true
                font.letterSpacing: 0.6
            }
            // The oscillator's wavetable, the LFO's shape.
            Text {
                objectName: "shapeName"
                visible: editor.page === 0 || editor.page === 4
                anchors.left: title.right
                anchors.leftMargin: 8
                anchors.right: shapeButton.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: editor.page === 0 ? (patch.wavetableNames[editor.choice] || "")
                                        : (patch.lfoNames[editor.choice] || "")
                elide: Text.ElideRight
                color: Theme.accent
                font: Theme.uiFont(8)
            }
            RoleButton {
                id: shapeButton
                objectName: "shapeButton"
                visible: editor.page === 0 || editor.page === 4
                anchors.right: output.visible ? output.left : parent.right
                anchors.rightMargin: output.visible ? 4 : 0
                anchors.verticalCenter: parent.verticalCenter
                role: "small"
                text: editor.page === 0 ? qsTr("Wavetable…") : qsTr("Shape…")
                font.pointSize: 7
                height: editor.headerHeight
                tooltip: editor.page === 0 ? qsTr("Load a wavetable, or set a basic shape")
                                           : qsTr("Set the LFO's shape")
                onClicked: editor.showShapeMenu(shapeButton)
            }
            Choice {
                id: output
                visible: editor.page === 0 || editor.page === 1
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 74
                height: editor.headerHeight
                paramId: editor.prefix + "destination"
                tooltip: qsTr("Where it plays: into a filter, both, or straight out (or into the effects)")
            }
        }

        // The drawing.
        Item {
            id: drawing
            anchors.top: header.bottom
            anchors.topMargin: 4
            anchors.bottom: footer.visible ? footer.top : parent.bottom
            anchors.bottomMargin: footer.visible ? 4 : 0
            width: parent.width

            VitalWavetableGraph {
                objectName: "wavetableGraph"
                anchors.fill: parent
                visible: editor.page === 0
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                prefix: visible ? editor.prefix : "osc_1_"
                index: visible ? editor.choice : 0
            }
            VitalFilterGraph {
                objectName: "filterGraph"
                anchors.fill: parent
                visible: editor.page === 2
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                prefix: editor.page === 2 ? editor.prefix : "filter_1_"
            }
            VitalEnvelopeGraph {
                objectName: "envelopeGraph"
                anchors.fill: parent
                visible: editor.page === 3
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                prefix: visible ? editor.prefix : "env_1_"
                index: visible ? editor.choice : 0
            }
            VitalLfoGraph {
                objectName: "lfoGraph"
                anchors.fill: parent
                visible: editor.page === 4
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                prefix: visible ? editor.prefix : "lfo_1_"
                index: visible ? editor.choice : 0
            }
            VitalScope {
                id: voiceScope
                objectName: "scope"
                anchors.fill: parent
                visible: editor.page === 1 || editor.page === 5 || editor.page === 8
                mode: editor.page === 8 ? "spectrum" : "scope"
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
            }

            // The effects: a tile each, lit while on; the one clicked is shown in the grid.
            Grid {
                id: tiles
                objectName: "effects"
                visible: editor.page === 6
                anchors.fill: parent
                columns: 3
                spacing: 4
                readonly property real tileWidth: (width - 2 * spacing) / 3
                readonly property real tileHeight: (height - 2 * spacing) / 3

                Repeater {
                    model: editor.effects
                    Rectangle {
                        id: tile
                        required property var modelData
                        required property int index
                        readonly property bool on: tilePower.lit
                        readonly property bool shown: editor.page === 6 && editor.choice === index
                        objectName: "effect_" + modelData.key
                        width: tiles.tileWidth
                        height: tiles.tileHeight
                        radius: 3
                        gradient: Gradient {
                            GradientStop { position: 0; color: tile.on ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20) : "#1b1e25" }
                            GradientStop { position: 1; color: "#0f1115" }
                        }
                        border.color: tile.shown ? Theme.accent : Qt.rgba(1, 1, 1, tileArea.containsMouse ? 0.18 : 0.07)
                        border.width: tile.shown ? 1.5 : 1

                        MouseArea {
                            id: tileArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: editor.choose(tile.index)
                        }
                        Power {
                            id: tilePower
                            objectName: "power"
                            x: 4
                            anchors.verticalCenter: parent.verticalCenter
                            paramId: tile.modelData.key + "_on"
                            tooltip: qsTr("Switch the %1 on or off").arg(tile.modelData.name)
                        }
                        Text {
                            x: 22
                            width: parent.width - 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: tile.modelData.name
                            elide: Text.ElideRight
                            color: tile.on ? Theme.text : Theme.textDim
                            font: Theme.uiFont(8, tile.shown)
                        }
                    }
                }
            }
        }

        // The footer: the section's lists and switches.
        Row {
            id: footer
            objectName: "footer"
            anchors.bottom: parent.bottom
            width: parent.width
            height: editor.footerHeight
            spacing: 4
            visible: editor.page === 0 || editor.page === 1 || editor.page === 2 || editor.page === 4 || editor.page === 5

            // Oscillator: the spectral morph, the warp, the unison stack.
            Choice {
                visible: editor.page === 0
                width: (footer.width - 2 * footer.spacing) / 3
                caption: qsTr("Morph")
                paramId: editor.prefix + "spectral_morph_type"
                tooltip: qsTr("How the spectrum is reshaped (the Morph knobs)")
            }
            Choice {
                visible: editor.page === 0
                width: (footer.width - 2 * footer.spacing) / 3
                caption: qsTr("Warp")
                paramId: editor.prefix + "distortion_type"
                tooltip: qsTr("How the wave is warped (the Warp knobs)")
            }
            Choice {
                visible: editor.page === 0
                width: (footer.width - 2 * footer.spacing) / 3
                caption: qsTr("Stack")
                paramId: editor.prefix + "stack_style"
                tooltip: qsTr("How the unison voices are stacked")
            }
            // Sample.
            Repeater {
                model: editor.page === 1 ? [["sample_loop", qsTr("Loop")], ["sample_bounce", qsTr("Bounce")],
                                            ["sample_keytrack", qsTr("Key Track")],
                                            ["sample_random_phase", qsTr("Rnd Phase")]] : []
                Toggle {
                    required property var modelData
                    width: (footer.width - 3 * footer.spacing) / 4
                    paramId: modelData[0]
                    text: modelData[1]
                }
            }
            // Filter.
            Choice {
                visible: editor.page === 2
                width: (footer.width - footer.spacing) / 2
                caption: qsTr("Model")
                paramId: editor.prefix + "model"
            }
            Choice {
                visible: editor.page === 2
                width: (footer.width - footer.spacing) / 2
                caption: qsTr("Style")
                paramId: editor.prefix + "style"
            }
            // LFO.
            Choice {
                visible: editor.page === 4
                width: 92
                caption: qsTr("Mode")
                paramId: editor.prefix + "sync_type"
                tooltip: qsTr("When it starts again: on each note, free with the song, once (an envelope), or looping")
            }
            Choice {
                visible: editor.page === 4
                width: footer.width - 92 - 60 - 2 * footer.spacing
                caption: qsTr("Timing")
                paramId: editor.prefix + "sync"
                tooltip: qsTr("Its rate in seconds, in beats (straight, dotted or triplets), or following the key")
            }
            Toggle {
                visible: editor.page === 4
                width: 60
                paramId: editor.prefix + "smooth_mode"
                text: qsTr("Smooth")
            }
            // Random.
            Choice {
                visible: editor.page === 5
                width: 84
                caption: qsTr("Style")
                paramId: editor.prefix + "style"
            }
            Choice {
                visible: editor.page === 5
                width: footer.width - 84 - 50 - 2 * footer.spacing
                caption: qsTr("Timing")
                paramId: editor.prefix + "sync"
                tooltip: qsTr("Its rate in seconds, in beats (straight, dotted or triplets), or following the key")
            }
            Toggle {
                visible: editor.page === 5
                width: 50
                paramId: editor.prefix + "sync_type"
                text: qsTr("Sync")
                tooltip: qsTr("In step with the song, not started again by each note")
            }
        }

        Text {
            objectName: "loadError"
            visible: editor.loadError !== "" && editor.page === 0
            anchors.left: drawing.left
            anchors.right: drawing.right
            anchors.bottom: drawing.bottom
            anchors.margins: 5
            text: editor.loadError
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.textDim
            font: Theme.uiFont(7)
        }
    }

    // --- The grid -----------------------------------------------------------------------------------

    Column {
        id: grid
        objectName: "knobs"
        x: editor.gridX
        y: 6
        visible: editor.page !== 7
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
                        text: editor.compact(param.text)
                        cellWidth: span * editor.pageCellWidth + (span - 1) * editor.cellSpacing
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

    // --- The modulation matrix ----------------------------------------------------------------------

    Item {
        id: matrix
        objectName: "matrix"
        x: editor.panelX
        y: 6
        width: editor.gridX + editor.gridWidth - editor.panelX
        height: editor.height - 12
        visible: editor.page === 7

        Row {
            id: adding
            spacing: 6
            height: 22

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("MODULATION")
                color: Theme.text
                font.family: Theme.uiFont(8, true).family
                font.pointSize: 8
                font.bold: true
                font.letterSpacing: 0.6
            }
            ComboBox {
                id: source
                objectName: "source"
                width: 150
                focusPolicy: Qt.NoFocus
                model: patch.sources
                textRole: "name"
                valueRole: "id"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "→"
                color: Theme.modulation
                font: Theme.uiFont(10)
            }
            ComboBox {
                id: destination
                objectName: "destination"
                width: 260
                editable: true
                model: patch.destinations
                textRole: "name"
                valueRole: "id"
                onAccepted: {
                    const found = find(editText)
                    if (found >= 0)
                        currentIndex = found
                }
            }
            RoleButton {
                objectName: "addModulation"
                role: "small"
                text: qsTr("Add")
                height: 20
                anchors.verticalCenter: parent.verticalCenter
                enabled: patch.freeSlots > 0 && source.currentIndex >= 0 && destination.currentIndex >= 0
                tooltip: qsTr("Modulate the destination by the source (in the first free of 64 slots)")
                onClicked: patch.add(source.currentValue, destination.currentValue)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: patch.modulations.length === 0 ? qsTr("No modulations yet")
                                                     : qsTr("%n modulation(s), %1 slots free", "", patch.modulations.length)
                                                       .arg(patch.freeSlots)
                color: Theme.textDim
                font: Theme.uiFont(8)
            }
        }

        Rectangle {
            id: well
            anchors.top: adding.bottom
            anchors.topMargin: 6
            width: parent.width
            height: parent.height - adding.height - 6
            radius: 3
            gradient: Gradient {
                GradientStop { position: 0; color: "#1b1e25" }
                GradientStop { position: 1; color: "#0f1115" }
            }
            border.color: Qt.rgba(1, 1, 1, 0.07)

            Text {
                anchors.centerIn: parent
                visible: patch.modulations.length === 0
                text: qsTr("Choose a source and a destination, then Add")
                color: Theme.textDisabled
                font: Theme.uiFont(8)
            }

            ListView {
                id: list
                objectName: "modulations"
                anchors.fill: parent
                anchors.margins: 3
                clip: true
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds
                model: patch.modulations
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: modulation
                    required property var modelData
                    required property int index
                    readonly property int slot: modelData.slot
                    readonly property DeviceParam amountParam: m.get(patch.slotParam(slot, "amount"))
                    readonly property bool bypassed: m.get(patch.slotParam(slot, "bypass"))
                                                     ? m.get(patch.slotParam(slot, "bypass")).value >= 0.5 : false
                    objectName: "modulation_" + slot
                    width: list.width - 10
                    height: 24
                    radius: 2
                    color: index % 2 === 0 ? Qt.rgba(1, 1, 1, 0.035) : "transparent"
                    opacity: bypassed ? 0.5 : 1

                    DeviceParamMap {
                        id: m
                        trackId: editor.trackId
                        deviceId: editor.deviceId
                        ids: ["amount", "bipolar", "stereo", "bypass"].map(w => patch.slotParam(modulation.slot, w))
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        spacing: 8

                        // The source as a chip in the modulators' colour.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 120
                            height: 16
                            radius: 8
                            color: Qt.rgba(Theme.modulation.r, Theme.modulation.g, Theme.modulation.b, 0.18)
                            border.color: Qt.rgba(Theme.modulation.r, Theme.modulation.g, Theme.modulation.b, 0.5)
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 7
                                anchors.rightMargin: 7
                                verticalAlignment: Text.AlignVCenter
                                text: modulation.modelData.sourceName
                                elide: Text.ElideRight
                                color: Theme.text
                                font: Theme.uiFont(8)
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "→"
                            color: Theme.modulation
                            font: Theme.uiFont(9)
                        }
                        Text {
                            width: 250
                            anchors.verticalCenter: parent.verticalCenter
                            text: modulation.modelData.destinationName
                            elide: Text.ElideRight
                            color: Theme.text
                            font: Theme.uiFont(9)
                        }
                        ParamKnob {
                            objectName: "amount"
                            size: 20
                            anchors.verticalCenter: parent.verticalCenter
                            param: modulation.amountParam
                            color: Theme.modulation
                        }
                        // The amount as a bar from the middle, and its figure.
                        Item {
                            width: 90
                            height: parent.height
                            Rectangle {
                                id: track
                                anchors.verticalCenter: parent.verticalCenter
                                width: 52
                                height: 4
                                radius: 2
                                color: Theme.meterBg
                                Rectangle {
                                    readonly property real amount: modulation.amountParam
                                                                   ? Math.max(-1, Math.min(1, modulation.amountParam.value)) : 0
                                    x: amount >= 0 ? track.width / 2 : track.width / 2 * (1 + amount)
                                    width: Math.abs(amount) * track.width / 2
                                    height: parent.height
                                    radius: 2
                                    color: Theme.modulation
                                }
                            }
                            Text {
                                anchors.left: track.right
                                anchors.leftMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: modulation.amountParam ? modulation.amountParam.text : ""
                                color: Theme.text
                                font: Theme.uiFont(8)
                            }
                        }
                        ParamButton {
                            objectName: "bipolar"
                            anchors.verticalCenter: parent.verticalCenter
                            param: m.get(patch.slotParam(modulation.slot, "bipolar"))
                            text: qsTr("Bipolar")
                            tooltip: qsTr("Modulate either way from the destination's value, not only up")
                        }
                        ParamButton {
                            objectName: "stereo"
                            anchors.verticalCenter: parent.verticalCenter
                            param: m.get(patch.slotParam(modulation.slot, "stereo"))
                            text: qsTr("Stereo")
                            tooltip: qsTr("The right side modulated the opposite way")
                        }
                        ParamButton {
                            objectName: "bypass"
                            anchors.verticalCenter: parent.verticalCenter
                            param: m.get(patch.slotParam(modulation.slot, "bypass"))
                            text: qsTr("Bypass")
                            tooltip: qsTr("Switch this modulation off, keeping it")
                        }
                        RoleButton {
                            objectName: "remove"
                            role: "small"
                            text: "×"
                            width: 20
                            height: 18
                            anchors.verticalCenter: parent.verticalCenter
                            tooltip: qsTr("Remove this modulation")
                            onClicked: patch.remove(modulation.slot)
                        }
                    }
                }
            }
        }
    }
}
