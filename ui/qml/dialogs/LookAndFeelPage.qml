import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SUBstation

// Preferences › Look and Feel: the theme (Theme.names: Default, Disableton,
// Flashbang, Gay). A theme is colours only; it applies at once, everywhere,
// and is kept for the next run (Theme.name). And the maximum display frame
// rate (Session.displayPreferences): how often the meters, the playhead and the
// devices' displays refresh, no faster than the screen; kept too.
ColumnLayout {
    id: page

    spacing: 10

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 6

        Label { text: qsTr("Theme") }
        ChoiceBox {
            objectName: "theme"
            Layout.fillWidth: true
            choices: Theme.names.map(name => ({ label: name, value: name }))
            chosenIndex: Theme.names.indexOf(Theme.name)
            onChosen: index => Theme.name = Theme.names[index]
        }

        Label { text: qsTr("Maximum display frame rate") }
        ChoiceBox {
            objectName: "displayRate"
            Layout.fillWidth: true
            choices: Session.displayPreferences.rateChoices
            chosenIndex: Session.displayPreferences.rateIndex
            onChosen: index => Session.displayPreferences.chooseRate(index)
        }
    }

    Label {
        objectName: "displayRateStatus"
        Layout.fillWidth: true
        Layout.maximumWidth: 520
        wrapMode: Text.WordWrap
        color: Theme.textDim
        text: Session.displayPreferences.status +
              qsTr(" Faster suits a high refresh rate screen; the meters and the spectra cost more of the CPU.")
    }

    Item { Layout.fillHeight: true }
}
