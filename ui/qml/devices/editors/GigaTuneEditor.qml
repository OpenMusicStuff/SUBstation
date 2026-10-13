import QtQuick
import QtQuick.Controls
import SUBstation

// GigaTune's editor, compact: along the top the key and the scale it corrects to (Song: the song's, named beside
// it once known) and HQ (Quality: High Quality, or off Low Latency); under them its display (GigaTuneMeter: the
// note it corrects to, the pitch heard against it in cents and where the correction takes it, the scale's notes)
// beside the four knobs, Retune Speed and Strength over Pitch and Formant. Every control shows its parameter as it is now, sets it undoably, touches it
// when pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias meter: meter
    readonly property int margin: 8
    readonly property int gap: 10  // between the display and the knobs
    // The knobs' cells: as wide as their captions and every text their readouts take.
    readonly property int cellWidth: Math.max(52, cellTexts.needed)

    // The song's key (Session.harmony: the project's key, else the one its notes are in), as Hue reads it.
    readonly property var rootNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    readonly property string songKey: Session.harmony.keyLabel  // "A Minor"; "": none known
    readonly property bool songMinor: songKey.endsWith("Minor")
    readonly property int songTonic: {
        const tonic = songKey.split(" ")[0]  // (the key's names spell Eb, Ab and Bb flat)
        const flats = { "Eb": 3, "Ab": 8, "Bb": 10 }
        return flats[tonic] !== undefined ? flats[tonic] : rootNames.indexOf(tonic)
    }
    readonly property string songKeyShort: songKey === "" ? "" : songKey.split(" ")[0] + (songMinor ? "m" : "")
    // The choices' names, Song's with the song's key once known.
    readonly property var keyLabels: {
        const param = p.get("key")
        const labels = param ? param.labels.slice() : []
        if (labels.length > 0 && songKeyShort !== "")
            labels[0] = qsTr("Song · %1").arg(songKeyShort)
        return labels
    }
    readonly property var scaleLabels: {
        const param = p.get("scale")
        const labels = param ? param.labels.slice() : []
        if (labels.length > 0 && songKey !== "")
            labels[0] = songMinor ? qsTr("Song · Minor") : qsTr("Song · Major")
        return labels
    }

    implicitWidth: margin + meter.implicitWidth + gap + knobs.implicitWidth + margin
    implicitHeight: 6 + header.height + 6 + Math.max(meter.implicitHeight, knobs.implicitHeight) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["key", "scale", "speed", "strength", "pitch", "formant", "quality"]
    }

    EditorTextsWidth {
        id: cellTexts
        knobs: [speedCell, strengthCell, pitchCell, formantCell]
    }
    // The choices: as wide as their longest names, so they don't jump as they change.
    EditorTextsWidth {
        id: keyTexts
        font: keyChoice.button.font
        texts: editor.keyLabels
    }
    EditorTextsWidth {
        id: scaleTexts
        font: scaleChoice.button.font
        texts: editor.scaleLabels
    }

    component Knob: EditorKnob {
        width: editor.cellWidth
    }

    // Along the top: the key, the scale, and HQ at the right.
    Item {
        id: header
        x: editor.margin
        y: 6
        width: editor.width - 2 * editor.margin
        height: 19

        Row {
            spacing: 4

            ParamChoice {
                id: keyChoice
                objectName: "keyChoice"
                width: keyTexts.needed + button.leftPadding + button.rightPadding
                height: header.height
                param: p.get("key")
                labels: editor.keyLabels
                tooltip: editor.songKey !== ""
                         ? qsTr("Key: the root of the notes it corrects to. Song: the song's, %1").arg(editor.songKey)
                         : qsTr("Key: the root of the notes it corrects to. Song: the song's key, once there is one\n"
                                + "(the project's key, or the one its MIDI tracks are in; until then all twelve notes)")
            }
            ParamChoice {
                id: scaleChoice
                objectName: "scaleChoice"
                width: scaleTexts.needed + button.leftPadding + button.rightPadding
                height: header.height
                param: p.get("scale")
                labels: editor.scaleLabels
                tooltip: qsTr("Scale: the notes it corrects to, on the key's root.\n"
                              + "Song: the song key's, major or natural minor; Chromatic: all twelve")
            }
        }
        ParamButton {
            id: qualityButton
            objectName: "quality"
            anchors.right: parent.right
            width: 34
            height: header.height
            param: p.get("quality")
            text: qsTr("HQ")
            tooltip: qsTr("High Quality: hears the pitch with SwiftF0, a small neural network, sure of the voice\n"
                          + "through noise and reverb (about 277 ms of latency: not for singing through).\n"
                          + "Off: Low Latency, hearing it with pYIN as soon as MAutoPitch does (21 ms), and better\n"
                          + "on stacked voices. Not automatable: a change starts the device again")
        }
    }

    GigaTuneMeter {
        id: meter
        objectName: "gigatuneMeter"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        songTonic: editor.songTonic
        songMinor: editor.songMinor
        x: editor.margin
        y: header.y + header.height + 6
        width: implicitWidth
        height: Math.max(implicitHeight, editor.height - y - 6)

        HoverHandler {
            id: meterHover
        }
        ToolTip.visible: meterHover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("The note the voice is corrected to, and under it the note out while Pitch moves it.\n"
                           + "The needle: the pitch heard, in cents of that note (to ±50); the lit dot: where the\n"
                           + "correction takes it (green round 0: in tune). Along the bottom the twelve notes,\n"
                           + "the scale's lit, the one corrected to glowing")
    }

    // Beside it, the knobs: the correction over what follows it.
    Grid {
        id: knobs
        x: meter.x + meter.width + editor.gap
        y: meter.y + Math.round((meter.height - height) / 2)
        columns: 2
        columnSpacing: 4
        rowSpacing: 6

        Knob {
            id: speedCell
            objectName: "speed"
            param: p.get("speed")
            title: qsTr("Speed")
            tooltip: qsTr("Retune Speed: how fast the pitch is pulled onto the note, from each note's start.\n"
                          + "0 holds it on the note exactly (the hard, robotic tune)")
        }
        Knob {
            id: strengthCell
            objectName: "strength"
            param: p.get("strength")
            title: qsTr("Strength")
            tooltip: qsTr("Strength: how much of the correction is made (0: the voice passes as it is)")
        }
        Knob {
            id: pitchCell
            objectName: "pitch"
            param: p.get("pitch")
            title: qsTr("Pitch")
            tooltip: qsTr("Pitch: after the correction, moves the voice by whole semitones, its formants kept")
        }
        Knob {
            id: formantCell
            objectName: "formant"
            param: p.get("formant")
            title: qsTr("Formant")
            tooltip: qsTr("Formant: moves the voice's formants (its character), its pitch kept")
        }
    }
}
