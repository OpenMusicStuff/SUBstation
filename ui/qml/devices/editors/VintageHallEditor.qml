import QtQuick
import QtQuick.Controls
import SUBstation

// The Vintage Hall's editor: its loop (VintageHallLoop: the ring the sound runs
// round, its energy glowing at the taps, comets for each onset running round at
// the loop's own speed and fading as Decay says; drag it for Decay and Size),
// then what is always shown: the space (Decay, Predelay, Size, Attack), the
// filters on the way in (High Cut over Low Cut), and Dry/Wet over the Advanced
// button. Advanced (folded at first; not saved: how the device is looked at)
// slides open the tail's tone (Bass Mult and Xover, High Shelf and Freq) and its
// texture (Early and Late Diffusion, Mod Rate and Depth). Every control shows
// its parameter as it is now (its automation's value while that plays), sets it
// undoably, touches it when pressed, and right-click gives its menu.
Item {
    id: editor

    required property string trackId
    required property string deviceId

    readonly property alias loop: loop
    // The knobs' cells: as wide as their captions and every text their readouts take (EditorTextsWidth); Dry/Wet's
    // as the Advanced button under it too.
    readonly property int cellWidth: Math.max(52, cellTexts.needed)
    readonly property int mixWidth: Math.max(cellWidth, Math.ceil(advancedButton.implicitWidth))
    readonly property int groupGap: 7  // either side of a group's line
    // Advanced: the tone and texture knobs shown. `reveal` follows it, 0..1, as the section slides open or shut.
    readonly property bool expanded: DeviceViews.value(deviceId, "advanced", false)
    property real reveal: expanded ? 1 : 0
    Behavior on reveal {
        NumberAnimation {
            duration: 180
            easing.type: Easing.OutCubic
        }
    }

    function setExpanded(open) {
        DeviceViews.setValue(deviceId, "advanced", open)
    }

    implicitWidth: Math.ceil(knobs.x + knobs.implicitWidth) + 8
    implicitHeight: 6 + Math.max(knobs.implicitHeight, loop.implicitHeight) + 6

    DeviceParamMap {
        id: p
        trackId: editor.trackId
        deviceId: editor.deviceId
        ids: ["mix", "predelay", "decay", "size", "attack", "bass_mult", "bass_xover", "high_shelf", "high_freq",
              "early_diffusion", "late_diffusion", "mod_rate", "mod_depth", "high_cut", "low_cut"]
    }

    EditorTextsWidth {
        id: cellTexts
        knobs: [decayCell, predelayCell, sizeCell, attackCell, bassMultCell, bassXoverCell, highShelfCell,
                highFreqCell, earlyCell, lateCell, modRateCell, modDepthCell, highCutCell, lowCutCell, mixCell]
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

    VintageHallLoop {
        id: loop
        objectName: "hallLoop"
        session: Session
        trackId: editor.trackId
        deviceId: editor.deviceId
        x: 8
        y: 6
        width: Math.max(implicitWidth, editor.height - 12)  // (square: the ring fills it)
        height: Math.max(implicitHeight, editor.height - 12)

        HoverHandler {
            id: loopHover
        }
        ToolTip.visible: loopHover.hovered && !loop.dragging
        ToolTip.delay: 700
        ToolTip.text: qsTr("The hall's loop: the sound runs round it, the left half amber, the right blue.\n"
                           + "It glows where it is heard; each onset sends a comet round at the loop's own speed.\n"
                           + "Inside, its all-passes (Early and Late Diffusion); outside, the taps, as big as Attack weighs them.\n"
                           + "Drag up and down for Decay, across for Size (Shift: finely); double-click to reset them")
    }

    Row {
        id: knobs
        x: loop.x + loop.width + 10
        y: Math.max(6, Math.round((editor.height - implicitHeight) / 2))
        spacing: editor.groupGap

        // The space.
        Grid {
            columns: 2
            columnSpacing: 4
            rowSpacing: 6

            Knob {
                id: decayCell
                objectName: "decay"
                param: p.get("decay")
                title: qsTr("Decay")
                color: Theme.accent
                tooltip: qsTr("Decay: how long the middle of the tail takes to fall 60 dB. Drag the loop up and down")
            }
            Knob {
                id: predelayCell
                objectName: "predelay"
                param: p.get("predelay")
                title: qsTr("Predelay")
                tooltip: qsTr("Predelay: how long before the reverb starts")
            }
            Knob {
                id: sizeCell
                objectName: "size"
                param: p.get("size")
                title: qsTr("Size")
                color: Theme.accent
                tooltip: qsTr("Size: the hall from a small room to its natural size (a 560 ms loop). Drag the loop across")
            }
            Knob {
                id: attackCell
                objectName: "attack"
                param: p.get("attack")
                title: qsTr("Attack")
                tooltip: qsTr("Attack: low, the reverb starts at once; high, it swells in over a quarter of a second\n"
                              + "(the taps round the loop, as big as it weighs them)")
            }
        }
        GroupLine {}
        // In and out.
        Grid {
            columns: 2
            columnSpacing: 4
            rowSpacing: 6

            Knob {
                id: highCutCell
                objectName: "high_cut"
                param: p.get("high_cut")
                title: qsTr("High Cut")
                tooltip: qsTr("High Cut: takes the highs off what goes into the reverb")
            }
            Knob {
                id: mixCell
                objectName: "mix"
                width: editor.mixWidth
                param: p.get("mix")
                title: qsTr("Dry/Wet")
                tooltip: qsTr("Dry/Wet: the input and the reverb blended with equal power (100 % on a return track)")
            }
            Knob {
                id: lowCutCell
                objectName: "low_cut"
                param: p.get("low_cut")
                title: qsTr("Low Cut")
                tooltip: qsTr("Low Cut: takes the lows off what goes into the reverb")
            }
            // Advanced, level with the knob beside it.
            Item {
                width: editor.mixWidth
                height: lowCutCell.height

                RoleButton {
                    id: advancedButton
                    objectName: "advancedButton"
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 16
                    role: "small"
                    text: editor.expanded ? qsTr("Advanced ‹") : qsTr("Advanced ›")
                    checkable: false
                    checked: editor.expanded
                    tooltip: editor.expanded ? qsTr("Hide the tail's tone and texture")
                                             : qsTr("Show the tail's tone (Bass, High Shelf) and texture (Diffusion, Modulation)")
                    onClicked: editor.setExpanded(!editor.expanded)
                }
            }
        }

        // Advanced: sliding open from under the knobs before it (clipped to what is revealed so far).
        Item {
            id: advanced
            objectName: "advancedSection"
            width: Math.round(editor.reveal * advancedRow.implicitWidth)
            height: advancedRow.implicitHeight
            visible: editor.reveal > 0
            clip: true

            Row {
                id: advancedRow
                x: advanced.width - implicitWidth  // (it slides in from the left edge)
                spacing: editor.groupGap
                opacity: editor.reveal

                GroupLine {}
                // The tail's tone.
                Grid {
                    columns: 2
                    columnSpacing: 4
                    rowSpacing: 6

                    Knob {
                        id: bassMultCell
                        objectName: "bass_mult"
                        param: p.get("bass_mult")
                        title: qsTr("Bass Mult")
                        tooltip: qsTr("Bass Mult: what is below Bass Xover rings this many times as long")
                    }
                    Knob {
                        id: bassXoverCell
                        objectName: "bass_xover"
                        param: p.get("bass_xover")
                        title: qsTr("Bass Xover")
                        tooltip: qsTr("Bass Xover: where the bass Bass Mult acts on ends")
                    }
                    Knob {
                        id: highShelfCell
                        objectName: "high_shelf"
                        param: p.get("high_shelf")
                        title: qsTr("High Shelf")
                        tooltip: qsTr("High Shelf: darkens the tail every time round (−24 dB: the classic dark hall; 0: bright)")
                    }
                    Knob {
                        id: highFreqCell
                        objectName: "high_freq"
                        param: p.get("high_freq")
                        title: qsTr("High Freq")
                        tooltip: qsTr("High Freq: where the high shelf starts")
                    }
                }
                GroupLine {}
                // Its texture.
                Grid {
                    columns: 2
                    columnSpacing: 4
                    rowSpacing: 6

                    Knob {
                        id: earlyCell
                        objectName: "early_diffusion"
                        param: p.get("early_diffusion")
                        title: qsTr("Early Diff")
                        tooltip: qsTr("Early Diffusion: smears the sound as it comes in (the short arcs by each input)")
                    }
                    Knob {
                        id: lateCell
                        objectName: "late_diffusion"
                        param: p.get("late_diffusion")
                        title: qsTr("Late Diff")
                        tooltip: qsTr("Late Diffusion: how soon the tail turns from echoes into a smooth wash\n"
                                      + "(the nested arcs inside the loop; the comets smear faster)")
                    }
                    Knob {
                        id: modRateCell
                        objectName: "mod_rate"
                        param: p.get("mod_rate")
                        title: qsTr("Mod Rate")
                        tooltip: qsTr("Mod Rate: how fast the tail's delays wander")
                    }
                    Knob {
                        id: modDepthCell
                        objectName: "mod_depth"
                        param: p.get("mod_depth")
                        title: qsTr("Mod Depth")
                        tooltip: qsTr("Mod Depth: how far they wander: a gentle chorus that keeps the tail from ringing metallic\n"
                                      + "(0: a still tail)")
                    }
                }
            }
        }
    }
}
