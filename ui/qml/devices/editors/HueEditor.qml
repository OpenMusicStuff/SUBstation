import QtQuick
import QtQuick.Controls
import SUBstation

// Hue's editor: its spectrum across the device (HueSpectrum: what comes out
// coloured by the notes it is, over what goes in, the notes' comb; the band it
// processes between Low Cut and High Cut as handles to drag; drag up and down
// anywhere else for Color), with its controls over the top edge: the key (Auto,
// at first: the song's key, the project's or the one its MIDI tracks are in, as
// the harmony infers it; or a root, and beside it a scale, setting the twelve
// notes at once; "Custom" when they are no scale; "MIDI" once MIDI notes choose
// them), HQ, and at the right the arrow
// that slides open the rest (folded at first; not saved: how the device is
// looked at): the note wheel (HueWheel: the twelve notes round a colour wheel
// as Hue treats them, the sound's chromagram inside; click a note to switch
// it) and the finer controls, Wet Gain, Pre- and Post-Shift, Mute Low and
// High. Every control shows its parameter as it is now, sets it undoably,
// touches it when pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias wheel: wheel
    readonly property alias spectrum: spectrum
    // The knobs' cells: as wide as their captions and every text their readouts take (EditorTextsWidth).
    readonly property int cellWidth: Math.max(52, cellTexts.needed)
    readonly property int groupGap: 7  // either side of a group's line
    // The spectrum, from edge to edge of the body (no margins): as wide as the Vintage Hall folded (its loop, beside
    // it four cells, their gaps and lines, and its margins).
    readonly property int spectrumWidth: Math.max(110, height - 12) + 4 * cellWidth + 49
    // Whether MIDI notes choose the notes (the engine says so once they come).
    readonly property bool midi: spectrum.midi || wheel.midi
    // Open: the wheel and the finer controls shown. `reveal` follows it, 0..1, as the section slides.
    readonly property bool expanded: DeviceViews.value(deviceId, "notes", false)
    property real reveal: expanded ? 1 : 0
    Behavior on reveal {
        NumberAnimation {
            duration: 220
            easing.type: Easing.OutCubic
        }
    }

    function setExpanded(open) {
        DeviceViews.setValue(deviceId, "notes", open)
    }

    // --- The key: a root and a scale, as the twelve note buttons --------------------------------

    readonly property var rootNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    readonly property var scales: [
        { name: qsTr("Major"), steps: [0, 2, 4, 5, 7, 9, 11] },
        { name: qsTr("Minor"), steps: [0, 2, 3, 5, 7, 8, 10] },
        { name: qsTr("Harmonic Minor"), steps: [0, 2, 3, 5, 7, 8, 11] },
        { name: qsTr("Melodic Minor"), steps: [0, 2, 3, 5, 7, 9, 11] },
        { name: qsTr("Dorian"), steps: [0, 2, 3, 5, 7, 9, 10] },
        { name: qsTr("Phrygian"), steps: [0, 1, 3, 5, 7, 8, 10] },
        { name: qsTr("Phrygian Dominant"), steps: [0, 1, 4, 5, 7, 8, 10] },
        { name: qsTr("Lydian"), steps: [0, 2, 4, 6, 7, 9, 11] },
        { name: qsTr("Mixolydian"), steps: [0, 2, 4, 5, 7, 9, 10] },
        { name: qsTr("Locrian"), steps: [0, 1, 3, 5, 6, 8, 10] },
        { name: qsTr("Major Pentatonic"), steps: [0, 2, 4, 7, 9] },
        { name: qsTr("Minor Pentatonic"), steps: [0, 3, 5, 7, 10] },
        { name: qsTr("Blues"), steps: [0, 3, 5, 6, 7, 10] },
        { name: qsTr("Major Triad"), steps: [0, 4, 7] },
        { name: qsTr("Minor Triad"), steps: [0, 3, 7] },
        { name: qsTr("Power Chord"), steps: [0, 7] },
        { name: qsTr("Root Only"), steps: [0] }
    ]
    // The note buttons as bits (C the lowest).
    readonly property int buttons: {
        let bits = 0
        for (let c = 0; c < 12; ++c) {
            const param = p.get(wheel.noteParam(c))
            if (param && param.value >= 0.5)
                bits |= 1 << c
        }
        return bits
    }
    // The key they are: the one last chosen if they still are it, else the first scale (in the list's order) on
    // the lowest root they are; null: no scale (Custom), or no notes.
    readonly property var key: {
        const bits = editor.buttons
        if (bits === 0)
            return null
        const chosen = DeviceViews.value(deviceId, "key", null)
        if (chosen && editor.bitsOf(chosen.root, chosen.scale) === bits)
            return chosen
        for (let s = 0; s < scales.length; ++s) {
            for (let root = 0; root < 12; ++root) {
                if (editor.bitsOf(root, s) === bits)
                    return { root: root, scale: s }
            }
        }
        return null
    }
    // Auto: the notes are the song's key's (Session.harmony: the project's key, else the one its notes are in).
    readonly property bool autoKey: p.get("auto") ? p.get("auto").value >= 0.5 : true
    readonly property string songKey: Session.harmony.keyLabel  // "A Minor"; "": none known
    readonly property bool songMinor: songKey.endsWith("Minor")
    // The song key's notes (bits; -1: none known), as the engine takes them (major, or natural minor).
    readonly property int songNotes: {
        const tonic = songKey.split(" ")[0]  // (the key's names spell Eb, Ab and Bb flat)
        const flats = { "Eb": 3, "Ab": 8, "Bb": 10 }
        const root = flats[tonic] !== undefined ? flats[tonic] : rootNames.indexOf(tonic)
        return root < 0 ? -1 : bitsOf(root, songMinor ? 1 : 0)
    }
    // "Am", "F#": the song key, short.
    readonly property string songKeyShort: songKey === "" ? "" : songKey.split(" ")[0] + (songMinor ? "m" : "")
    readonly property string keyText: {
        if (editor.midi)
            return qsTr("MIDI")
        if (editor.autoKey)
            return editor.songKey !== "" ? editor.songKey : qsTr("Auto")
        if (editor.buttons === 0)
            return qsTr("No Notes")
        if (!editor.key)
            return qsTr("Custom")
        return rootNames[key.root] + " " + shortScaleName(scales[key.scale].name)
    }

    function bitsOf(root, scale) {
        let bits = 0
        for (const step of scales[scale].steps)
            bits |= 1 << ((root + step) % 12)
        return bits
    }
    function shortScaleName(name) {
        return name.replace("Harmonic", "Harm.").replace("Melodic", "Mel.").replace("Pentatonic", "Pent.")
                   .replace("Dominant", "Dom.")
    }
    // The root shown (the key's; with Custom notes the one last chosen, else C).
    function shownRoot() {
        if (editor.key)
            return editor.key.root
        const chosen = DeviceViews.value(deviceId, "key", null)
        return chosen ? chosen.root : 0
    }
    function chooseKey(root, scale) {
        DeviceViews.setValue(deviceId, "key", { root: root, scale: scale })
        wheel.setNotes(bitsOf(root, scale), qsTr("Change Key"))
    }
    function chooseRoot(root) {
        if (editor.autoKey) {  // out of Auto: the scale last chosen, else the song key's mode
            const chosen = DeviceViews.value(deviceId, "key", null)
            chooseKey(root, chosen ? chosen.scale : (editor.songMinor ? 1 : 0))
        } else if (editor.key) {
            chooseKey(root, editor.key.scale)
        } else if (editor.buttons !== 0) {  // Custom: the notes move with the root
            const shift = (root - shownRoot() + 12) % 12
            const bits = ((editor.buttons << shift) | (editor.buttons >> (12 - shift))) & 0xfff
            const chosen = DeviceViews.value(deviceId, "key", null)
            DeviceViews.setValue(deviceId, "key", { root: root, scale: chosen ? chosen.scale : 0 })
            wheel.setNotes(bits, qsTr("Change Key"))
        } else {
            const chosen = DeviceViews.value(deviceId, "key", null)
            chooseKey(root, chosen ? chosen.scale : 0)
        }
    }
    function chooseScale(scale) {
        if (scale < 0)
            wheel.setNotes(0, qsTr("Clear Notes"))
        else
            chooseKey(shownRoot(), scale)
    }

    // (a margin after the rest as it opens; none round the spectrum)
    implicitWidth: Math.ceil(notesSection.x + notesSection.width + 8 * reveal)
    implicitHeight: Math.max(spectrum.implicitHeight, 6 + finer.implicitHeight + 6)

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["start", "end", "color", "wet_gain", "pre_shift", "post_shift", "mute_low", "mute_high", "quality", "auto",
              "note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f", "note_fs", "note_g", "note_gs", "note_a",
              "note_as", "note_b"]
    }

    EditorTextsWidth {
        id: cellTexts
        knobs: [wetGainCell, preShiftCell, postShiftCell]
        texts: [qsTr("Mute Low"), qsTr("Mute High")]
    }

    component Knob: EditorKnob {
        width: editor.cellWidth
    }
    component GroupLine: Rectangle {
        width: 1
        height: editor.height - 16
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: Theme.border
    }
    // A small drop-down of `names` (as ParamChoice's), `index` shown ("text" instead, when set), chosen(i) on a pick.
    component KeyChoice: Item {
        id: choice

        property var names: []
        property int index: -1
        property string text: ""
        property var extra: []  // entries after the names: [{ name, index }]
        property string tooltip: ""
        signal chosen(int index)

        implicitWidth: Math.ceil(choiceButton.implicitWidth)
        implicitHeight: 16

        RoleButton {
            id: choiceButton
            anchors.fill: parent
            role: "small"
            text: choice.text !== "" ? choice.text : (choice.names[choice.index] || "–")
            tooltip: choice.tooltip
            checkable: false
            rightPadding: 13
            onClicked: choiceMenu.show()

            Icon {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                name: "fold"
                size: 7
                color: choiceButton.look.text
            }
        }
        DynamicMenu {
            id: choiceMenu
            objectName: "choiceMenu"

            function show() {
                clear()
                for (let i = 0; i < choice.names.length; ++i) {
                    const index = i
                    entry(choice.names[i], () => choice.chosen(index), choice.index === i)
                }
                for (const more of choice.extra) {
                    const index = more.index
                    entry(more.name, () => choice.chosen(index), false)
                }
                popup(choice, 0, choice.height)
            }
        }
    }

    HueSpectrum {
        id: spectrum
        objectName: "hueSpectrum"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        width: Math.max(implicitWidth, editor.spectrumWidth)
        height: Math.max(implicitHeight, editor.height)
        autoNotes: editor.songNotes

        // Over its top edge: the key and HQ at the left, the arrow at the right (a little bigger than the body's).
        Row {
            id: overlay
            x: 6
            y: 6
            spacing: 4

            // Auto, or a root (entry 0: Auto; then C to B).
            KeyChoice {
                id: rootChoice
                objectName: "rootChoice"
                width: Math.max(40, implicitWidth)
                height: 19
                enabled: !editor.midi
                names: [qsTr("Auto")].concat(editor.rootNames)
                index: editor.autoKey ? 0 : editor.buttons === 0 && !editor.key ? -1 : editor.shownRoot() + 1
                text: editor.midi ? qsTr("MIDI Notes")
                                  : editor.autoKey ? (editor.songKey !== "" ? qsTr("Auto · %1").arg(editor.songKeyShort)
                                                                            : qsTr("Auto"))
                                                   : ""
                tooltip: editor.midi ? qsTr("The MIDI notes played to Hue choose the notes (each chord held until the next)")
                                     : editor.autoKey ? (editor.songKey !== ""
                                                         ? qsTr("Auto: the song's key, %1 (the project's key, or the one its MIDI tracks are in).\n"
                                                                + "Choose a root to pick the key yourself").arg(editor.songKey)
                                                         : qsTr("Auto: the song's key, once there is one (the project's key, or the one its\n"
                                                                + "MIDI tracks are in; until then the sound passes). Choose a root to pick the key yourself"))
                                                      : qsTr("The key's root: the scale moves with it (Custom notes too). Auto: the song's key")
                onChosen: index => index === 0 ? wheel.setAuto(true) : editor.chooseRoot(index - 1)
            }
            // The scale, beside a root chosen.
            KeyChoice {
                id: scaleChoice
                objectName: "scaleChoice"
                visible: !editor.autoKey && !editor.midi
                width: 120
                height: 19
                names: editor.scales.map(scale => scale.name)
                index: editor.key ? editor.key.scale : -1
                text: editor.buttons === 0 ? qsTr("No Notes") : editor.key ? "" : qsTr("Custom")
                extra: [{ name: qsTr("No Notes"), index: -1 }]
                tooltip: qsTr("The scale: sets the twelve notes at once (Custom: notes that are no scale;\n"
                              + "the arrow at the right opens the wheel to choose them one by one).\n"
                              + "Notes from a MIDI track (MIDI From, or the track's own) choose them instead")
                onChosen: index => editor.chooseScale(index)
            }
            ParamButton {
                id: hqButton
                objectName: "quality"
                width: 34
                height: 19
                param: p.get("quality")
                text: qsTr("HQ")
                tooltip: qsTr("High Quality: each moved note through a shifter of its own, tuned to it: it lands on\n"
                              + "the note exactly, cleanly (about twice the CPU, the same latency).\n"
                              + "Off: Low Latency, moving notes through two shared shifters (a few cents off, a little rough)")
            }
        }
        RoleButton {
            id: notesButton
            objectName: "notesButton"
            x: parent.width - width - 12  // (clear of the Color gauge)
            y: 6
            width: 25
            height: 19
            role: "small"
            checkable: false
            checked: editor.expanded
            tooltip: editor.expanded ? qsTr("Hide the note wheel and the finer controls")
                                     : qsTr("Show the note wheel (click the notes one by one), Wet Gain, Pre- and Post-Shift and the mutes")
            onClicked: editor.setExpanded(!editor.expanded)

            Icon {
                anchors.centerIn: parent
                name: "fold"
                size: 8
                color: notesButton.look.text
                rotation: -90 + 180 * editor.reveal  // (pointing out folded, back open)
            }
        }
    }

    // The rest: sliding open from under the spectrum's right edge (clipped to what is revealed so far).
    Item {
        id: notesSection
        objectName: "notesSection"
        x: spectrum.x + spectrum.width
        width: Math.round(editor.reveal * notesRow.implicitWidth)
        height: spectrum.height
        visible: editor.reveal > 0
        clip: true

        Row {
            id: notesRow
            x: notesSection.width - implicitWidth  // (it slides in from the left edge)
            height: parent.height
            spacing: editor.groupGap
            opacity: editor.reveal
            leftPadding: 10 - editor.groupGap

            GroupLine {}
            // The notes, one by one: click one on the wheel to switch it.
            HueWheel {
                id: wheel
                objectName: "hueWheel"
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                caption: editor.keyText
                autoNotes: editor.songNotes
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(implicitWidth, notesSection.height - 12)  // (square, as tall as the body less its margins)
                height: width

                HoverHandler {
                    id: wheelHover
                }
                ToolTip.visible: wheelHover.hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("The twelve notes round a colour wheel, as Hue treats them: lit, kept; shaded with an arrow,\n"
                                   + "moved onto the note beside it; dark, taken out. Click a note to switch it.\n"
                                   + "Inside, the sound between Low and High Cut: petals for what comes out, the outline for what goes in")
            }
            GroupLine {}
            // The finer controls.
            Grid {
                id: finer
                anchors.verticalCenter: parent.verticalCenter
                columns: 2
                columnSpacing: 4
                rowSpacing: 6

                Knob {
                    id: wetGainCell
                    objectName: "wet_gain"
                    param: p.get("wet_gain")
                    title: qsTr("Wet Gain")
                    tooltip: qsTr("Wet Gain: the level of the processed band")
                }
                Knob {
                    id: preShiftCell
                    objectName: "pre_shift"
                    param: p.get("pre_shift")
                    title: qsTr("Pre-Shift")
                    tooltip: qsTr("Pre-Shift: shifts the band before the notes are picked (to catch a detuned sound)")
                }
                Knob {
                    id: postShiftCell
                    objectName: "post_shift"
                    param: p.get("post_shift")
                    title: qsTr("Post-Shift")
                    tooltip: qsTr("Post-Shift: tunes the notes themselves")
                }
                Item {
                    width: editor.cellWidth
                    height: postShiftCell.height

                    Column {
                        anchors.centerIn: parent
                        spacing: 4

                        ParamButton {
                            objectName: "mute_low"
                            width: editor.cellWidth
                            param: p.get("mute_low")
                            text: qsTr("Mute Low")
                            tooltip: qsTr("Mute Low: takes away what is below Low Cut, to hear the processed band alone")
                        }
                        ParamButton {
                            objectName: "mute_high"
                            width: editor.cellWidth
                            param: p.get("mute_high")
                            text: qsTr("Mute High")
                            tooltip: qsTr("Mute High: takes away what is above High Cut")
                        }
                    }
                }
            }
        }
    }
}
