import QtQuick
import QtQuick.Controls
import SUBstation

// A built-in device parameter's right-click menu: Show Automation (if it can be
// automated), Delete Automation (if it has an envelope), Re-Enable Automation
// (while its automation is overridden) and, on a device inside a rack, Map to
// Macro (the rack's macros, by name, the one it is mapped to checked) and Unmap
// from its macro; and Modulate With (the modulators on its track: an LFO, an
// Envelope), Invert Modulation and Remove Modulation for one a modulator moves.
// Its entries are made for how things are when it opens: show() fills it and
// pops it up at the mouse.
DynamicMenu {
    id: menu

    property DeviceParam param: null

    // Fills the menu for the parameter as it is now.
    function build() {
        clear()
        if (!param)
            return
        const p = param
        automationEntries(p)
        const rack = p.rackId()
        if (rack !== "" && (p.macroNames().length > 0 || p.macro() >= 0)) {  // its rack's macros can move it (a Drum Rack has none)
            separator()
            const mapped = p.macro()
            const mappedRack = p.macroRack()
            const names = p.macroNames()
            const macros = submenu(qsTr("Map to Macro"))
            for (let i = 0; i < names.length; ++i) {
                const index = i
                entry(names[i], () => p.mapToMacro(index), mappedRack === rack && mapped === i, true, macros)
            }
            if (mapped >= 0) {
                const name = mappedRack === rack && mapped < names.length ? names[mapped]
                                                                          : qsTr("Macro %1").arg(mapped + 1)
                entry(qsTr("Unmap from %1").arg(name), () => p.unmapFromMacro())
            }
        }
        const modulators = p.modulatorChoices()
        if (modulators.length > 0 || p.modulated) {
            separator()
            if (modulators.length > 0) {
                const choose = submenu(qsTr("Modulate With"))
                for (let i = 0; i < modulators.length; ++i) {
                    const id = modulators[i].id
                    entry(modulators[i].name, () => p.modulateWith(id), p.modulator === id, true, choose)
                }
            }
            if (p.modulated) {
                entry(qsTr("Invert Modulation"), () => p.setModulationInverted(!p.modulationInverted),
                      p.modulationInverted)
                entry(qsTr("Remove Modulation (%1)").arg(p.modulatorName), () => p.unmodulate())
            }
        }
    }

    function show() {
        build()
        popup()
    }
}
