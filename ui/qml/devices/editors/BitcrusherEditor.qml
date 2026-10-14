import QtQuick
import QtQuick.Controls
import SUBstation

// The Bitcrusher's editor: what it does to a sine (two cycles, the dry wave
// dim behind it) as it stands, its held samples' steps across and its bits'
// steps up and down, worked out here from the parameters as the engine works
// them out (Bitcrusher.cpp: drive, clipped, held at the rate, rounded to the
// bits); beside it the knobs, four to a row.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int previewWidth: 176
    readonly property int knobWidth: 56
    readonly property int spacing: 10

    implicitWidth: 8 + previewWidth + spacing + 4 * knobWidth + 3 * 6 + 8 - 2
    implicitHeight: 6 + Math.max(knobs.implicitHeight, 120) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["bits", "rate", "drive", "dither", "tone", "output", "mix"]
    }

    // The preview's held samples: {x, w, y} (y in -1..1), over the preview's width.
    readonly property var steps: {
        const bits = p.get("bits") ? p.get("bits").value : 8
        const rate = p.get("rate") ? p.get("rate").value : 11025
        const drive = Math.pow(10, (p.get("drive") ? p.get("drive").value : 0) / 20)
        const kRate = 48000, kFreq = 200, kSamples = 2 * kRate / kFreq  // two cycles of 200 Hz at 48 kHz
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

        readonly property real middle: height / 2
        readonly property real amplitude: height / 2 - 6

        Rectangle {  // zero
            y: preview.middle
            width: parent.width
            height: 1
            color: Theme.border
        }
        // The dry sine, dim: a dot a pixel.
        Repeater {
            model: Math.floor(preview.width)
            Rectangle {
                required property int index
                x: index
                y: preview.middle - preview.amplitude * 0.8 * Math.sin(4 * Math.PI * index / preview.width)
                width: 1
                height: 1
                color: Theme.textDim
            }
        }
        // The crushed one: each held sample a bar from zero, its top its value.
        Repeater {
            model: editor.steps
            Rectangle {
                id: bar
                required property var modelData
                readonly property real level: preview.middle - preview.amplitude * modelData.y  // its value's y
                x: modelData.x * preview.width
                y: Math.min(level, preview.middle)
                width: Math.max(1, modelData.w * preview.width)
                height: Math.max(1, Math.abs(level - preview.middle))
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)

                Rectangle {  // its top
                    y: bar.level <= preview.middle ? 0 : bar.height - 1
                    width: parent.width
                    height: 1
                    color: Theme.accent
                }
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
        columns: 4
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
            objectName: "output"
            width: editor.knobWidth
            param: p.get("output")
            title: qsTr("Output")
        }
        EditorKnob {
            objectName: "mix"
            width: editor.knobWidth
            param: p.get("mix")
            title: qsTr("Dry/Wet")
        }
    }
}
