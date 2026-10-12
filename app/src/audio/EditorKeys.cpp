#include "audio/EditorKeys.h"

#include <Qt>

#include "EditorHost.h"

namespace sub::app {

bool editorsAreEngineWindows() { return sub::editorsUseHost(); }

void setEditorKeyFilter(EditorKeyFilter filter) {
    if (!filter) {
        sub::setEditorKeyFilter(nullptr);
        return;
    }
    sub::setEditorKeyFilter([filter = std::move(filter)](const sub::EditorKey& key) {
        int modifiers = Qt::NoModifier;
        if (key.modifiers & sub::EditorKey::kShift) modifiers |= Qt::ShiftModifier;
        if (key.modifiers & sub::EditorKey::kControl) modifiers |= Qt::ControlModifier;
        if (key.modifiers & sub::EditorKey::kAlt) modifiers |= Qt::AltModifier;
        return filter(key.keysym, modifiers, key.repeat);
    });
}

bool closeForemostEditor() { return sub::closeForemostEditor(); }

}  // namespace sub::app
