import QtQuick
import QtQuick.Controls
import SUBstation

// The bar on top of the window, in three groups, as a hardware transport lays
// them out. On the left, the song: a display of the tempo, the time signature,
// the swing (the project's default groove) and the project key; the metronome
// (its count-in a drop-down on its right), the computer MIDI keyboard and
// Constrain Delay Compensation. In the middle of the window (while there is
// room), the transport: the position (bar, beat, sixteenth), Play, Stop,
// Record, Re-Enable Automation and the oscilloscope. On the right, Punch In,
// loop, Punch Out and follow, and a display of the CPU load (its last seconds
// drawn behind it), the disk (while files stream from it) and the audio device
// (a click opens Preferences). (Lock Envelopes is in the arrangement's corner,
// as Ableton's: GridInfo.)
//
// Two kinds of thing, each with one look. Displays are wells set into the bar
// (Theme.display: dark, but in a light theme), each value over a small
// caption, as Logic's and Studio One's; what can be changed in one is tinted
// under the mouse. Buttons are raised, side by
// side where they belong together (one border between them). Play and Record
// fill with their colour while on, as Live's, and so does Re-Enable
// Automation while there is automation to bring back; the other switches
// light their icon in the accent over a faint wash of it, as Bitwig's.
//
// Its controls show the project, the bridge and the session, and call them:
// Play's and Record's checked state follows the bridge, never the click; the
// toggles show what the model has (a click asks it to change). Every display,
// button and the scope are Theme.controlHeight (26 px) tall; none takes the
// keyboard focus, so Space stays play/stop.
Rectangle {
    id: bar

    // The arrangement view (its `follow`, read and written by the Follow button).
    property Item arrangement: null
    readonly property alias transportState: transport

    // The device's name was clicked.
    signal preferencesRequested()

    // The displays' type: values, the position's bigger digits, and the
    // captions under them (small capitals, spaced out).
    readonly property font valueFont: Qt.font({family: Theme.font.family, pointSize: 9, weight: Font.DemiBold})
    readonly property font positionFont: Qt.font({family: Theme.font.family, pointSize: 10.5,
                                                  weight: Font.DemiBold})
    readonly property font captionFont: Qt.font({family: Theme.font.family, pointSize: 6.5, weight: Font.DemiBold,
                                                 letterSpacing: 0.7, capitalization: Font.AllUppercase})
    // A caption that is a reading, not a name ("48 kHz"): as it is written.
    readonly property font readingFont: Qt.font({family: Theme.font.family, pointSize: 6.5, weight: Font.DemiBold,
                                                 letterSpacing: 0.5})
    // A caption's line: its room along a display's bottom, and where its baseline is.
    readonly property int captionRoom: 10
    readonly property int captionBaseline: 22

    // `color` at `opacity` (Qt 6.4's QML has no Qt.alpha()).
    function faded(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity)
    }

    objectName: "transportBar"
    implicitHeight: 34
    color: Theme.panel

    TransportState {
        id: transport
        session: Session
    }

    // A digit of the position's place: as wide as the widest digit.
    TextMetrics {
        id: digitMetrics
        readonly property real slot: Math.ceil(advanceWidth)
        font: bar.positionFont
        text: "0"
    }

    // The line along its bottom.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    // --- Displays ---------------------------------------------------------------------

    // A well set into the bar (shaded along its top inside), its fields side by side.
    component Display: Rectangle {
        default property alias fields: fieldRow.data

        implicitWidth: fieldRow.implicitWidth
        implicitHeight: Theme.controlHeight
        radius: Theme.radius
        color: Theme.display
        border.color: Theme.border

        Rectangle {
            x: Theme.radius
            y: 1
            width: parent.width - 2 * Theme.radius
            height: 1
            color: Qt.rgba(0, 0, 0, 0.09)
        }
        Row {
            id: fieldRow
            height: parent.height
        }
    }

    // The line between two fields of a display.
    component FieldLine: Rectangle {
        y: 6
        width: 1
        height: Theme.controlHeight - 12
        color: bar.faded(Theme.text, 0.09)
    }

    // A field's value, centred above the caption's room.
    component FieldValue: Text {
        width: parent.width
        height: parent.height - bar.captionRoom
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font: bar.valueFont
        color: Theme.text
    }

    // A field's caption, under its value.
    component Caption: Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: bar.captionBaseline - baselineOffset
        font: bar.captionFont
        color: Theme.textDim
    }

    // Under the mouse (or open), a field one can change is tinted.
    component FieldTint: Rectangle {
        property bool shown: false
        anchors.fill: parent
        anchors.margins: 1
        radius: Theme.radius - 1
        color: shown ? bar.faded(Theme.text, 0.07) : "transparent"
    }

    // A field of text: its value over its caption, `padding` either side.
    component Field: Item {
        property alias value: valueText.text
        property alias valueColor: valueText.color
        property alias caption: captionText.text
        property alias captionFont: captionText.font
        property int padding: 8

        implicitWidth: Math.max(valueText.implicitWidth, captionText.implicitWidth) + 2 * padding
        height: Theme.controlHeight

        FieldValue {
            id: valueText
            x: parent.padding
            width: parent.width - 2 * parent.padding
            elide: Text.ElideRight
        }
        Caption {
            id: captionText
        }
    }

    // A number of the display that drags (ValueBox), its caption under it.
    component FieldBox: ValueBox {
        property alias caption: boxCaption.text

        width: implicitWidth
        height: Theme.controlHeight
        bare: true
        bottomPadding: bar.captionRoom
        font: bar.valueFont

        Caption {
            id: boxCaption
        }
    }

    // A part of the position (bar, beat or sixteenth), its digits over its
    // caption: each digit in a place of its own, as wide as the widest, so the
    // number holds still as it counts.
    component PositionPart: Item {
        id: part

        property string value
        property alias caption: partCaption.text

        height: Theme.controlHeight

        Row {
            anchors.horizontalCenter: parent.horizontalCenter

            Repeater {
                model: part.value.split("")

                FieldValue {
                    required property string modelData
                    width: digitMetrics.slot
                    height: Theme.controlHeight - bar.captionRoom
                    text: modelData
                    font: bar.positionFont
                }
            }
        }
        Caption {
            id: partCaption
        }
    }

    // The dot between two parts of the position.
    component PositionDot: Text {
        width: 4
        height: Theme.controlHeight - bar.captionRoom
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: "."
        font: bar.positionFont
        color: Theme.textDim
    }

    // --- Buttons ----------------------------------------------------------------------

    // A button's face: raised (lit faintly along its top), lighter under the
    // mouse, darker pressed; `wash` over it while a switch is on, or all `fill`.
    component Face: Rectangle {
        property bool hovered: false
        property bool down: false
        property color wash: "transparent"
        property color fill: "transparent"

        readonly property color ground: down ? Theme.panel : hovered ? Theme.surfaceHover : Theme.surface

        color: fill.a > 0 ? (hovered && !down ? Qt.lighter(fill, 1.08) : fill) : Qt.tint(ground, wash)
        border.color: Theme.border

        Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: 1
            color: Qt.rgba(1, 1, 1, parent.fill.a > 0 ? 0.22 : 0.05)
        }
    }

    // A switch of the bar. On, its icon lights in the accent over a wash of it;
    // with a `fill` (Play's, Record's), it fills with that instead, its icon dark.
    component BarSwitch: ToggleButton {
        id: button

        property color fill: "transparent"
        readonly property bool fills: fill.a > 0

        implicitWidth: 28
        implicitHeight: Theme.controlHeight
        role: "tool"
        checkable: false
        iconColor: !enabled || !lit ? undefined : fills ? Theme.accentText : Theme.accent

        background: Face {
            hovered: button.hovered
            down: button.down
            wash: button.lit && !button.fills ? bar.faded(Theme.accent, 0.14) : "transparent"
            fill: button.lit && button.fills ? button.fill : "transparent"
        }
    }

    // Buttons that belong together, side by side: one border between each two.
    component Segments: Row {
        spacing: -1
    }

    // --- The song ---------------------------------------------------------------------

    Row {
        id: song
        x: 8
        y: 4
        spacing: 4

        Display {
            FieldBox {
                id: tempo
                objectName: "tempo"
                caption: qsTr("BPM")
                from: 20
                to: 999
                step: 0.25
                decimals: 2
                sampleText: "999.00"
                value: Session.project.tempo
                onMoved: (value, gestureKey) => Session.editor.setTempo(value, gestureKey)

                ToolTip.visible: hovered && !dragging
                ToolTip.text: qsTr("Tempo (drag, Shift for fine steps, double-click to type)")
                ToolTip.delay: 700
            }
            FieldLine {}
            // The time signature: two numbers, one caption under both.
            Item {
                width: signature.width + 12
                height: Theme.controlHeight

                Row {
                    id: signature
                    x: 6

                    FieldBox {
                        objectName: "numerator"
                        width: 17
                        from: 1
                        to: 32
                        step: 0.1
                        decimals: 0
                        sampleText: "32"
                        formatter: value => String(Math.round(value))
                        value: transport.numerator
                        onMoved: value => Session.editor.setTimeSignature(Math.round(value), transport.denominator)

                        ToolTip.visible: hovered && !dragging
                        ToolTip.text: qsTr("Beats in a bar (drag or wheel)")
                        ToolTip.delay: 700
                    }
                    FieldValue {
                        width: 5
                        height: Theme.controlHeight - bar.captionRoom
                        text: "/"
                        color: Theme.textDim
                    }
                    FieldBox {
                        objectName: "denominator"
                        width: 17
                        from: 1
                        to: 32
                        decimals: 0
                        choices: transport.denominators
                        sampleText: "32"
                        formatter: value => String(Math.round(value))
                        value: transport.denominator
                        onMoved: value => Session.editor.setTimeSignature(transport.numerator, Math.round(value))

                        ToolTip.visible: hovered && !dragging
                        ToolTip.text: qsTr("The note a beat is (drag or wheel)")
                        ToolTip.delay: 700
                    }
                }
                Caption {
                    text: qsTr("Time")
                }
            }
            FieldLine {}
            // The project's default groove as a swing (Edit › Default Groove has the rest): Off at 50 %,
            // the groove's name while it is one of the project's grooves (dragging makes it a swing).
            FieldBox {
                objectName: "swing"
                caption: qsTr("Swing")
                from: 50
                to: 75
                step: 0.5
                decimals: 1
                sampleText: "66.7 %"
                formatter: {
                    const kind = Session.grooves.defaultKind
                    const name = Session.grooves.defaultName
                    return v => kind === "swing" ? v.toFixed(1) + " %" : kind === "" ? qsTr("Off") : name
                }
                value: Session.grooves.defaultSwing
                onMoved: (value, gestureKey) => Session.grooves.setDefaultSwing(value, gestureKey)

                ToolTip.visible: hovered && !dragging
                ToolTip.text: qsTr("Swing: the project's default groove, MPC swing on 16ths (66.7 %: a triplet feel).\nGroove devices and clips set to Project play it; Edit › Default Groove chooses a groove instead")
                ToolTip.delay: 700
            }
            FieldLine {}
            // The project's key: audio added with a key in its file name is transposed to it;
            // the piano roll tints notes out of it. ("C maj", "F# min"; a dash for none.)
            ChoiceBox {
                id: key
                objectName: "key"
                width: 58
                height: Theme.controlHeight
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0
                choices: transport.keys
                chosenIndex: transport.keyIndex
                tooltip: qsTr("Project key. Audio files with a key in their name (\"Loop_128_Am\")\n"
                              + "are transposed to it when added; a tempo in the name warps them to it.\n"
                              + "In the piano roll, notes out of it show red (none: the key the MIDI is in).")
                onChosen: index => Session.editor.setKeyByName(transport.keys[index].name)

                background: FieldTint {
                    shown: key.hovered || key.popup.visible
                }
                indicator: null
                contentItem: Item {
                    FieldValue {
                        text: key.currentIndex > 0 ? key.displayText.replace(" Major", " maj").replace(" Minor", " min")
                                                   : "—"
                        color: key.currentIndex > 0 ? Theme.text : Theme.textDim
                    }
                    Caption {
                        text: qsTr("Key")
                    }
                }
            }
        }

        Item {
            width: 4
            height: 1
        }

        // The metronome, its count-in a drop-down on its right, as Live's.
        Segments {
            BarSwitch {
                objectName: "metronome"
                iconName: "metronome"
                tooltip: qsTr("Metronome")
                checked: Session.bridge.metronome
                onClicked: Session.bridge.metronome = !Session.bridge.metronome
            }
            ChoiceBox {
                id: countIn
                objectName: "countIn"
                width: 14
                height: Theme.controlHeight
                leftPadding: 0
                rightPadding: 0
                contentItem: Item {}
                indicator: Icon {
                    anchors.centerIn: parent
                    name: "fold"
                    color: Session.countInBars > 0 ? Theme.accent : Theme.textDim
                    size: 8
                }
                background: Face {
                    hovered: countIn.hovered
                    down: countIn.down || countIn.popup.visible
                }
                choices: Session.countInChoices
                chosenIndex: {
                    const choices = Session.countInChoices
                    for (let i = 0; i < choices.length; ++i)
                        if (choices[i].value === Session.countInBars)
                            return i
                    return 0
                }
                tooltip: qsTr("Count-in: the metronome counts in this long before recording starts (%1)")
                             .arg(displayText)
                onChosen: index => Session.countInBars = Session.countInChoices[index].value
            }
        }
        BarSwitch {
            objectName: "computerKeys"
            iconName: "keyboard"
            iconSize: 16
            tooltip: Session.computerKeyboard.toolTip
            checked: Session.computerKeyboard.enabled
            onClicked: Session.computerKeyboard.toggle()
        }
        BarSwitch {
            objectName: "constrainLatency"
            iconName: "constrain_latency"
            tooltip: qsTr("Constrain Delay Compensation: while a track is monitored (armed), devices with\n"
                          + "more than %1 ms of latency on its way out (its own, its groups', the master's)\n"
                          + "are bypassed, and its live MIDI plays without waiting for the other tracks.\n"
                          + "Exports and freezing hear every device. The threshold is in Preferences.")
                             .arg(Session.bridge.constrainThresholdMs)
            checked: Session.bridge.constrainLatency
            onClicked: Session.bridge.constrainLatency = !Session.bridge.constrainLatency
        }
    }

    // --- The transport ----------------------------------------------------------------

    // In the middle of the window, or as near it as the groups either side leave room for.
    Row {
        id: deck
        // As wide as it is with the scope.
        readonly property real fullWidth: position.width + transportButtons.width + reEnable.width
                                          + scope.implicitWidth + 3 * spacing
        x: {
            const middle = (bar.width - width) / 2
            const least = song.x + song.width + 16
            const most = controls.x - 16 - width
            return Math.round(Math.max(least, Math.min(middle, most)))
        }
        y: 4
        spacing: 6

        Display {
            id: position
            objectName: "position"
            // "  1. 1. 1": bar, beat and sixteenth, each in a place of its own.
            readonly property string text: transport.positionText
            readonly property var parts: text.split(".").map(part => part.trim())

            Item {
                width: 4
                height: 1
            }
            PositionPart {
                width: 36
                value: position.parts[0]
                caption: qsTr("Bar")
            }
            PositionDot {}
            PositionPart {
                width: 28
                value: position.parts[1]
                caption: qsTr("Beat")
            }
            PositionDot {}
            PositionPart {
                width: 28
                value: position.parts[2]
                caption: qsTr("16th")
            }
            Item {
                width: 4
                height: 1
            }
        }

        Segments {
            id: transportButtons

            BarSwitch {
                objectName: "play"
                implicitWidth: 32
                fill: Theme.playOn
                iconName: "play"
                tooltip: qsTr("Play / Stop (Space)")
                checked: Session.bridge.playing
                onClicked: Session.togglePlay()
            }
            BarSwitch {
                objectName: "stop"
                implicitWidth: 32
                iconName: "stop"
                tooltip: qsTr("Stop (press again to return to the start)")
                onClicked: Session.stop()
            }
            BarSwitch {
                objectName: "record"
                implicitWidth: 32
                fill: Theme.recordOn
                iconName: "record"
                tooltip: qsTr("Arrangement Record (F9): records the armed tracks")
                checked: Session.bridge.recording
                onClicked: Session.toggleRecord()
            }
        }
        // Lit while automation is overridden (a target changed by hand): a click brings it back.
        BarSwitch {
            id: reEnable
            objectName: "reEnable"
            fill: Theme.accent
            iconName: "re_enable_automation"
            tooltip: qsTr("Re-Enable Automation")
            enabled: Session.automationOverridden
            checked: Session.automationOverridden
            onClicked: Session.bridge.reEnableAutomation()
        }
        // Only while there is room for it: in a narrow window it goes first.
        Oscilloscope {
            id: scope
            objectName: "scope"
            height: Theme.controlHeight
            visible: controls.x - (song.x + song.width) - 32 >= deck.fullWidth
            feed: Session.bridge
        }
    }

    // --- The view and the machine -----------------------------------------------------

    Row {
        id: controls
        x: bar.width - 8 - width
        y: 4
        spacing: 4

        Segments {
            BarSwitch {
                objectName: "punchIn"
                iconName: "punch_in"
                tooltip: qsTr("Punch In: recording keeps only what is played from the loop's start\n(recorded tracks play their clips before it)")
                checked: Session.project.punchIn
                onClicked: Session.editor.setPunchIn(!Session.project.punchIn)
            }
            BarSwitch {
                objectName: "loop"
                iconName: "loop"
                tooltip: qsTr("Loop (Ctrl+L)")
                checked: Session.project.loopEnabled
                onClicked: Session.editor.setLoopEnabled(!Session.project.loopEnabled)
            }
            BarSwitch {
                objectName: "punchOut"
                iconName: "punch_out"
                tooltip: qsTr("Punch Out: recording keeps only what is played up to the loop's end\n(recorded tracks play their clips after it)")
                checked: Session.project.punchOut
                onClicked: Session.editor.setPunchOut(!Session.project.punchOut)
            }
        }
        BarSwitch {
            objectName: "follow"
            readonly property bool available: bar.arrangement !== null && bar.arrangement.follow !== undefined
            iconName: "follow"
            tooltip: qsTr("Follow the playhead")
            checked: available && bar.arrangement.follow === true
            onClicked: if (available) bar.arrangement.follow = !bar.arrangement.follow
        }

        Item {
            width: 4
            height: 1
        }

        Display {
            // The audio thread's load, its last readings drawn behind it (the oldest on the
            // left), green, yellow past 60 %, red past 85 %, as a meter's.
            Field {
                id: cpu
                objectName: "cpu"
                readonly property string text: transport.cpuText

                function levelColor(load) {
                    return load > 0.85 ? Theme.meterHigh : load > 0.6 ? Theme.meterMid : Theme.meterLow
                }

                width: 48  // (two pixels a reading)
                value: Math.round(transport.cpuLoad * 100) + "%"
                valueColor: transport.cpuLoad > 0.85 ? Theme.meterHigh : Theme.text
                caption: qsTr("CPU")

                Row {
                    z: -1
                    anchors.right: parent.right
                    anchors.rightMargin: 1
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 1
                    height: parent.height - 3

                    Repeater {
                        objectName: "cpuGraph"
                        model: transport.cpuHistory

                        Rectangle {
                            required property real modelData
                            anchors.bottom: parent.bottom
                            width: 2
                            height: Math.round(Math.min(1, modelData) * parent.height)
                            color: bar.faded(cpu.levelColor(modelData), 0.35)
                        }
                    }
                }
            }
            FieldLine {
                visible: disk.visible
            }
            // While long files stream from disk: how many times it came late.
            Field {
                id: disk
                objectName: "disk"
                readonly property string text: transport.diskText

                visible: transport.diskVisible
                value: transport.diskLate > 0 ? transport.diskLate + "×" : qsTr("OK")
                valueColor: transport.diskLate > 0 ? Theme.meterHigh : Theme.text
                caption: transport.diskLate > 0 ? qsTr("Disk late") : qsTr("Disk")

                MouseArea {
                    id: diskArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
                ToolTip.visible: diskArea.containsMouse
                ToolTip.text: transport.diskToolTip
                ToolTip.delay: 700
            }
            FieldLine {}
            // The audio device, its rate under it: a click opens Preferences.
            Field {
                id: device
                objectName: "device"
                readonly property string text: transport.deviceText

                width: Math.min(implicitWidth, 168)
                value: transport.deviceName !== "" ? transport.deviceName : qsTr("No device")
                valueColor: transport.deviceName !== "" ? Theme.text : Theme.textDim
                caption: transport.deviceRate !== "" ? transport.deviceRate : qsTr("Audio off")
                captionFont: transport.deviceRate !== "" ? bar.readingFont : bar.captionFont

                FieldTint {
                    z: -1
                    shown: deviceArea.containsMouse
                }
                MouseArea {
                    id: deviceArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onPressed: bar.preferencesRequested()
                }
                ToolTip.visible: deviceArea.containsMouse
                ToolTip.text: transport.deviceToolTip
                ToolTip.delay: 700
            }
        }
    }
}
