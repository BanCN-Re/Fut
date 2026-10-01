// D:\Work\Fut\cheat\src\input.cpp
#include "input.h"
#include "gui.h"
#include "log.h"

#include <windows.h>

namespace input {

static const int kHotkeyId = 0xF17;

// "please toggle" flag. Any detection path sets it; the render thread consumes
// it in Tick(), so the menu state is only ever mutated on one thread.
static volatile LONG g_togglePending = 0;

// diagnostics
static volatile LONG g_cWndProc = 0;
static volatile LONG g_cHotkey  = 0;
static volatile LONG g_cPoll    = 0;
static volatile LONG g_wndProcWorks = 0;
static volatile LONG g_hotkeyWorks  = 0;
static const char*   g_activePath = "none";
static bool          g_keyDown = false;

// ---------------------------------------------------------------------------
// DEBOUNCE -- MUST BE ATOMIC. THIS WAS THE MENU FLICKER.
//
// Three independent detection paths can all observe the same physical INSERT
// press:
//   path 1  WndProc on the game's message thread
//   path 2  RegisterHotKey on our own thread
//   path 3  GetAsyncKeyState poll on the render thread
//
// g_lastToggle used to be a plain ULONGLONG read and written from all three. The
// check-then-write is not atomic, so two threads could both pass the
// "now - g_lastToggle < 250" test and both queue a toggle. One press then toggled
// the menu OPEN and immediately CLOSED again.
//
// Observed in the log as the click-through state flapping several times a second:
//     click-through=0 ... click-through=1 ... click-through=0
// With the menu closed half the time, clicks were swallowed -- which is why the
// user could not interact with anything.
//
// InterlockedCompareExchange makes the check-and-claim a single operation, so
// exactly one path wins each press.
// ---------------------------------------------------------------------------
static volatile LONG g_lastToggleTick = 0;

static void RequestToggle(const char* path) {
    const LONG now = (LONG)GetTickCount64();

    for (;;) {
        const LONG prev = InterlockedCompareExchange(&g_lastToggleTick, 0, 0);
        // 250 ms debounce; unsigned wrap is handled by comparing the difference
        if ((ULONG)(now - prev) < 250)
            return;
        if (InterlockedCompareExchange(&g_lastToggleTick, now, prev) == prev)
            break;                      // we claimed this press
    }

    g_activePath = path;
    InterlockedExchange(&g_togglePending, 1);
}

// ---------------------------------------------------------------------------
// path 1: the game window's message queue
// ---------------------------------------------------------------------------
bool OnWndProcMessage(void* hwnd, unsigned int msg, uintptr_t wparam, intptr_t lparam) {
    (void)hwnd;
    if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN &&
        msg != WM_KEYUP   && msg != WM_SYSKEYUP)
        return false;

    const int vk = static_cast<int>(wparam);
    if (vk != gui::g_menuKey)
        return false;

    const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
    if (down) {
        // bit 30 of lParam = previous key state; set means auto-repeat
        const bool wasDown = (lparam & (1LL << 30)) != 0;
        InterlockedExchange(&g_wndProcWorks, 1);
        InterlockedIncrement(&g_cWndProc);
        if (!wasDown)
            RequestToggle("WndProc");
    }
    return true;   // swallow: the game must not act on our menu key
}

// ---------------------------------------------------------------------------
// path 2: RegisterHotKey on our own thread (independent of the game entirely)
// ---------------------------------------------------------------------------
static DWORD WINAPI HotkeyThread(LPVOID) {
    if (!RegisterHotKey(nullptr, kHotkeyId, MOD_NOREPEAT, static_cast<UINT>(gui::g_menuKey))) {
        LOG("[input] RegisterHotKey FAILED err=%lu (path 2 disabled)", GetLastError());
        return 0;
    }
    InterlockedExchange(&g_hotkeyWorks, 1);
    LOG("[input] RegisterHotKey OK vk=0x%X", gui::g_menuKey);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_HOTKEY && static_cast<int>(msg.wParam) == kHotkeyId) {
            InterlockedIncrement(&g_cHotkey);
            RequestToggle("RegisterHotKey");
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// path 3: polling (only used when the two better paths are dead)
// ---------------------------------------------------------------------------
static void Poll() {
    if (InterlockedCompareExchange(&g_wndProcWorks, 0, 0))
        return;                                   // path 1 is alive
    if (InterlockedCompareExchange(&g_hotkeyWorks, 0, 0))
        return;                                   // path 2 is alive

    const bool down = (GetAsyncKeyState(gui::g_menuKey) & 0x8000) != 0;
    if (down && !g_keyDown) {
        InterlockedIncrement(&g_cPoll);
        RequestToggle("GetAsyncKeyState");
    }
    g_keyDown = down;
}

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
void Init() {
    LOG("[input] menu key vk=0x%X -- starting 3 detection paths", gui::g_menuKey);
    CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
}

void Tick() {
    Poll();
    if (InterlockedExchange(&g_togglePending, 0)) {
        gui::ToggleMenu();
        LOG("[input] toggle via %s -> menu %s", g_activePath,
            gui::g_open ? "OPEN" : "closed");
    }
}

const char* ActivePath() { return g_activePath; }
unsigned CountWndProc()  { return static_cast<unsigned>(g_cWndProc); }
unsigned CountHotkey()   { return static_cast<unsigned>(g_cHotkey); }
unsigned CountPoll()     { return static_cast<unsigned>(g_cPoll); }
bool     KeyDown()       { return (GetAsyncKeyState(gui::g_menuKey) & 0x8000) != 0; }

} // namespace input
