// D:\Work\Fut\cheat\src\capture_guard.h
#pragma once
#include <windows.h>

namespace capture_guard {

// ---------------------------------------------------------------------------
// Make a window invisible to screen capture.
//
// THE API: SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)
//   WDA_EXCLUDEFROMCAPTURE = 0x11  -> the window is omitted from captures
//                                     entirely (Windows 10 2004 / build 19041+)
//   WDA_MONITOR            = 0x01  -> legacy fallback: the window shows as a
//                                     BLACK RECTANGLE in captures
//   WDA_NONE               = 0x00  -> no protection
//
// Windows enforces that only the process OWNING the window may set this --
// an external tool calling it on our window gets ERROR_ACCESS_DENIED. That is
// why CaptureGuard injects a DLL; we already run inside the process, so we can
// simply call it ourselves.
//
// ---------------------------------------------------------------------------
// THE WS_EX_LAYERED CONFLICT -- WHY THIS FILE IS NOT ONE LINE
//
// The overlay window currently uses:
//     WS_EX_LAYERED + SetLayeredWindowAttributes(magenta, LWA_COLORKEY)
// to punch transparency through a colour key. In that configuration
// WDA_EXCLUDEFROMCAPTURE is unreliable: depending on driver and Windows build it
// either silently does nothing or degrades to the black-rectangle WDA_MONITOR
// behaviour. This is the pitfall described in the CaptureGuard notes, and it is
// real.
//
// The fix is to NOT use a colour key at all in protected mode. Windows lets a
// layered window use PER-PIXEL alpha instead, and a D3D11 swap chain that clears
// to transparent black plus DwmExtendFrameIntoClientArea(-1) gives exactly that.
// So:
//
//     colour-key mode : WS_EX_LAYERED + LWA_COLORKEY   (legacy, capturable)
//     protected mode  : no colour key, per-pixel alpha (WDA works)
//
// Enable() performs that style transition itself, so callers only ask for
// "protected" or "not protected".
// ---------------------------------------------------------------------------

// Result of an Enable() attempt, reported honestly -- never claim protection
// that is not actually active.
enum class Mode {
    None,        // no protection
    Excluded,    // WDA_EXCLUDEFROMCAPTURE: gone from captures (what we want)
    Blocked,     // WDA_MONITOR: a black rectangle in captures (degraded)
    Failed,      // the call was rejected
};

// Turn protection on/off for `hwnd`.
//
// When enabling, the window's colour-key transparency is converted to per-pixel
// alpha first (switching off WS_EX_LAYERED's colour key), because the two are
// not compatible.
Mode Enable(HWND hwnd);
Mode Disable(HWND hwnd);

// Last mode actually applied, for the status panel.
Mode Current();
const char* ModeName(Mode m);

// True when the OS supports WDA_EXCLUDEFROMCAPTURE at all (build >= 19041).
bool Supported();

// True once Enable() reported Excluded.
bool Active();

} // namespace capture_guard
