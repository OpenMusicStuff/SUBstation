import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// The clip view: what a double-clicked clip opens. MIDI clips get the piano
// roll (several edited together), and in the header the key their notes are
// judged by (out of it, red), Notes (Transpose, Legato, Quantize and the rest
// on the selected notes, or every note of every clip shown), Generate (chords
// or a bass line from the song's chords, into the lead clip; or Progressions…,
// progression ideas in a mood to try in it, in a panel beside the roll), and
// their groove (Groove ▾). Audio clips get their controls on the left (Warp,
// Pitch, Mix, Groove) and large waveforms on the right, with the playhead over
// them, zoomed and scrolled as the arrangement is (a scroll bar under them while
// zoomed in); with several open, the controls edit all of them at once
// (ClipViewController has the rules). Given clips of both kinds, it shows one
// kind's and the header's Audio and MIDI buttons switch to the other's.
// zoom(factor) and zoomToFit() are the window's Zoom In, Out and Z while it shows.
//
// Give it the clips to show:
//
//   ClipView {
//       trackId: track            // the track of the ids in clipIds
//       clipIds: [clip]           // or [{trackId: t, clipId: c}, ...] for clips on several tracks
//       leadClipId: clip          // optional: the clip double-clicked (a MIDI one opens the MIDI clips)
//       onCloseRequested: ...     // Esc, ×, or its clips are gone: back to the devices
//       onLocateRequested: beat => ...  // the piano roll's ruler was clicked: play from this
//                                         // arrangement beat (MainWindow.locate: the insert marker
//                                         // there, and the bridge located)
//       onStatusMessage: message => window.showMessage(message)
//   }
//
// They show top track first, then by time. Esc (or ×) asks to be closed.
// Shown, it opens them afresh (a MIDI clip fitted to the view); hidden, it
// forgets them (a key sounding stops). When it is shown or given clips, the
// notes (MIDI) or the view (audio, for Esc) take the keyboard; the notes take
// Delete, Ctrl+A, Ctrl+D and Ctrl+U before the window's shortcuts while they
// have it.
FocusScope {
    id: view

    property string trackId
    property var clipIds: []
    property string leadClipId

    readonly property alias controller: clipController
    readonly property alias pianoRollView: rollView
    readonly property alias waveform: waveform
    readonly property alias progressionPanel: progressionPanel
    readonly property alias progressionIdeas: progressionIdeas
    // Showing a MIDI clip in the piano roll (else audio clips, or nothing).
    readonly property bool midi: clipController.midi

    signal closeRequested()
    signal locateRequested(real beat)
    // One of several audio clips clicked (it and its track selected): show it alone.
    signal clipChosen(string trackId, string clipId)
    signal statusMessage(string message)

    // The window's Zoom In and Out (by `factor`, more than 1 in) and Zoom to
    // Arrangement (Z: the whole clip again), on what shows: the waveforms (about
    // their middle) or the piano roll (about the grid's).
    function zoom(factor) {
        if (clipController.midi)
            rollView.roll.zoomAt(rollView.grid.width / 2, factor)
        else
            waveform.zoomAt(waveform.width / 2, factor)
    }
    function zoomToFit() {
        if (clipController.midi)
            rollView.roll.zoomToFit()
        else
            waveform.zoomToFit()
    }

    // The notes (MIDI) or the view (audio) take the keyboard.
    function focusContent() {
        if (clipController.midi)
            rollView.focusNotes()
        else
            view.forceActiveFocus()
    }

    implicitWidth: 900
    implicitHeight: 360

    ClipViewController {
        id: clipController
        session: Session
        pianoRoll: rollView.roll
        trackId: view.trackId
        clipIds: view.clipIds
        leadClipId: view.leadClipId
        active: view.visible
        onCloseRequested: view.closeRequested()
        onStatusMessage: message => view.statusMessage(message)
        onOpened: if (view.visible) view.focusContent()
    }

    Keys.onEscapePressed: view.closeRequested()

    Rectangle {
        anchors.fill: parent
        color: Theme.panel
    }

    // --- Header: colour, name, info, close ---

    Item {
        id: header
        width: parent.width
        height: 30

        Rectangle {
            width: 5
            height: header.height - 1
            color: clipController.color
            visible: clipController.count > 0
        }

        Row {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            // Audio and MIDI clips among those opened: which kind shows (the other kind's stay open, a click away).
            Row {
                id: kindSwitch
                objectName: "kindSwitch"
                anchors.verticalCenter: parent.verticalCenter
                visible: clipController.mixed && clipController.count > 0
                spacing: 1

                ToggleButton {
                    objectName: "showAudio"
                    width: implicitWidth + 14
                    height: 20
                    role: "monitor"
                    checkable: false
                    checked: !clipController.midi
                    text: qsTr("Audio %1").arg(clipController.audioCount)
                    tooltip: qsTr("The audio clips among those selected: their settings and waveforms")
                    onClicked: clipController.showKind(false)
                }
                ToggleButton {
                    objectName: "showMidi"
                    width: implicitWidth + 14
                    height: 20
                    role: "monitor"
                    checkable: false
                    checked: clipController.midi
                    text: qsTr("MIDI %1").arg(clipController.midiCount)
                    tooltip: qsTr("The MIDI clips among those selected, in the piano roll, edited together")
                    onClicked: clipController.showKind(true)
                }
            }
            Label {
                objectName: "clipName"
                anchors.verticalCenter: parent.verticalCenter
                text: clipController.name
                font: Theme.uiFont(10, true)
            }
            Label {
                objectName: "clipInfo"
                anchors.verticalCenter: parent.verticalCenter
                text: clipController.info
                color: Theme.textDim
            }
        }

        Row {
            anchors.right: closeButton.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            visible: clipController.midi

            Label {
                id: keyLabel
                objectName: "keyLabel"
                // Read only while it shows: asking for the key infers the harmony.
                readonly property bool active: view.visible && clipController.midi && Session.harmony.shown
                readonly property string key: active ? Session.harmony.keyLabel : ""
                readonly property bool inferred: active && Session.harmony.keyInferred
                anchors.verticalCenter: parent.verticalCenter
                visible: key !== ""
                text: inferred ? qsTr("Key: %1 (inferred)").arg(key) : qsTr("Key: %1").arg(key)
                color: Theme.textDim

                HoverHandler {
                    id: keyHover
                }
                ToolTip.visible: keyHover.hovered
                ToolTip.delay: 700
                ToolTip.text: keyLabel.inferred
                    ? qsTr("The key the song's MIDI is most likely in (the project has none):\nnotes out of it show red. C shows or hides the chords and the key")
                    : qsTr("The project's key: notes out of it show red.\nC shows or hides the chords and the key")
            }
            RoleButton {
                id: notesButton
                objectName: "notesMenuButton"
                anchors.verticalCenter: parent.verticalCenter
                role: "small"
                text: qsTr("Notes ▾")
                tooltip: qsTr("Transpose, Legato, Quantize and the rest on the selected notes,\nor on every note of every clip shown when none are selected")
                onClicked: notesMenu.popup(notesButton, 0, notesButton.height)

                Menu {
                    id: notesMenu
                    objectName: "notesMenu"
                    // (The notes take the keyboard back.)
                    onClosed: rollView.focusNotes()

                    MenuItem {
                        objectName: "notesTarget"
                        enabled: false
                        text: rollView.roll.selectedCount === 1 ? qsTr("The selected note")
                            : rollView.roll.selectedCount > 1 ? qsTr("The %1 selected notes").arg(rollView.roll.selectedCount)
                            : rollView.roll.clipCount > 1 ? qsTr("Every note of the %1 clips").arg(rollView.roll.clipCount)
                                                           : qsTr("Every note")
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "transposeUp"
                        text: qsTr("Transpose Up")
                        onTriggered: rollView.roll.transpose(1)
                    }
                    MenuItem {
                        objectName: "transposeDown"
                        text: qsTr("Transpose Down")
                        onTriggered: rollView.roll.transpose(-1)
                    }
                    MenuItem {
                        objectName: "octaveUp"
                        text: qsTr("Octave Up")
                        onTriggered: rollView.roll.transpose(12)
                    }
                    MenuItem {
                        objectName: "octaveDown"
                        text: qsTr("Octave Down")
                        onTriggered: rollView.roll.transpose(-12)
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "notesLegato"
                        text: qsTr("Legato")
                        onTriggered: rollView.roll.legato()
                    }
                    MenuItem {
                        objectName: "notesQuantize"
                        text: qsTr("Quantize (%1)").arg(rollView.roll.quantizeGrid)
                        onTriggered: rollView.roll.quantize()
                    }
                    MenuItem {
                        objectName: "notesDouble"
                        text: qsTr("Timing ×2")
                        onTriggered: rollView.roll.scaleTime(2.0)
                    }
                    MenuItem {
                        objectName: "notesHalve"
                        text: qsTr("Timing ÷2")
                        onTriggered: rollView.roll.scaleTime(0.5)
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "selectAllNotes"
                        text: qsTr("Select All Notes")
                        onTriggered: rollView.roll.selectAll()
                    }
                }
            }
            RoleButton {
                id: grooveButton
                objectName: "grooveMenuButton"
                anchors.verticalCenter: parent.verticalCenter
                role: "small"
                text: clipController.groove.kind ? qsTr("Groove: %1 ▾").arg(clipController.groove.name) : qsTr("Groove ▾")
                tooltip: qsTr("The groove the clip's notes play in (Swing, the project's, one of its grooves),\nand making one of the clip's feel")
                onClicked: groovePopup.open()

                Popup {
                    id: groovePopup
                    objectName: "groovePopup"
                    y: grooveButton.height
                    padding: 8
                    background: Rectangle {
                        color: Theme.panelAlt
                        border.color: Theme.border
                        radius: 4
                    }

                    ClipGrooveControls {
                        objectName: "midiGroove"
                        width: 240
                        controller: clipController
                        midi: true
                    }
                }
            }
            RoleButton {
                id: generateButton
                objectName: "generate"
                anchors.verticalCenter: parent.verticalCenter
                role: "small"
                text: qsTr("Generate ▾")
                tooltip: qsTr("Write block chords or a bass line into the clip, from the song's chords\n(or from a progression in its key, where it has none); or try progression ideas in a mood")
                onClicked: generateMenu.popup(generateButton, 0, generateButton.height)

                Menu {
                    id: generateMenu
                    objectName: "generateMenu"

                    MenuItem {
                        objectName: "generateChords"
                        text: qsTr("Chords")
                        onTriggered: rollView.roll.generateChords()
                    }
                    MenuItem {
                        objectName: "generateBass"
                        text: qsTr("Bass")
                        onTriggered: rollView.roll.generateBass()
                    }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "generateProgressions"
                        text: qsTr("Progressions…")
                        onTriggered: {
                            progressionPanel.wanted = true
                            progressionPanel.open()
                        }
                    }
                }
            }
        }

        RoleButton {
            id: closeButton
            objectName: "closeButton"
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            role: "flat"
            text: "×"
            font.pointSize: 14
            tooltip: qsTr("Back to the arrangement (Esc)")
            onClicked: view.closeRequested()
        }

        Rectangle {
            y: header.height - 1
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    Item {
        id: body
        y: header.height
        width: parent.width
        height: parent.height - header.height

        // --- Audio clips: controls and waveforms ---

        Item {
            id: audioPage
            objectName: "audioPage"
            anchors.fill: parent
            visible: clipController.count > 0 && !clipController.midi

            Flickable {
                id: controls
                objectName: "audioControls"
                width: 260
                height: parent.height
                contentWidth: width
                contentHeight: column.y + column.implicitHeight + 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                Column {
                    id: column
                    x: 8
                    y: 8
                    width: controls.width - 16
                    spacing: 6

                    ClipViewSection {
                        width: column.width
                        title: qsTr("Warp")

                        // The old grid: Warp; the mode across; "Seg. BPM" and its box; :2 and ×2
                        // under the box. Columns as QGridLayout sized them: each its widest
                        // control (the caption, the 34 px buttons) plus a third of what is left.
                        Item {
                            id: warpGrid

                            readonly property real extra: Math.max(0, width - 12 - bpmCaption.implicitWidth - 68)
                            readonly property real first: bpmCaption.implicitWidth + extra / 3
                            readonly property real other: 34 + extra / 3

                            width: parent.width
                            height: markTransients.y + markTransients.height

                            ToggleButton {
                                objectName: "warp"
                                width: warpGrid.first
                                height: 20
                                role: "activator"
                                text: qsTr("Warp")
                                tooltip: qsTr("Lock the clip to the beat grid, so it follows the project tempo")
                                checked: clipController.warp
                                onToggled: clipController.setWarp(checked)
                            }

                            ComboBox {
                                id: warpMode
                                objectName: "warpMode"
                                y: 24
                                width: parent.width
                                focusPolicy: Qt.NoFocus
                                model: clipController.warpModes
                                currentIndex: clipController.warpModeIndex
                                displayText: currentIndex < 0 ? qsTr("Mixed") : currentText
                                onActivated: index => clipController.setWarpMode(index)
                                ToolTip.visible: hovered && !popup.visible
                                ToolTip.text: qsTr("How the clip is stretched (and transposed)")
                                ToolTip.delay: 700

                                delegate: ItemDelegate {
                                    required property var modelData
                                    required property int index
                                    width: ListView.view ? ListView.view.width : implicitWidth
                                    text: modelData
                                    highlighted: warpMode.highlightedIndex === index
                                    hoverEnabled: true
                                    ToolTip.visible: hovered
                                    ToolTip.text: clipController.warpModeTips[index]
                                    ToolTip.delay: 700
                                }
                            }

                            Label {
                                id: bpmCaption
                                y: segmentBpm.y
                                width: warpGrid.first
                                height: segmentBpm.height
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: qsTr("Seg. BPM")
                                color: Theme.textDim
                                font: Theme.uiFont(8)
                            }
                            ValueBox {
                                id: segmentBpm
                                objectName: "segmentBpm"
                                x: warpGrid.first + 6
                                y: warpMode.y + warpMode.height + 4
                                width: parent.width - x
                                height: 20
                                from: 20
                                to: 999
                                step: 0.01
                                decimals: 2
                                sampleText: "999.00"
                                formatter: v => v.toFixed(2)
                                value: clipController.segmentBpm
                                onMoved: (value, gestureKey) => clipController.setSegmentBpm(value, gestureKey)
                                ToolTip.visible: hovered && !dragging
                                ToolTip.text: qsTr("Segment BPM: the tempo of the audio in the clip. Warped clips play at\nproject tempo ÷ segment BPM speed.")
                                ToolTip.delay: 700
                            }

                            RoleButton {
                                id: halveBpm
                                objectName: "halveBpm"
                                x: segmentBpm.x
                                y: segmentBpm.y + segmentBpm.height + 4
                                width: warpGrid.other
                                height: 20
                                topPadding: 0
                                bottomPadding: 0
                                text: ":2"
                                tooltip: qsTr("Halve each clip's segment BPM (warped clips play twice as fast)")
                                onClicked: clipController.scaleBpm(0.5)
                            }
                            RoleButton {
                                objectName: "doubleBpm"
                                x: halveBpm.x + halveBpm.width + 6
                                y: halveBpm.y
                                width: parent.width - x
                                height: 20
                                topPadding: 0
                                bottomPadding: 0
                                text: "×2"
                                tooltip: qsTr("Double each clip's segment BPM (warped clips play half as fast)")
                                onClicked: clipController.scaleBpm(2.0)
                            }
                            RoleButton {
                                id: markTransients
                                objectName: "markTransients"
                                y: halveBpm.y + halveBpm.height + 4
                                width: parent.width
                                height: 20
                                topPadding: 0
                                bottomPadding: 0
                                enabled: clipController.warpMarkable
                                text: qsTr("Mark Transients")
                                tooltip: qsTr("A warp marker on each transient the clip plays (the ticks along the waveform's top), to drag onto the grid")
                                onClicked: clipController.markTransients()
                            }
                        }
                    }

                    ClipViewSection {
                        width: column.width
                        title: qsTr("Pitch")

                        RowLayout {
                            width: parent.width

                            ClipViewKnob {
                                objectName: "transpose"
                                Layout.fillWidth: true
                                controller: clipController
                                knob: "transpose"
                                active: !clipController.repitch
                            }
                            ClipViewKnob {
                                objectName: "detune"
                                Layout.fillWidth: true
                                controller: clipController
                                knob: "detune"
                                active: !clipController.repitch
                            }
                        }
                    }

                    ClipViewSection {
                        width: column.width
                        title: qsTr("Mix")

                        RowLayout {
                            width: parent.width

                            ClipViewKnob {
                                objectName: "gain"
                                Layout.fillWidth: true
                                controller: clipController
                                knob: "gain"
                            }
                            ClipViewKnob {
                                objectName: "pan"
                                Layout.fillWidth: true
                                controller: clipController
                                knob: "pan"
                            }
                        }
                    }

                    ClipViewSection {
                        width: column.width
                        title: qsTr("Groove")

                        ClipGrooveControls {
                            objectName: "audioGroove"
                            width: parent.width
                            controller: clipController
                        }
                    }

                    Label {
                        width: column.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Warped clips follow the project tempo. Transpose keeps the speed.")
                        color: Theme.textDisabled
                    }
                }
            }

            ClipWaveform {
                id: waveform
                objectName: "clipWaveform"
                x: controls.width
                width: parent.width - controls.width
                // (Zoomed in: a scroll bar under it.)
                height: parent.height - (waveformBar.visible ? waveformBar.height : 0)
                clip: true
                session: Session
                controller: clipController
                onLocateRequested: beat => view.locateRequested(beat)
                onClipClicked: (trackId, clipId) => view.clipChosen(trackId, clipId)
            }
            // Several clips that don't fit: a scroll bar beside their bands.
            ScrollBar {
                id: bandsBar
                objectName: "bandsBar"
                x: waveform.x + waveform.width - width
                y: waveform.y + waveform.rulerHeight
                width: Theme.scrollBarWidth
                height: waveform.height - waveform.rulerHeight
                orientation: Qt.Vertical
                policy: ScrollBar.AlwaysOn
                focusPolicy: Qt.NoFocus
                visible: waveform.contentHeight > height + 0.5
                size: waveform.contentHeight > 0 ? height / waveform.contentHeight : 1
                stepSize: 0.05
                onPositionChanged: if (pressed) waveform.scrollY = position * waveform.contentHeight

                Binding on position {
                    when: !bandsBar.pressed
                    value: waveform.contentHeight > 0 ? waveform.scrollY / waveform.contentHeight : 0
                }
            }
            ClipPlayhead {
                objectName: "clipPlayhead"
                x: waveform.x
                width: waveform.width
                height: waveform.height
                clip: true
                waveform: waveform
            }
            ScrollBar {
                id: waveformBar
                objectName: "waveformBar"
                x: waveform.x
                y: parent.height - height
                width: waveform.width
                height: Theme.scrollBarWidth
                orientation: Qt.Horizontal
                policy: ScrollBar.AlwaysOn
                focusPolicy: Qt.NoFocus
                visible: waveform.viewTo - waveform.viewFrom < 1
                size: waveform.viewTo - waveform.viewFrom
                stepSize: 0.05 * size
                onPositionChanged: if (pressed) waveform.scrollTo(position)

                Binding on position {
                    when: !waveformBar.pressed
                    value: waveform.viewFrom
                }
            }
        }

        // --- A MIDI clip: the piano roll, and progression ideas beside it ---

        PianoRollView {
            id: rollView
            objectName: "pianoRollView"
            width: parent.width - (progressionPanel.visible ? progressionPanel.width : 0)
            height: parent.height
            visible: clipController.count > 0 && clipController.midi
            onLocateRequested: beat => view.locateRequested(beat)
        }
        Connections {
            target: rollView.roll
            function onStatusMessage(message) { view.statusMessage(message) }
        }

        ProgressionIdeas {
            id: progressionIdeas
            roll: rollView.roll
            onStatusMessage: message => view.statusMessage(message)
        }
        ProgressionPanel {
            id: progressionPanel
            objectName: "progressionPanel"
            // Shown with the piano roll once opened, until closed.
            property bool wanted: false
            x: parent.width - width
            width: implicitWidth
            height: parent.height
            visible: wanted && rollView.visible
            ideas: progressionIdeas
            onCloseRequested: {
                wanted = false
                view.focusContent()
            }
        }
    }
}
