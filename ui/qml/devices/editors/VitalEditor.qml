import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import SUBstation

// Vital's editor: its whole synth in nine pages, named in the device's title
// bar, laid out as Vital lays out its own window's sections.
//
// Osc (1, 2, 3), Sample, Filter (1, 2), Env (1-6), LFO (1-8), Random (1-4) and
// FX (its nine effects) each show their section's controls in two rows; the
// column on the left picks which oscillator, filter, envelope, LFO, random LFO
// or effect, and shows the loaded preset's name. On Osc it shows the
// oscillator's wavetable, and Wavetable… loads another (a .wav wavetable or
// Vital's .vitaltable; any audio, vocoded or pitched, as Vital loads it) or
// sets a basic shape; on LFO it shows the LFO's shape, and Shape… sets one. Matrix is the modulation
// matrix: a source and a destination chosen and added (the first free of
// Vital's 64 slots), then each modulation in a row: its amount, Bipolar,
// Stereo and Bypass, and × to remove it. Voice is everything else: volume,
// voices, glide, pitch bend, the macros (named as the preset names them), the
// wheels and oversampling.
//
// Every control is a DeviceParamKnob (or a list) bound to Vital's parameter:
// shown as Vital shows its value, set undoably, touched when pressed (so its
// automation shows), right-click for its menu. The matrix's edits go through
// the editor too (VitalPatchInfo), one undo step each. Vital's own presets
// load and save from the device's menu (Load Vital Preset…, Save Vital
// Preset…), or drop a .vital file on it. The page and each page's choice are
// view state (DeviceViews), kept while the frame is made again.
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
    readonly property real cellWidth: 66
    readonly property real cellSpacing: 6
    readonly property int columns: 12
    readonly property real selectorWidth: 112
    readonly property real knobsWidth: columns * cellWidth + (columns - 1) * cellSpacing

    implicitWidth: 8 + selectorWidth + 10 + knobsWidth + 8
    implicitHeight: 6 + knobRows.implicitHeight + 6

    // Each page's choices (the left column), and which is shown.
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

    // The page's two rows of controls: [parameter id, caption] each.
    function rowsOf(p, c) {
        const n = String(c + 1)
        const osc = "osc_" + n + "_"
        const filter = p === 6 ? "filter_fx_" : "filter_" + n + "_"
        switch (p) {
        case 0: return [
            [[osc + "on", qsTr("On")], [osc + "level", qsTr("Level")], [osc + "pan", qsTr("Pan")],
             [osc + "transpose", qsTr("Transpose")], [osc + "tune", qsTr("Tune")], [osc + "wave_frame", qsTr("Frame")],
             [osc + "spectral_morph_type", qsTr("Freq Morph")], [osc + "spectral_morph_amount", qsTr("Morph Amt")],
             [osc + "spectral_morph_spread", qsTr("Morph Spread")], [osc + "distortion_type", qsTr("Warp")],
             [osc + "distortion_amount", qsTr("Warp Amt")], [osc + "destination", qsTr("Output")]],
            [[osc + "unison_voices", qsTr("Unison")], [osc + "unison_detune", qsTr("Detune")],
             [osc + "detune_range", qsTr("Range")], [osc + "detune_power", qsTr("Detune Pwr")],
             [osc + "unison_blend", qsTr("Blend")], [osc + "stereo_spread", qsTr("Stereo")],
             [osc + "frame_spread", qsTr("Frame Spread")], [osc + "distortion_spread", qsTr("Warp Spread")],
             [osc + "distortion_phase", qsTr("Warp Phase")], [osc + "phase", qsTr("Phase")],
             [osc + "random_phase", qsTr("Random")], [osc + "stack_style", qsTr("Stack")]]]
        case 1: return [
            [["sample_on", qsTr("On")], ["sample_level", qsTr("Level")], ["sample_pan", qsTr("Pan")],
             ["sample_transpose", qsTr("Transpose")], ["sample_tune", qsTr("Tune")],
             ["sample_destination", qsTr("Output")]],
            [["sample_loop", qsTr("Loop")], ["sample_bounce", qsTr("Bounce")], ["sample_keytrack", qsTr("Key Track")],
             ["sample_random_phase", qsTr("Random Phase")]]]
        case 2: return filterRows(filter)
        case 3: {
            const env = "env_" + n + "_"
            return [
                [[env + "delay", qsTr("Delay")], [env + "attack", qsTr("Attack")], [env + "hold", qsTr("Hold")],
                 [env + "decay", qsTr("Decay")], [env + "sustain", qsTr("Sustain")], [env + "release", qsTr("Release")]],
                [[env + "attack_power", qsTr("Attack Curve")], [env + "decay_power", qsTr("Decay Curve")],
                 [env + "release_power", qsTr("Release Curve")]]]
        }
        case 4: {
            const lfo = "lfo_" + n + "_"
            return [
                [[lfo + "sync", qsTr("Sync")], [lfo + "frequency", qsTr("Rate")], [lfo + "tempo", qsTr("Tempo")],
                 [lfo + "sync_type", qsTr("Mode")], [lfo + "phase", qsTr("Phase")], [lfo + "stereo", qsTr("Stereo")]],
                [[lfo + "delay_time", qsTr("Delay")], [lfo + "fade_time", qsTr("Fade In")],
                 [lfo + "smooth_mode", qsTr("Smooth")], [lfo + "smooth_time", qsTr("Smooth Time")],
                 [lfo + "keytrack_transpose", qsTr("Transpose")], [lfo + "keytrack_tune", qsTr("Tune")]]]
        }
        case 5: {
            const random = "random_" + n + "_"
            return [
                [[random + "style", qsTr("Style")], [random + "sync", qsTr("Sync")], [random + "frequency", qsTr("Rate")],
                 [random + "tempo", qsTr("Tempo")], [random + "sync_type", qsTr("Mode")], [random + "stereo", qsTr("Stereo")]],
                [[random + "keytrack_transpose", qsTr("Transpose")], [random + "keytrack_tune", qsTr("Tune")]]]
        }
        case 6: return effectRows(effects[c].key)
        case 8: {
            const macro = i => ["macro_control_" + (i + 1), patch.macroNames.length > i ? patch.macroNames[i]
                                                                                          : qsTr("Macro %1").arg(i + 1)]
            return [
                [["volume", qsTr("Volume")], ["polyphony", qsTr("Voices")], ["voice_priority", qsTr("Priority")],
                 ["voice_override", qsTr("Override")], ["legato", qsTr("Legato")], ["velocity_track", qsTr("Velocity")],
                 ["pitch_bend_range", qsTr("Bend Range")], ["voice_tune", qsTr("Tune")],
                 ["voice_transpose", qsTr("Transpose")], ["stereo_routing", qsTr("Stereo")],
                 ["stereo_mode", qsTr("Stereo Mode")], ["oversampling", qsTr("Oversampling")]],
                [["portamento_time", qsTr("Glide")], ["portamento_slope", qsTr("Glide Slope")],
                 ["portamento_force", qsTr("Always Glide")], ["portamento_scale", qsTr("Octave Scale")],
                 macro(0), macro(1), macro(2), macro(3), ["mod_wheel", qsTr("Mod Wheel")],
                 ["pitch_wheel", qsTr("Pitch Wheel")], ["voice_amplitude", qsTr("Amplitude")]]]
        }
        default: return [[], []]
        }
    }
    function filterRows(f) {
        return [
            [[f + "on", qsTr("On")], [f + "model", qsTr("Model")], [f + "style", qsTr("Style")],
             [f + "cutoff", qsTr("Cutoff")], [f + "resonance", qsTr("Resonance")], [f + "drive", qsTr("Drive")],
             [f + "blend", qsTr("Blend")], [f + "keytrack", qsTr("Key Track")], [f + "mix", qsTr("Mix")]],
            [[f + "blend_transpose", qsTr("Comb Offset")], [f + "formant_x", qsTr("Formant X")],
             [f + "formant_y", qsTr("Formant Y")], [f + "formant_transpose", qsTr("Formant Shift")],
             [f + "formant_resonance", qsTr("Formant Res")], [f + "formant_spread", qsTr("Formant Spread")]]]
    }
    function effectRows(key) {
        const e = key + "_"
        switch (key) {
        case "chorus": return [
            [[e + "on", qsTr("On")], [e + "dry_wet", qsTr("Mix")], [e + "voices", qsTr("Voices")],
             [e + "sync", qsTr("Sync")], [e + "frequency", qsTr("Rate")], [e + "tempo", qsTr("Tempo")],
             [e + "mod_depth", qsTr("Depth")], [e + "feedback", qsTr("Feedback")]],
            [[e + "delay_1", qsTr("Delay 1")], [e + "delay_2", qsTr("Delay 2")], [e + "cutoff", qsTr("Cutoff")],
             [e + "spread", qsTr("Spread")]]]
        case "compressor": return [
            [[e + "on", qsTr("On")], [e + "mix", qsTr("Mix")], [e + "attack", qsTr("Attack")],
             [e + "release", qsTr("Release")], [e + "enabled_bands", qsTr("Bands")], [e + "low_gain", qsTr("Low Gain")],
             [e + "band_gain", qsTr("Mid Gain")], [e + "high_gain", qsTr("High Gain")]],
            [[e + "low_upper_threshold", qsTr("Low Upper")], [e + "low_upper_ratio", qsTr("Low Ratio")],
             [e + "low_lower_threshold", qsTr("Low Lower")], [e + "low_lower_ratio", qsTr("Low Exp")],
             [e + "band_upper_threshold", qsTr("Mid Upper")], [e + "band_upper_ratio", qsTr("Mid Ratio")],
             [e + "band_lower_threshold", qsTr("Mid Lower")], [e + "band_lower_ratio", qsTr("Mid Exp")],
             [e + "high_upper_threshold", qsTr("High Upper")], [e + "high_upper_ratio", qsTr("High Ratio")],
             [e + "high_lower_threshold", qsTr("High Lower")], [e + "high_lower_ratio", qsTr("High Exp")]]]
        case "delay": return [
            [[e + "on", qsTr("On")], [e + "dry_wet", qsTr("Mix")], [e + "style", qsTr("Style")],
             [e + "feedback", qsTr("Feedback")], [e + "filter_cutoff", qsTr("Cutoff")],
             [e + "filter_spread", qsTr("Spread")]],
            [[e + "sync", qsTr("Sync")], [e + "frequency", qsTr("Rate")], [e + "tempo", qsTr("Tempo")],
             [e + "aux_sync", qsTr("Sync 2")], [e + "aux_frequency", qsTr("Rate 2")], [e + "aux_tempo", qsTr("Tempo 2")]]]
        case "distortion": return [
            [[e + "on", qsTr("On")], [e + "type", qsTr("Type")], [e + "drive", qsTr("Drive")], [e + "mix", qsTr("Mix")]],
            [[e + "filter_order", qsTr("Filter")], [e + "filter_cutoff", qsTr("Cutoff")],
             [e + "filter_resonance", qsTr("Resonance")], [e + "filter_blend", qsTr("Blend")]]]
        case "eq": return [
            [[e + "on", qsTr("On")], [e + "low_mode", qsTr("Low")], [e + "low_cutoff", qsTr("Low Freq")],
             [e + "low_gain", qsTr("Low Gain")], [e + "low_resonance", qsTr("Low Res")], [e + "band_mode", qsTr("Band")],
             [e + "band_cutoff", qsTr("Band Freq")], [e + "band_gain", qsTr("Band Gain")],
             [e + "band_resonance", qsTr("Band Res")]],
            [[e + "high_mode", qsTr("High")], [e + "high_cutoff", qsTr("High Freq")], [e + "high_gain", qsTr("High Gain")],
             [e + "high_resonance", qsTr("High Res")]]]
        case "filter_fx": return filterRows("filter_fx_")
        case "flanger": return [
            [[e + "on", qsTr("On")], [e + "dry_wet", qsTr("Mix")], [e + "sync", qsTr("Sync")],
             [e + "frequency", qsTr("Rate")], [e + "tempo", qsTr("Tempo")]],
            [[e + "center", qsTr("Center")], [e + "feedback", qsTr("Feedback")], [e + "mod_depth", qsTr("Depth")],
             [e + "phase_offset", qsTr("Offset")]]]
        case "phaser": return [
            [[e + "on", qsTr("On")], [e + "dry_wet", qsTr("Mix")], [e + "sync", qsTr("Sync")],
             [e + "frequency", qsTr("Rate")], [e + "tempo", qsTr("Tempo")]],
            [[e + "center", qsTr("Center")], [e + "feedback", qsTr("Feedback")], [e + "mod_depth", qsTr("Depth")],
             [e + "phase_offset", qsTr("Offset")], [e + "blend", qsTr("Blend")]]]
        case "reverb": return [
            [[e + "on", qsTr("On")], [e + "dry_wet", qsTr("Mix")], [e + "size", qsTr("Size")],
             [e + "decay_time", qsTr("Decay")], [e + "delay", qsTr("Delay")], [e + "pre_low_cutoff", qsTr("Pre Low")],
             [e + "pre_high_cutoff", qsTr("Pre High")]],
            [[e + "low_shelf_cutoff", qsTr("Low Freq")], [e + "low_shelf_gain", qsTr("Low Gain")],
             [e + "high_shelf_cutoff", qsTr("High Freq")], [e + "high_shelf_gain", qsTr("High Gain")],
             [e + "chorus_amount", qsTr("Chorus")], [e + "chorus_frequency", qsTr("Chorus Rate")]]]
        default: return [[], []]
        }
    }
    readonly property var rows: rowsOf(page, choice)
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

    // --- The left column: the preset's name, and the page's choices ---------------------------------

    Column {
        id: side
        objectName: "side"
        x: 8
        y: 6
        width: editor.selectorWidth
        spacing: 6

        Text {
            objectName: "presetName"
            width: parent.width
            text: patch.presetName !== "" ? patch.presetName : qsTr("Init")
            elide: Text.ElideRight
            color: Theme.text
            font: Theme.uiFont(9)

            HoverHandler {
                id: nameHover
            }
            ToolTip.visible: nameHover.hovered
            ToolTip.delay: 700
            ToolTip.text: qsTr("The patch: load or save Vital presets (.vital) from the device's menu, or drop one here")
        }

        Grid {
            objectName: "choices"
            columns: editor.choices.length > 4 ? 3 : editor.choices.length
            spacing: 3

            Repeater {
                model: editor.choices
                RoleButton {
                    required property string modelData
                    required property int index
                    objectName: "choice_" + index
                    role: "small"
                    text: modelData
                    font.pointSize: 8
                    width: editor.choices.length > 4 ? (editor.selectorWidth - 6) / 3 : 24
                    height: 18
                    checkable: false
                    checked: editor.choice === index
                    onClicked: editor.choose(index)
                }
            }
        }

        // The oscillator's wavetable, the LFO's shape.
        Text {
            objectName: "shapeName"
            visible: editor.page === 0 || editor.page === 4
            width: parent.width
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
            role: "small"
            text: editor.page === 0 ? qsTr("Wavetable…") : qsTr("Shape…")
            width: parent.width
            height: 18
            tooltip: editor.page === 0 ? qsTr("Load a wavetable, or set a basic shape")
                                       : qsTr("Set the LFO's shape")
            onClicked: editor.showShapeMenu(shapeButton)
        }
        Text {
            objectName: "loadError"
            visible: editor.loadError !== "" && editor.page === 0
            width: parent.width
            text: editor.loadError
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.textDim
            font: Theme.uiFont(7)
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

    // --- The controls -----------------------------------------------------------------------------

    Column {
        id: knobRows
        objectName: "knobs"
        x: side.x + editor.selectorWidth + 10
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
                        objectName: "param_" + modelData[0]
                        trackId: editor.trackId
                        deviceId: editor.deviceId
                        paramId: modelData[0]
                        name: modelData[1]
                        cellWidth: editor.cellWidth
                    }
                }
            }
        }
    }

    // (How tall a row of knobs is, for the matrix's page to match.)
    DeviceParamKnob {
        id: probe
        visible: false
        trackId: editor.trackId
        deviceId: editor.deviceId
        paramId: "volume"
        cellWidth: editor.cellWidth
    }

    // --- The modulation matrix --------------------------------------------------------------------

    Item {
        id: matrix
        objectName: "matrix"
        x: knobRows.x
        y: 6
        width: editor.knobsWidth
        height: editor.height - 12
        visible: editor.page === 7

        Row {
            id: adding
            spacing: 6
            height: 22

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
                color: Theme.textDim
                font: Theme.uiFont(9)
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
                                                     : qsTr("%n modulation(s)", "", patch.modulations.length)
                color: Theme.textDim
                font: Theme.uiFont(8)
            }
        }

        ListView {
            id: list
            objectName: "modulations"
            anchors.top: adding.bottom
            anchors.topMargin: 6
            width: parent.width
            height: parent.height - adding.height - 6
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: patch.modulations
            ScrollBar.vertical: ScrollBar {}

            delegate: Row {
                id: modulation
                required property var modelData
                readonly property int slot: modelData.slot
                objectName: "modulation_" + slot
                spacing: 8
                height: 24

                DeviceParamMap {
                    id: p
                    trackId: editor.trackId
                    deviceId: editor.deviceId
                    ids: ["amount", "bipolar", "stereo", "bypass"].map(w => patch.slotParam(modulation.slot, w))
                }

                Text {
                    width: 330
                    anchors.verticalCenter: parent.verticalCenter
                    text: modulation.modelData.sourceName + "  →  " + modulation.modelData.destinationName
                    elide: Text.ElideRight
                    color: Theme.text
                    font: Theme.uiFont(9)
                }
                ParamKnob {
                    objectName: "amount"
                    size: 20
                    anchors.verticalCenter: parent.verticalCenter
                    param: p.get(patch.slotParam(modulation.slot, "amount"))
                }
                Text {
                    width: 56
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property DeviceParam amount: p.get(patch.slotParam(modulation.slot, "amount"))
                    text: amount ? amount.text : ""
                    color: Theme.text
                    font: Theme.uiFont(8)
                }
                ParamButton {
                    objectName: "bipolar"
                    anchors.verticalCenter: parent.verticalCenter
                    param: p.get(patch.slotParam(modulation.slot, "bipolar"))
                    text: qsTr("Bipolar")
                    tooltip: qsTr("Modulate either way from the destination's value, not only up")
                }
                ParamButton {
                    objectName: "stereo"
                    anchors.verticalCenter: parent.verticalCenter
                    param: p.get(patch.slotParam(modulation.slot, "stereo"))
                    text: qsTr("Stereo")
                    tooltip: qsTr("The right side modulated the opposite way")
                }
                ParamButton {
                    objectName: "bypass"
                    anchors.verticalCenter: parent.verticalCenter
                    param: p.get(patch.slotParam(modulation.slot, "bypass"))
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
