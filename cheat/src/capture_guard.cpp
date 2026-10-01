// D:\Work\Fut\cheat\src\capture_guard.cpp
// Screen-capture exclusion for the overlay window. See capture_guard.h for the
// rationale, the API, and the WS_EX_LAYERED conflict this file works around.
#include "capture_guard.h"
#include "log.h"
#include "obf.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace capture_guard {

// WDA_EXCLUDEFROMCAPTURE is 0x11 but the SDK header shipped with some toolchains
// predates it, so define it rather than depending on the SDK version.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

static Mode s_mode = Mode::None;

Mode Current() { return s_mode; }

const char* ModeName(Mode m) {
    switch (m) {
    case Mode::Excluded: return "EXCLUDED (invisible to capture)";
    case Mode::Blocked:  return "BLOCKED (black rect in capture)";
    case Mode::Failed:   return "FAILED";
    default:             return "OFF";
    }
}

bool Active() { return s_mode == Mode::Excluded; }

bool Supported() {
    // WDA_EXCLUDEFROMCAPTURE needs Windows 10 2004 (build 19041).
    typedef LONG(WINAPI* tRtlGetVersion)(PRTL_OSVERSIONINFOW);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (!nt) return false;
    auto fn = reinterpret_cast<tRtlGetVersion>(
        GetProcAddress(nt, "RtlGetVersion"));
    if (!fn) return false;

    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn(&vi) != 0) return false;
    if (vi.dwMajorVersion > 10) return true;
    if (vi.dwMajorVersion < 10) return false;
    return vi.dwBuildNumber >= 19041;
}

// ---------------------------------------------------------------------------
// colour key  ->  per-pixel alpha
//
// This is the part that makes WDA actually work.
//
// With WS_EX_LAYERED + SetLayeredWindowAttributes(colourKey, 0, LWA_COLORKEY)
// the window's transparency is a colour-key operation, and WDA_EXCLUDEFROMCAPTURE
// does not compose correctly with it (observed to no-op or to degrade to a black
// rectangle).
//
// Switching the SAME layered window to per-pixel alpha removes the conflict:
//   * SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)  -> alpha blending
//   * the swap chain clears to (0,0,0,0)                   -> those pixels vanish
//   * DwmExtendFrameIntoClientArea(-1)                     -> DWM composites it
//
// The overlay's renderer must therefore clear with alpha 0 instead of magenta.
// overlay.cpp does that when protected mode is on.
// ---------------------------------------------------------------------------
static void UsePerPixelAlpha(HWND hwnd) {
    // -----------------------------------------------------------------------
    // NO SetLayeredWindowAttributes HERE.
    //
    // This used to call SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA), which
    // is UNIFORM alpha: it forces every pixel opaque and cancels the per-pixel
    // alpha the renderer writes through DWM. Combined with the overlay's own call
    // it turned the whole screen into a solid black sheet.
    //
    // Per-pixel alpha requires WS_EX_LAYERED present and NO layered-window
    // attribute call at all; DWM then honours the alpha channel in the swap chain.
    // -----------------------------------------------------------------------
    MARGINS all{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &all);

    LOG(SBOX("[guard] window set to per-pixel alpha (no layered attribute call)", 0xC0));
}

Mode Enable(HWND hwnd) {
    if (!hwnd) return s_mode = Mode::Failed;

    if (!Supported()) {
        LOG(SBOX("[guard] WDA_EXCLUDEFROMCAPTURE needs Win10 2004+; falling back", 0xC1));
    }

    // Step 1: remove the colour key, which is what breaks WDA.
    UsePerPixelAlpha(hwnd);

    // Step 2: ask for capture exclusion.
    SetLastError(0);
    BOOL ok = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    DWORD err = GetLastError();

    if (ok) {
        s_mode = Mode::Excluded;
        LOG(SBOX("[guard] window EXCLUDED from capture (WDA_EXCLUDEFROMCAPTURE)", 0xC2));
        return s_mode;
    }

    LOG(SBOX("[guard] WDA_EXCLUDEFROMCAPTURE rejected (err=%lu); trying WDA_MONITOR", 0xC3),
        (unsigned long)err);

    // Step 3: degraded fallback -- a black rectangle is better than leaking the
    // ESP into a recording.
    SetLastError(0);
    ok = SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
    err = GetLastError();
    if (ok) {
        s_mode = Mode::Blocked;
        LOG(SBOX("[guard] window BLOCKED via WDA_MONITOR (black rect in capture)", 0xC4));
        return s_mode;
    }

    LOG(SBOX("[guard] WDA_MONITOR also rejected (err=%lu); capture protection OFF", 0xC5),
        (unsigned long)err);
    return s_mode = Mode::Failed;
}

Mode Disable(HWND hwnd) {
    if (!hwnd) return s_mode = Mode::Failed;
    SetWindowDisplayAffinity(hwnd, WDA_NONE);
    LOG(SBOX("[guard] capture protection disabled", 0xC6));
    return s_mode = Mode::None;
}

} // namespace capture_guard
