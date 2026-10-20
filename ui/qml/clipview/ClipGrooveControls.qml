import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// A clip's groove, in the clip view (the audio controls' Groove section, the
// MIDI header's Groove menu): the groove it plays (None, Swing, the project's
// default, one of the project's grooves), Swing and Step (Swing's), and how
// much of it plays: Timing, Velocity (MIDI), Random, Quantize. Apply writes it
// into the clips and clears it; Extract makes a groove of the lead clip's feel.
// Every shown clip changes together (ClipViewController).
Column {
    id: controls

    required property ClipViewController controller
    property bool midi: false

    readonly property var setting: controller.groove
    readonly property string kind: setting.kind || ""
    readonly property real captionWidth: 52

    // The grooves to choose from: None, Swing, Project, the library's (and its own, while it has one).
    readonly property var choices: {
        const list = [{ kind: "", grooveId: "", name: qsTr("None") },
                      { kind: "swing", grooveId: "", name: qsTr("Swing") },
                      { kind: "project", grooveId: "", name: qsTr("Project: %1").arg(Session.grooves.defaultName) }]
        for (const g of Session.grooves.grooves)
            list.push({ kind: "library", grooveId: g.id, name: g.name })
        if (kind === "own")
            list.push({ kind: "own", grooveId: "", name: qsTr("Its own: %1").arg(setting.name || "") })
        return list
    }
    readonly property int choiceIndex: {
        for (let i = 0; i < choices.length; ++i) {
            const c = choices[i]
            if (c.kind === kind && (kind !== "library" || c.grooveId === setting.grooveId))
                return i
        }
        return 0
    }

    spacing: 4
    enabled: controller.grooveable

    component Caption: Label {
        width: controls.captionWidth
        height: 20
        verticalAlignment: Text.AlignVCenter
        color: Theme.textDim
        font: Theme.uiFont(8)
    }
    component Amount: Row {
        id: amount
        property string field
        property string title
        property string tip
        property real from: 0
        property real to: 100
        property real defaultValue: 0
        spacing: 4
        visible: controls.kind !== ""

        Caption {
            text: amount.title
        }
        ValueBox {
            objectName: "groove_" + amount.field
            width: controls.width - controls.captionWidth - 4
            height: 20
            from: amount.from
            to: amount.to
            step: 1
            decimals: 0
            sampleText: "-100 %"
            formatter: v => v.toFixed(0) + " %"
            value: 100 * (controls.setting[amount.field] !== undefined ? controls.setting[amount.field]
                                                                        : amount.defaultValue)
            onMoved: (value, gestureKey) => controls.controller.setGrooveValue(amount.field, value, gestureKey)
            ToolTip.visible: hovered && !dragging
            ToolTip.text: amount.tip
            ToolTip.delay: 700
        }
    }

    Row {
        spacing: 4

        Caption {
            text: qsTr("Groove")
        }
        ComboBox {
            id: chooser
            objectName: "groove"
            width: controls.width - controls.captionWidth - 4
            height: 20
            focusPolicy: Qt.NoFocus
            model: controls.choices
            textRole: "name"
            currentIndex: controls.choiceIndex
            onActivated: index => {
                const chosen = controls.choices[index]
                if (chosen.kind !== "own")
                    controls.controller.setGroove(chosen.kind, chosen.grooveId)
            }
            ToolTip.visible: hovered && !popup.visible
            ToolTip.delay: 700
            ToolTip.text: controls.midi
                ? qsTr("The groove the clip's notes play in (they stay where they are: Apply writes it into them)")
                : qsTr("The groove the clip's audio plays in: its hits moved through its warp\n(the clip is warped for it; its own warp markers stay as they are)")
        }
    }
    Row {
        spacing: 4
        visible: controls.kind === "swing"

        Caption {
            text: qsTr("Swing")
        }
        ValueBox {
            objectName: "groove_swing"
            width: (controls.width - controls.captionWidth - 8) / 2
            height: 20
            from: 50
            to: 75
            step: 0.5
            decimals: 1
            sampleText: "66.7 %"
            formatter: v => v.toFixed(1) + " %"
            value: controls.setting.swing || 50
            onMoved: (value, gestureKey) => controls.controller.setGrooveValue("swing", value, gestureKey)
            ToolTip.visible: hovered && !dragging
            ToolTip.text: qsTr("Every other step late, landing this far through its pair (66.7 %: a triplet feel)")
            ToolTip.delay: 700
        }
        ComboBox {
            objectName: "groove_step"
            width: (controls.width - controls.captionWidth - 8) / 2
            height: 20
            focusPolicy: Qt.NoFocus
            model: Session.grooves.stepNames
            currentIndex: controls.setting.stepIndex !== undefined ? controls.setting.stepIndex : 2
            onActivated: index => controls.controller.setGrooveValue("step", index)
            ToolTip.visible: hovered && !popup.visible
            ToolTip.delay: 700
            ToolTip.text: qsTr("The steps Swing moves every other one of")
        }
    }
    Amount {
        field: "timing"
        title: qsTr("Timing")
        to: 200
        defaultValue: 1
        tip: qsTr("How far the groove moves things (100 %: as it has them)")
    }
    Amount {
        field: "velocity"
        title: qsTr("Velocity")
        visible: controls.midi && controls.kind !== ""
        from: -100
        defaultValue: 1
        tip: qsTr("How much the groove's accents change the notes' velocities (negative: turned round)")
    }
    Amount {
        field: "random"
        title: qsTr("Random")
        tip: qsTr("A jitter on each hit's timing, up to a quarter step either way, the same every time")
    }
    Amount {
        field: "quantize"
        title: qsTr("Quantize")
        tip: qsTr("Hits pulled onto the groove's grid before it moves them")
    }
    Row {
        spacing: 4

        RoleButton {
            objectName: "applyGroove"
            width: (controls.width - 4) / 2
            height: 20
            topPadding: 0
            bottomPadding: 0
            enabled: controls.kind !== ""
            text: qsTr("Apply")
            tooltip: controls.midi ? qsTr("Write the groove into the notes, and clear it")
                                   : qsTr("Write the groove into the clip's warp markers, and clear it")
            onClicked: controls.controller.applyGroove()
        }
        RoleButton {
            objectName: "extractGroove"
            width: (controls.width - 4) / 2
            height: 20
            topPadding: 0
            bottomPadding: 0
            text: qsTr("Extract")
            tooltip: controls.midi ? qsTr("Make a groove of the clip's feel (its notes against the 16th grid, over a bar),\ninto the project's grooves")
                                   : qsTr("Make a groove of the clip's feel (its transients against the 16th grid, over a bar),\ninto the project's grooves")
            onClicked: controls.controller.extractGroove(2, 1, false)
        }
    }
}
