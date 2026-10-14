import QtQuick
import QtQuick.Controls
import SUBstation

// The Bitcrusher's editor: what it does to one cycle of a sine (the dry wave
// dim behind it) as it stands, its held samples' steps across and its bits'
// steps up and down, worked out here from the parameters as the engine works
// them out (Bitcrusher.cpp: drive, clipped, held at the rate, rounded to the
// bits), as tall as Output makes it, with Output's box over its corner; beside
// it the other knobs, three to a row.
//
// Its animation: while sound comes in (the display `level`, through a
// DisplayValue) the wave runs along, glowing as loud as the input is; a change
// of the parameters fades the new steps in.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int previewWidth: 176
    readonly property int knobWidth: 56
    readonly property int spacing: 10

    implicitWidth: 8 + previewWidth + spacing + 3 * knobWidth + 2 * 6 + 8 - 2
    implicitHeight: 6 + Math.max(knobs.implicitHeight, 120) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["bits", "rate", "drive", "dither", "tone", "output", "mix"]
    }

    DisplayValue {
        id: input
        objectName: "levelDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "level"
        smoothing: 0.35
        width: 0
        height: 0
    }

    // The preview's held samples over one cycle of 200 Hz at 48 kHz: {x, w, y} (x and w in 0..1 of the
    // cycle, y in -1..1).
    readonly property var steps: {
        const bits = p.get("bits") ? p.get("bits").value : 8
        const rate = p.get("rate") ? p.get("rate").value : 11025
        const drive = Math.pow(10, (p.get("drive") ? p.get("drive").value : 0) / 20)
        const kRate = 48000, kFreq = 200, kSamples = kRate / kFreq
        const levels = Math.pow(2, Math.max(1, Math.min(16, bits)) - 1)
        const hold = Math.max(1, kRate / rate)  // samples each held one lasts
        // At most a step a pixel: past that the steps are finer than can be seen.
        const stride = Math.max(hold, kSamples / editor.previewWidth)
        const out = []
        for (let t = 0; t < kSamples; t += stride) {
            const x = Math.max(-1, Math.min(1, drive * 0.8 * Math.sin(2 * Math.PI * kFreq * Math.floor(t) / kRate)))
            const y = Math.max(-1, Math.min(1, Math.round(x * levels) / levels))
            out.push({ x: t / kSamples, w: Math.min(stride, kSamples - t) / kSamples, y: y })
        }
        return out
    }
    // How tall Output makes the wave (its gain, held to what fits; not read-only, so its Behavior can ease it).
    property real outputGain: Math.min(1.25, Math.pow(10, (p.get("output") ? p.get("output").value : 0) / 20))
    Behavior on outputGain {
        NumberAnimation {
            duration: 120
            easing.type: Easing.OutCubic
        }
    }

    // One cycle: the dry sine's dots, and the crushed wave as bars from zero.
    component Cycle: Item {
        id: cycle

        readonly property real middle: height / 2
        readonly property real amplitude: (height / 2 - 6) * editor.outputGain

        Repeater {
            model: Math.floor(cycle.width / 2)
            Rectangle {
                required property int index
                x: 2 * index
                y: cycle.middle - (cycle.height / 2 - 6) * 0.8 * Math.sin(2 * Math.PI * 2 * index / cycle.width)
                width: 1
                height: 1
                color: Theme.textDim
            }
        }
        Repeater {
            model: editor.steps
            Rectangle {
                id: bar
                required property var modelData
                readonly property real level: cycle.middle - cycle.amplitude * modelData.y  // its value's y
                x: modelData.x * cycle.width
                y: Math.min(level, cycle.middle)
                width: Math.max(1, modelData.w * cycle.width)
                height: Math.max(1, Math.abs(level - cycle.middle))
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.3 + 0.35 * Math.min(1, input.smoothed))
                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity {
                    NumberAnimation {
                        duration: 160
                    }
                }

                Rectangle {  // its top
                    y: bar.level <= cycle.middle ? 0 : bar.height - 1
                    width: parent.width
                    height: 1
                    color: Theme.accent
                }
            }
        }
    }

    Rectangle {
        id: preview
        objectName: "crushPreview"
        x: 8
        y: 6
        width: editor.previewWidth
        height: editor.height - 12
        color: Theme.lane
        border.color: Theme.border
        clip: true

        Rectangle {  // zero
            y: preview.height / 2
            width: parent.width
            height: 1
            color: Theme.border
        }
        // Two cycles side by side, running along while sound comes in (the second takes the first's place).
        Row {
            id: wave
            objectName: "wave"
            height: preview.height

            Cycle {
                width: preview.width
                height: preview.height
            }
            Cycle {
                width: preview.width
                height: preview.height
            }

            NumberAnimation on x {
                from: 0
                to: -preview.width
                duration: 2400
                loops: Animation.Infinite
                running: true
                paused: !input.active  // (held where it is while nothing comes in)
            }
        }

        // Output, over the wave's corner.
        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            spacing: 3

            EditorCaption {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Out")
            }
            ParamBox {
                objectName: "output"
                width: 58
                param: p.get("output")
                step: 0.1
                decimals: 1
                defaultValue: 0.0
                formatter: v => v.toFixed(1) + " dB"
                sampleText: "-24.0 dB"
                tooltip: qsTr("Output: the level after the crusher (the wave grows and shrinks with it)")
            }
        }

        HoverHandler {
            id: hover
        }
        ToolTip.visible: hover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("A sine as the Bitcrusher has it: Rate holds each sample longer (the steps across), "
                           + "Bits rounds it to fewer levels (the steps up and down), Drive pushes it into the clip")
    }

    Grid {
        id: knobs
        x: preview.x + preview.width + editor.spacing
        y: 6
        columns: 3
        columnSpacing: 6
        rowSpacing: 4

        EditorKnob {
            objectName: "bits"
            width: editor.knobWidth
            param: p.get("bits")
            title: qsTr("Bits")
            formatter: v => v.toFixed(1)
            tooltip: qsTr("Bits: how many levels the signal is rounded to (8: 256; fractions between)")
        }
        EditorKnob {
            objectName: "rate"
            width: editor.knobWidth
            param: p.get("rate")
            title: qsTr("Rate")
            tooltip: qsTr("Rate: the sample rate it holds the signal at; low rates alias, as early samplers did")
        }
        EditorKnob {
            objectName: "drive"
            width: editor.knobWidth
            param: p.get("drive")
            title: qsTr("Drive")
            tooltip: qsTr("Drive: louder into the crusher (clipped at full scale): more of the levels used, then grit")
        }
        EditorKnob {
            objectName: "dither"
            width: editor.knobWidth
            param: p.get("dither")
            title: qsTr("Dither")
            tooltip: qsTr("Dither: noise of up to a level added first: a hiss in place of the steps' buzz on quiet sounds")
        }
        EditorKnob {
            objectName: "tone"
            width: editor.knobWidth
            param: p.get("tone")
            title: qsTr("Tone")
            tooltip: qsTr("Tone: smooths the steps' edges (a low-pass from 1 kHz, open at 100%)")
        }
        EditorKnob {
            objectName: "mix"
            width: editor.knobWidth
            param: p.get("mix")
            title: qsTr("Dry/Wet")
        }
    }
}
