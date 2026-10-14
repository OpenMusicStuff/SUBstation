// HostLoop.h on Windows: the thread's window messages (the editors', and
// whatever windows the plug-ins make) and an event the reader thread sets. A key
// pressed in an editor window goes through the key filter before it is
// dispatched: the application's keys never reach the plug-in.

#include <windows.h>

#include <string>

#include "EditorHost.h"
#include "HostLoop.h"

namespace sub::pluginhost {

namespace {

constexpr const wchar_t* kEditorWindowClass = L"SUBstationPluginEditor";  // EditorWindowWin32.cpp's

std::wstring className(HWND hwnd) {
    wchar_t name[64] = {};
    const int length = GetClassNameW(hwnd, name, 64);
    return std::wstring(name, length > 0 ? static_cast<size_t>(length) : 0);
}

// The window, or the top-level window it is in, is one of our editors.
bool inEditor(HWND hwnd) {
    const HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
    return root && className(root) == kEditorWindowClass;
}

// A text field Windows knows (an Edit or RichEdit control).
bool isTextField(HWND hwnd) {
    const std::wstring name = className(hwnd);
    return name.rfind(L"Edit", 0) == 0 || name.rfind(L"RichEdit", 0) == 0;
}

int pressedModifiers() {
    int modifiers = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= EditorKey::kShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= EditorKey::kControl;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= EditorKey::kAlt;
    return modifiers;
}

BOOL CALLBACK findEditor(HWND hwnd, LPARAM found) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || className(hwnd) != kEditorWindowClass) return TRUE;
    *reinterpret_cast<HWND*>(found) = hwnd;
    return FALSE;
}

// Editors drawn at each screen's scale, as the application's own windows are (Qt's): per-monitor
// DPI awareness, version 2 (Windows 10 1703 and later; asked for by name, where it is there).
void perMonitorDpiAware() {
    using SetContext = BOOL(WINAPI*)(HANDLE);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto set = user32 ? reinterpret_cast<SetContext>(
                                  reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
                            : nullptr;
    if (set) set(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

class WinHostLoop final : public HostLoop {
public:
    WinHostLoop() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) { perMonitorDpiAware(); }
    ~WinHostLoop() override { CloseHandle(event_); }

    void wake() noexcept override { SetEvent(event_); }

    void wait(int milliseconds) override {
        MsgWaitForMultipleObjectsEx(1, &event_, static_cast<DWORD>(milliseconds < 0 ? 0 : milliseconds), QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (taken(msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void setKeyFilter(KeyFilter filter) override { filter_ = std::move(filter); }

    bool closeForemostEditor() override {
        HWND found = nullptr;
        EnumWindows(findEditor, reinterpret_cast<LPARAM>(&found));  // (top-level windows, the foremost first)
        if (!found) return false;
        PostMessageW(found, WM_CLOSE, 0, 0);
        return true;
    }

private:
    bool taken(const MSG& msg) const {
        if (!filter_ || (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) || !inEditor(msg.hwnd)) return false;
        constexpr LPARAM kRepeatBit = LPARAM(1) << 30;  // the key was down already
        KeyPress press;
        press.key = static_cast<uint32_t>(msg.wParam);
        press.modifiers = pressedModifiers();
        press.repeat = (msg.lParam & kRepeatBit) != 0;
        press.textField = isTextField(msg.hwnd);
        return filter_(press);
    }

    HANDLE event_;
    KeyFilter filter_;
};

}  // namespace

std::unique_ptr<HostLoop> HostLoop::create() { return std::make_unique<WinHostLoop>(); }

}  // namespace sub::pluginhost
