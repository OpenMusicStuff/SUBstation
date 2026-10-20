import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SUBstation

// Preferences › Audio (Session.audioPreferences, the old PreferencesDialog's
// form): the driver type (ASIO greyed out in a build without it, its tooltip
// saying so), the device and its Hardware Setup (ASIO), the outputs the master
// plays on (ASIO), the sample rate, the buffer size (greyed out when the
// driver sets it), exclusive mode (WASAPI), the audio threads and the latency
// Constrain Delay Compensation bypasses devices over; disk streaming (long
// files read from disk as they play: how long, the memory it takes, reads at
// once, the cache folder and its size); below them what runs, or why it didn't
// open. Each choice opens the device with it at
// once, since what a device offers is only known while it is open; the lists
// then show what it offers.
ColumnLayout {
    id: page

    readonly property var prefs: Session.audioPreferences

    // Hardware Setup is showing (true) or done (false).
    signal controlPanelShowing(bool showing)

    function showControlPanel() {
        controlPanelShowing(true)
        prefs.showControlPanel()
        controlPanelShowing(false)
    }

    spacing: 10

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 6

        Label { text: qsTr("Driver Type") }
        ChoiceBox {
            objectName: "driver"
            Layout.fillWidth: true
            choices: page.prefs.driverChoices
            chosenIndex: page.prefs.driverIndex
            onChosen: index => page.prefs.chooseDriver(index)
        }

        Label { text: qsTr("Audio Device") }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ChoiceBox {
                objectName: "device"
                Layout.fillWidth: true
                enabled: page.prefs.deviceEnabled
                choices: page.prefs.deviceChoices
                chosenIndex: page.prefs.deviceIndex
                onChosen: index => page.prefs.chooseDevice(index)
            }
            RoleButton {
                objectName: "controlPanel"
                visible: page.prefs.controlPanelVisible
                enabled: page.prefs.controlPanelEnabled
                text: qsTr("Hardware Setup")
                tooltip: qsTr("The driver's own settings (buffer size, clock, routing...)")
                onClicked: page.showControlPanel()
            }
        }

        Label {
            visible: page.prefs.outputsVisible
            text: qsTr("Output Channels")
        }
        ChoiceBox {
            objectName: "outputs"
            Layout.fillWidth: true
            visible: page.prefs.outputsVisible
            tooltip: qsTr("The outputs the master plays on")
            choices: page.prefs.outputChoices
            chosenIndex: page.prefs.outputIndex
            onChosen: index => page.prefs.chooseOutputs(index)
        }

        Label { text: qsTr("Sample Rate") }
        ChoiceBox {
            objectName: "sampleRate"
            Layout.fillWidth: true
            choices: page.prefs.sampleRateChoices
            chosenIndex: page.prefs.sampleRateIndex
            onChosen: index => page.prefs.chooseSampleRate(index)
        }

        Label { text: qsTr("Buffer Size") }
        ChoiceBox {
            objectName: "bufferSize"
            Layout.fillWidth: true
            enabled: page.prefs.bufferEnabled
            tooltip: page.prefs.bufferToolTip
            choices: page.prefs.bufferChoices
            chosenIndex: page.prefs.bufferIndex
            onChosen: index => page.prefs.chooseBufferSize(index)
        }

        Item {
            visible: page.prefs.exclusiveVisible
            implicitWidth: 1
            implicitHeight: 1
        }
        CheckBox {
            id: exclusive
            objectName: "exclusive"
            visible: page.prefs.exclusiveVisible
            focusPolicy: Qt.NoFocus
            text: qsTr("Exclusive mode (lower latency; other apps are silenced)")
            checked: page.prefs.exclusive
            onToggled: {
                page.prefs.setExclusive(checked)
                checked = Qt.binding(() => page.prefs.exclusive)
            }
        }

        Label { text: qsTr("Audio Threads") }
        ChoiceBox {
            objectName: "threads"
            Layout.fillWidth: true
            tooltip: qsTr("Tracks render on this many threads at once (the audio thread and its helpers). "
                          + "1 renders every track on the audio thread.")
            choices: page.prefs.threadChoices
            chosenIndex: page.prefs.threadIndex
            onChosen: index => page.prefs.chooseThreads(index)
        }

        Label { text: qsTr("Constrain Latency Over") }
        ChoiceBox {
            objectName: "constrainThreshold"
            // The usual thresholds, and the one saved if it is another (set by hand).
            readonly property var values: {
                const usual = [0, 1, 2, 3, 5, 10, 20, 50]
                const saved = Session.bridge.constrainThresholdMs
                return usual.includes(saved) ? usual : usual.concat([saved]).sort((a, b) => a - b)
            }
            Layout.fillWidth: true
            tooltip: qsTr("Constrain Delay Compensation (the stopwatch in the transport bar) bypasses the devices "
                          + "with more latency than this on the way out of a monitored track.")
            choices: values.map(v => ({label: v === 0 ? qsTr("Any latency") : qsTr("%1 ms").arg(v), value: v}))
            chosenIndex: Math.max(0, values.indexOf(Session.bridge.constrainThresholdMs))
            onChosen: index => Session.bridge.constrainThresholdMs = values[index]
        }

        Label { text: qsTr("Disk Streaming") }
        CheckBox {
            objectName: "streaming"
            focusPolicy: Qt.NoFocus
            text: qsTr("Stream long files from disk (memory follows what plays soon)")
            checked: Session.bridge.streaming
            onToggled: {
                Session.bridge.streaming = checked
                checked = Qt.binding(() => Session.bridge.streaming)
            }
        }

        Label {
            visible: Session.bridge.streaming
            text: qsTr("Stream Files Longer Than")
        }
        ChoiceBox {
            objectName: "streamThreshold"
            // The usual lengths (seconds), and the one saved if it is another.
            readonly property var values: {
                const usual = [10, 30, 60, 120, 300]
                const saved = Session.bridge.streamThreshold
                return usual.includes(saved) ? usual : usual.concat([saved]).sort((a, b) => a - b)
            }
            Layout.fillWidth: true
            visible: Session.bridge.streaming
            tooltip: qsTr("Shorter files (one-shots, loops) are decoded into memory, as they are without streaming.")
            choices: values.map(v => ({label: v < 60 ? qsTr("%1 s").arg(v) : qsTr("%1 min").arg(v / 60), value: v}))
            chosenIndex: Math.max(0, values.indexOf(Session.bridge.streamThreshold))
            onChosen: index => Session.bridge.streamThreshold = values[index]
        }

        Label {
            visible: Session.bridge.streaming
            text: qsTr("Streaming Memory")
        }
        ChoiceBox {
            objectName: "streamMemory"
            readonly property var values: {
                const usual = [64, 128, 256, 512, 1024, 2048, 4096]
                const saved = Session.bridge.streamMemory
                return usual.includes(saved) ? usual : usual.concat([saved]).sort((a, b) => a - b)
            }
            Layout.fillWidth: true
            visible: Session.bridge.streaming
            tooltip: qsTr("What the audio read ahead of the playhead may take (the start of each file and clip comes on top). "
                          + "More holds more streamed tracks playing at once.")
            choices: values.map(v => ({label: v < 1024 ? qsTr("%1 MB").arg(v) : qsTr("%1 GB").arg(v / 1024), value: v}))
            chosenIndex: Math.max(0, values.indexOf(Session.bridge.streamMemory))
            onChosen: index => Session.bridge.streamMemory = values[index]
        }

        Label {
            visible: Session.bridge.streaming
            text: qsTr("Disk Reads at Once")
        }
        ChoiceBox {
            objectName: "streamDiskThreads"
            readonly property var values: {
                const usual = [1, 2, 4]
                const saved = Session.bridge.streamDiskThreads
                return usual.includes(saved) ? usual : usual.concat([saved]).sort((a, b) => a - b)
            }
            Layout.fillWidth: true
            visible: Session.bridge.streaming
            tooltip: qsTr("One for a spinning disk; an SSD reads faster with several.")
            choices: values.map(v => ({label: v === 1 ? qsTr("1 (a spinning disk)") : v === 4 ? qsTr("4 (an SSD)")
                                                                                       : qsTr("%1").arg(v), value: v}))
            chosenIndex: Math.max(0, values.indexOf(Session.bridge.streamDiskThreads))
            onChosen: index => Session.bridge.streamDiskThreads = values[index]
        }

        Label {
            visible: Session.bridge.streaming
            text: qsTr("Cache Folder")
        }
        RowLayout {
            Layout.fillWidth: true
            visible: Session.bridge.streaming
            spacing: 6

            Label {
                objectName: "streamCacheFolder"
                Layout.fillWidth: true
                Layout.maximumWidth: 360
                elide: Text.ElideMiddle
                text: Session.bridge.streamCacheFolder !== "" ? Session.bridge.streamCacheFolder
                                                             : Session.bridge.defaultStreamCacheFolder
                color: Theme.textDim
            }
            RoleButton {
                objectName: "chooseStreamCacheFolder"
                text: qsTr("Choose…")
                tooltip: qsTr("Where files that can't stream as they are (FLAC, MP3, another sample rate) are kept, "
                              + "decoded, and every streamed file's waveform")
                onClicked: cacheFolderDialog.open()
            }
            RoleButton {
                objectName: "defaultStreamCacheFolder"
                visible: Session.bridge.streamCacheFolder !== ""
                text: qsTr("Default")
                onClicked: Session.bridge.streamCacheFolder = ""
            }
        }

        Label {
            visible: Session.bridge.streaming
            text: qsTr("Cache Size Limit")
        }
        ChoiceBox {
            objectName: "streamCacheSize"
            readonly property var values: {
                const usual = [5, 10, 20, 50, 100]
                const saved = Session.bridge.streamCacheSize
                return usual.includes(saved) ? usual : usual.concat([saved]).sort((a, b) => a - b)
            }
            Layout.fillWidth: true
            visible: Session.bridge.streaming
            tooltip: qsTr("Past it, the files used least lately go (they are made again when needed).")
            choices: values.map(v => ({label: qsTr("%1 GB").arg(v), value: v}))
            chosenIndex: Math.max(0, values.indexOf(Session.bridge.streamCacheSize))
            onChosen: index => Session.bridge.streamCacheSize = values[index]
        }
    }

    FolderDialog {
        id: cacheFolderDialog
        objectName: "cacheFolderDialog"
        title: qsTr("Cache Folder for Streaming")
        currentFolder: FileUrls.folderUrl(Session.bridge.streamCacheFolder !== "" ? Session.bridge.streamCacheFolder
                                                                                 : Session.bridge.defaultStreamCacheFolder)
        onAccepted: Session.bridge.streamCacheFolder = FileUrls.localPath(selectedFolder)
    }

    Label {
        objectName: "audioStatus"
        Layout.fillWidth: true
        Layout.maximumWidth: 520
        text: page.prefs.status
        textFormat: Text.RichText
        wrapMode: Text.Wrap
        color: Theme.textDim
    }

    // (Steinberg's ASIO usage guidelines: the attribution where ASIO is set up.)
    Label {
        objectName: "asioTrademark"
        Layout.fillWidth: true
        Layout.maximumWidth: 520
        visible: text !== ""
        text: page.prefs.trademarkNotice
        wrapMode: Text.Wrap
        color: Theme.textDim
        font: Theme.smallFont
    }

    Item {
        Layout.fillHeight: true
    }
}
