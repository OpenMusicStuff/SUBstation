#pragma once
// Plug-in editors as windows of the engine's own (Linux's X11 editors), for the
// UI, which doesn't see the engine: their keys through the engine's key filter
// (its EditorHost.h), in Qt's terms, and the foremost of them.

#include <QtGlobal>

#include <functional>

namespace sub::app {

// Whether plug-in editors are windows of the engine's own here, whose keys the key filter is asked about.
bool editorsAreEngineWindows();

// What the application does with a key pressed in an editor: (its X11 keysym, unshifted; Qt's modifiers; held
// down) → taken (else the plug-in has it). Null: the plug-ins have every key.
using EditorKeyFilter = std::function<bool(quint32 keysym, int modifiers, bool repeat)>;
void setEditorKeyFilter(EditorKeyFilter filter);

// Closes the foremost of the engine's editor windows, as its close button would: false if none is shown.
bool closeForemostEditor();

}  // namespace sub::app
