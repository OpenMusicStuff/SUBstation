#pragma once

// The main window's shortcuts while a plug-in's editor has the focus.
//
// On Windows plug-in editors are plain Win32 windows (the engine's
// EditorWindowWin32.cpp, window class kEditorWindowClass), so Qt never sees
// their keys as key events, but their messages still come through Qt's event
// loop. This native event filter catches a key press there and, if it is one
// of the main window's shortcuts the plug-in shouldn't keep, triggers that
// Action (or Shortcut) of the window instead of letting the plug-in have it.
// (Posting the key to the main window wouldn't do: Qt Quick's shortcuts only
// fire in the window that has the focus.)
//
// On Linux editors are X11 windows of the engine's own connections
// (EditorWindowX11.cpp), whose keys Qt never sees: the engine grabs the keys
// a shortcut can be in them and asks this object, its key filter (through the
// application layer's audio/EditorKeys.h), whether the window takes each, by
// its X keysym (keysymPressed()).
//
// The rules:
// - Without Ctrl or Alt, only the DAW's keys are taken: Space (play / stop)
//   and S (solo); not with Shift, not while the focus is in a text field
//   Windows knows (an Edit or RichEdit control: most plug-ins draw their own,
//   which can't be told apart), and not a key the computer MIDI keyboard plays
//   while it is on, as in the main window. Other keys without Ctrl or Alt
//   (Delete, letters...) stay with the plug-in.
// - The text-editing shortcuts (Ctrl+A/C/V/X/Z/Y, Ctrl+Shift+Z) always stay
//   with the plug-in: it may be typing into a field of its own.
// - Otherwise the window's first enabled Action or Shortcut whose key sequence
//   matches exactly. Held down, a key without Ctrl or Alt acts once (its
//   repeats are swallowed): Space held doesn't start and stop over and over.
// - While a render's dialog is up the window takes no keys, from plug-ins'
//   editors either.
//
// Ctrl+W (View › Close Plug-in Editor) closes the foremost editor, from the
// main window or from any editor (closeForemostEditor()).
// Ctrl+Shift+W (View › Reopen Closed Plug-in Editor) reopens the selected
// track's editor closed last, from either too (an Action like any other).
//
// Where plug-ins show no editor (neither Windows nor Linux with X11 editors)
// nothing is installed and closeForemostEditor() does nothing. The rules
// themselves (actionFor(), keyPressed()) are plain code, and the tests run
// them anywhere.
// Native event filters see every message of the process: what runs for a
// message that isn't a key press in an editor stays cheap.

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

#include "session/Session.h"

namespace sub::ui {

class PluginEditorKeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    QML_ELEMENT
    // The window whose Actions and Shortcuts the keys trigger.
    Q_PROPERTY(QObject* target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(sub::app::Session* session READ session WRITE setSession NOTIFY sessionChanged)
    // Whether plug-in editors are windows whose keys this sees (Windows; Linux with X11 editors).
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    static constexpr const wchar_t* kEditorWindowClass = L"SUBstationPluginEditor";  // EditorWindow.cpp's kWindowClass

    explicit PluginEditorKeys(QObject* parent = nullptr);
    ~PluginEditorKeys() override;

    QObject* target() const { return target_; }
    void setTarget(QObject* target);
    app::Session* session() const { return session_; }
    void setSession(app::Session* session);
    static bool supported();

    // The Qt key for a Windows virtual-key code, for the keys shortcuts use
    // (letters, digits, F-keys and a few others); 0 for others.
    static int qtKey(int virtualKey);
    // The Qt key for an X11 keysym, for the same keys (unshifted: XK_a for A); 0 for others.
    static int qtKeyForKeysym(quint32 keysym);
    // The target's Action or Shortcut for this key pressed in a plug-in's
    // editor (`textField`: its focus is in a Windows text field), if the plug-in
    // shouldn't keep it; else null.
    Q_INVOKABLE QObject* actionFor(int virtualKey, int modifiers, bool textField = false) const;
    // A key pressed in a plug-in's editor: true if the window takes it (its
    // action is triggered, unless it is a `repeat` of a key held without Ctrl
    // or Alt); false: the plug-in has it.
    Q_INVOKABLE bool keyPressed(int virtualKey, int modifiers, bool textField = false, bool repeat = false);
    // The same for a key pressed in an X11 editor (Linux), by its keysym. (No text field is seen there.) The
    // action is triggered once the event loop runs again: it is asked from inside the editor's own events.
    Q_INVOKABLE bool keysymPressed(quint32 keysym, int modifiers, bool repeat = false);
    // Closes the foremost plug-in editor of this process, as its close button
    // would: false if none is shown (or plug-ins show no editor here).
    Q_INVOKABLE static bool closeForemostEditor();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

Q_SIGNALS:
    void targetChanged();
    void sessionChanged();

private:
    // The rules, for a Qt key.
    QObject* actionForKey(int key, int modifiers, bool textField) const;
    bool pressed(int key, int modifiers, bool textField, bool repeat, Qt::ConnectionType when = Qt::DirectConnection);

    QPointer<QObject> target_;
    QPointer<app::Session> session_;
    bool installed_ = false;  // the native event filter (Windows)
    bool filtering_ = false;  // the engine's key filter (Linux)
};

}  // namespace sub::ui
