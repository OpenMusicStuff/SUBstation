import QtQuick
import QtQuick.Controls
import SUBstation

// The Vocoder's editor, laid out as Ableton's: down the left the carrier (Noise, External, Modulator or Pitch
// Tracking) with its own settings under it (Noise: Rate and Crackle; Pitch Tracking: the wave, Pitch and the range it
// tracks; External and Modulator: what they take), Enhance, and beside them the unvoiced detection (Unvoiced, Sens.,
// Fast or Slow); in the middle the filter bank (VocoderGraph: each band's level from the modulator, the bands'
// filters, Formant's carrier bands marked, the pitch followed) with the bands' count, range, width and Precise or
// Retro under it; down the right Formant, Depth and Gate over Attack, Release and Level, then the channels (Mono,
// Stereo, L/R) and Dry/Wet. Every control shows its parameter as it is now, sets it undoably, touches it when
// pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias graph: graph
    readonly property int margin: 8
    readonly property int gap: 8
    readonly property int carrierWidth: Math.max(116, 54 + trackBoxes.needed)
    readonly property int cellWidth: Math.max(50, cellTexts.needed)
    readonly property int graphWidth: Math.max(268, bankRow.implicitWidth)
    readonly property int barHeight: 22
    readonly property int carrier: p.get("carrier") ? p.get("carrier").index : 0

    implicitWidth: margin + carrierWidth + gap + cellWidth + gap + graphWidth + gap + 3 * cellWidth + 2 * 4 + gap
                   + channelsColumn.width + margin
    implicitHeight: 6 + Math.max(2 * unvoicedCell.implicitHeight + 6 + 16, 2 * formantCell.implicitHeight + 6,
                                  graph.implicitHeight + barHeight, 118) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["carrier", "noise_rate", "crackle", "pitch", "wave", "track_low", "track_high", "enhance", "unvoiced",
              "sensitivity", "speed", "bands", "low", "high", "bandwidth", "filter", "formant", "gate", "depth",
              "attack", "release", "channels", "level", "mix"]
    }

    EditorBoxWidth {
        id: trackBoxes
        params: [p.get("track_low"), p.get("track_high")]
    }
    EditorBoxWidth {
        id: rangeBoxes
        params: [p.get("low"), p.get("high")]
    }
    EditorBoxWidth {
        id: widthBox
        params: [p.get("bandwidth")]
    }
    EditorTextsWidth {
        id: cellTexts
        knobs: [unvoicedCell, sensCell, formantCell, depthCell, gateCell, attackCell, releaseCell, levelCell, mixCell]
    }

    component Knob: EditorKnob {
        width: editor.cellWidth
        size: 24
    }
    component Caption: Text {
        color: Theme.textDim
        font: Theme.uiFont(8)
    }

    // --- The carrier ---------------------------------------------------------------------------

    Column {
        id: carrierColumn
        x: editor.margin
        y: 6
        width: editor.carrierWidth
        spacing: 5

        Caption {
            text: qsTr("Carrier")
        }
        ParamChoice {
            objectName: "param_carrier"
            width: parent.width
            height: 18
            param: p.get("carrier")
            tooltip: qsTr("Carrier: the sound that speaks.\n"
                          + "Noise: its own noise. External: the sidechain (a synth on another track).\n"
                          + "Modulator: the input itself. Pitch Tracking: an oscillator following the input's pitch")
        }

        // Noise: Rate and Crackle.
        Row {
            visible: editor.carrier === 0
            spacing: 4

            EditorKnob {
                objectName: "param_noise_rate"
                width: 56
                size: 24
                param: p.get("noise_rate")
                title: qsTr("Rate")
                tooltip: qsTr("Rate: the noise's sample rate: lower is grainier (20 kHz: plain white noise)")
            }
            EditorKnob {
                objectName: "param_crackle"
                width: 56
                size: 24
                param: p.get("crackle")
                title: qsTr("Crackle")
                tooltip: qsTr("Crackle: 100%: noise; lower, ever sparser clicks, as loud")
            }
        }

        // External and Modulator: what they take.
        Text {
            visible: editor.carrier === 1 || editor.carrier === 2
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font: Theme.uiFont(8)
            text: editor.carrier === 1 ? qsTr("The sidechain's sound: choose its source in the device's title bar.")
                                       : qsTr("The input as its own carrier: Enhance makes a filter bank of it.")
        }

        // Pitch Tracking: the wave, Pitch, the range tracked.
        Column {
            visible: editor.carrier === 3
            width: parent.width
            spacing: 4

            ParamChoice {
                objectName: "param_wave"
                width: parent.width
                height: 16
                param: p.get("wave")
                tooltip: qsTr("Wave: the oscillator's shape: a saw, or pulses narrower and brighter")
            }
            Row {
                spacing: 4

                EditorKnob {
                    objectName: "param_pitch"
                    width: 50
                    size: 24
                    param: p.get("pitch")
                    title: qsTr("Pitch")
                    tooltip: qsTr("Pitch: moves the oscillator from the pitch tracked, in semitones")
                }
                Column {
                    spacing: 2

                    Caption {
                        text: qsTr("Low / High")
                    }
                    ParamBox {
                        objectName: "param_track_low"
                        width: trackBoxes.needed
                        sampleText: trackBoxes.sample
                        param: p.get("track_low")
                        logScale: true
                        tooltip: qsTr("Low: the lowest pitch it looks for")
                    }
                    ParamBox {
                        objectName: "param_track_high"
                        width: trackBoxes.needed
                        sampleText: trackBoxes.sample
                        param: p.get("track_high")
                        logScale: true
                        tooltip: qsTr("High: the highest pitch it looks for")
                    }
                }
            }
        }
    }
    ParamButton {
        objectName: "param_enhance"
        x: editor.margin
        y: editor.height - 6 - height
        width: editor.carrierWidth
        height: 16
        param: p.get("enhance")
        text: qsTr("Enhance")
        tooltip: qsTr("Enhance: brings every band of the carrier to the same steady level:\n"
                      + "a brighter, clearer voice from any carrier")
    }

    // --- Unvoiced ------------------------------------------------------------------------------

    Column {
        id: unvoicedColumn
        x: carrierColumn.x + carrierColumn.width + editor.gap
        y: 6
        width: editor.cellWidth
        spacing: 3

        Knob {
            id: unvoicedCell
            objectName: "param_unvoiced"
            param: p.get("unvoiced")
            title: qsTr("Unvoiced")
            tooltip: qsTr("Unvoiced: noise added where the input has no pitch (s, f, t),\n"
                          + "so consonants come through on any carrier")
        }
        Knob {
            id: sensCell
            objectName: "param_sensitivity"
            param: p.get("sensitivity")
            title: qsTr("Sens.")
            tooltip: qsTr("Sens.: how readily a sound is taken as unvoiced")
        }
        Row {
            spacing: 2

            Repeater {
                model: [qsTr("Fast"), qsTr("Slow")]
                ParamButton {
                    required property int index
                    required property string modelData
                    objectName: "speed" + index
                    width: (editor.cellWidth - 2) / 2
                    height: 16
                    param: p.get("speed")
                    choice: index
                    text: modelData
                    tooltip: index === 0 ? qsTr("Fast: unvoiced sounds heard at once")
                                         : qsTr("Slow: unvoiced sounds heard more steadily")
                }
            }
        }
    }

    // --- The filter bank -----------------------------------------------------------------------

    VocoderGraph {
        id: graph
        objectName: "vocoderGraph"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        x: unvoicedColumn.x + unvoicedColumn.width + editor.gap
        y: 6
        width: editor.graphWidth
        height: editor.height - 12 - editor.barHeight

        HoverHandler {
            id: graphHover
        }
        ToolTip.visible: graphHover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("The filter bank: each band lit as loud as the input is in it, its filter drawn over it.\n"
                           + "The marks along the bottom: the carrier's bands, where Formant moves them")
    }
    Row {
        id: bankRow
        x: graph.x
        y: graph.y + graph.height + 4
        height: 18
        spacing: 4

        Caption {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Bands")
        }
        ParamChoice {
            objectName: "param_bands"
            width: 38
            height: 16
            anchors.verticalCenter: parent.verticalCenter
            param: p.get("bands")
            tooltip: qsTr("Bands: how many bands the sound is split in: more, more intelligible")
        }
        ParamBox {
            objectName: "param_low"
            width: rangeBoxes.needed
            sampleText: rangeBoxes.sample
            param: p.get("low")
            logScale: true
            tooltip: qsTr("Low: the lowest band's frequency")
        }
        ParamBox {
            objectName: "param_high"
            width: rangeBoxes.needed
            sampleText: rangeBoxes.sample
            param: p.get("high")
            logScale: true
            tooltip: qsTr("High: the highest band's frequency")
        }
        Caption {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("BW")
        }
        ParamBox {
            objectName: "param_bandwidth"
            width: widthBox.needed
            sampleText: widthBox.sample
            param: p.get("bandwidth")
            tooltip: qsTr("BW: how wide each band is (100%: they meet at -3 dB); wider is smoother, narrower more\n"
                          + "resonant")
        }
        ParamChoice {
            objectName: "param_filter"
            width: 54
            height: 16
            anchors.verticalCenter: parent.verticalCenter
            param: p.get("filter")
            tooltip: qsTr("Precise: every band as loud and as wide.\nRetro: gentler slopes, the high bands narrower and louder")
        }
    }

    // --- Formant, the envelopes, the output ----------------------------------------------------

    Grid {
        id: envelopeGrid
        x: graph.x + graph.width + editor.gap
        y: 6
        columns: 3
        columnSpacing: 4
        rowSpacing: 6

        Knob {
            id: formantCell
            objectName: "param_formant"
            param: p.get("formant")
            title: qsTr("Formant")
            tooltip: qsTr("Formant: moves the carrier's bands up or down: a smaller or larger voice")
        }
        Knob {
            id: depthCell
            objectName: "param_depth"
            param: p.get("depth")
            title: qsTr("Depth")
            tooltip: qsTr("Depth: how much the input's levels shape the carrier.\n"
                          + "0%: the carrier as it is; 200%: only the input's peaks come through")
        }
        Knob {
            id: gateCell
            objectName: "param_gate"
            param: p.get("gate")
            title: qsTr("Gate")
            tooltip: qsTr("Gate: bands the input is quieter in than this are silent")
        }
        Knob {
            id: attackCell
            objectName: "param_attack"
            param: p.get("attack")
            title: qsTr("Attack")
            tooltip: qsTr("Attack: how quickly a band follows the input as it gets louder")
        }
        Knob {
            id: releaseCell
            objectName: "param_release"
            param: p.get("release")
            title: qsTr("Release")
            tooltip: qsTr("Release: how quickly a band falls as the input gets quieter (long: a freeze)")
        }
        Knob {
            id: levelCell
            objectName: "param_level"
            param: p.get("level")
            title: qsTr("Level")
            tooltip: qsTr("Level: the vocoded sound's level")
        }
    }

    Column {
        id: channelsColumn
        x: envelopeGrid.x + envelopeGrid.width + editor.gap
        y: 6
        width: editor.cellWidth
        spacing: 2

        Repeater {
            model: [qsTr("Mono"), qsTr("Stereo"), qsTr("L/R")]
            ParamButton {
                required property int index
                required property string modelData
                objectName: "channels" + index
                width: parent.width
                height: 16
                param: p.get("channels")
                choice: index
                text: modelData
                tooltip: [qsTr("Mono: the input and the carrier each summed to one"),
                          qsTr("Stereo: the input summed, the carrier in stereo"),
                          qsTr("L/R: each side vocoded on its own")][index]
            }
        }
    }
    Knob {
        id: mixCell
        objectName: "param_mix"
        x: channelsColumn.x
        y: editor.height - 6 - implicitHeight
        param: p.get("mix")
        title: qsTr("Dry/Wet")
        tooltip: qsTr("Dry/Wet: how much of the vocoded sound is heard")
    }
}
