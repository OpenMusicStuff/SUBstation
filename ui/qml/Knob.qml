import QtQuick
import QtQuick.Controls
import SUBstation

// The rotary knob (KnobItem): its value as its tooltip, and the small text
// field typing a digit opens when it has a `parser`: centred on the knob, at
// least 48 px wide, applied on Return or when it loses the focus.
//
//   Knob {
//       from: -1; to: 1; defaultValue: 0; bipolar: true; wheel: false
//       value: model.pan
//       formatter: v => formatPan(v)
//       parser: text => parsePan(text)       // a number, or null
//       onMoved: (value, gestureKey) => editor.setTrackParam(id, "pan", value, gestureKey)
//       onTouched: editor.touchParameter(id, "pan")
//   }
KnobItem {
    id: knob

    HoverHandler {
        id: hover
    }

    // Over a modulated knob's amount handle, the tooltip says what it does.
    readonly property bool overModulationHandle: hover.hovered && isOnModulationHandle(hover.point.position)

    ToolTip.visible: hover.hovered && !dragging && !draggingAmount && !editor.visible && text !== ""
    ToolTip.text: overModulationHandle
                  ? qsTr("Modulation %1%2 (%3): drag to change · Alt+Shift-click: %4")
                        .arg(modulationAmount > 0 && modulationBipolar ? "±" : "")
                        .arg(Math.round(modulationAmount * 100) + "%")
                        .arg(modulationBipolar ? qsTr("bipolar") : qsTr("unipolar"))
                        .arg(modulationBipolar ? qsTr("unipolar") : qsTr("bipolar"))
                  : text
    ToolTip.delay: 700

    onEditRequested: initialText => editor.openWith(initialText)

    Popup {
        id: editor

        property bool done: true

        function openWith(initialText) {
            field.text = initialText
            done = false
            open()
            field.forceActiveFocus()
            field.cursorPosition = field.length
        }

        function finish() {
            if (done)
                return
            done = true
            knob.applyTyped(field.text)
        }

        width: Math.max(48, metrics.advanceWidth + 16)
        height: 20
        x: (knob.width - width) / 2
        y: knob.height / 2 - 10
        padding: 0
        focus: true
        closePolicy: Popup.CloseOnPressOutside
        onClosed: finish()

        TextMetrics {
            id: metrics
            font: field.font
            text: "100%"
        }

        contentItem: TextField {
            id: field
            padding: 0
            horizontalAlignment: TextInput.AlignHCenter
            onAccepted: editor.close()
        }
        background: null
    }
}
