import QtQuick
import QtQuick.Controls
import SUBstation

// The Auto Filter's editor, laid out as Live 12.2's:
//
// - at the left, the Sidechain fold: opened, the envelope's Attack and Release beside it (whether it is open is
//   view state, by device: DeviceViews);
// - Frequency (the large knob), the filter type (its icon), the slope (12, 24; the Morph type's Morph in their
//   place) and Resonance;
// - the display (AutoFilterGraph: the response that plays, sweeping as the envelope and the LFO move it; drag for
//   Frequency and Resonance), the LFO's strip along its bottom (Phase or Spin and its value, Offset, Quantize and
//   its steps), and under it LFO Amount, Rate (or the synced Rate), the waveform, the LFO's Morph, its Mode (Rate
//   or Sync) and the Envelope's amount;
// - at the right, Drive (dimmed on Clean), the circuit, Clip, Output and Dry/Wet.
//
// Its animations are subtle: the display's (see AutoFilterGraph.h), and the controls that do nothing fading out
// and back.
Item {
    id: editor

    required property string trackId
    required property string deviceId
    readonly property alias graph: graph

    readonly property int column: 56   // the left and right columns
    readonly property int cell: 50     // the knobs under the display
    readonly property int stripHeight: 16
    readonly property int foldWidth: 14
    readonly property int gap: 1

    readonly property bool synced: on("lfo_sync")
    readonly property bool spinning: on("spin_on")
    readonly property bool morphing: p.get("type") ? p.get("type").index === 4 : false
    readonly property bool driven: p.get("circuit") ? p.get("circuit").index !== 0 : false
    readonly property bool lfoOn: p.get("lfo_amount") ? p.get("lfo_amount").value > 0 : false
    readonly property bool quantized: p.get("lfo_quantize") ? p.get("lfo_quantize").index === 1 : false
    // The Sidechain fold opened (not saved: how the device is looked at).
    readonly property bool expanded: DeviceViews.value(deviceId, "expanded", false)

    implicitWidth: foldWidth + (expanded ? 4 + cell : 0) + 6 + column + 8 + centre.width + 8 + column + 6
    implicitHeight: 4 + Math.max(left.implicitHeight, right.implicitHeight,
                                 graph.implicitHeight + stripHeight + 2 + lower.implicitHeight) + 4

    function on(id) {
        const param = p.get(id)
        return param ? param.value >= 0.5 : false
    }

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["type", "circuit", "slope", "freq", "resonance", "drive", "morph", "env_amount", "env_attack",
              "env_release", "lfo_amount", "lfo_wave", "lfo_sync", "lfo_freq", "lfo_rate", "spin_on", "phase", "spin",
              "lfo_offset", "output", "mix", "lfo_morph", "lfo_quantize", "lfo_steps", "soft_clip"]
    }

    // A labelled knob of this editor's.
    component CellKnob: EditorKnob {
        width: editor.cell
        size: 28
    }

    // --- The Sidechain fold --------------------------------------------------------------------------------

    Rectangle {
        id: fold
        objectName: "sidechainFold"
        width: editor.foldWidth
        height: editor.height
        color: Theme.panelAlt

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 4
            name: "fold"
            size: 8
            checked: !editor.expanded
        }
        Text {
            anchors.centerIn: parent
            rotation: -90
            text: qsTr("Sidechain")
            color: Theme.textDim
            font: Theme.uiFont(8)
        }
        MouseArea {
            anchors.fill: parent
            onClicked: DeviceViews.setValue(editor.deviceId, "expanded", !editor.expanded)
        }
        HoverHandler {
            id: foldHover
        }
        ToolTip.visible: foldHover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("The envelope's Attack and Release (choose a sidechain in the title bar to have the "
                           + "envelope follow it)")
    }

    Column {
        id: extra
        x: fold.width + 4
        y: 4
        spacing: editor.gap
        visible: editor.expanded
        opacity: editor.expanded ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: 160
            }
        }

        CellKnob {
            objectName: "envAttack"
            param: p.get("env_attack")
            title: qsTr("Attack")
            tooltip: qsTr("Envelope Attack: how fast it follows the level up")
        }
        CellKnob {
            objectName: "envRelease"
            param: p.get("env_release")
            title: qsTr("Release")
            tooltip: qsTr("Envelope Release: how fast it falls back")
        }
    }

    // --- Frequency, type, slope, Resonance -----------------------------------------------------------------

    Column {
        id: left
        x: fold.width + (editor.expanded ? 4 + editor.cell : 0) + 6
        y: 4
        width: editor.column
        spacing: editor.gap

        EditorKnob {
            id: freqKnob
            objectName: "freq"
            width: parent.width
            size: 32
            param: p.get("freq")
            title: qsTr("Freq")
            tooltip: qsTr("Frequency: the filter's cutoff, where the envelope and the LFO move it from")
        }
        ParamChoice {
            objectName: "type"
            width: parent.width
            param: p.get("type")
            icons: ["filter_lowpass", "filter_highpass", "filter_bandpass", "filter_notch", ""]
            iconOnly: !editor.morphing
            tooltip: qsTr("Filter type: Lowpass, Highpass, Bandpass, Notch, or Morph (Morph sweeps from one "
                          + "to the next)")
        }
        Item {
            width: parent.width
            height: 16

            Row {
                objectName: "slopeRow"
                spacing: editor.gap
                visible: !editor.morphing

                Repeater {
                    model: 2
                    ParamButton {
                        required property int index
                        objectName: "slope" + (index === 0 ? "12" : "24")
                        width: (editor.column - editor.gap) / 2
                        param: p.get("slope")
                        choice: index
                        text: index === 0 ? "12" : "24"
                        tooltip: qsTr("Slope: 12 or 24 dB an octave")
                    }
                }
            }
            ParamBox {
                objectName: "morph"
                anchors.fill: parent
                visible: editor.morphing
                param: p.get("morph")
                step: 0.1
                decimals: 1
                formatter: v => v.toFixed(1) + " %"
                sampleText: "100.0 %"
                tooltip: qsTr("Morph: Lowpass, Bandpass, Highpass, Notch and back (it plays at the slope set "
                              + "before)")
            }
        }
        EditorKnob {
            id: resKnob
            objectName: "resonance"
            width: parent.width
            size: 26
            param: p.get("resonance")
            title: qsTr("Res")
            tooltip: qsTr("Resonance: a peak at the cutoff; the saturating circuits ring and growl past 100 %")
        }
    }

    // --- The display, its LFO strip and the knobs under it -------------------------------------------------

    Column {
        id: centre
        x: left.x + left.width + 8
        y: 4
        width: lower.implicitWidth
        spacing: 2

        Rectangle {
            width: parent.width
            height: editor.height - 8 - lower.height - parent.spacing
            color: Theme.meterBg
            border.color: Theme.border

            AutoFilterGraph {
                id: graph
                objectName: "autoFilterGraph"
                x: 1
                y: 1
                width: parent.width - 2
                height: parent.height - 2 - editor.stripHeight
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId

                HoverHandler {
                    id: graphHover
                }
                ToolTip.visible: graphHover.hovered && !graph.dragging
                ToolTip.delay: 700
                ToolTip.text: qsTr("Drag: Frequency across, Resonance up and down; double-click to reset them. "
                                   + "The curve sweeps as the envelope and the LFO move it")
            }

            // The LFO's strip: Phase or Spin and its value, Offset, Quantize and its steps.
            Row {
                objectName: "lfoStrip"
                x: 4
                y: parent.height - editor.stripHeight - 1
                height: editor.stripHeight
                spacing: 4
                opacity: editor.lfoOn ? 1 : 0.55
                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                    }
                }

                EditorCaption {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("LFO")
                    color: Theme.text
                }
                ParamButton {
                    objectName: "spinOn"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34
                    height: 13
                    param: p.get("spin_on")
                    text: editor.spinning ? qsTr("Spin") : qsTr("Phase")
                    tooltip: qsTr("Phase: the right side's LFO a set angle ahead of the left's; Spin: running "
                                  + "faster than it")
                }
                ParamBox {
                    objectName: "phase"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 13
                    param: editor.spinning ? p.get("spin") : p.get("phase")
                    step: 0.1
                    decimals: 1
                    formatter: editor.spinning ? (v => v.toFixed(1) + " %") : (v => v.toFixed(1) + "°")
                    sampleText: "360.0°"
                    tooltip: editor.spinning ? qsTr("Spin: the right side's LFO runs this much faster")
                                             : qsTr("Phase: the right side's LFO this far ahead of the left's")
                }
                EditorCaption {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "↻"
                }
                ParamBox {
                    objectName: "lfoOffset"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 13
                    param: p.get("lfo_offset")
                    step: 0.1
                    decimals: 1
                    formatter: v => v.toFixed(1) + "°"
                    sampleText: "360.0°"
                    tooltip: qsTr("Offset: turns the LFO's cycle (both sides)")
                }
                ParamChoice {
                    objectName: "lfoQuantize"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44
                    height: 13
                    param: p.get("lfo_quantize")
                    tooltip: qsTr("Quantize: the LFO's value in steps")
                }
                ParamBox {
                    objectName: "lfoSteps"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 13
                    param: p.get("lfo_steps")
                    step: 1
                    decimals: 0
                    formatter: v => v.toFixed(0)
                    sampleText: "64"
                    opacity: editor.quantized ? 1 : 0.55
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 120
                        }
                    }
                    tooltip: qsTr("Steps: how many levels the quantized LFO has")
                }
            }
        }

        Row {
            id: lower
            spacing: 4

            CellKnob {
                objectName: "lfoAmount"
                param: p.get("lfo_amount")
                title: qsTr("LFO Amt")
                tooltip: qsTr("LFO Amount: how far the LFO sweeps the cutoff either way; at 100 % four octaves")
            }
            CellKnob {
                id: rateKnob
                objectName: "lfoRate"
                param: editor.synced ? p.get("lfo_rate") : p.get("lfo_freq")
                title: qsTr("Rate")
                opacity: editor.lfoOn ? 1 : 0.55
                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                    }
                }
                tooltip: editor.synced ? qsTr("LFO Rate: a cycle every so much of a bar, following the song")
                                       : qsTr("LFO Rate: cycles a second")
            }
            // Wave, Morph and Mode, a caption before each.
            Grid {
                anchors.verticalCenter: parent.verticalCenter
                columns: 2
                columnSpacing: 4
                rowSpacing: editor.gap
                verticalItemAlignment: Grid.AlignVCenter
                opacity: editor.lfoOn ? 1 : 0.55
                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                    }
                }

                EditorCaption {
                    text: qsTr("Wave")
                }
                ParamChoice {
                    objectName: "lfoWave"
                    width: 52
                    param: p.get("lfo_wave")
                    icons: ["wave_sine", "wave_triangle", "wave_saw_up", "wave_saw_down", "wave_square",
                            "wave_random", "wave_random"]
                    iconOnly: true
                    tooltip: qsTr("The LFO's waveform: Sine, Triangle, Saw Up, Saw Down, Square, S&H (a random "
                                  + "step each cycle) or Smooth (random, gliding)")
                }
                EditorCaption {
                    text: qsTr("Morph")
                }
                ParamBox {
                    objectName: "lfoMorph"
                    width: 52
                    height: 16
                    param: p.get("lfo_morph")
                    step: 0.1
                    decimals: 1
                    formatter: v => v.toFixed(1) + " %"
                    sampleText: "-100.0 %"
                    tooltip: qsTr("LFO Morph: leans the waveform's cycle one way or the other")
                }
                EditorCaption {
                    text: qsTr("Mode")
                }
                ParamChoice {
                    objectName: "lfoSync"
                    width: 52
                    param: p.get("lfo_sync")
                    labels: [qsTr("Rate"), qsTr("Sync")]
                    tooltip: qsTr("Mode: the LFO's Rate in cycles a second, or synced to the song in note values")
                }
            }
            CellKnob {
                id: envKnob
                objectName: "envAmount"
                width: 56
                param: p.get("env_amount")
                title: qsTr("Envelope")
                knob.bipolar: true
                tooltip: qsTr("Envelope Amount: how far the input's level (or the sidechain's) moves the cutoff, "
                              + "up or down; at 100 % six octaves. Its Attack and Release are in the Sidechain "
                              + "fold, at the left")
            }
        }
    }

    // --- Drive, circuit, Clip, Output, Dry/Wet -------------------------------------------------------------

    Column {
        id: right
        x: centre.x + centre.width + 8
        y: 4
        width: editor.column
        spacing: editor.gap

        EditorKnob {
            id: driveKnob
            objectName: "drive"
            width: parent.width
            size: 24
            param: p.get("drive")
            title: qsTr("Drive")
            enabled: editor.driven
            tooltip: qsTr("Drive: louder into the circuit (not Clean)")
        }
        ParamChoice {
            objectName: "circuit"
            width: parent.width
            param: p.get("circuit")
            tooltip: qsTr("Circuit: Clean is linear; OSR, MS2, SMP and PRD saturate as analog filters do, "
                          + "the input and the ringing resonance alike, and hear Drive")
        }
        ParamButton {
            objectName: "softClip"
            width: parent.width
            param: p.get("soft_clip")
            text: qsTr("Clip")
            tooltip: qsTr("Clip: rounds off the output's peaks softly")
        }
        ParamBox {
            objectName: "output"
            width: parent.width
            param: p.get("output")
            step: 0.1
            decimals: 1
            formatter: v => v.toFixed(1) + " dB"
            sampleText: "-24.0 dB"
            tooltip: qsTr("Output: the level after the filter")
        }
        EditorKnob {
            id: mixKnob
            objectName: "mix"
            width: parent.width
            size: 26
            param: p.get("mix")
            title: qsTr("Dry/Wet")
        }
    }
}
