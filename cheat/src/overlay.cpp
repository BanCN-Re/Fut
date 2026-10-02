// D:\Work\Fut\cheat\src\overlay.cpp
// Self-owned transparent topmost overlay window + its own D3D11 device.
//
// Technique (proven in the old project's esp_v4 overlay_window.cpp):
//   * WS_EX_TOPMOST | WS_EX_LAYERED  -> always on top, colour-keyed
//   * SetLayeredWindowAttributes(magenta, LWA_COLORKEY) -> magenta pixels vanish
//   * DwmExtendFrameIntoClientArea(-1) -> let DWM compose it
//   * we clear to magenta so only ImGui pixels survive
//
// Click-through is toggled at runtime: normally WS_EX_TRANSPARENT so the game
// keeps receiving the mouse; when the menu is open we drop WS_EX_TRANSPARENT so
// the menu is clickable.
#include "overlay.h"
#include "gui.h"
#include "capture_guard.h"
#include "esp.h"
#include "aim.h"
#include "skill.h"
#include "input.h"
#include "ue.h"
#include "log.h"
#include "obf.h"
#include "font.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// file-scope flag defined in canvas.cpp. Declared HERE, outside namespace
// overlay: an `extern` inside the namespace would declare overlay::g_canvasActive
// and fail to link against the global definition.
extern bool g_canvasActive;
// ---------------------------------------------------------------------------
// Defined at FILE scope on purpose.
//
// capture_guard reads it and esp.cpp reads it, and both are outside this
// namespace. An in-namespace definition would resolve to overlay::g_captureProtect
// while the extern declarations in esp.h refer to ::g_captureProtect, producing
// LNK2001 on every user -- the same mistake that broke RenderInGame earlier.
// ---------------------------------------------------------------------------
bool g_captureProtect = false;

namespace overlay {

static const wchar_t* kClass = L"FutOverlayCls";
static const COLORREF kColorKey = RGB(255, 0, 255);

// ---------------------------------------------------------------------------
// CAPTURE PROTECTION
//
// When on, the overlay is excluded from screen capture (OBS, screenshots) via
// WDA_EXCLUDEFROMCAPTURE, and -- critically -- the ESP must be drawn HERE rather
// than inside the game's frame.
//
// WHY THE ESP LOCATION IS TIED TO THIS FLAG:
//   WDA applies per WINDOW. Anything drawn through UCanvas lands in the game's
//   own window and is therefore captured no matter what we do to ours. To make
//   the ESP invisible to recording, it has to live on the window we protect.
//
//   So the two modes are mutually exclusive by design:
//     protected OFF -> ESP in-game (zero latency, but any recording shows it)
//     protected ON  -> ESP on this overlay (excluded from capture, one compositor
//                      tick of latency)
//
//   The flag is defined here and read by dllmain/canvas so a single setting
//   controls both.
// ---------------------------------------------------------------------------


static HWND                     s_hwnd = nullptr;
static ID3D11Device*            s_device = nullptr;
static ID3D11DeviceContext*     s_context = nullptr;
static IDXGISwapChain*          s_swap = nullptr;
static ID3D11RenderTargetView*  s_rtv = nullptr;
static bool                     s_init = false;
static bool                     s_ready = false;
// Set when Present can no longer be called safely (device removed, reset, or an
// exception was caught inside it). See the crash-fix comment in RenderOnce: the
// overlay stops drawing rather than risking a process-killing fault.
static bool                     s_presentDead = false;
static uint64_t                 s_frames = 0;
static bool                     s_clickThrough = true;
static bool                     s_tearing = false;   // flip-model + ALLOW_TEARING
static HWND                     s_gameHwnd = nullptr;

// Overlay frame budget.
//
// WHY THIS IS NOT 60 Hz ANYMORE
//   The game runs at 239 FPS. An overlay capped at 60 Hz therefore refreshes
//   every ~16.7 ms while the game draws every ~4.2 ms, so a box can be up to
//   FOUR game frames stale -- that is the "完全不跟手" the user reported. The
//   boxes were correct, just late.
//
// ---------------------------------------------------------------------------
// FRAME PACING -- 60 Hz. DO NOT REMOVE THIS CAP.
//
// History, because this has now been got wrong twice:
//
//   v1: uncapped (~170 Hz)  -> game crashed in dxgi!Present+0xdad0
//   v2: 60 Hz cap           -> stable
//   v3: 2 ms cap (~500 Hz)  -> game crashed in dxgi!Present+0xdad0 AGAIN
//
// The v3 change was made on the theory that the crash came from our own Present
// hook rather than the frame rate. That theory is now DISPROVEN: the hook is not
// installed (d3d::Init is never called), yet the crash returned the moment the
// cap was relaxed, at the identical address:
//
//     Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x218b9ad6
//     0x00007ff99842dad0 dxgi.dll!UnknownFunction      <- base+0xdad0
//     [gfx] dxgi.dll own base=0x7FF998420000
//
// Mechanism: our overlay owns a second D3D11 swap chain presenting to DWM while
// the game drives its own flip-model chain. Presenting far faster than the
// display refresh makes the two compete for the same GPU queue and DWM's
// composition of the game's chain, and the game's render thread faults inside
// dxgi. Rate-limiting the overlay removes the contention entirely.
//
// The overlay only has to be smooth, not fast. 60 Hz matches the display and
// leaves the GPU to the game.
// ---------------------------------------------------------------------------
static const ULONGLONG kFrameMs = 16;   // 60 Hz -- see the note above

// The overlay must never be larger than the real monitor. A stale/odd game
// client rect (e.g. 2560x1600 on a 2048x1280 display) makes a layered topmost
// window cover the whole screen with mismatched geometry, which is another way
// to disturb DWM composition of the game's flip-model swap chain.
static void ClampToMonitor(int& x, int& y, int& w, int& h) {
    POINT pt{ x, y };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(mon, &mi)) return;

    const RECT& m = mi.rcMonitor;
    if (x < m.left) x = m.left;
    if (y < m.top)  y = m.top;
    if (w > m.right - m.left) w = m.right - m.left;
    if (h > m.bottom - m.top) h = m.bottom - m.top;
    if (x + w > m.right) x = m.right - w;
    if (y + h > m.bottom) y = m.bottom - h;
}

bool Ready() { return s_ready; }
uint64_t Frames() { return s_frames; }

// ---------------------------------------------------------------------------
// device -- DISCARD swap chain, R8G8B8A8_UNORM, DWM glass.
//
// A FLIP_DISCARD chain with DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING was tried here to
// escape the refresh-rate throttle. That was wrong on two counts:
//
//   * tearing is a fullscreen-exclusive optimisation; combining it with a
//     DWM-composited topmost overlay produced the dxgi Present crash
//     (dxgi.dll+0xdad0, EXCEPTION_ACCESS_VIOLATION reading 0x218b9ad6)
//   * it bought nothing, because the overlay's rate is governed by kFrameMs in
//     the render loop, not by Present
//
// DISCARD + R8G8B8A8_UNORM + DwmExtendFrameIntoClientArea is the combination that
// composites correctly with per-pixel alpha: the renderer clears to (0,0,0,0) and
// DWM drops those pixels.
// ---------------------------------------------------------------------------
static bool CreateDevice() {
    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL lvl{};

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = s_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = 0;
    s_tearing = false;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2, D3D11_SDK_VERSION,
        &sd, &s_swap, &s_device, &lvl, &s_context);

    if (FAILED(hr) || !s_swap) {
        LOG("[overlay] D3D11CreateDeviceAndSwapChain failed hr=0x%08X", hr);
        return false;
    }
    LOG("[overlay] DISCARD swap chain created (DWM glass, per-pixel alpha)");
    // the driver tells us via CheckColorSpaceSupport / the factory. We simply try
    // it at Present time and fall back if the call is rejected.

    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(s_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back) {
        s_device->CreateRenderTargetView(back, nullptr, &s_rtv);
        back->Release();
    }
    return s_rtv != nullptr;
}

static void ReleaseRTV() {
    if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
}

// ---------------------------------------------------------------------------
// window proc: only for our own window, so ImGui gets its input
// ---------------------------------------------------------------------------
static LRESULT CALLBACK OverlayWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // -----------------------------------------------------------------------
    // CLICK-THROUGH -- VIA WM_NCHITTEST, NOT WS_EX_TRANSPARENT.
    //
    // THIS IS THE FIX FOR "CANNOT CLICK ANYTHING UNDERNEATH".
    //
    // WS_EX_TRANSPARENT does NOT make a window click-through. It only affects
    // painting order (the window is skipped when the area below is repainted).
    // Mouse input still goes to it.
    //
    // The old build appeared to work because it also had WS_EX_LAYERED, and a
    // layered window whose pixels are fully transparent is skipped by hit-testing.
    // When WS_EX_LAYERED was removed to fix the black screen, that side effect went
    // with it and the overlay started swallowing every click.
    //
    // The explicit, style-independent way is to answer WM_NCHITTEST with
    // HTTRANSPARENT: the system then routes the message to the window below us.
    // That works with or without WS_EX_LAYERED, so the two fixes no longer
    // conflict.
    //
    // When the menu is open we return HTCLIENT instead so ImGui receives the
    // mouse normally.
    // -----------------------------------------------------------------------
    // NOTE: WM_NCHITTEST / HTTRANSPARENT is deliberately NOT used. A topmost
    // window receives the mouse before the window beneath it, so returning
    // HTTRANSPARENT cannot hand input to the game. Click-through comes from the
    // colour key instead (see SetOpaque).

    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l))
        return TRUE;
    switch (m) {
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default: break;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
// GAME-WINDOW WNDPROC DETOUR
//
// WHY THIS IS NEEDED AT ALL
//
// The overlay window is click-through (colour key + WS_EX_TRANSPARENT) and is
// usually not even the focused window, so it never sees the input the user cares
// about. The GAME window does. Two features depend on reading that message
// stream:
//
//   1. the menu key, so INSERT works while playing (input::OnWndProcMessage)
//   2. KEY BINDING for the mouse side buttons X1/X2
//
// (2) is the reason this detour was added now: Windows delivers X1/X2 ONLY as
// WM_XBUTTONDOWN / WM_XBUTTONUP with the index in the HIGH word of wParam. There
// is no VK code for them, GetAsyncKeyState cannot see them, and raw input belongs
// to the game. A WndProc detour is therefore the only way to bind them.
//
// This is a SetWindowLongPtr subclass, NOT a code patch: no game .text is touched,
// so it cannot trip a byte-integrity check, and it is removed on shutdown.
// ---------------------------------------------------------------------------
static WNDPROC s_origGameWndProc = nullptr;
static HWND    s_subclassedHwnd  = nullptr;

static LRESULT CALLBACK GameWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // Hand every message to the input layer. It returns true only when the
    // message was ours to consume (the menu key, or a key being captured for a
    // binding) -- everything else falls through to the game untouched, which
    // matters: eating the fire button would be fatal.
    __try {
        if (input::OnWndProcMessage(h, m, w, l))
            return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // never let our handler take the game's window procedure down
    }

    if (s_origGameWndProc)
        return CallWindowProcA(s_origGameWndProc, h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

// Install on the game window. Safe to call repeatedly; does nothing once
// installed on the current window.
static void InstallGameWndProcHook(HWND game) {
    if (!game || !IsWindow(game)) return;
    if (s_subclassedHwnd == game) return;

    // Restore any previous target first (the game can recreate its window on a
    // resolution change).
    if (s_subclassedHwnd && s_origGameWndProc) {
        SetWindowLongPtrA(s_subclassedHwnd, GWLP_WNDPROC, (LONG_PTR)s_origGameWndProc);
        s_origGameWndProc = nullptr;
        s_subclassedHwnd = nullptr;
    }

    SetLastError(0);
    const LONG_PTR prev = SetWindowLongPtrA(game, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
    if (prev == 0 && GetLastError() != 0) {
        LOG("[overlay] game WndProc subclass FAILED err=%lu", GetLastError());
        return;
    }
    s_origGameWndProc = reinterpret_cast<WNDPROC>(prev);
    s_subclassedHwnd  = game;
    LOG("[overlay] game WndProc subclassed (menu key + X1/X2 binding live)");
}

// ---------------------------------------------------------------------------
// size / position to match the game window
// ---------------------------------------------------------------------------
void SyncToGameWindow() {
    if (!s_hwnd || !s_gameHwnd) return;
    if (!IsWindow(s_gameHwnd) || IsIconic(s_gameHwnd)) return;

    RECT cr{};
    if (!GetClientRect(s_gameHwnd, &cr)) return;
    POINT tl{ cr.left, cr.top };
    ClientToScreen(s_gameHwnd, &tl);

    const int w0 = cr.right - cr.left;
    const int h0 = cr.bottom - cr.top;
    if (w0 <= 0 || h0 <= 0) return;

    int x = tl.x, y = tl.y, w = w0, h = h0;
    ClampToMonitor(x, y, w, h);
    if (w <= 0 || h <= 0) return;

    RECT cur{};
    GetWindowRect(s_hwnd, &cur);
    const int cw = cur.right - cur.left;
    const int ch = cur.bottom - cur.top;
    // Tolerance: a 1-2 px difference must not trigger a ResizeBuffers every
    // frame. Repeated buffer recreation on a live swap chain is expensive and
    // was a contributor to the stutter.
    if (cur.left == x && cur.top == y &&
        (cw > w ? cw - w : w - cw) <= 2 && (ch > h ? ch - h : h - ch) <= 2) {
        return;
    }

    SetWindowPos(s_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // Recreate our OWN swap chain buffers at the new size.
    //
    // IMPORTANT: only OUR swap chain is resized here. An earlier build also
    // hooked and called ResizeBuffers on the GAME's swap chain, which is what
    // crashed the game inside dxgi.dll (0xC0000005, faulting module dxgi.dll)
    // and produced the progressively worsening stutter. That hook is gone.
    if (s_swap && s_device) {
        ReleaseRTV();
        if (SUCCEEDED(s_swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, s_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0))) {
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(s_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back) {
                s_device->CreateRenderTargetView(back, nullptr, &s_rtv);
                back->Release();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// click-through toggle
//
// WHAT ACTUALLY MAKES THIS WORK -- AND WHY MY LAST "FIX" WAS WRONG
//
// I previously reported click-through as fixed because WindowFromPoint() returned
// the game's pid. That was a bad test: WindowFromPoint does not consult the
// hit-test the input system uses, so it proved nothing. The user could not click
// through, which is the only measurement that counts.
//
// The real mechanism, for a TOPMOST overlay over a game:
//
//   LWA_COLORKEY. A layered window with a colour key is transparent for BOTH
//   painting AND hit-testing: pixels matching the key are skipped by the mouse
//   as well as by the compositor. That is the only configuration that gives
//   see-through + click-through at the same time.
//
//   DWM glass (DwmExtendFrameIntoClientArea) does NOT do this. USER32 has no
//   knowledge of the alpha channel a D3D swap chain writes, so the whole window
//   stays solid to the mouse no matter how transparent it looks. That is exactly
//   the state the last build was left in: visually transparent, fully click-
//   blocking.
//
//   WS_EX_TRANSPARENT alone does nothing for input either -- it only affects
//   repaint order. WM_NCHITTEST/HTTRANSPARENT cannot help a topmost window,
//   because the mouse is delivered to it before the window below.
//
// So the colour key is restored, and this function only toggles the style bits;
// the key itself is set in SetOpaque().
// ---------------------------------------------------------------------------
static void UpdateClickThrough() {
    const bool want = !gui::g_open;      // menu open -> accept mouse
    static bool s_applied = false;
    if (s_applied && want == s_clickThrough) return;
    s_applied = true;

    LONG_PTR ex = GetWindowLongPtrW(s_hwnd, GWL_EXSTYLE);
    if (want) ex |= WS_EX_TRANSPARENT | WS_EX_LAYERED;
    else      ex &= ~(WS_EX_TRANSPARENT);

    SetWindowLongPtrW(s_hwnd, GWL_EXSTYLE, ex);

    // Re-assert the colour key: changing GWL_EXSTYLE can drop the layered
    // attributes, and without the key the window becomes opaque to the mouse
    // again (the failure the user hit).
    if (want)
        SetLayeredWindowAttributes(s_hwnd, kColorKey, 0, LWA_COLORKEY);

    s_clickThrough = want;

    if (!want) {
        // make sure the OS cursor is usable while the menu is up
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
    LOG("[overlay] click-through=%d (colorkey %s)", (int)want, want ? "on" : "off");
}

// ---------------------------------------------------------------------------
// opacity mode
//
// LWA_COLORKEY is fine while we only draw ESP boxes (magenta = see-through),
// but it is NOT reliable for a solid menu: with DwmExtendFrameIntoClientArea
// the DWM composition can still let the game show through dark pixels.
//
// So when the menu is open we switch the layered window from COLOURKEY mode to
// plain ALPHA mode (alpha = 255, no key at all). The window is then genuinely
// opaque and the menu cannot bleed.
// ---------------------------------------------------------------------------
static bool s_opaque = false;

static void SetOpaque(bool opaque) {
    // -----------------------------------------------------------------------
    // THE WINDOW NEVER CHANGES MODE. THIS WAS THE FULL-SCREEN MAGENTA BUG.
    // -----------------------------------------------------------------------
    // WHAT WENT WRONG
    //
    // This function used to switch the window between two layered modes:
    //
    //     menu open   -> SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)
    //     menu closed -> SetLayeredWindowAttributes(hwnd, key, 0, LWA_COLORKEY)
    //
    // But the renderer ALWAYS clears to the magenta key (see RenderOnce), and
    // those two are incompatible:
    //
    //   * COLOURKEY mode -> the magenta clear is punched out. Correct.
    //   * ALPHA mode     -> there is no key, so the magenta clear is painted
    //                       literally. The entire screen turns magenta.
    //
    // The user opened the menu, closed it, and the overlay stayed on the ALPHA
    // path with a magenta backdrop: solid magenta, permanent, unclickable.
    //
    // WHY THE SWITCH WAS NEVER NEEDED
    //
    // It existed to make the menu opaque. The menu already does that itself:
    // gui.cpp paints a full-screen rectangle as its backdrop. The window does not
    // have to become opaque for the menu to be readable, so there is nothing to
    // switch.
    //
    // ONE MODE, ALWAYS:
    //     WS_EX_LAYERED + LWA_COLORKEY + magenta clear
    //   -> the key colour is invisible to the eye AND skipped by hit-testing
    //   -> the menu paints its own solid backdrop over it
    // -----------------------------------------------------------------------
    (void)opaque;      // kept for call-site compatibility; the mode is fixed

    // -----------------------------------------------------------------------
    // APPLY ONCE, NOT EVERY FRAME. THIS WAS THE "OPENING THE MENU LAGS" BUG.
    //
    // DwmExtendFrameIntoClientArea and SetLayeredWindowAttributes both round-trip
    // to the compositor. Calling them from the render loop meant two synchronous
    // DWM calls per frame, and the cost lands on the frame the menu opens or
    // closes -- reported as "open the GUI and the game stutters".
    //
    // Both settings are sticky: once applied they stay until something else
    // changes them, and the only thing that ever did was SetOpaque itself. So they
    // are applied once and the result is not re-asserted.
    //
    // The verbose comment above explains why there is no mode to toggle; this
    // guard is what makes that a saving rather than just a simplification.
    // -----------------------------------------------------------------------
    static bool s_applied = false;
    if (s_applied) return;
    s_applied = true;

    MARGINS all{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(s_hwnd, &all);
    SetLayeredWindowAttributes(s_hwnd, kColorKey, 0, LWA_COLORKEY);

    // If anything ever re-layers the window (a theme change, a DPI change), the
    // key would be lost and the screen would go magenta. That is rare enough to
    // ignore here; if it ever shows up, re-assert from the WndProc rather than
    // from the render loop.
    s_opaque = false;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
bool Init() {
    // find the game window (the one UE4 owns, class UnrealWindow)
    s_gameHwnd = nullptr;
    {
        struct Ctx { HWND best; DWORD pid; };
        Ctx ctx{ nullptr, GetCurrentProcessId() };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
            wchar_t cls[128]{};
            GetClassNameW(h, cls, 128);
            if (wcscmp(cls, L"UnrealWindow") == 0) { c->best = h; return FALSE; }
            RECT r{};
            GetClientRect(h, &r);
            if (!c->best && (r.right - r.left) > 320) c->best = h;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        s_gameHwnd = ctx.best;
    }
    if (!s_gameHwnd) {
        LOG("[overlay] game window NOT found -- falling back to primary monitor");
    } else {
        LOG("[overlay] game hwnd=0x%llX", (unsigned long long)(uintptr_t)s_gameHwnd);
    }

    // class
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) {
        DWORD e = GetLastError();
        if (e != ERROR_CLASS_ALREADY_EXISTS) {
            LOG("[overlay] RegisterClassExW failed err=%lu", e);
            return false;
        }
    }

    int x = 0, y = 0, w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    if (s_gameHwnd) {
        RECT cr{}; GetClientRect(s_gameHwnd, &cr);
        POINT tl{ 0, 0 }; ClientToScreen(s_gameHwnd, &tl);
        x = tl.x; y = tl.y; w = cr.right; h = cr.bottom;
    }
    // never exceed the real monitor: a game client rect larger than the display
    // would otherwise create an oversized layered topmost window
    ClampToMonitor(x, y, w, h);
    if (w <= 0 || h <= 0) {
        x = 0; y = 0;
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }

    s_hwnd = CreateWindowExW(
        // -------------------------------------------------------------------
        // NO WS_EX_LAYERED. THIS IS THE "BLACK SCREEN" FIX.
        //
        // WS_EX_LAYERED only produces transparency when the window's content is
        // supplied through one of the layered-window APIs:
        //     SetLayeredWindowAttributes()   (uniform alpha or a colour key)
        //     UpdateLayeredWindow()          (a DIB the app owns)
        //
        // A layered window with NEITHER is undefined: Windows composites whatever
        // is in the window's surface, which for a D3D swap chain is opaque -- the
        // observed solid black sheet.
        //
        // The per-pixel-alpha recipe for a D3D overlay does not use WS_EX_LAYERED
        // at all. Transparency comes from DWM glass:
        //     DwmExtendFrameIntoClientArea(hwnd, MARGINS{-1,-1,-1,-1})
        //     + swap chain in R8G8B8A8_UNORM
        //     + clear to (0,0,0,0)
        // DWM then treats the whole client area as glass and drops the alpha-0
        // pixels. That is the configuration below.
        //
        // Bonus: WDA_EXCLUDEFROMCAPTURE is also unreliable on layered windows, so
        // removing the style makes capture protection work as intended.
        // -------------------------------------------------------------------
        // -------------------------------------------------------------------
        // WS_EX_LAYERED IS REQUIRED. THIS IS THE CLICK-THROUGH FIX.
        //
        // Both requirements are satisfied by ONE configuration:
        //
        //   WS_EX_LAYERED  +  DwmExtendFrameIntoClientArea(-1)
        //                  +  NO SetLayeredWindowAttributes call
        //                  +  R8G8B8A8_UNORM swap chain cleared to (0,0,0,0)
        //
        //   * transparency: DWM drops the alpha-0 pixels (per-pixel alpha)
        //   * click-through: a layered window whose pixel is fully transparent is
        //     SKIPPED BY HIT-TESTING, so the click falls through to the game
        //
        // Why the previous attempts each failed:
        //   - with SetLayeredWindowAttributes(...LWA_ALPHA) the window was made
        //     UNIFORMLY opaque -> solid black sheet (the black-screen bug)
        //   - removing WS_EX_LAYERED fixed the black screen but destroyed the
        //     hit-test skip -> the overlay swallowed every click
        //   - WM_NCHITTEST/HTTRANSPARENT cannot substitute, because a TOPMOST
        //     window receives the mouse before the window beneath it ever sees it
        //
        // So: keep the style, keep DWM glass, and never call the layered-window
        // attribute API.
        // -------------------------------------------------------------------
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        kClass, L"FUT Overlay", WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!s_hwnd) {
        LOG("[overlay] CreateWindowExW failed err=%lu", GetLastError());
        return false;
    }

    // Colour-key transparency: magenta pixels vanish for the compositor and are
    // skipped by hit-testing (see SetOpaque for why this and not per-pixel alpha).
    SetLayeredWindowAttributes(s_hwnd, kColorKey, 0, LWA_COLORKEY);
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(s_hwnd, &m);

    // Apply capture protection immediately when enabled. Doing it here -- not     // only from the menu -- means a restart keeps the window hidden from     // recorders without the user re-toggling anything.     if (::g_captureProtect)         capture_guard::Enable(s_hwnd);

    if (!CreateDevice()) return false;

    ShowWindow(s_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(s_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // UI1001 palette
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 10.0f; st.ChildRounding = 8.0f; st.FrameRounding = 3.0f;
    st.GrabRounding = 3.0f; st.PopupRounding = 12.0f; st.ScrollbarRounding = 0.0f;
    st.ScrollbarSize = 7.0f; st.GrabMinSize = 5.0f;
    st.WindowPadding = ImVec2(8, 8); st.FramePadding = ImVec2(6, 4);
    st.ItemSpacing = ImVec2(6, 6); st.WindowBorderSize = 0.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]           = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    c[ImGuiCol_WindowBg]       = ImVec4(18/255.f, 20/255.f, 23/255.f, 0.96f);
    c[ImGuiCol_Border]         = ImVec4(0.20f, 0.21f, 0.23f, 0.50f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    c[ImGuiCol_FrameBgActive]  = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_TitleBg]        = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.12f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_Button]         = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.28f, 0.29f, 0.31f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.35f, 0.36f, 0.38f, 1.00f);
    c[ImGuiCol_Header]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    c[ImGuiCol_HeaderActive]   = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    c[ImGuiCol_PopupBg]        = ImVec4(0.11f, 0.11f, 0.11f, 0.94f);
    c[ImGuiCol_ScrollbarBg]    = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Separator]      = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);

    // -----------------------------------------------------------------------
    // FONT MUST BE LOADED BEFORE THE BACKEND INIT.
    //
    // ImGui_ImplDX11_Init() immediately calls CreateDeviceObjects(), which calls
    // io.Fonts->GetTexDataAsRGBA32() -- that BUILDS the atlas and uploads it as a
    // GPU texture. Any font added afterwards is in the atlas data but never
    // reaches the texture, so its glyphs render as '?' (or as the default font's
    // missing-glyph box).
    //
    // That was the real cause of the "some characters show, some are ???" bug:
    // the font was being added one line AFTER the backend created the texture.
    // -----------------------------------------------------------------------
    if (font::LoadChineseFont(ImGui::GetIO(), 20.0f))
        LOG("[overlay] chinese font loaded (pre-backend)");
    else
        LOG("[overlay] WARN: chinese font missing, default font will be used");

    ImGui_ImplWin32_Init(s_hwnd);
    ImGui_ImplDX11_Init(s_device, s_context);

    s_init = true;
    LOG("[overlay] ready (hwnd=0x%llX device=%p size=%dx%d)",
        (unsigned long long)(uintptr_t)s_hwnd, s_device, w, h);
    return true;
}

// ---------------------------------------------------------------------------
// DEVICE / SWAP CHAIN RECOVERY
//
// ===========================================================================
// THIS IS THE "UI DIES AFTER A WHILE AND NEVER COMES BACK" FIX
// ===========================================================================
//
// The old behaviour on any Present failure was:
//
//     LOG("exception in Present -- stopping present loop");
//     s_presentDead = true;      // and that was the end of it
//
// s_presentDead was never cleared, so the overlay thread kept looping while
// RenderOnce() returned immediately, forever. Everything living in this window
// then stopped working permanently:
//
//   * the ImGui menu (could not be opened any more)
//   * the always-on status strip
//   * the ESP, whenever capture protection routes it here
//
// The game kept running because the in-game UCanvas path is a separate hook --
// which is exactly the reported symptom: "the game is fine but the UI is dead".
// Seen in the trace as:
//
//     22:09:15.803  [overlay] exception in Present -- stopping present loop
//     ... and not one [overlay] frame line ever again
//
// A failed Present is usually RECOVERABLE: DXGI_ERROR_DEVICE_REMOVED after a
// driver hiccup, a display mode switch, or a lost device. The right response is
// to tear the D3D objects down and build them again, not to give up.
//
// So a failure now marks the device dirty and retries a rebuild on a timer. The
// ImGui DX11 backend is shut down and re-initialised against the new device,
// because its font texture / shaders / buffers belong to the old one.
//
// A capped attempt count stops a genuine hard failure from becoming a spin loop.
// ---------------------------------------------------------------------------
static ULONGLONG s_rebuildAfter = 0;      // 0 = no rebuild pending
static int       s_rebuildAttempts = 0;
static const int kMaxRebuildAttempts = 10;
static const ULONGLONG kRebuildDelayMs = 2000;

static void ScheduleRebuild(const char* why) {
    if (s_rebuildAfter != 0) return;                 // already scheduled
    if (s_rebuildAttempts >= kMaxRebuildAttempts) {
        LOG("[overlay] rebuild attempts exhausted (%d); overlay disabled", s_rebuildAttempts);
        s_presentDead = true;
        return;
    }
    ++s_rebuildAttempts;
    s_rebuildAfter = GetTickCount64() + kRebuildDelayMs;
    LOG("[overlay] %s -- rebuild #%d scheduled", why, s_rebuildAttempts);
}

// Tear down the D3D objects and build fresh ones. True on success.
static bool RebuildDevice() {
    LOG("[overlay] rebuilding D3D11 device + swap chain");

    // ImGui's DX11 backend holds references to the old device; drop them first.
    // The Win32 backend is device independent and stays up.
    if (s_init) ImGui_ImplDX11_Shutdown();

    ReleaseRTV();

    if (s_swap)    { s_swap->Release();    s_swap = nullptr; }
    if (s_context) { s_context->Release(); s_context = nullptr; }
    if (s_device)  { s_device->Release();  s_device = nullptr; }

    if (!CreateDevice()) {
        LOG("[overlay] rebuild failed at CreateDevice");
        return false;
    }

    if (s_init && !ImGui_ImplDX11_Init(s_device, s_context)) {
        LOG("[overlay] rebuild failed at ImGui_ImplDX11_Init");
        return false;
    }

    s_presentDead = false;
    s_frames = 0;
    LOG("[overlay] rebuild OK -- overlay live again");
    return true;
}

// ---------------------------------------------------------------------------
// render loop (its own thread -- independent of the game entirely)
// ---------------------------------------------------------------------------
static void RenderOnce() {
    // -----------------------------------------------------------------------
    // RECOVERY FIRST, BEFORE THE DEAD CHECK.
    //
    // The old code returned unconditionally once s_presentDead was set, so a
    // single Present failure permanently killed the menu and the status strip.
    // The dead state is now a RETRY state: after a short delay the device and
    // swap chain are rebuilt and drawing resumes.
    // -----------------------------------------------------------------------
    if (s_rebuildAfter != 0 && GetTickCount64() >= s_rebuildAfter) {
        s_rebuildAfter = 0;
        if (RebuildDevice())
            s_rebuildAttempts = 0;          // healthy again: reset the budget
        else
            ScheduleRebuild("rebuild failed");
    }

    if (s_presentDead) return;

    SyncToGameWindow();
    UpdateClickThrough();

    // opaque only while the Menu is actually being drawn
    SetOpaque(gui::g_open && gui::g_opaqueMenu);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    // our own message pump feeds ImGui, but drive the cursor from the OS too
    POINT p{};
    if (GetCursorPos(&p)) {
        ScreenToClient(s_hwnd, &p);
        ImGui::GetIO().MousePos = ImVec2((float)p.x, (float)p.y);
    }

    input::Tick();
    gui::PollEscape();
    ImGui::NewFrame();

    __try {
        // ---- ESP ----
        //
        // WHERE THE ESP IS DRAWN IS DECIDED BY CAPTURE PROTECTION, and the two
        // options are mutually exclusive:
        //
        //  * protection OFF -> drawn in-game (esp::RenderInGame, from the
        //    PostRender hook). Zero compositor latency, but those boxes are part
        //    of the game frame and therefore appear in every recording.
        //
        //  * protection ON  -> drawn HERE, on this overlay window, which carries
        //    WDA_EXCLUDEFROMCAPTURE. WDA works per window, so this is the only
        //    way the boxes can be invisible to OBS/screenshots. The cost is one
        //    compositor tick of latency.
        //
        // Drawing in both places would double-draw and put a laggy copy on top of
        // the low-latency one, so exactly one of the two paths runs.
        if (!g_canvasActive || ::g_captureProtect)
            esp::Render();    // collects entities; publishes them for aim/skill
        aim::Tick();          // reads the published list, may write ControlRotation
        skill::Tick();        // lock maintenance + optional auto fire
        skill::RenderOverlay();  // lock markers (foreground draw list)
        gui::Render();
        gui::RenderDebugStrip();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG("[overlay] exception in render path (swallowed)");
    }

    ImGui::Render();

    // ---------------------------------------------------------------------
    // Backdrop.
    //
    // The window is layered with a MAGENTA colour key, so magenta pixels are
    // punched out and the game shows through. That is what we want while just
    // drawing ESP boxes.
    //
    // When the MENU is open we instead clear to opaque BLACK, so the game is
    // fully covered and the menu is readable (no bleed-through).
    // ---------------------------------------------------------------------
    if (false) {
        // (the opaque-backdrop path now lives in gui.cpp, which paints a
        //  full-screen rectangle, so the clear is ALWAYS transparent)
        const float black[4] = { 0.043f, 0.047f, 0.055f, 1.0f };
        s_context->OMSetRenderTargets(1, &s_rtv, nullptr);
        s_context->ClearRenderTargetView(s_rtv, black);
    } else if (::g_captureProtect) {
        // ---- PROTECTED MODE ----
        //
        // Clear to FULLY TRANSPARENT (alpha 0) instead of magenta.
        //
        // In protected mode capture_guard switched the window to per-pixel alpha,
        // because WDA_EXCLUDEFROMCAPTURE is unreliable on a colour-keyed layered
        // window. Under per-pixel alpha the magenta key is meaningless -- it would
        // paint an opaque magenta sheet -- and alpha 0 is what makes those pixels
        // vanish.
        //
        // NOTE: this branch previously contained COMMENTS ONLY and never cleared
        // the render target at all, so the previous frame stayed on screen and the
        // picture smeared/garbled as soon as capture protection was enabled.
        const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        s_context->OMSetRenderTargets(1, &s_rtv, nullptr);
        s_context->ClearRenderTargetView(s_rtv, transparent);
    } else {
        // ALWAYS clear to fully transparent. The window uses per-pixel alpha,
        // so alpha 0 is what makes a pixel vanish.
        // The magenta colour key: these pixels are removed by the compositor AND
        // skipped by hit-testing, which is what gives click-through. Clearing to
        // alpha 0 instead (per-pixel alpha) looks the same but blocks the mouse,
        // because USER32 cannot read a D3D swap chain's alpha channel.
        const float key[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        s_context->OMSetRenderTargets(1, &s_rtv, nullptr);
        s_context->ClearRenderTargetView(s_rtv, key);
    }

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // ---------------------------------------------------------------------
    // CRASH FIX (2026-10-01).
    //
    // This Present runs on OUR thread, not the game's render thread. Crash dumps
    // showed a consistent fault at dxgi!CDXGISwapChain::Present+0xdad0 reached
    // from zw_core (kernel32!BaseThreadInitThunk -> zw_core -> dxgi), always after
    // 3-5 minutes of otherwise healthy frames. That is the signature of an
    // independent swap chain being presented while the device is in a transient
    // state -- typically device-removed/reset during a mode change or alt-tab.
    //
    // A device-removed condition is recoverable (we can rebuild), but a crash is
    // not, so we take three precautions:
    //
    //   1. Check the device BEFORE presenting. GetDeviceRemovedReason is the
    //      documented way to learn the device is gone; calling Present on a
    //      removed device is what faults.
    //   2. Wrap the Present in SEH. Even a correct check can race with a reset on
    //      another thread, so the call must not be able to take the process down.
    //   3. On failure, stop presenting and mark the overlay dead rather than
    //      retrying in a loop. A dead overlay is recoverable by a restart; a
    //      crash-looping Present is not.
    //
    // We do NOT attempt to recreate the swap chain here: doing so from a
    // non-render thread while the game owns the device risks corrupting the
    // game's own state, which is worse than a disabled overlay.
    // ---------------------------------------------------------------------
    if (s_device) {
        HRESULT reason = s_device->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            LOG("[overlay] device removed (0x%08lX) -- scheduling rebuild",
                (unsigned long)reason);
            ScheduleRebuild("device removed");
            return;
        }
    }

    __try {
        HRESULT hr = s_swap->Present(0, s_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
        if (FAILED(hr)) {
            // DXGI_STATUS_OCCLUDED and friends are benign; only a hard failure
            // stops the loop.
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
                hr == DXGI_ERROR_INVALID_CALL) {
                LOG("[overlay] Present failed (0x%08lX) -- scheduling rebuild",
                    (unsigned long)hr);
                ScheduleRebuild("Present failed");
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // A fault inside Present is the case the user actually hit: the overlay
        // died at 22:09:15 and the menu never came back. Recover instead of
        // giving up -- see ScheduleRebuild().
        LOG("[overlay] exception in Present -- scheduling rebuild");
        ScheduleRebuild("Present exception");
        return;
    }

    s_ready = true;
    ++s_frames;
    if (s_frames == 1 || (s_frames % 300) == 0) {
        LOG("[overlay] frame %llu  menu=%s  obj=%d  tgt=%d  path=%s",
            (unsigned long long)s_frames, gui::g_open ? "OPEN" : "closed",
            ue::NumObjects(), esp::LastTargetCount(), input::ActivePath());
    }
}

DWORD WINAPI ThreadProc(LPVOID) {
    if (!Init()) { LOG("[overlay] init FAILED"); return 1; }

    MSG msg{};
    ULONGLONG last = GetTickCount64();

    for (;;) {
        while (PeekMessageW(&msg, s_hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { Shutdown(); return 0; }
        }

        // -------------------------------------------------------------------
        // FRAME PACING -- this is the fix for the stutter, the growing lag and
        // the rendering-thread crashes.
        //
        // A previous revision removed the frame cap. That made the overlay
        // present ~170 times a second on its OWN D3D11 device while the game
        // drives D3D12 on the SAME GPU. Two swap chains competing for one
        // device queue is what produced:
        //   * the stutter ("一卡一卡")
        //   * the lag that got worse over time ("越来越卡")
        //   * the rendering-thread EXCEPTION_ACCESS_VIOLATION in the game
        //     (identical game-side stack every time -- a deterministic path)
        //
        // An overlay needs to be smooth, not fast. 60 Hz matches the display
        // and leaves the GPU to the game.
        // -------------------------------------------------------------------
        // -------------------------------------------------------------------
        // WHEN THE IN-GAME CANVAS IS LIVE, THIS WINDOW MUST BE INVISIBLE.
        //
        // Not merely "skip drawing": the window is layered + topmost with a
        // magenta colour key. If it stays on screen while nothing paints into it
        // there is nothing for the key to key out, so the user sees a full-screen
        // magenta sheet (observed) -- and it steals the foreground from the game.
        //
        // So: hide it, stop presenting, idle. The menu is unreachable in this mode
        // by design; the ESP is drawn in-game instead.
        // -------------------------------------------------------------------
        // -------------------------------------------------------------------
        // The overlay is hidden while the in-game canvas owns the ESP, BUT it
        // must come back when the menu is open: the menu is ImGui and lives in
        // this window, so hiding unconditionally made the menu unreachable.
        //
        // Showing it only for the menu is safe: a menu does not track a moving
        // target, so the compositor latency that made the ESP feel "not 跟手"
        // does not matter here. The ESP itself is still drawn in-game.
        // -------------------------------------------------------------------
        // -------------------------------------------------------------------
        // INPUT MUST RUN BEFORE ANY CANVAS GATING. THIS WAS THE "GUI NEVER
        // OPENS" BUG.
        //
        // input::Tick() used to be called from inside RenderOnce(). But when the
        // in-game canvas is active this loop `continue`s before reaching
        // RenderOnce(), so Tick() never ran -- the INSERT keypress was detected by
        // the hotkey/WndProc paths, queued as "pending", and then never consumed.
        // The menu could therefore never open no matter how many times it was
        // pressed, and no log line was produced either, which made it look like
        // the input layer was dead.
        //
        // Polling here guarantees exactly one Tick() per loop iteration in every
        // mode. RenderOnce() still calls it too, and Tick() is written to be
        // idempotent (it consumes a pending flag), so a double call is harmless.
        // -------------------------------------------------------------------
        // Re-assert the game-window subclass. The game recreates its window on a
        // resolution or fullscreen change, which silently drops the subclass and
        // with it the menu key and the X1/X2 binding. The call is cheap (one
        // pointer compare) and only does work when the handle actually changed.
        InstallGameWndProcHook(s_gameHwnd);

        input::Tick();

        // -------------------------------------------------------------------
        // IDLE MODE -- zero Present while the menu is closed.
        //
        // The third dxgi crash (identical stack: dxgi!Present+0xdad0 reached
        // from the game's render thread) happened with the cap already at
        // 60 Hz, so rate-limiting alone does not remove the DWM contention --
        // it only lowers it. The only way to take the overlay's second swap
        // chain out of the equation entirely is to stop presenting it.
        //
        // While the menu is closed and the in-game canvas owns the ESP there
        // is nothing this window needs to animate: the status strip moved
        // into the game canvas (esp::RenderInGame), the ESP is in-game, and
        // input::Tick() above still polls INSERT. So: present ONE final
        // fully-transparent frame (so no stale strip stays composited), then
        // idle at 8 ms until the menu opens again.
        //
        // The overlay still renders when: the menu is open, capture
        // protection is on (the ESP lives here in that mode), or the in-game
        // canvas is not active.
        // -------------------------------------------------------------------
        {
            const bool overlayNeeded = gui::g_open || ::g_captureProtect || !g_canvasActive;
            static bool s_idle = false;
            if (!overlayNeeded) {
                if (!s_idle) {
                    // one blank present, then silence
                    if (s_context && s_rtv && s_swap) {
                        // MUST be the MAGENTA KEY, not transparent black.
                        //
                        // The window is in LWA_COLORKEY mode while the menu is closed,
                        // so only pixels equal to the key are punched out. Clearing
                        // to (0,0,0,0) does NOT match the key, which left an opaque
                        // black sheet covering the screen -- the black-screen report.
                        const float key[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
                        s_context->OMSetRenderTargets(1, &s_rtv, nullptr);
                        s_context->ClearRenderTargetView(s_rtv, key);
                        s_swap->Present(1, 0);
                    }
                    s_idle = true;
                    LOG("[overlay] idle: menu closed, in-game canvas owns drawing (no Present)");
                }
                // AIM MUST KEEP TICKING. RenderOnce() normally drives this, but
                // it does not run while idle. aim/skill only read the entity
                // snapshot published by the in-game canvas and write rotation
                // -- no D3D involved, safe to run from the idle path.
                aim::Tick();
                skill::Tick();
                Sleep(8);              // input::Tick() keeps INSERT responsive
                continue;
            }
            if (s_idle) {
                s_idle = false;
                LOG("[overlay] awake: menu/protection needs this window again");
            }
        }

        // -------------------------------------------------------------------
        // THE OVERLAY WINDOW STAYS SHOWN AT ALL TIMES.
        //
        // It used to be hidden whenever the in-game canvas owned the ESP:
        //
        //     if (canvasActive && !menuOpen) { ShowWindow(SW_HIDE); continue; }
        //
        // That `continue` also skipped RenderOnce(), which is where the menu and
        // the always-on status strip are drawn -- so with the canvas active there
        // was NO menu and NO status strip anywhere. That is the reported "常驻条
        // 也没显示 / GUI 看不到".
        //
        // Hiding is no longer needed at all: the window now uses per-pixel alpha
        // (see SetOpaque), so a frame that draws nothing is fully transparent and
        // invisible without being hidden. Keeping it shown means the menu and the
        // strip render in every mode.
        // -------------------------------------------------------------------
        if (!IsWindowVisible(s_hwnd)) {
            ShowWindow(s_hwnd, SW_SHOWNOACTIVATE);
            LOG("[overlay] window re-shown (menu + status strip must always render)");
        }
        // (window stays shown permanently; see the note above)

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG dt = now - last;
        if (dt < kFrameMs) {
            Sleep((DWORD)(kFrameMs - dt));
            continue;
        }
        last = GetTickCount64();

        RenderOnce();
    }
}

void Shutdown() {
    if (s_init) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        s_init = false;
    }
    ReleaseRTV();
    if (s_swap)    { s_swap->Release();    s_swap = nullptr; }
    if (s_context) { s_context->Release(); s_context = nullptr; }
    if (s_device)  { s_device->Release();  s_device = nullptr; }
    if (s_hwnd)    { DestroyWindow(s_hwnd); s_hwnd = nullptr; }
    UnregisterClassW(kClass, GetModuleHandleW(nullptr));
}

// ---------------------------------------------------------------------------
// Toggle capture protection at runtime (from the menu).
//
// This does three things together, and they must stay together:
//   1. switches the window between colour-key and per-pixel alpha
//      (WDA does not work with a colour key)
//   2. applies / removes WDA_EXCLUDEFROMCAPTURE
//   3. flips g_captureProtect, which moves the ESP between the in-game path and
//      this overlay -- the only way the boxes can be excluded from a recording
// ---------------------------------------------------------------------------
void ApplyCaptureProtection(bool on) {
    if (!s_hwnd) {
        LOG("[guard] no window yet; protection will apply on next init");
        return;
    }
    if (on) {
        capture_guard::Enable(s_hwnd);
        // force the opaque/alpha state to be re-evaluated for the new mode
        s_opaque = !s_opaque;
        SetOpaque(gui::g_open && gui::g_opaqueMenu);
        if (!IsWindowVisible(s_hwnd))
            ShowWindow(s_hwnd, SW_SHOWNOACTIVATE);
    } else {
        capture_guard::Disable(s_hwnd);
        s_opaque = !s_opaque;
        SetOpaque(gui::g_open && gui::g_opaqueMenu);
    }
}

} // namespace overlay
