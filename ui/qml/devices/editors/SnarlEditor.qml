import QtQuick
import QtQuick.Controls
import SUBstation

// Snarl's editor, compact, made to play with. At its left the mouth: six styles
// to start from (Growl, Reese, Wub, Screech, Riddim, Tearout: each sets the
// sound in one undo step), over the scope (SnarlScope: the sound's cycle,
// coloured by the growl as it plays) with the dice in its corner (a click rolls
// a new bass, Shift+click nudges the one there: one undo step each) and, with
// the Vowel filter, the vowel it speaks. Then the sections, a name over each
// that lights as it works: OSC (Octave, Wave, Unison, Detune, Width), SUB (Sub,
// Split), GROWL (its mode, Growl, Ratio), FILTER (its type, Freq, Res), DIRT (its
// type, Drive, Squash), WOBBLE (Rate, Shape, Retrig; how far it moves the
// filter, the growl and the pitch; its shape with a dot riding it: SnarlWobble)
// and AMP (Glide, Attack, Release, Volume).
//
// Its animations, subtle and only while it sounds: the scope's trace easing
// from cycle to cycle and running from the accent to acid green to magenta as it
// growls; the Growl, Freq and Drive knobs, and the wobble's three, haloed as hard
// as each works now; the section names lighting; the dot riding the wobble; a
// style or a roll swelling the trace, the dice tumbling to its new face. All of
// it from the displays (DisplayValues: `growl`, `lfo`, `level`, `filter`).
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property int knobSize: 28
    // The knobs' cells as wide as the widest caption or readout any of them shows (EditorTextsWidth, in the
    // font they are drawn in), the lists as their longest names with the arrow.
    readonly property int knobWidth: Math.max(46, knobTexts.needed + 2)
    function listWidth(choice, names) {
        return names.needed + choice.button.leftPadding + choice.button.rightPadding
    }
    readonly property int mouthWidth: 152
    readonly property int sectionGap: 5

    implicitWidth: sections.x + sections.implicitWidth + 8

    EditorTextsWidth {
        id: knobTexts
        knobs: [waveKnob, unisonKnob, detuneKnob, widthKnob, subKnob, splitKnob, growlKnob, ratioKnob, cutoffKnob,
                resonanceKnob, driveKnob, squashKnob, wobbleFilterKnob, wobbleGrowlKnob, wobblePitchKnob, glideKnob,
                attackKnob, releaseKnob, volumeKnob]
    }
    EditorTextsWidth {
        id: growlModes
        font: growlMode.button.font
        texts: growlMode.names
    }
    EditorTextsWidth {
        id: filterTypes
        font: filterType.button.font
        texts: filterType.names
    }
    EditorTextsWidth {
        id: dirtTypes
        font: dirtType.button.font
        texts: dirtType.names
    }
    EditorTextsWidth {
        id: rates
        font: wobbleRate.button.font
        texts: wobbleRate.names
    }
    EditorTextsWidth {
        id: shapes
        font: wobbleShape.button.font
        texts: wobbleShape.names
    }
    implicitHeight: 6 + Math.max(sections.implicitHeight, 132) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["wave", "unison", "detune", "width", "octave", "sub", "split", "growl_mode", "growl", "ratio",
              "filter_type", "cutoff", "resonance", "dirt_type", "drive", "wobble_rate", "wobble_shape",
              "wobble_filter", "wobble_growl", "wobble_pitch", "wobble_retrig", "squash", "glide", "attack",
              "release", "volume"]
    }

    // --- What it plays, for the animations ------------------------------------------------

    DisplayValue {
        id: growlDisplay
        objectName: "growlDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "growl"
        smoothing: 0.12
        width: 0
        height: 0
    }
    DisplayValue {
        id: lfoDisplay
        objectName: "lfoDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "lfo"
        smoothing: 0.05
        width: 0
        height: 0
    }
    DisplayValue {
        id: levelDisplay
        objectName: "levelDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "level"
        smoothing: 0.25
        width: 0
        height: 0
    }
    DisplayValue {
        id: filterDisplay
        objectName: "filterDisplay"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        display: "filter"
        smoothing: 0.05
        width: 0
        height: 0
    }

    readonly property bool sounding: levelDisplay.active && editor.visible
    readonly property real level: sounding ? Math.min(1, levelDisplay.smoothed * 2) : 0
    readonly property real wobble: lfoDisplay.active ? lfoDisplay.smoothed : 0
    function amount(id) {
        const param = p.get(id)
        return param ? param.value : 0
    }
    function choiceOf(id) {
        const param = p.get(id)
        return param ? param.index : 0
    }
    function mix(a, b, t) {
        t = Math.max(0, Math.min(1, t))
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1)
    }
    readonly property color heat: scope.traceColor

    // The Vowel filter's vowel at a place along the filter's range (0..1): the nearest, or the two it is between.
    readonly property string vowels: "UOAEI"
    function vowelText(position) {
        const place = Math.max(0, Math.min(1, position)) * 4
        const nearest = Math.round(place)
        if (Math.abs(place - nearest) < 0.2)
            return vowels[nearest]
        return vowels[Math.floor(place)] + "→" + vowels[Math.min(4, Math.floor(place) + 1)]
    }
    function freqPosition(hz) {
        return Math.log(Math.max(30, Math.min(18000, hz)) / 30) / Math.log(18000 / 30)
    }

    // --- Styles and the dice ---------------------------------------------------------------

    // The style last chosen (view state: it isn't saved).
    property string style: ""
    readonly property var styles: [
        {name: "Growl", tip: qsTr("Growl: FM through a talking vowel filter, the wobble moving both"),
         patch: {wave: 50, unison: 2, detune: 10, width: 40, octave: 0, sub: 60, split: 110, growl_mode: 0, growl: 45,
                 ratio: 3, filter_type: 2, cutoff: 1200, resonance: 45, dirt_type: 0, drive: 55, wobble_rate: 7,
                 wobble_shape: 0, wobble_filter: 35, wobble_growl: 45, wobble_pitch: 0, wobble_retrig: 1,
                 squash: 70, glide: 40, attack: 2, release: 60}},
        {name: "Reese", tip: qsTr("Reese: seven detuned saws, wide and slowly breathing"),
         patch: {wave: 50, unison: 7, detune: 30, width: 85, octave: 0, sub: 70, split: 140, growl_mode: 0, growl: 0,
                 ratio: 1, filter_type: 0, cutoff: 2200, resonance: 15, dirt_type: 0, drive: 30, wobble_rate: 0,
                 wobble_shape: 0, wobble_filter: 15, wobble_growl: 0, wobble_pitch: 0, wobble_retrig: 0,
                 squash: 35, glide: 80, attack: 5, release: 150}},
        {name: "Wub", tip: qsTr("Wub: a resonant low-pass opened and shut on the beat, clipped hard"),
         patch: {wave: 50, unison: 3, detune: 12, width: 45, octave: 0, sub: 65, split: 100, growl_mode: 0, growl: 10,
                 ratio: 3, filter_type: 0, cutoff: 180, resonance: 60, dirt_type: 1, drive: 50, wobble_rate: 4,
                 wobble_shape: 0, wobble_filter: 75, wobble_growl: 15, wobble_pitch: 0, wobble_retrig: 1,
                 squash: 50, glide: 30, attack: 2, release: 60}},
        {name: "Screech", tip: qsTr("Screech: synced squares an octave up through a comb, rectified"),
         patch: {wave: 75, unison: 3, detune: 20, width: 50, octave: 1, sub: 55, split: 160, growl_mode: 1, growl: 55,
                 ratio: 3, filter_type: 3, cutoff: 1800, resonance: 70, dirt_type: 2, drive: 45, wobble_rate: 9,
                 wobble_shape: 3, wobble_filter: 30, wobble_growl: 35, wobble_pitch: 0, wobble_retrig: 1,
                 squash: 60, glide: 20, attack: 1, release: 50}},
        {name: "Riddim", tip: qsTr("Riddim: one folded square, a band-pass chopped in triplets"),
         patch: {wave: 75, unison: 1, detune: 0, width: 0, octave: 0, sub: 70, split: 120, growl_mode: 2, growl: 40,
                 ratio: 3, filter_type: 1, cutoff: 650, resonance: 50, dirt_type: 1, drive: 65, wobble_rate: 8,
                 wobble_shape: 4, wobble_filter: 50, wobble_growl: 25, wobble_pitch: 0, wobble_retrig: 1,
                 squash: 75, glide: 0, attack: 1, release: 30}},
        {name: "Tearout", tip: qsTr("Tearout: ring-modulated, vowels at random every sixteenth, rectified and squashed"),
         patch: {wave: 50, unison: 4, detune: 15, width: 60, octave: 0, sub: 60, split: 130, growl_mode: 3, growl: 50,
                 ratio: 7, filter_type: 2, cutoff: 900, resonance: 60, dirt_type: 2, drive: 70, wobble_rate: 9,
                 wobble_shape: 5, wobble_filter: 55, wobble_growl: 50, wobble_pitch: 0, wobble_retrig: 1,
                 squash: 85, glide: 25, attack: 1, release: 40}}
    ]

    // Sets `values` (id -> value) in one undo step named `text`.
    function apply(values, text) {
        const ids = Object.keys(values)
        Session.editor.setDeviceParams(editor.trackId, editor.deviceId, ids, ids.map(id => values[id]), "", text)
        scope.punch()
    }
    function applyStyle(index) {
        const chosen = editor.styles[index]
        editor.style = chosen.name
        editor.apply(chosen.patch, qsTr("Snarl Style: %1").arg(chosen.name))
    }

    // The dice: a new bass from the ranges bass music lives in (the sub, the split and the amp kept as they
    // are), or (`nudge`) the one there moved a little.
    function roll(nudge) {
        const r = Math.random
        const pick = list => list[Math.floor(r() * list.length)]
        const values = {}
        if (nudge) {
            for (const id of ["wave", "detune", "width", "growl", "resonance", "drive", "wobble_filter", "wobble_growl",
                              "squash"]) {
                const param = p.get(id)
                if (param)
                    values[id] = Math.max(param.minimum, Math.min(param.maximum,
                                                                  param.value + (r() - 0.5) * 0.3 * (param.maximum - param.minimum)))
            }
            const cutoff = p.get("cutoff")
            if (cutoff)
                values.cutoff = Math.max(cutoff.minimum, Math.min(cutoff.maximum, cutoff.value * Math.pow(2, (r() - 0.5) * 1.4)))
        } else {
            values.growl_mode = Math.floor(r() * 4)
            values.growl = 15 + r() * 65
            values.ratio = Math.floor(r() * 8)
            values.wave = r() * 100
            values.unison = 1 + Math.floor(r() * 5)
            values.detune = r() * 35
            values.width = r() * 80
            values.octave = r() < 0.75 ? 0 : 1
            values.filter_type = Math.floor(r() * 4)
            values.cutoff = 150 * Math.pow(25, r())
            values.resonance = 10 + r() * 70
            values.dirt_type = Math.floor(r() * 4)
            values.drive = 20 + r() * 65
            values.wobble_rate = pick([4, 5, 7, 8, 9, 10])
            values.wobble_shape = Math.floor(r() * 6)
            values.wobble_filter = 15 + r() * 70
            values.wobble_growl = r() * 60
            values.wobble_pitch = r() < 0.8 ? 0 : pick([-12, 12])
            values.squash = 35 + r() * 55
            editor.style = ""
        }
        dice.face = 1 + Math.floor(r() * 6)
        tumble.restart()
        editor.apply(values, nudge ? qsTr("Nudge Snarl") : qsTr("Roll Snarl"))
    }

    // --- The mouth -------------------------------------------------------------------------

    Item {
        id: mouth
        objectName: "mouth"
        x: 8
        y: 6
        width: editor.mouthWidth
        height: editor.height - 12

        Grid {
            id: chips
            objectName: "styles"
            width: parent.width
            columns: 3
            columnSpacing: 2
            rowSpacing: 2

            Repeater {
                model: editor.styles.length
                RoleButton {
                    required property int index
                    readonly property var entry: editor.styles[index]
                    objectName: "style" + entry.name
                    width: (chips.width - 2 * chips.columnSpacing) / 3
                    height: 16
                    role: "small"
                    text: entry.name
                    tooltip: entry.tip
                    lit: editor.style === entry.name
                    onClicked: editor.applyStyle(index)
                }
            }
        }

        SnarlScope {
            id: scope
            objectName: "scope"
            session: Session
            trackId: editor.trackId
            deviceId: editor.deviceId
            y: chips.height + 4
            width: parent.width
            height: parent.height - y

            // With the Vowel filter, the vowel it speaks now, faint behind the trace.
            Text {
                objectName: "vowel"
                anchors.centerIn: parent
                visible: editor.choiceOf("filter_type") === 2
                text: editor.vowelText(filterDisplay.active ? filterDisplay.smoothed
                                                            : editor.freqPosition(editor.amount("cutoff")))
                color: editor.heat
                opacity: 0.1 + 0.18 * scope.activity
                font.family: Theme.uiFont(8).family
                font.pixelSize: Math.min(34, parent.height * 0.45)
                font.bold: true
            }

            HoverHandler {
                id: scopeHover
            }
            ToolTip.visible: scopeHover.hovered && !diceHover.hovered
            ToolTip.delay: 700
            ToolTip.text: qsTr("What it plays: its latest cycles, coloured by the growl")
        }

        // The dice, in the scope's corner: a click rolls a new bass, Shift+click nudges this one.
        Rectangle {
            id: dice
            objectName: "dice"
            property int face: 5
            x: scope.x + scope.width - width - 5
            y: scope.y + scope.height - height - 5
            width: 17
            height: 17
            radius: 4
            color: diceArea.pressed ? Theme.accent : (diceHover.hovered ? Theme.surfaceHover : Theme.surface)
            border.color: diceHover.hovered ? Theme.accent : Theme.border
            antialiasing: true

            // Its pips, for its face (1..6).
            Repeater {
                model: [[0.5, 0.5, [1, 3, 5]], [0.25, 0.25, [2, 3, 4, 5, 6]], [0.75, 0.75, [2, 3, 4, 5, 6]],
                        [0.75, 0.25, [4, 5, 6]], [0.25, 0.75, [4, 5, 6]], [0.25, 0.5, [6]], [0.75, 0.5, [6]]]
                Rectangle {
                    required property var modelData
                    width: 3
                    height: 3
                    radius: 1.5
                    x: modelData[0] * dice.width - 1.5
                    y: modelData[1] * dice.height - 1.5
                    color: diceArea.pressed ? Theme.accentText : Theme.text
                    visible: modelData[2].indexOf(dice.face) >= 0
                }
            }

            SequentialAnimation {
                id: tumble
                NumberAnimation { target: dice; property: "rotation"; from: 0; to: 360; duration: 420; easing.type: Easing.OutBack }
            }

            MouseArea {
                id: diceArea
                anchors.fill: parent
                onClicked: mouse => editor.roll(mouse.modifiers & Qt.ShiftModifier)
            }
            HoverHandler {
                id: diceHover
            }
            ToolTip.visible: diceHover.hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Roll a new bass (Shift: nudge this one)")
        }
    }

    // --- The sections ----------------------------------------------------------------------

    // A section: its name over it, lit (in `tint`) as hard as `glow` says, then what is given.
    component Section: Column {
        id: section
        property string title: ""
        property color tint: Theme.accent
        property real glow: 0
        spacing: 2

        Item {
            width: name.implicitWidth
            height: 12
            Text {
                id: name
                objectName: "title"
                anchors.verticalCenter: parent.verticalCenter
                text: section.title
                color: editor.mix(Theme.textDim, section.tint, section.glow)
                font.family: Theme.uiFont(8).family
                font.pointSize: 7
                font.bold: true
                font.letterSpacing: 1
            }
            Rectangle {  // the rule after it, across the section
                x: name.implicitWidth + 4
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, section.width - x)
                height: 1
                color: section.tint
                opacity: 0.18 + 0.5 * section.glow
            }
        }
    }

    // A knob with a halo round its dial as hard as `glow` says (what it does now, as it plays).
    component GlowKnob: EditorKnob {
        id: glowKnob
        property real glow: 0
        property color glowColor: Theme.accent
        width: editor.knobWidth
        size: editor.knobSize

        Rectangle {  // (in the knob, behind its dial)
            objectName: "halo"
            parent: glowKnob.knob
            z: -1
            anchors.centerIn: parent
            width: glowKnob.size + 8
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 3
            border.color: glowKnob.glowColor
            opacity: 0.5 * glowKnob.glow
            visible: opacity > 0.01
        }
    }

    // A section's line of controls over its knobs (a list, a box), as tall in every section.
    component TopRow: Row {
        height: 16
        spacing: 3
    }

    component Divider: Rectangle {
        width: 1
        height: oscSection.height
        color: Theme.border
    }

    Row {
        id: sections
        objectName: "sections"
        x: mouth.x + mouth.width + 8
        y: Math.max(6, Math.round((editor.height - height) / 2))
        spacing: editor.sectionGap

        Section {
            id: oscSection
            objectName: "osc"
            title: qsTr("OSC")
            width: oscKnobs.width
            glow: editor.level * 0.6
            TopRow {
                EditorCaption {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Octave")
                }
                ParamBox {
                    objectName: "octave"
                    width: 34
                    height: 16
                    param: p.get("octave")
                    step: 1
                    decimals: 0
                    defaultValue: 0
                    sampleText: "+2"
                    formatter: v => (v > 0 ? "+" : "") + Math.round(v)
                    tooltip: qsTr("Octave: the oscillators' octave (the sub stays at the note)")
                }
            }
            Grid {
                id: oscKnobs
                columns: 2
                columnSpacing: 2
                EditorKnob {
                    id: waveKnob
                    objectName: "wave"
                    width: editor.knobWidth
                    size: editor.knobSize
                    param: p.get("wave")
                    title: qsTr("Wave")
                    formatter: v => ["Sine", "Tri", "Saw", "Square", "Pulse"][Math.round(v / 25)]
                                    + (Math.abs(v / 25 - Math.round(v / 25)) > 0.02 ? "~" : "")
                    tooltip: qsTr("Wave: sine, triangle, saw, square and a narrow pulse, morphing between them")
                }
                EditorKnob {
                    id: unisonKnob
                    objectName: "unison"
                    width: editor.knobWidth
                    size: editor.knobSize
                    step: 1
                    param: p.get("unison")
                    title: qsTr("Unison")
                    formatter: v => Math.round(v) === 1 ? qsTr("1 voice") : qsTr("%1 voices").arg(Math.round(v))
                    tooltip: qsTr("Unison: how many oscillators play the note")
                }
                EditorKnob {
                    id: detuneKnob
                    objectName: "detune"
                    width: editor.knobWidth
                    size: editor.knobSize
                    param: p.get("detune")
                    title: qsTr("Detune")
                    tooltip: qsTr("Detune: the unison spread apart in pitch (cents either way): the reese's beating")
                }
                EditorKnob {
                    id: widthKnob
                    objectName: "width"
                    width: editor.knobWidth
                    size: editor.knobSize
                    param: p.get("width")
                    title: qsTr("Width")
                    tooltip: qsTr("Width: the unison spread across the stereo field (the sub stays in the middle)")
                }
            }
        }
        Divider {}
        Section {
            objectName: "subSection"
            title: qsTr("SUB")
            width: editor.knobWidth
            glow: editor.level * editor.amount("sub") / 100
            TopRow {}
            GlowKnob {
                id: subKnob
                objectName: "sub"
                param: p.get("sub")
                title: qsTr("Sub")
                glow: editor.level * editor.amount("sub") / 100
                tooltip: qsTr("Sub: a clean sine at the note, under everything")
            }
            EditorKnob {
                id: splitKnob
                objectName: "split"
                width: editor.knobWidth
                size: editor.knobSize
                param: p.get("split")
                title: qsTr("Split")
                tooltip: qsTr("Split: the rest high-passed here, so the sub owns the lows")
            }
        }
        Divider {}
        Section {
            objectName: "growlSection"
            title: qsTr("GROWL")
            width: Math.max(editor.knobWidth, growlMode.width)
            tint: editor.heat
            glow: editor.sounding ? growlDisplay.smoothed : 0
            TopRow {
                ParamChoice {
                    id: growlMode
                    objectName: "growlMode"
                    width: editor.listWidth(growlMode, growlModes)
                    param: p.get("growl_mode")
                    tooltip: qsTr("Growl Mode: FM, Sync (windowed), Fold or Ring")
                }
            }
            GlowKnob {
                id: growlKnob
                objectName: "growl"
                anchors.horizontalCenter: parent.horizontalCenter
                param: p.get("growl")
                title: qsTr("Growl")
                color: editor.heat
                glowColor: editor.heat
                glow: editor.sounding ? growlDisplay.smoothed : 0
                tooltip: qsTr("Growl: how hard the mode bends the oscillators")
            }
            EditorKnob {
                id: ratioKnob
                objectName: "ratio"
                anchors.horizontalCenter: parent.horizontalCenter
                width: editor.knobWidth
                size: editor.knobSize
                step: 1
                param: p.get("ratio")
                title: qsTr("Ratio")
                formatter: v => "×" + ["0.5", "1", "1.5", "2", "3", "4", "5", "7"][Math.max(0, Math.min(7, Math.round(v)))]
                opacity: editor.choiceOf("growl_mode") === 0 || editor.choiceOf("growl_mode") === 3 ? 1 : 0.55
                tooltip: qsTr("Ratio: the modulator's frequency to the note's (FM and Ring)")
            }
        }
        Divider {}
        Section {
            objectName: "filterSection"
            title: qsTr("FILTER")
            width: Math.max(editor.knobWidth, filterType.width)
            glow: editor.sounding ? Math.min(1, 0.25 + editor.wobble * editor.amount("wobble_filter") / 100) * editor.level : 0
            TopRow {
                ParamChoice {
                    id: filterType
                    objectName: "filterType"
                    width: editor.listWidth(filterType, filterTypes)
                    param: p.get("filter_type")
                    tooltip: qsTr("Filter: Low, Band, Vowel (the frequency picks the vowel) or Comb")
                }
            }
            GlowKnob {
                id: cutoffKnob
                objectName: "cutoff"
                anchors.horizontalCenter: parent.horizontalCenter
                param: p.get("cutoff")
                title: editor.choiceOf("filter_type") === 2 ? qsTr("Vowel") : qsTr("Freq")
                formatter: editor.choiceOf("filter_type") === 2 ? (v => editor.vowelText(editor.freqPosition(v))) : null
                glow: editor.sounding ? editor.wobble * editor.amount("wobble_filter") / 100 : 0
                tooltip: qsTr("Freq: where the filter sits (with Vowel, the vowel: U O A E I, dark to bright); the wobble opens it from here")
            }
            EditorKnob {
                id: resonanceKnob
                objectName: "resonance"
                anchors.horizontalCenter: parent.horizontalCenter
                width: editor.knobWidth
                size: editor.knobSize
                param: p.get("resonance")
                title: qsTr("Res")
                tooltip: qsTr("Resonance")
            }
        }
        Divider {}
        Section {
            objectName: "dirtSection"
            title: qsTr("DIRT")
            width: Math.max(editor.knobWidth, dirtType.width)
            tint: Theme.recordOn
            glow: editor.level * editor.amount("drive") / 100
            TopRow {
                ParamChoice {
                    id: dirtType
                    objectName: "dirtType"
                    width: editor.listWidth(dirtType, dirtTypes)
                    param: p.get("dirt_type")
                    tooltip: qsTr("Dirt: Tube, Hard, Rectify (an octave up) or Crush")
                }
            }
            GlowKnob {
                id: driveKnob
                objectName: "drive"
                anchors.horizontalCenter: parent.horizontalCenter
                param: p.get("drive")
                title: qsTr("Drive")
                glowColor: Theme.recordOn
                glow: editor.level * editor.amount("drive") / 100
                tooltip: qsTr("Drive: how hard it is driven into the dirt")
            }
            GlowKnob {
                id: squashKnob
                objectName: "squash"
                anchors.horizontalCenter: parent.horizontalCenter
                param: p.get("squash")
                title: qsTr("Squash")
                glow: editor.level * editor.amount("squash") / 100
                tooltip: qsTr("Squash: multiband upward and downward compression (Over The Top's), what makes it loud and in your face")
            }
        }
        Divider {}
        Section {
            objectName: "wobbleSection"
            title: qsTr("WOBBLE")
            width: Math.max(wobbleKnobs.width, wobbleTop.implicitWidth)
            glow: editor.sounding ? editor.wobble : 0
            TopRow {
                id: wobbleTop
                ParamChoice {
                    id: wobbleRate
                    objectName: "wobbleRate"
                    width: editor.listWidth(wobbleRate, rates)
                    param: p.get("wobble_rate")
                    tooltip: qsTr("Rate: the wobble's cycle, a note value of the song (D dotted, T triplet)")
                }
                ParamChoice {
                    id: wobbleShape
                    objectName: "wobbleShape"
                    width: editor.listWidth(wobbleShape, shapes)
                    param: p.get("wobble_shape")
                    tooltip: qsTr("Shape: the wobble's shape")
                }
                ParamButton {
                    objectName: "retrig"
                    width: 18
                    height: 16
                    param: p.get("wobble_retrig")
                    text: "R"
                    tooltip: qsTr("Retrig: each note starts the wobble again (off: it follows the song)")
                }
            }
            Grid {
                id: wobbleKnobs
                columns: 3
                columnSpacing: 2
                GlowKnob {
                    id: wobbleFilterKnob
                    objectName: "wobbleFilter"
                    param: p.get("wobble_filter")
                    title: qsTr("> Filter")
                    glow: editor.sounding ? editor.wobble * editor.amount("wobble_filter") / 100 : 0
                    tooltip: qsTr("Wobble > Filter: how far the wobble opens the filter (up to 6 octaves)")
                }
                GlowKnob {
                    id: wobbleGrowlKnob
                    objectName: "wobbleGrowl"
                    param: p.get("wobble_growl")
                    title: qsTr("> Growl")
                    glowColor: editor.heat
                    glow: editor.sounding ? editor.wobble * editor.amount("wobble_growl") / 100 : 0
                    tooltip: qsTr("Wobble > Growl: how much growl the wobble adds")
                }
                GlowKnob {
                    id: wobblePitchKnob
                    objectName: "wobblePitch"
                    param: p.get("wobble_pitch")
                    title: qsTr("> Pitch")
                    glow: editor.sounding ? editor.wobble * Math.min(1, Math.abs(editor.amount("wobble_pitch")) / 12) : 0
                    tooltip: qsTr("Wobble > Pitch: how far the wobble bends the note (semitones; down when negative)")
                }
            }
            SnarlWobble {
                id: wobbleGraph
                objectName: "wobbleGraph"
                session: Session
                trackId: editor.trackId
                deviceId: editor.deviceId
                width: wobbleKnobs.width
                height: waveKnob.height  // (a row of knobs)
            }
        }
        Divider {}
        Section {
            objectName: "ampSection"
            title: qsTr("AMP")
            width: ampKnobs.width
            glow: editor.level * 0.6
            TopRow {}
            Grid {
                id: ampKnobs
                columns: 2
                columnSpacing: 2
                EditorKnob {
                    id: glideKnob
                    objectName: "glide"
                    width: editor.knobWidth
                    size: editor.knobSize
                    param: p.get("glide")
                    title: qsTr("Glide")
                    tooltip: qsTr("Glide: a note played while another is held slides there in this time")
                }
                EditorKnob {
                    id: attackKnob
                    objectName: "attack"
                    width: editor.knobWidth
                    size: editor.knobSize
                    param: p.get("attack")
                    title: qsTr("Attack")
                }
                EditorKnob {
                    id: releaseKnob
                    objectName: "release"
                    width: editor.knobWidth
                    size: editor.knobSize
                    param: p.get("release")
                    title: qsTr("Release")
                }
                EditorKnob {
                    id: volumeKnob
                    objectName: "volume"
                    width: editor.knobWidth
                    size: editor.knobSize
                    param: p.get("volume")
                    title: qsTr("Volume")
                }
            }
        }
    }
}
