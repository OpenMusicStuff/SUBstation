import QtQuick
import QtQuick.Controls
import SUBstation

// Hue's editor: its spectrum (HueSpectrum: what comes out coloured by the
// notes it is, over what goes in, the notes' comb, and the band processed
// between Low Cut and High Cut as handles to drag), then what is always shown:
// the key (a root and a scale, setting the twelve notes at once; "Custom" when
// they are no scale) over HQ (Quality) and MIDI (MIDI Notes), and Color over
// the Notes button. Notes (folded at first; not saved: how the device is looked
// at) slides open the note wheel (HueWheel: the twelve notes round a colour
// wheel as Hue treats them, the sound's chromagram inside; click a note to
// switch it) and the finer controls: Wet Gain, Pre- and Post-Shift, Mute Low
// and High. Every control shows its parameter as it is now, sets it undoably,
// touches it when pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias wheel: wheel
    readonly property alias spectrum: spectrum
    // The knobs' cells: as wide as their captions and every text their readouts take (EditorTextsWidth); Color's
    // as the Notes button under it too.
    readonly property int cellWidth: Math.max(52, cellTexts.needed)
    readonly property int mixWidth: Math.max(cellWidth, Math.ceil(notesButton.implicitWidth))
    readonly property int groupGap: 7  // either side of a group's line
    // Notes: the wheel and the finer controls shown. `reveal` follows it, 0..1, as the section slides.
    readonly property bool expanded: DeviceViews.value(deviceId, "notes", false)
    property real reveal: expanded ? 1 : 0
    Behavior on reveal {
        NumberAnimation {
            duration: 180
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
    readonly property string keyText: {
        if (wheel.midi)
            return qsTr("MIDI")
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
        if (editor.key) {
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

    implicitWidth: Math.ceil(knobs.x + knobs.implicitWidth) + 8
    implicitHeight: 6 + Math.max(knobs.implicitHeight, spectrum.implicitHeight) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["start", "end", "color", "wet_gain", "pre_shift", "post_shift", "mute_low", "mute_high", "quality", "midi",
              "note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f", "note_fs", "note_g", "note_gs", "note_a",
              "note_as", "note_b"]
    }

    EditorTextsWidth {
        id: cellTexts
        knobs: [colorCell, wetGainCell, preShiftCell, postShiftCell]
        texts: [qsTr("Mute Low"), qsTr("Mute High")]
    }

    component Knob: EditorKnob {
        width: editor.cellWidth
    }
    component GroupLine: Rectangle {
        width: 1
        height: knobs.height - 4
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: Theme.border
    }
    // A small drop-down of `names` (as ParamChoice's), `index` shown ("text" instead, when set), chosen(i) on a pick.
    component KeyChoice: Item {
        id: choice

        property var names: []
        property int index: -1
        property string text: ""
        property var extra: []  // entries after a separator: [{ name, index }]
        property string tooltip: ""
        signal chosen(int index)

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
        x: 8
        y: 6
        // (as wide as the Vintage Hall's loop and a knob cell beside it: the two editors as wide folded)
        width: Math.max(110, editor.height - 12) + editor.cellWidth + 4
        height: Math.max(implicitHeight, editor.height - 12)

        HoverHandler {
            id: spectrumHover
        }
        ToolTip.visible: spectrumHover.hovered && !spectrum.dragging && spectrum.hovered === ""
        ToolTip.delay: 700
        ToolTip.text: qsTr("What comes out, coloured by the notes it is (grey outside the band), over what goes in (the faint line);
"
                           + "the thin lines are the notes it is pulled onto. Drag the handles for Low Cut and High Cut, or the
"
                           + "band between them to move both (Shift: finely); double-click a handle to reset it")
    }

    Row {
        id: knobs
        x: spectrum.x + spectrum.width + 10
        y: Math.max(6, Math.round((editor.height - implicitHeight) / 2))
        spacing: editor.groupGap

        // The key, and how.
        Column {
            id: keyGroup
            width: 2 * editor.cellWidth + 4
            spacing: 6

            Item {
                width: parent.width
                height: colorCell.height

                EditorCaption {
                    width: parent.width
                    text: qsTr("Key")
                }
                Row {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 9
                    spacing: 4

                    KeyChoice {
                        id: rootChoice
                        objectName: "rootChoice"
                        width: 36
                        enabled: !wheel.midi
                        names: editor.rootNames
                        index: editor.buttons === 0 && !editor.key ? -1 : editor.shownRoot()
                        tooltip: qsTr("The key's root: the scale moves with it (Custom notes too)")
                        onChosen: index => editor.chooseRoot(index)
                    }
                    KeyChoice {
                        id: scaleChoice
                        objectName: "scaleChoice"
                        width: keyGroup.width - rootChoice.width - 4
                        enabled: !wheel.midi
                        names: editor.scales.map(scale => scale.name)
                        index: editor.key ? editor.key.scale : -1
                        text: wheel.midi ? qsTr("MIDI Notes")
                                         : editor.buttons === 0 ? qsTr("No Notes")
                                                                : editor.key ? "" : qsTr("Custom")
                        extra: [{ name: qsTr("No Notes"), index: -1 }]
                        tooltip: qsTr("The scale: sets the twelve notes at once (Custom: notes that are no scale; open Notes\n"
                                      + "to choose them one by one, or click them on the wheel)")
                        onChosen: index => editor.chooseScale(index)
                    }
                }
            }
            Item {
                width: parent.width
                height: colorCell.height

                EditorCaption {
                    width: parent.width
                    text: qsTr("Mode")
                }
                Row {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 9
                    spacing: 4

                    ParamButton {
                        id: hqButton
                        objectName: "quality"
                        width: editor.cellWidth
                        param: p.get("quality")
                        text: qsTr("HQ")
                        tooltip: qsTr("High Quality: each moved note through a shifter of its own, tuned to it: it lands on\n"
                                      + "the note exactly, cleanly (about twice the CPU, the same latency).\n"
                                      + "Off: Low Latency, moving notes as the original does (a few cents off, a little rough)")
                    }
                    ParamButton {
                        id: midiButton
                        objectName: "midi"
                        width: editor.cellWidth
                        param: p.get("midi")
                        text: qsTr("MIDI")
                        tooltip: qsTr("MIDI Notes: the notes held on the track choose the notes instead of the key\n"
                                      + "(with nothing held, the sound passes)")
                    }
                }
            }
        }
        GroupLine {}
        // How much, and the notes.
        Column {
            spacing: 6

            Knob {
                id: colorCell
                objectName: "color"
                width: editor.mixWidth
                param: p.get("color")
                title: qsTr("Color")
                color: Theme.accent
                tooltip: qsTr("Color: from the plain sound (0 %) to the processed one (100 %); past 100 % the notes
"
                              + "louder and narrower, what lies between them quieter")
            }
            // Notes, level with the knobs' second row.
            Item {
                width: editor.mixWidth
                height: colorCell.height

                RoleButton {
                    id: notesButton
                    objectName: "notesButton"
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 16
                    role: "small"
                    text: editor.expanded ? qsTr("Notes ‹") : qsTr("Notes ›")
                    checkable: false
                    checked: editor.expanded
                    tooltip: editor.expanded ? qsTr("Hide the note wheel and the finer controls")
                                             : qsTr("Show the note wheel (click the notes one by one), Wet Gain, Pre- and Post-Shift and the mutes")
                    onClicked: editor.setExpanded(!editor.expanded)
                }
            }
        }

        // Notes: sliding open from under the controls before it (clipped to what is revealed so far).
        Item {
            id: notesSection
            objectName: "notesSection"
            width: Math.round(editor.reveal * notesRow.implicitWidth)
            height: notesRow.implicitHeight
            visible: editor.reveal > 0
            clip: true

            Row {
                id: notesRow
                x: notesSection.width - implicitWidth  // (it slides in from the left edge)
                spacing: editor.groupGap
                opacity: editor.reveal

                GroupLine {}
                // The notes, one by one: click one on the wheel to switch it.
                HueWheel {
                    id: wheel
                    objectName: "hueWheel"
                    session: Session
                    trackId: editor.trackId
                    deviceId: editor.deviceId
                    caption: editor.keyText
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(implicitWidth, 2 * colorCell.height + 6)  // (square, as tall as two rows of knobs)
                    height: width

                    HoverHandler {
                        id: wheelHover
                    }
                    ToolTip.visible: wheelHover.hovered
                    ToolTip.delay: 700
                    ToolTip.text: qsTr("The twelve notes round a colour wheel, as Hue treats them: lit, kept; shaded with an arrow,
"
                                       + "moved onto the note beside it; dark, taken out. Click a note to switch it.
"
                                       + "Inside, the sound between Low and High Cut: petals for what comes out, the outline for what goes in")
                }
                GroupLine {}
                // The finer controls.
                Grid {
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
}
