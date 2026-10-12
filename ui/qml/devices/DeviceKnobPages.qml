import QtQuick
import SUBstation

// The body of a built-in device without an editor of its own: a knob per
// parameter (or a list), four to a page in a 2×2 grid (PARAMS_PER_PAGE,
// PARAM_COLUMNS), centred under the title bar (KnobQuadrants:
// equal margins left and right; DevicePanel's deviceHeight has room for its
// rowSpacing), the device's title bar paging through them (`pages`, `page`,
// as an editor's).
Item {
    id: body

    required property string trackId
    required property string deviceId
    readonly property int perPage: 4
    readonly property int columns: 2
    readonly property int pages: Math.max(1, Math.ceil(params.count / perPage))
    property int page: 0
    readonly property alias params: params
    readonly property int rowSpacing: 14

    implicitWidth: 216 - 2
    implicitHeight: grid.implicitHeight

    DeviceParams {
        id: params
        session: Session
        trackId: body.trackId
        deviceId: body.deviceId
    }

    KnobQuadrants {
        id: grid
        width: body.width
        height: body.height
        rowSpacing: body.rowSpacing
        cells: deviceCells

        Repeater {
            id: deviceCells
            model: params.ids.slice(body.page * body.perPage, (body.page + 1) * body.perPage)
            DeviceParamKnob {
                required property string modelData
                required property int index
                objectName: "param_" + modelData
                x: grid.cellX(index, implicitWidth)
                y: grid.cellY(index)
                trackId: body.trackId
                deviceId: body.deviceId
                paramId: modelData
            }
        }
    }
}
