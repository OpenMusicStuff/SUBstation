import QtQuick
import QtQuick.Controls
import SUBstation

// The Auto Filter's editor, laid out as Ableton's, compact: the filter's type,
// circuit and slope at the left; the display (AutoFilterGraph: the response
// that plays, sweeping as the envelope and the LFO move it, over the input's
// spectrum; drag for Frequency and Resonance); then the filter's knobs
// (Frequency, Resonance, Drive and Morph, each dimmed where it does nothing),
// the envelope's (Amount, Attack, Release) with the LFO's Amount, the LFO's
// (Freq or a synced Rate with its ♪, the waveform, Phase or Spin with its
// switch, Offset) and Output and Dry/Wet. Two rows of knobs, 2 x 2 a section.
//
// Its animations are subtle: the display's (see AutoFilterGraph.h), and the
// knobs that do nothing fading out and back as the type, the circuit or the
// LFO's Amount changes.
Item {
    id: editor

    required property string trackId
    required property string deviceId
    readonly property alias graph: graph

    // A knob's cell: 46 px, or as wide as its widest caption or readout (every form of each, the swapped knobs'
    // both), a pixel clear either side.
    readonly property int cell: Math.max(46, cellTexts.needed + 2)
    readonly property int knobSize: 28
    readonly property int gap: 4
    readonly property int sectionGap: 8
    readonly property int choiceWidth: 66

    readonly property bool synced: on("lfo_sync")
    readonly property bool spinning: on("spin_on")
    readonly property bool morphing: p.get("type") ? p.get("type").index === 4 : false
    readonly property bool driven: p.get("circuit") ? p.get("circuit").index !== 0 : false
    readonly property bool lfoOn: p.get("lfo_amount") ? p.get("lfo_amount").value > 0 : false

    // 8 px in, the filter's lists, the display, three sections of 2 x 2 knobs and Output over Dry/Wet, a
    // section's gap between each, and 6 px to the body's edge (8 less the frame's border).
    implicitWidth: 8 + choiceWidth + sectionGap + graph.implicitWidth + 3 * (sectionGap + 2 * cell + gap)
                   + sectionGap + cell + 8 - 2
    implicitHeight: 6 + Math.max(2 * freqKnob.implicitHeight + gap, graph.implicitHeight) + 6

    function on(id) {
        const param = p.get(id)
        return param ? param.value >= 0.5 : false
    }

    EditorTextsWidth {
        id: cellTexts
        knobs: [freqKnob, resKnob, driveKnob, morphKnob, envKnob, attackKnob, releaseKnob, lfoKnob, offsetKnob,
                outputKnob, mixKnob]
        texts: [qsTr("Freq"), qsTr("Rate"), qsTr("Phase"), qsTr("Spin"), "##.## Hz", "#.## Hz", "###°", "## %"]
            .concat(p.get("lfo_rate") ? p.get("lfo_rate").labels : [])
    }

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["type", "circuit", "slope", "freq", "resonance", "drive", "morph", "env_amount", "env_attack",
              "env_release", "lfo_amount", "lfo_wave", "lfo_sync", "lfo_freq", "lfo_rate", "spin_on", "phase", "spin",
              "lfo_offset", "output", "mix"]
    }

    component CellKnob: EditorKnob {
        width: editor.cell
        size: editor.knobSize
    }

    Row {
        x: 8
        y: 6
        height: editor.height - 12
        spacing: editor.sectionGap

        // The filter's type, circuit and slope.
        Column {
            width: editor.choiceWidth
            spacing: 3

            EditorCaption {
                width: parent.width
                text: qsTr("Filter")
            }
            ParamChoice {
                objectName: "type"
                width: parent.width
                param: p.get("type")
                tooltip: qsTr("Filter type: Lowpass, Highpass, Bandpass, Notch, or Morph (Morph sweeps from one "
                              + "to the next)")
            }
            ParamChoice {
                objectName: "circuit"
                width: parent.width
                param: p.get("circuit")
                tooltip: qsTr("Circuit: Clean is linear; OSR, MS2, SMP and PRD saturate as analog filters do, "
                              + "the input and the ringing resonance alike, and hear Drive")
            }
            Row {
                spacing: 2
                Repeater {
                    model: 2
                    ParamButton {
                        required property int index
                        objectName: "slope" + (index === 0 ? "12" : "24")
                        width: (editor.choiceWidth - 2) / 2
                        param: p.get("slope")
                        choice: index
                        text: index === 0 ? "12" : "24"
                        tooltip: qsTr("Slope: 12 or 24 dB an octave")
                    }
                }
            }
        }

        AutoFilterGraph {
            id: graph
            objectName: "autoFilterGraph"
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            width: implicitWidth
            height: Math.max(implicitHeight, parent.height)

            HoverHandler {
                id: graphHover
            }
            ToolTip.visible: graphHover.hovered && !graph.dragging
            ToolTip.delay: 700
            ToolTip.text: qsTr("Drag: Frequency across, Resonance up and down; double-click to reset them. The "
                               + "curve sweeps as the envelope and the LFO move it; the bar along the bottom is "
                               + "how far they can")
        }

        // Frequency, Resonance; Drive, Morph.
        Grid {
            columns: 2
            columnSpacing: editor.gap
            rowSpacing: editor.gap

            CellKnob {
                id: freqKnob
                objectName: "freq"
                param: p.get("freq")
                title: qsTr("Freq")
                tooltip: qsTr("Frequency: the filter's cutoff, where the envelope and the LFO move it from")
            }
            CellKnob {
                id: resKnob
                objectName: "resonance"
                param: p.get("resonance")
                title: qsTr("Res")
                tooltip: qsTr("Resonance: a peak at the cutoff; the saturating circuits ring and growl past 100 %")
            }
            CellKnob {
                id: driveKnob
                objectName: "drive"
                param: p.get("drive")
                title: qsTr("Drive")
                enabled: editor.driven
                tooltip: qsTr("Drive: louder into the circuit (not Clean)")
            }
            CellKnob {
                id: morphKnob
                objectName: "morph"
                param: p.get("morph")
                title: qsTr("Morph")
                enabled: editor.morphing
                tooltip: qsTr("Morph (the Morph type): Lowpass, Bandpass, Highpass, Notch and back")
            }
        }

        // The envelope: Amount, Attack; Release; and the LFO's Amount.
        Grid {
            columns: 2
            columnSpacing: editor.gap
            rowSpacing: editor.gap

            CellKnob {
                id: envKnob
                objectName: "envAmount"
                param: p.get("env_amount")
                title: qsTr("Env")
                knob.bipolar: true
                tooltip: qsTr("Envelope Amount: how far the input's level (or the sidechain's) moves the cutoff, "
                              + "up or down; at 100 % six octaves")
            }
            CellKnob {
                id: attackKnob
                objectName: "envAttack"
                param: p.get("env_attack")
                title: qsTr("Attack")
                tooltip: qsTr("Envelope Attack: how fast it follows the level up")
            }
            CellKnob {
                id: releaseKnob
                objectName: "envRelease"
                param: p.get("env_release")
                title: qsTr("Release")
                tooltip: qsTr("Envelope Release: how fast it falls back")
            }
            CellKnob {
                id: lfoKnob
                objectName: "lfoAmount"
                param: p.get("lfo_amount")
                title: qsTr("LFO")
                tooltip: qsTr("LFO Amount: how far the LFO sweeps the cutoff either way; at 100 % four octaves")
            }
        }

        // The LFO: Rate, the waveform with the Sync and Spin switches under it; Phase or Spin, Offset. Its
        // controls dim while its Amount is 0.
        Grid {
            columns: 2
            columnSpacing: editor.gap
            rowSpacing: editor.gap
            opacity: editor.lfoOn ? 1 : 0.55
            Behavior on opacity {
                NumberAnimation {
                    duration: 120
                }
            }

            CellKnob {
                id: rateKnob
                objectName: "lfoRate"
                param: editor.synced ? p.get("lfo_rate") : p.get("lfo_freq")
                title: editor.synced ? qsTr("Rate") : qsTr("Freq")
                tooltip: editor.synced ? qsTr("LFO Rate: a cycle every so much of a bar, following the song")
                                       : qsTr("LFO Frequency: cycles a second")
            }
            Column {
                width: editor.cell
                spacing: 3

                EditorCaption {
                    width: parent.width
                    text: qsTr("Shape")
                }
                ParamChoice {
                    objectName: "lfoWave"
                    width: parent.width
                    param: p.get("lfo_wave")
                    tooltip: qsTr("The LFO's waveform: Sine, Triangle, Saw Up, Saw Down, Square, S&H (a random "
                                  + "step each cycle) or Smooth (random, gliding)")
                }
                Row {
                    spacing: 2

                    ParamButton {
                        objectName: "lfoSync"
                        width: 16
                        param: p.get("lfo_sync")
                        iconName: "note"
                        iconSize: 9
                        tooltip: qsTr("Sync the LFO to the song's tempo: its Rate in note values")
                    }
                    ParamButton {
                        objectName: "spinOn"
                        width: editor.cell - 18
                        param: p.get("spin_on")
                        text: qsTr("Spin")
                        tooltip: qsTr("Spin: the right side's LFO runs faster, instead of a Phase ahead")
                    }
                }
            }
            CellKnob {
                id: phaseKnob
                objectName: "phase"
                param: editor.spinning ? p.get("spin") : p.get("phase")
                title: editor.spinning ? qsTr("Spin") : qsTr("Phase")
                tooltip: editor.spinning ? qsTr("Spin: the right side's LFO runs this much faster")
                                         : qsTr("Phase: the right side's LFO this far ahead of the left's")
            }
            CellKnob {
                id: offsetKnob
                objectName: "lfoOffset"
                param: p.get("lfo_offset")
                title: qsTr("Offset")
                tooltip: qsTr("Offset: turns the LFO's cycle (both sides)")
            }
        }

        Column {
            spacing: editor.gap

            CellKnob {
                id: outputKnob
                objectName: "output"
                param: p.get("output")
                title: qsTr("Output")
            }
            CellKnob {
                id: mixKnob
                objectName: "mix"
                param: p.get("mix")
                title: qsTr("Dry/Wet")
            }
        }
    }
}
