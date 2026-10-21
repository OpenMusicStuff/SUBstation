import QtQuick
import QtQuick.Controls
import SUBstation

// The browser's sidebar: CATEGORIES (All, Samples and what they sound like,
// Built-in and its categories, Plug-ins and its, Presets and a sub-entry per
// device) and PLACES (each place, Add Folder…), from the controller's
// SidebarModel. A click shows an entry (its scope); "Add Folder…" asks for a
// folder instead (the controller's addPlaceRequested). An entry with sub-entries
// has an arrow that collapses or expands it (Samples starts collapsed). A
// right-click opens the entry's menu (contextMenuRequested). The entry shown
// is on a wash of the accent, a stripe of it on its left; a sub-entry's title
// lines up with its parent's (only the top level has icons).
Rectangle {
    id: sidebar

    readonly property var browser: Session.browser
    readonly property alias view: list
    // The row of the entry shown (the controller's scope).
    readonly property int currentRow: {
        browser.sidebar.count  // (made again: the rows may have moved)
        return browser.sidebar.find(browser.scope)
    }

    // What the entry shown is drawn on (the lists' selectionWash).
    readonly property color currentWash: Qt.tint(Theme.panel, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14))

    // A right-click on an entry (its scope; [] below the entries).
    signal contextMenuRequested(var scope)

    color: Theme.panel

    ListView {
        id: list
        objectName: "sidebarList"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: sidebar.browser.sidebar
        currentIndex: sidebar.currentRow
        highlightFollowsCurrentItem: false
        ScrollBar.vertical: ScrollBar {
            id: scrollBar
        }

        delegate: Item {
            id: entry

            required property int index
            required property string title
            required property var scope
            required property bool section
            required property int depth
            required property string icon
            required property string toolTip
            required property bool dim
            required property bool selectable
            required property bool expandable
            required property bool expanded

            readonly property bool current: selectable && sidebar.currentRow === index

            width: ListView.view.width
            // (A heading has room above it; the first, its capitals level with the list's first name.)
            implicitHeight: section ? heading.implicitHeight + (index === 0 ? 8 : 14)
                                    : Math.max(Theme.listRowHeight, label.implicitHeight + 4)

            Rectangle {
                anchors.fill: parent
                visible: !entry.section
                color: entry.current ? sidebar.currentWash : (hover.hovered ? Theme.panelAlt : "transparent")

                Rectangle {
                    visible: entry.current
                    width: 2
                    height: parent.height
                    color: Theme.accent
                }
            }

            Text {
                id: heading
                visible: entry.section
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                text: entry.title
                font: Theme.listHeadingFont
                color: Theme.textDim
            }

            Row {
                visible: !entry.section
                anchors.left: parent.left
                anchors.leftMargin: 6 + entry.depth * 20  // (a level in: an icon and the space after it)
                anchors.right: arrow.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                // The icon's place, on the top level (Add Folder…'s is empty: its title lines up).
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.depth === 0
                    width: Theme.iconSize
                    height: Theme.iconSize

                    Icon {
                        visible: entry.icon !== ""
                        name: entry.icon
                        color: Theme.textDim
                    }
                }
                Text {
                    id: label
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x - 4
                    text: entry.title
                    font: Theme.listFont
                    elide: Text.ElideRight
                    color: entry.dim ? Theme.textDim : Theme.text
                }
            }

            // Its arrow: right while collapsed, down while expanded.
            Item {
                id: arrow
                objectName: entry.expandable ? "sidebarArrow:" + entry.scope.join("/") : ""
                anchors.right: parent.right
                anchors.rightMargin: scrollBar.size < 1 ? scrollBar.width : 0  // (clear of the scroll bar)
                width: entry.expandable ? 18 : 0
                height: parent.height

                Icon {
                    anchors.centerIn: parent
                    visible: entry.expandable
                    name: "fold"
                    checked: !entry.expanded
                    size: 9
                    color: Theme.textDim
                }
                TapHandler {
                    enabled: entry.expandable
                    onTapped: sidebar.browser.setExpanded(entry.scope, !entry.expanded)
                }
            }

            HoverHandler {
                id: hover
            }
            // A click shows the entry: up to its arrow (a click on the arrow only collapses or expands it).
            Item {
                anchors.left: parent.left
                anchors.right: arrow.left
                height: parent.height

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    enabled: entry.selectable
                    onTapped: sidebar.browser.scope = entry.scope
                }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: sidebar.contextMenuRequested(entry.section ? [] : entry.scope)
            }

            ToolTip.visible: Window.window !== null && hover.hovered && entry.toolTip !== ""
            ToolTip.text: entry.toolTip
            ToolTip.delay: 700
        }

        // A right-click below the entries: the menu of none.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: eventPoint => {
                if (list.indexAt(eventPoint.position.x + list.contentX, eventPoint.position.y + list.contentY) < 0)
                    sidebar.contextMenuRequested([])
            }
        }
    }
}
