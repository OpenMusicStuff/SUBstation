import QtQuick
import QtQuick.Controls
import SUBstation

// Changeling's editor, two pages named in the device's title bar. Along the top of both: the modes (Transpose,
// Quantize, Robot), Quantize's key and scale (Song: the song's, named once known; dimmed in the other modes) and the
// quality (Live, Studio, HQ). Down the left of both, its display (ChangelingScope: the last 3 s of the pitch heard
// and the pitch out, the note held, the note out now).
//
// Shift: Pitch and Formant, large, Link between them; Glide, Drive, Mix and Output beside them.
// Mutate: Harmonics, Twin and Interval over Detune, Width and Smear.
//
// Every control shows its parameter as it is now, sets it undoably, touches it when pressed, and right-click gives
// its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId
    readonly property int pages: 2
    property int page: 0
    readonly property var pageNames: [qsTr("Shift"), qsTr("Mutate")]

    readonly property alias scope: scope
    readonly property int margin: 8
    readonly property int gap: 10
    readonly property int headerHeight: 19
    readonly property int scopeWidth: 220
    readonly property int cellWidth: Math.max(54, cellTexts.needed)
    readonly property int bigWidth: Math.max(66, bigTexts.needed)
    readonly property bool quantize: p.get("mode") ? p.get("mode").index === 1 : false

    // The song's key (Session.harmony), as GigaTune's editor reads it, for Key and Scale at Song.
    readonly property var rootNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    readonly property string songKey: Session.harmony.keyLabel
    readonly property bool songMinor: songKey.endsWith("Minor")
    readonly property string songKeyShort: songKey === "" ? "" : songKey.split(" ")[0] + (songMinor ? "m" : "")
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

    readonly property int shiftWidth: 2 * bigWidth + linkButton.width + 8 + 2 * cellWidth + 4
    readonly property int mutateWidth: 3 * cellWidth + 2 * 4
    implicitWidth: margin + scopeWidth + gap + Math.max(shiftWidth, mutateWidth) + margin
    implicitHeight: 6 + headerHeight + 6 + Math.max(scope.implicitHeight, 2 * glideCell.implicitHeight + 6) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["mode", "pitch", "formant", "link", "glide", "key", "scale", "drive", "mix", "output", "harmonics",
              "twin", "interval", "detune", "width", "smear", "quality"]
    }

    EditorTextsWidth {
        id: cellTexts
        knobs: [glideCell, driveCell, mixCell, outputCell, harmonicsCell, twinCell, intervalCell, detuneCell,
                widthCell, smearCell]
    }
    EditorTextsWidth {
        id: bigTexts
        knobs: [pitchCell, formantCell]
    }
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
        size: 26
    }
    component BigKnob: EditorKnob {
        width: editor.bigWidth
        size: 44
    }

    // --- Along the top: the modes, Quantize's key and scale, the quality -------------------------------------------
    Item {
        id: header
        x: editor.margin
        y: 6
        width: editor.width - 2 * editor.margin
        height: editor.headerHeight

        Row {
            id: modes
            spacing: 2

            Repeater {
                model: [
                    { name: qsTr("Transpose"), tip: qsTr("Transpose: moves the voice by Pitch, every note and slide as sung") },
                    { name: qsTr("Quantize"), tip: qsTr("Quantize: pulls each note onto the nearest of the key's scale (Chromatic: every\n"
                                                         + "semitone), the vibrato gone, then moves it by Pitch") },
                    { name: qsTr("Robot"), tip: qsTr("Robot: holds the voice on one note, C4 moved by Pitch, or the last MIDI note played\n"
                                                      + "into the device (held after its note-off)") }
                ]
                ParamButton {
                    required property int index
                    required property var modelData
                    objectName: "mode" + index
                    width: Math.max(58, implicitWidth + 12)
                    height: header.height
                    param: p.get("mode")
                    choice: index
                    text: modelData.name
                    tooltip: modelData.tip
                }
            }
        }
        Row {
            anchors.left: modes.right
            anchors.leftMargin: 10
            spacing: 4
            enabled: editor.quantize
            opacity: enabled ? 1 : 0.45

            ParamChoice {
                id: keyChoice
                objectName: "keyChoice"
                width: keyTexts.needed + button.leftPadding + button.rightPadding
                height: header.height
                param: p.get("key")
                labels: editor.keyLabels
                tooltip: editor.songKey !== ""
                         ? qsTr("Key: Quantize's root. Song: the song's, %1").arg(editor.songKey)
                         : qsTr("Key: Quantize's root. Song: the song's key, once there is one\n"
                                + "(the project's key, or the one its MIDI tracks are in)")
            }
            ParamChoice {
                id: scaleChoice
                objectName: "scaleChoice"
                width: scaleTexts.needed + button.leftPadding + button.rightPadding
                height: header.height
                param: p.get("scale")
                labels: editor.scaleLabels
                tooltip: qsTr("Scale: the notes Quantize pulls the voice to, on the key's root.\n"
                              + "Chromatic (at first): every semitone, as Little AlterBoy's; Song: the song key's")
            }
        }
        Row {
            anchors.right: parent.right
            spacing: 2

            Repeater {
                model: [
                    { name: qsTr("Live"), tip: qsTr("Live: 21 ms of latency, for singing through. A short note's start may\n"
                                                     + "come out unshifted while the pitch is still being heard") },
                    { name: qsTr("Studio"), tip: qsTr("Studio: 49 ms of latency, about Little AlterBoy's: each note heard from its start") },
                    { name: qsTr("HQ"), tip: qsTr("High Quality: hears the pitch with SwiftF0, a small neural network, sure of\n"
                                                   + "the voice through noise and reverb (about 300 ms of latency: for mixing)") }
                ]
                ParamButton {
                    required property int index
                    required property var modelData
                    objectName: "quality" + index
                    width: Math.max(32, implicitWidth + 10)
                    height: header.height
                    param: p.get("quality")
                    choice: index
                    text: modelData.name
                    tooltip: modelData.tip + qsTr("\nNot automatable: a change starts the device again")
                }
            }
        }
    }

    // --- Down the left: the display ------------------------------------------------------------------------------
    ChangelingScope {
        id: scope
        objectName: "changelingScope"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        x: editor.margin
        y: header.y + header.height + 6
        width: editor.scopeWidth
        height: Math.max(implicitHeight, editor.height - y - 6)

        HoverHandler {
            id: scopeHover
        }
        ToolTip.visible: scopeHover.hovered
        ToolTip.delay: 700
        ToolTip.text: qsTr("The last 3 s: the pitch heard (grey) and the pitch out (the bright line), a line a\n"
                           + "semitone; where Quantize or Robot holds the voice, that note as a band.\n"
                           + "In the corner the note out now, and the cents off it")
    }

    // --- Shift ---------------------------------------------------------------------------------------------------
    Item {
        id: shiftPage
        objectName: "shiftPage"
        visible: editor.page === 0
        x: scope.x + scope.width + editor.gap
        y: scope.y
        width: editor.shiftWidth
        height: scope.height

        BigKnob {
            id: pitchCell
            objectName: "pitch"
            y: Math.round((parent.height - height) / 2)
            param: p.get("pitch")
            title: qsTr("Pitch")
            tooltip: qsTr("Pitch: moves the voice up or down, in semitones and their hundredths, its formants\n"
                          + "kept (Robot: the note, from C4)")
        }
        ParamButton {
            id: linkButton
            objectName: "link"
            x: pitchCell.width + 4
            y: pitchCell.y + pitchCell.knob.y + Math.round((pitchCell.knob.height - height) / 2) + 1
            width: 34
            height: 17
            param: p.get("link")
            text: qsTr("Link")
            tooltip: qsTr("Link: the formants move with the pitch, as a tape sped up would (the chipmunk and the\n"
                          + "giant); Formant then moves them on from there")
        }
        BigKnob {
            id: formantCell
            objectName: "formant"
            x: linkButton.x + linkButton.width + 4
            y: pitchCell.y
            param: p.get("formant")
            title: qsTr("Formant")
            tooltip: qsTr("Formant: moves the voice's formants (its size, its character), its pitch kept.\n"
                          + "Down: bigger, darker; up: smaller, brighter")
        }
        Grid {
            x: formantCell.x + formantCell.width + 8
            y: Math.round((parent.height - height) / 2)
            columns: 2
            columnSpacing: 4
            rowSpacing: 6

            Knob {
                id: glideCell
                objectName: "glide"
                param: p.get("glide")
                title: qsTr("Glide")
                tooltip: qsTr("Glide: how long the voice takes to move to a new note (Quantize, Robot, MIDI)\n"
                              + "or a new Pitch: short snaps, long slides")
            }
            Knob {
                id: driveCell
                objectName: "drive"
                param: p.get("drive")
                title: qsTr("Drive")
                tooltip: qsTr("Drive: Little AlterBoy's saturation, from its colour at 1 to hard and loud at 10\n"
                              + "(it compresses past 6). 0: none")
            }
            Knob {
                id: mixCell
                objectName: "mix"
                param: p.get("mix")
                title: qsTr("Mix")
                tooltip: qsTr("Mix: the voice as it was against the shifted voice (both as late as each other)")
            }
            Knob {
                id: outputCell
                objectName: "output"
                param: p.get("output")
                title: qsTr("Output")
                tooltip: qsTr("Output: the level out")
            }
        }
    }

    // --- Mutate --------------------------------------------------------------------------------------------------
    Grid {
        id: mutatePage
        objectName: "mutatePage"
        visible: editor.page === 1
        x: scope.x + scope.width + editor.gap
        y: scope.y + Math.round((scope.height - height) / 2)
        columns: 3
        columnSpacing: 4
        rowSpacing: 6

        Knob {
            id: harmonicsCell
            objectName: "harmonics"
            param: p.get("harmonics")
            title: qsTr("Harmonics")
            tooltip: qsTr("Harmonics: shifts the voice's harmonics up or down by that many of them, its pitch\n"
                          + "kept: up thins it (the lowest are gone), down folds it. Between whole ones it rings")
        }
        Knob {
            id: twinCell
            objectName: "twin"
            param: p.get("twin")
            title: qsTr("Twin")
            tooltip: qsTr("Twin: a second voice at Interval from the first, as a tape at that speed would play it\n"
                          + "(an octave down: dark and huge; up: small). Its level; the two kept as loud as one")
        }
        Knob {
            id: intervalCell
            objectName: "interval"
            param: p.get("interval")
            title: qsTr("Interval")
            tooltip: qsTr("Interval: the twin's distance from the voice, in semitones")
        }
        Knob {
            id: detuneCell
            objectName: "detune"
            param: p.get("detune")
            title: qsTr("Detune")
            tooltip: qsTr("Detune: the left side that many cents lower, the right higher: a doubled voice")
        }
        Knob {
            id: widthCell
            objectName: "width"
            param: p.get("width")
            title: qsTr("Width")
            tooltip: qsTr("Width: every other grain of the voice to one side, the rest to the other. Together\n"
                          + "they are the voice; apart, two")
        }
        Knob {
            id: smearCell
            objectName: "smear"
            param: p.get("smear")
            title: qsTr("Smear")
            tooltip: qsTr("Smear: grains taken from up to 80 ms back and moved at random: the voice blurs and roughens")
        }
    }
}
