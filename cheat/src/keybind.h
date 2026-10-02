// D:\Work\Fut\cheat\src\keybind.h
#pragma once
//
// Runtime key binding.
//
// WHY THIS EXISTS
//   Aim needs its trigger bound at runtime: the user clicks "bind", presses a key,
//   and that key becomes the trigger. Anything the OS can report must be bindable
//   -- letters, digits, F-keys, numpad, and the mouse side buttons (X1/X2), which
//   are the ones people actually want for aim.
//
// THE THREE CLASSES OF KEY AND WHY THEY NEED DIFFERENT CODE
//
//   1. Keyboard keys
//        VK codes 0x08..0xFE. Read with GetAsyncKeyState(vk) and detected in the
//        WndProc via WM_KEYDOWN/WM_SYSKEYDOWN.
//
//   2. Mouse buttons reported as VK codes
//        VK_LBUTTON 0x01, VK_RBUTTON 0x02, VK_MBUTTON 0x04.
//        GetAsyncKeyState works, but the game may capture the cursor, so the
//        WndProc (WM_LBUTTONDOWN etc.) is the more reliable detector.
//
//   3. Mouse side buttons (X1 / X2)  <-- THE ONES THAT NEED SPECIAL HANDLING
//        Windows does NOT expose these as VK codes. They arrive ONLY as
//        WM_XBUTTONDOWN / WM_XBUTTONUP with the button in the HIGH WORD of
//        wParam (XBUTTON1 = 1, XBUTTON2 = 2). GetAsyncKeyState cannot see them at
//        all, and VK_XBUTTON1/2 only work through the raw-input path, which the
//        game owns.
//
//        So they are stored in a separate namespace (kMouseX1 / kMouseX2) and
//        tested through a flag set by the WndProc. That is the only way they can
//        be used as an aim trigger.
#include <cstdint>

namespace keybind {

// ---------------------------------------------------------------------------
// Bindable-key namespace
//
// Real VK codes are used directly (0x01..0xFE). The mouse side buttons do not
// have usable VK codes, so two values above the VK range are reserved for them.
// Nothing else uses those values, and VkToName/IsDown both know about them.
// ---------------------------------------------------------------------------
constexpr int kNone  = 0;
constexpr int kMouseX1 = 0x100;   // "back" side button
constexpr int kMouseX2 = 0x101;   // "forward" side button

// Human-readable name for a bound key ("无", "鼠标左键", "X1 (侧键后退)", "F5", "A").
const char* Name(int vk);

// True while the bound key is physically down.
//
// Keyboard and the three standard mouse buttons go through GetAsyncKeyState.
// X1/X2 consult the flag maintained by OnWndProcMessage, because no polling API
// can see them.
bool IsDown(int vk);

// Feed every WM_ message here. Records X1/X2 state and, when `capture` is on,
// reports the first key or button the user presses.
//
// Returns the newly pressed key (0 when nothing was pressed this message).
int OnWndProcMessage(unsigned int msg, uintptr_t wparam, intptr_t lparam);

// ---------------------------------------------------------------------------
// interactive capture ("click Bind, press a key")
// ---------------------------------------------------------------------------
// While capturing, OnWndProcMessage reports keys instead of ignoring them, and
// the UI shows "press a key...".
void BeginCapture();
void CancelCapture();
bool Capturing();

// The value captured since BeginCapture(), or kNone.
int  CapturedValue();

// Set once per capture when a key arrives, so the UI can confirm and close.
bool ConsumeCaptured(int& outVk);

} // namespace keybind
