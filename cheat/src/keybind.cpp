// D:\Work\Fut\cheat\src\keybind.cpp
// See keybind.h for the three key classes and why X1/X2 need their own path.
#include "keybind.h"
#include "log.h"
#include "obf.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace keybind {

// ---------------------------------------------------------------------------
// mouse side-button state
//
// Set by WM_XBUTTONDOWN/UP and cleared on capture change. These are the only
// signals Windows gives for X1/X2: no VK code, no GetAsyncKeyState, no
// GetKeyState. A flag is therefore the whole mechanism.
// ---------------------------------------------------------------------------
static volatile LONG s_x1Down = 0;
static volatile LONG s_x2Down = 0;

// ---------------------------------------------------------------------------
// capture state
// ---------------------------------------------------------------------------
static volatile LONG s_capturing = 0;
static volatile LONG s_captured  = 0;   // kNone until a key arrives

void BeginCapture() {
    InterlockedExchange(&s_captured, kNone);
    InterlockedExchange(&s_capturing, 1);
    // Reset the side-button latches: a button already held when capture starts
    // must not be mistaken for the user's choice.
    InterlockedExchange(&s_x1Down, 0);
    InterlockedExchange(&s_x2Down, 0);
    LOG(SBOX("[bind] capture started -- press any key or mouse button", 0x1F0));
}

void CancelCapture() {
    InterlockedExchange(&s_capturing, 0);
    InterlockedExchange(&s_captured, kNone);
}

bool Capturing() { return InterlockedCompareExchange(&s_capturing, 0, 0) != 0; }

int CapturedValue() { return (int)InterlockedCompareExchange(&s_captured, 0, 0); }

bool ConsumeCaptured(int& outVk) {
    if (InterlockedCompareExchange(&s_capturing, 0, 0) == 0) return false;
    const int v = (int)InterlockedCompareExchange(&s_captured, 0, 0);
    if (v == kNone) return false;
    InterlockedExchange(&s_capturing, 0);
    outVk = v;
    return true;
}

// ---------------------------------------------------------------------------
// message intake
// ---------------------------------------------------------------------------
int OnWndProcMessage(unsigned int msg, uintptr_t wparam, intptr_t lparam) {
    (void)lparam;

    // ---- always track X1/X2, capturing or not ------------------------------
    // The aim trigger needs this state even when no UI is open, so it is updated
    // unconditionally.
    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
        const WORD which = HIWORD(wparam);            // XBUTTON1 / XBUTTON2
        const bool down  = (msg == WM_XBUTTONDOWN);
        if (which == XBUTTON1) InterlockedExchange(&s_x1Down, down ? 1 : 0);
        if (which == XBUTTON2) InterlockedExchange(&s_x2Down, down ? 1 : 0);
    }

    if (InterlockedCompareExchange(&s_capturing, 0, 0) == 0) return kNone;

    // ---- capturing: report the first key/button seen -----------------------
    auto claim = [](int vk) -> int {
        if (InterlockedCompareExchange(&s_captured, vk, kNone) == kNone)
            return vk;                    // we won the race
        return kNone;                     // someone already claimed it
    };

    if (msg == WM_XBUTTONDOWN) {
        const WORD which = HIWORD(wparam);
        if (which == XBUTTON1) return claim(kMouseX1);
        if (which == XBUTTON2) return claim(kMouseX2);
        return kNone;
    }
    if (msg == WM_LBUTTONDOWN) return claim(VK_LBUTTON);
    if (msg == WM_RBUTTONDOWN) return claim(VK_RBUTTON);
    if (msg == WM_MBUTTONDOWN) return claim(VK_MBUTTON);

    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        const int vk = (int)(wparam & 0xFF);

        // bit 30 of lParam marks an auto-repeat; a held key must not re-bind.
        const bool wasDown = (lparam & (1LL << 30)) != 0;
        if (wasDown) return kNone;

        // Let ESC cancel instead of binding, so the button is not a trap.
        if (vk == VK_ESCAPE) { CancelCapture(); return kNone; }

        return claim(vk);
    }

    return kNone;
}

// ---------------------------------------------------------------------------
// query
// ---------------------------------------------------------------------------
bool IsDown(int vk) {
    if (vk == kNone) return false;

    if (vk == kMouseX1) return InterlockedCompareExchange(&s_x1Down, 0, 0) != 0;
    if (vk == kMouseX2) return InterlockedCompareExchange(&s_x2Down, 0, 0) != 0;

    if (vk < 0x01 || vk > 0xFE) return false;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------
const char* Name(int vk) {
    switch (vk) {
    case kNone:      return u8"无";
    case kMouseX1:   return u8"鼠标侧键 X1 (后退)";
    case kMouseX2:   return u8"鼠标侧键 X2 (前进)";
    case VK_LBUTTON: return u8"鼠标左键";
    case VK_RBUTTON: return u8"鼠标右键";
    case VK_MBUTTON: return u8"鼠标中键";
    case VK_XBUTTON1:return u8"鼠标侧键 X1";
    case VK_XBUTTON2:return u8"鼠标侧键 X2";
    case VK_SHIFT:   return u8"Shift";
    case VK_LSHIFT:  return u8"左 Shift";
    case VK_RSHIFT:  return u8"右 Shift";
    case VK_CONTROL: return u8"Ctrl";
    case VK_LCONTROL:return u8"左 Ctrl";
    case VK_RCONTROL:return u8"右 Ctrl";
    case VK_MENU:    return u8"Alt";
    case VK_LMENU:   return u8"左 Alt";
    case VK_RMENU:   return u8"右 Alt";
    case VK_SPACE:   return u8"空格";
    case VK_TAB:     return u8"Tab";
    case VK_CAPITAL: return u8"CapsLock";
    case VK_INSERT:  return u8"Insert";
    case VK_DELETE:  return u8"Delete";
    case VK_HOME:    return u8"Home";
    case VK_END:     return u8"End";
    case VK_PRIOR:   return u8"PageUp";
    case VK_NEXT:    return u8"PageDown";
    case VK_UP:      return u8"方向键 上";
    case VK_DOWN:    return u8"方向键 下";
    case VK_LEFT:    return u8"方向键 左";
    case VK_RIGHT:   return u8"方向键 右";
    case VK_OEM_3:   return u8"` 键";
    case VK_OEM_MINUS: return u8"- 键";
    case VK_OEM_PLUS:  return u8"= 键";
    case VK_OEM_4:   return u8"[ 键";
    case VK_OEM_6:   return u8"] 键";
    case VK_OEM_5:   return u8"\\ 键";
    case VK_OEM_1:   return u8"; 键";
    case VK_OEM_7:   return u8"' 键";
    case VK_OEM_COMMA:  return u8", 键";
    case VK_OEM_PERIOD: return u8". 键";
    case VK_OEM_2:   return u8"/ 键";
    case VK_NUMPAD0: return u8"小键盘 0";
    case VK_NUMPAD1: return u8"小键盘 1";
    case VK_NUMPAD2: return u8"小键盘 2";
    case VK_NUMPAD3: return u8"小键盘 3";
    case VK_NUMPAD4: return u8"小键盘 4";
    case VK_NUMPAD5: return u8"小键盘 5";
    case VK_NUMPAD6: return u8"小键盘 6";
    case VK_NUMPAD7: return u8"小键盘 7";
    case VK_NUMPAD8: return u8"小键盘 8";
    case VK_NUMPAD9: return u8"小键盘 9";
    default: break;
    }

    // F1..F24
    if (vk >= VK_F1 && vk <= VK_F24) {
        static thread_local char buf[8];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "F%d", vk - VK_F1 + 1);
        return buf;
    }
    // 0-9
    if (vk >= '0' && vk <= '9') {
        static thread_local char buf[4];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%c", (char)vk);
        return buf;
    }
    // A-Z
    if (vk >= 'A' && vk <= 'Z') {
        static thread_local char buf[4];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%c", (char)vk);
        return buf;
    }

    static thread_local char hex[24];
    _snprintf_s(hex, sizeof(hex), _TRUNCATE, u8"按键 0x%02X", vk);
    return hex;
}

} // namespace keybind
