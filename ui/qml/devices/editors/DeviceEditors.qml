pragma Singleton
import QtQuick

// The built-in devices' own editors: a device of one of these kinds shows its
// editor instead of a knob per parameter (DeviceParamKnob, in pages).
//
//   const url = DeviceEditors.editorFor(device.kind)   // "" for the generic knobs
//   Loader { Component.onCompleted: setSource(url, { trackId: t, deviceId: d }) }
//
// What an editor is, for the device view that hosts it:
// - `required property string trackId` and `deviceId`: the device it edits
//   (on its track, the master or a return; in a rack too).
// - It is the device's body: the frame around it (its border, the title bar
//   with its fold, on/off, name, sidechain, page and save buttons, the menu)
//   is the view's. Its `implicitWidth` is the body's width (the device's less
//   its 1 px border either side); it may change (the EQ's band controls shown
//   or collapsed, in every EQ at once). Give it the body's whole height: it
//   grows its graphs into it; `implicitHeight` is the least it needs.
// - Optional: `pages` (read) and `page` (read/write): its pages of knobs (the
//   Sampler's two); the title bar shows the page arrows while there is more
//   than one. `menuActions` (a list of Action): what the device's right-click
//   menu starts with (the Sampler's Load Sample… and Clear Sample).
//   `sidechainMenuRequested(from)`: show the device's sidechain menu, under
//   the item `from` (an editor's sidechain button), or without one in the
//   editor's middle (the Sidechain's hint over its curve).
// - Its items read their displays on each tick of the display clock
//   (DisplayClock: DeviceCanvas::refreshDisplays) while they are visible, at
//   the preferences' display rate, and it edits through the editor (undoably). Clicks it
//   doesn't take (on its background) go on to what is under it: the view's
//   frame (selecting the device, its menu).
QtObject {
    // kind -> the editor's file, beside this one.
    readonly property var editors: ({
        "amp": "AmpEditor.qml",
        "arpeggiator": "ArpeggiatorEditor.qml",
        "auto_filter": "AutoFilterEditor.qml",
        "bitcrusher": "BitcrusherEditor.qml",
        "chorus": "ChorusEditor.qml",
        "compressor": "CompressorEditor.qml",
        "delay": "DelayEditor.qml",
        "disperser": "DisperserEditor.qml",
        "envelope": "EnvelopeEditor.qml",
        "eq": "EqEditor.qml",
        "erosion": "ErosionEditor.qml",
        "gate": "GateEditor.qml",
        "gigatune": "GigaTuneEditor.qml",
        "glitch": "GlitchEditor.qml",
        "groove": "GrooveEditor.qml",
        "hue": "HueEditor.qml",
        "lfo": "LfoEditor.qml",
        "limiter": "LimiterEditor.qml",
        "multiband": "MultibandEditor.qml",
        "overdrive": "OverdriveEditor.qml",
        "phaser": "PhaserEditor.qml",
        "reverb": "ReverbEditor.qml",
        "sampler": "SamplerEditor.qml",
        "saturator": "SaturatorEditor.qml",
        "sidechain": "SidechainEditor.qml",
        "spectral": "SpectralEditor.qml",
        "supersaw": "SupersawEditor.qml",
        "tape_stop": "TapeStopEditor.qml",
        "transient": "TransientEditor.qml",
        "vintage_hall": "VintageHallEditor.qml",
        "vital": "VitalEditor.qml",
        "vocal_rider": "VocalRiderEditor.qml",
        "vocoder": "VocoderEditor.qml"
    })

    // The editor's component URL for built-in devices of `kind`, or "" when it has none of its own.
    function editorFor(kind) {
        const file = Object.prototype.hasOwnProperty.call(editors, kind) ? editors[kind] : ""
        return file ? Qt.resolvedUrl(file).toString() : ""
    }
}
