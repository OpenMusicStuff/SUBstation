import QtQuick

// Where a device body's four parameter cells go (PluginDeviceBody,
// DeviceKnobPages): two by two, the columns `columnSpacing` apart and centred
// across the area, so the margins left and right come out equal; each row in
// the middle of its half of the height.
// The body's Repeater of cells is a child, given as `cells`; each cell takes
// its x and y from cellX() and cellY() by its place among them. A row's cells
// line up at the top, centred as its tallest one (a list is shorter than a
// knob). The implicit height is the grid's as it was: 6 above, the rows
// `rowSpacing` apart, 6 below.
Item {
    id: quadrants

    property Repeater cells
    property real rowSpacing: 14  // (DeviceKnobPages's)
    property real columnSpacing: 9  // the knobs 93 px apart (PARAM_WIDTH 84 + 9)

    // The tallest cell of a row.
    function rowHeight(row) {
        let tallest = 0
        if (!cells)
            return tallest
        for (let i = 2 * row; i < Math.min(2 * row + 2, cells.count); ++i) {
            const item = cells.itemAt(i)
            if (item)
                tallest = Math.max(tallest, item.implicitHeight)
        }
        return tallest
    }
    function cellX(place, cellWidth) {
        const left = (width - 2 * cellWidth - columnSpacing) / 2
        return Math.round(left + (place % 2) * (cellWidth + columnSpacing))
    }
    function cellY(place) {
        const row = Math.floor(place / 2)
        return Math.round((row + 0.5) * height / 2 - rowHeight(row) / 2)
    }

    implicitHeight: cells && cells.count > 0
                    ? 6 + rowHeight(0) + (cells.count > 2 ? rowSpacing + rowHeight(1) : 0) + 6 : 0
}
