#include "PluginEditorKeys.h"

#include <QCoreApplication>
#include <QKeyCombination>
#include <QMetaObject>
#include <QVariant>
#include <QVariantList>

#include "audio/EditorKeys.h"
#include "audio/EngineBridge.h"
#include "input/Shortcuts.h"
#include "session/ComputerKeyboard.h"
#include "session/RenderProgress.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cwctype>
#include <string>
#endif

namespace sub::ui {

namespace {

const PluginEditorKeys* filterOwner = nullptr;  // the one the engine's key filter calls (Linux)

constexpr int kVirtualSpace = 0x20;

// The plug-in's own text editing, never taken from it.
const QList<QKeySequence>& keptForPlugin() {
    static const QList<QKeySequence> kept{
        QKeySequence(QStringLiteral("Ctrl+A")), QKeySequence(QStringLiteral("Ctrl+C")),
        QKeySequence(QStringLiteral("Ctrl+V")), QKeySequence(QStringLiteral("Ctrl+X")),
        QKeySequence(QStringLiteral("Ctrl+Z")), QKeySequence(QStringLiteral("Ctrl+Y")),
        QKeySequence(QStringLiteral("Ctrl+Shift+Z"))};
    return kept;
}

// Keys without Ctrl or Alt that are the main window's in a plug-in's editor too.
bool dawKey(int key) { return key == Qt::Key_Space || key == Qt::Key_S; }

bool matches(const QKeySequence& pressed, const QList<QKeySequence>& sequences) {
    for (const QKeySequence& sequence : sequences)
        if (!sequence.isEmpty() && pressed.matches(sequence) == QKeySequence::ExactMatch) return true;
    return false;
}

#ifdef Q_OS_WIN
std::wstring className(HWND hwnd) {
    wchar_t name[64] = {};
    const int length = GetClassNameW(hwnd, name, 64);
    return std::wstring(name, length > 0 ? size_t(length) : 0);
}

bool isEditorClass(HWND hwnd) { return className(hwnd) == PluginEditorKeys::kEditorWindowClass; }

// Whether `hwnd` is (inside) a plug-in editor window.
bool isPluginEditor(HWND hwnd) {
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    return root != nullptr && isEditorClass(root);
}

// Whether `hwnd` is one of Windows' own text fields (a plug-in typing into it keeps its keys).
bool isTextField(HWND hwnd) {
    std::wstring name = className(hwnd);
    for (wchar_t& c : name) c = static_cast<wchar_t>(std::towlower(c));
    return name.rfind(L"edit", 0) == 0 || name.rfind(L"richedit", 0) == 0;
}

int pressedModifiers() {
    int modifiers = Qt::NoModifier;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= Qt::ControlModifier;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= Qt::ShiftModifier;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= Qt::AltModifier;
    return modifiers;
}

// The foremost editor of these processes (top-level windows come from the top down).
struct EditorSearch {
    QList<int> processes;
    HWND found = nullptr;
};

BOOL CALLBACK findEditor(HWND hwnd, LPARAM search) {
    auto* s = reinterpret_cast<EditorSearch*>(search);
    DWORD process = 0;
    GetWindowThreadProcessId(hwnd, &process);
    if (s->processes.contains(int(process)) && IsWindowVisible(hwnd) && isEditorClass(hwnd)) {
        s->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

bool closeForemostOf(const QList<int>& processes) {
    EditorSearch search{processes};
    EnumWindows(findEditor, reinterpret_cast<LPARAM>(&search));
    if (!search.found) return false;
    PostMessageW(search.found, WM_CLOSE, 0, 0);  // (another process's too)
    return true;
}
#endif

// EditorKey's modifiers (the engine's EditorHost.h, which the UI doesn't include): the key table's.
constexpr int kEditorShift = 1, kEditorControl = 2, kEditorAlt = 4;

int editorModifiers(Qt::KeyboardModifiers modifiers) {
    return (modifiers & Qt::ShiftModifier ? kEditorShift : 0) | (modifiers & Qt::ControlModifier ? kEditorControl : 0) |
           (modifiers & Qt::AltModifier ? kEditorAlt : 0);
}

int qtModifiers(int editor) {
    int modifiers = Qt::NoModifier;
    if (editor & kEditorShift) modifiers |= Qt::ShiftModifier;
    if (editor & kEditorControl) modifiers |= Qt::ControlModifier;
    if (editor & kEditorAlt) modifiers |= Qt::AltModifier;
    return modifiers;
}

}  // namespace

PluginEditorKeys::PluginEditorKeys(QObject* parent) : QObject(parent) {
#ifdef Q_OS_WIN
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        installed_ = true;
    }
#else
    if (app::editorsAreEngineWindows()) {
        app::setEditorKeyFilter(
            [this](quint32 keysym, int modifiers, bool repeat) { return keysymPressed(keysym, modifiers, repeat); });
        filterOwner = this;
        filtering_ = true;
    }
#endif
}

PluginEditorKeys::~PluginEditorKeys() {
    if (installed_ && QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
    if (filtering_ && filterOwner == this) {  // (not a newer one's)
        app::setEditorKeyFilter(nullptr);
        filterOwner = nullptr;
    }
}

void PluginEditorKeys::setTarget(QObject* target) {
    if (target == target_) return;
    target_ = target;
    Q_EMIT targetChanged();
    updateSandboxKeys();
}

void PluginEditorKeys::setSession(app::Session* session) {
    if (session == session_) return;
    for (const QMetaObject::Connection& connection : sessionConnections_) disconnect(connection);
    sessionConnections_.clear();
    session_ = session;
    if (session_) {
        app::EngineBridge* bridge = session_->bridge();
        sessionConnections_ << connect(bridge, &app::EngineBridge::pluginEditorKey, this,
                                       &PluginEditorKeys::sandboxKeyPressed);
        // What the window takes may be other now: the table again.
        const auto update = [this] { updateSandboxKeys(); };
        sessionConnections_ << connect(bridge, &app::EngineBridge::pluginEditorChanged, this, update);
        sessionConnections_ << connect(session_->computerKeyboard(), &app::ComputerKeyboard::changed, this, update);
        sessionConnections_ << connect(session_->render(), &app::RenderProgress::activeChanged, this, update);
    }
    Q_EMIT sessionChanged();
    updateSandboxKeys();
}

bool PluginEditorKeys::supported() {
#ifdef Q_OS_WIN
    return true;
#else
    return app::editorsAreEngineWindows();
#endif
}

int PluginEditorKeys::qtKey(int vk) {
    if (vk >= 0x41 && vk <= 0x5A) return Qt::Key_A + (vk - 0x41);
    if (vk >= 0x30 && vk <= 0x39) return Qt::Key_0 + (vk - 0x30);
    if (vk >= 0x70 && vk <= 0x7B) return Qt::Key_F1 + (vk - 0x70);
    switch (vk) {
        case 0xBB: return Qt::Key_Equal;   // VK_OEM_PLUS: the = + key
        case 0xBC: return Qt::Key_Comma;   // VK_OEM_COMMA
        case 0xBD: return Qt::Key_Minus;   // VK_OEM_MINUS
        case 0xBE: return Qt::Key_Period;  // VK_OEM_PERIOD
        case 0x08: return Qt::Key_Backspace;
        case 0x09: return Qt::Key_Tab;
        case 0x2E: return Qt::Key_Delete;
        case 0x24: return Qt::Key_Home;
        case kVirtualSpace: return Qt::Key_Space;
        default: return 0;
    }
}

int PluginEditorKeys::qtKeyForKeysym(quint32 keysym) {
    if (keysym >= 0x61 && keysym <= 0x7A) return Qt::Key_A + int(keysym - 0x61);    // XK_a..XK_z
    if (keysym >= 0x30 && keysym <= 0x39) return Qt::Key_0 + int(keysym - 0x30);    // XK_0..XK_9
    if (keysym >= 0xFFBE && keysym <= 0xFFC9) return Qt::Key_F1 + int(keysym - 0xFFBE);  // XK_F1..XK_F12
    switch (keysym) {
        case 0x3D: return Qt::Key_Equal;   // XK_equal
        case 0x2C: return Qt::Key_Comma;   // XK_comma
        case 0x2D: return Qt::Key_Minus;   // XK_minus
        case 0x2E: return Qt::Key_Period;  // XK_period
        case 0xFF08: return Qt::Key_Backspace;
        case 0xFF09: return Qt::Key_Tab;
        case 0xFFFF: return Qt::Key_Delete;
        case 0xFF50: return Qt::Key_Home;
        case 0x20: return Qt::Key_Space;
        default: return 0;
    }
}

QObject* PluginEditorKeys::actionFor(int virtualKey, int modifiers, bool textField) const {
    return actionForKey(qtKey(virtualKey), modifiers, textField);
}

namespace {

// An Action's or Shortcut's key sequences.
QList<QKeySequence> sequencesOf(QObject* candidate) {
    QVariant shortcut;
    if (candidate->inherits("QQuickAction"))
        shortcut = candidate->property("shortcut");
    else if (candidate->inherits("QQuickShortcut"))
        shortcut = QVariantList{candidate->property("sequence"), candidate->property("sequences")};
    else
        return {};
    return keySequences(shortcut);
}

}  // namespace

QList<QObject*> PluginEditorKeys::candidates() const {
    if (!target_) return {};
    // The window's Actions (the menus') and its Shortcuts (their other keys), in the order they were made.
    QList<QObject*> found{target_.data()};
    found += target_->findChildren<QObject*>();
    return found;
}

bool PluginEditorKeys::takesKey(int key, int modifiers) const {
    if (key == 0) return false;
    const auto mods = Qt::KeyboardModifiers(modifiers) & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    if (!(mods & (Qt::ControlModifier | Qt::AltModifier))) {
        if (!dawKey(key) || (mods & Qt::ShiftModifier)) return false;
        if (session_ && session_->computerKeyboard()->takesKey(key)) return false;
    }
    return !matches(QKeySequence(QKeyCombination(mods, Qt::Key(key))), keptForPlugin());
}

QObject* PluginEditorKeys::actionForKey(int key, int modifiers, bool textField) const {
    if (!target_ || !takesKey(key, modifiers)) return nullptr;
    const auto mods = Qt::KeyboardModifiers(modifiers) & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    if (!(mods & (Qt::ControlModifier | Qt::AltModifier)) && textField) return nullptr;
    const QKeySequence pressed(QKeyCombination(mods, Qt::Key(key)));
    for (QObject* candidate : candidates()) {
        if (!candidate->inherits("QQuickAction") && !candidate->inherits("QQuickShortcut")) continue;
        if (!candidate->property("enabled").toBool()) continue;
        if (matches(pressed, sequencesOf(candidate))) return candidate;
    }
    return nullptr;
}

quint32 PluginEditorKeys::systemKey(int key) {
#ifdef Q_OS_WIN
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return quint32(0x41 + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return quint32(0x30 + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return quint32(0x70 + (key - Qt::Key_F1));
    switch (key) {
        case Qt::Key_Equal: return 0xBB;
        case Qt::Key_Comma: return 0xBC;
        case Qt::Key_Minus: return 0xBD;
        case Qt::Key_Period: return 0xBE;
        case Qt::Key_Backspace: return 0x08;
        case Qt::Key_Tab: return 0x09;
        case Qt::Key_Delete: return 0x2E;
        case Qt::Key_Home: return 0x24;
        case Qt::Key_Space: return quint32(kVirtualSpace);
        default: return 0;
    }
#else
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return quint32(0x61 + (key - Qt::Key_A));  // XK_a..XK_z
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return quint32(0x30 + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return quint32(0xFFBE + (key - Qt::Key_F1));
    switch (key) {
        case Qt::Key_Equal: return 0x3D;
        case Qt::Key_Comma: return 0x2C;
        case Qt::Key_Minus: return 0x2D;
        case Qt::Key_Period: return 0x2E;
        case Qt::Key_Backspace: return 0xFF08;
        case Qt::Key_Tab: return 0xFF09;
        case Qt::Key_Delete: return 0xFFFF;
        case Qt::Key_Home: return 0xFF50;
        case Qt::Key_Space: return 0x20;
        default: return 0;
    }
#endif
}

QList<QPair<quint32, int>> PluginEditorKeys::sandboxKeys() const {
    QList<QPair<quint32, int>> keys;
    if (session_ && session_->render()->active()) return keys;  // (the window takes no keys meanwhile)
    for (QObject* candidate : candidates()) {
        for (const QKeySequence& sequence : sequencesOf(candidate)) {
            if (sequence.count() != 1) continue;  // (a chord of several keys: not from an editor)
            const QKeyCombination combination = sequence[0];
            const int key = int(combination.key());
            const auto mods = combination.keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
            const quint32 code = systemKey(key);
            if (code == 0 || !takesKey(key, int(mods))) continue;
            const QPair<quint32, int> entry{code, editorModifiers(mods)};
            if (!keys.contains(entry)) keys.append(entry);
        }
    }
    return keys;
}

void PluginEditorKeys::updateSandboxKeys() {
    if (session_) session_->bridge()->setSandboxEditorKeys(sandboxKeys());
}

void PluginEditorKeys::sandboxKeyPressed(quint32 key, int modifiers, bool repeat, bool textField) {
#ifdef Q_OS_WIN
    const int qt = qtKey(int(key));
#else
    const int qt = qtKeyForKeysym(key);
#endif
    // (What the key does is done once the bridge is done with what the plug-ins reported.)
    pressed(qt, qtModifiers(modifiers), textField, repeat, Qt::QueuedConnection);
}

bool PluginEditorKeys::keyPressed(int virtualKey, int modifiers, bool textField, bool repeat) {
    return pressed(qtKey(virtualKey), modifiers, textField, repeat);
}

bool PluginEditorKeys::keysymPressed(quint32 keysym, int modifiers, bool repeat) {
    // (Asked from inside the editor window's own events: what the key does, which may close it or every editor,
    // is done once that is over.)
    return pressed(qtKeyForKeysym(keysym), modifiers, false, repeat, Qt::QueuedConnection);
}

bool PluginEditorKeys::pressed(int key, int modifiers, bool textField, bool repeat, Qt::ConnectionType when) {
    if (session_ && session_->render()->active()) return false;  // (the window takes no keys meanwhile)
    QObject* action = actionForKey(key, modifiers, textField);
    if (!action) return false;
    const bool held = repeat && !(Qt::KeyboardModifiers(modifiers) & (Qt::ControlModifier | Qt::AltModifier));
    if (!held) {  // (Space held down doesn't start and stop playing over and over)
        if (action->inherits("QQuickShortcut"))
            QMetaObject::invokeMethod(action, "activated", when);
        else
            QMetaObject::invokeMethod(action, "trigger", when);
    }
    return true;
}

bool PluginEditorKeys::closeForemostEditor() {
#ifdef Q_OS_WIN
    return closeForemostOf({int(GetCurrentProcessId())});
#else
    return app::closeForemostEditor();
#endif
}

bool PluginEditorKeys::closeForemostPluginEditor() {
#ifdef Q_OS_WIN
    QList<int> processes{int(GetCurrentProcessId())};
    if (session_) processes += session_->bridge()->sandboxProcessIds();
    return closeForemostOf(processes);
#else
    if (closeForemostEditor()) return true;
    return session_ && session_->bridge()->closeForemostSandboxEditor();
#endif
}

bool PluginEditorKeys::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" || !message) return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if ((msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN) || !isPluginEditor(msg->hwnd)) return false;
    constexpr LPARAM kRepeatBit = LPARAM(1) << 30;  // a WM_KEYDOWN's lParam: the key was down already
    if (!keyPressed(int(msg->wParam), pressedModifiers(), isTextField(msg->hwnd), (msg->lParam & kRepeatBit) != 0))
        return false;
    if (result) *result = 0;
    return true;
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}

}  // namespace sub::ui
