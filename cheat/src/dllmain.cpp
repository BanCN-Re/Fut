// ===========================================================================
// zw_core.dll -- CORE MODULE  [architecture: split]
// ===========================================================================
//
// This used to be the zlibwapi proxy itself. It is now a plain DLL loaded at
// runtime by the thin carrier (cheat/loader/loader.cpp), which forwards the 129
// zlib exports and calls ZwCoreStart() here.
//
// WHY: the monolith was 4,105,216 B against a genuine 133,936 B zlibwapi.dll --
// 31x too large under the zlib name. The carrier is now ~21 KB (smaller than the
// real library, so no size anomaly) and everything heavy lives here, where the
// file name carries no zlib expectation.
//
// HONEST LIMIT: this hides nothing from a module enumerator. zw_core.dll shows up
// in the loader list and the VAD exactly like any other DLL. The split removes a
// SIZE signature, not the module itself.
//
// Boot order matters:
//   1. resolve our own path and open the trace log
//   2. module concealment, if enabled (before anything walks the PEB)
//   3. network filter EARLY -- before ACE can upload early detections
//   4. wait for the engine to be up
//   5. install the Present hook
//   6. network filter again after the engine loads its modules
//   7. periodic re-patch (ACE can load new modules later)
#include "log.h"
#include "obf.h"
#include "stealth.h"
#include "ace_blackhole.h"
#include "fake_server.h"
#include "fake_trust.h"
#include "d3d.h"
#include "ue.h"
#include "gui.h"
#include "input.h"
#include "gfxprobe.h"
#include "overlay.h"
#include "canvas.h"

#include <windows.h>

static HMODULE g_self = nullptr;

// ---------------------------------------------------------------------------
// periodic blackhole refresh
// ---------------------------------------------------------------------------
static DWORD WINAPI PeriodicThread(LPVOID) {
    for (;;) {
        Sleep(60000);
        ace::InstallDnsBlackholePeriodic();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// boot thread
// ---------------------------------------------------------------------------
static DWORD WINAPI BootThread(LPVOID param) {
    logutil::Init(param);
    // P1: no "fut-cheat" literal in the image any more.
    LOG(SBOX("[boot] proxy starting (pid=%lu)", 0x4C), GetCurrentProcessId());

    // ---- 1. fake report server, FIRST ----
    // Must come before the network filter: the filter's connect() redirects
    // blocklisted report destinations to this responder, so the responder has to
    // be listening before any upload is attempted. Starting it here also means it
    // is up before ACE finishes its early initialisation.
    //
    // This is the "black hole" in its real sense: accept the upload, complete a
    // genuine TLS handshake, answer with a plausible success, and let the client
    // believe reporting worked. A refusal would be observable; a success is not.
    LOG(SBOX("[boot] fake responder installing", 0x1F));
    fakesrv::Install();
    fakesrv::InstallTrustHooks();
    LOG(SBOX("[boot] fake responder %s (port=%u)", 0x37),
        fakesrv::Ready() ? "ready" : "UNAVAILABLE", (unsigned)fakesrv::LocalPort());

    // ---- 2. network filter, early (P1: no plaintext technique description) ----
    LOG(SBOX("[boot] net filter (early) installing", 0x1D));
    ace::InstallDnsBlackholeEarly();
    LOG(SBOX("[boot] net filter (early) done", 0x2F));

    // ---- 2b. optional module concealment (P4) ----
    // Off unless ZLW_SELF_STEALTH=1. Runs AFTER the log is open (it needs our
    // own path) and BEFORE the long engine wait, so the unlink happens while the
    // loader is not mid-iteration. See stealth.h for why this is opt-in.
    stealth::ApplyIfEnabled();

    // ---- 2. wait for the engine ----
    // The game needs a while to load modules and build GObjects. Poll instead of
    // a fixed sleep so we hook as soon as it is safe.
    uintptr_t base = ue::ImageBase();
    LOG("[boot] image base = 0x%llX", (unsigned long long)base);

    for (int i = 0; i < 600; ++i) {          // up to 5 minutes
        if (ue::Init()) break;
        Sleep(500);
    }
    LOG("[boot] GObjects %s after wait", ue::GObjectsReady() ? "ready" : "NOT ready");

    // Let the engine settle before hooking the swap chain, so we do not
    // interfere with early device creation.
    Sleep(8000);

    // ---- 3. overlay ----
    // We do NOT hook the game's swap chain: the game presents through D3D12
    // (d3d12.dll is loaded), so the D3D11 IDXGISwapChain::Present hook never
    // fires in-game. A self-owned transparent topmost window renders our UI
    // regardless of which graphics API the game uses, and hooks nothing.
    LOG("[boot] graphics modules at hook time:");
    gfxprobe::DumpLoadedGraphicsModules();

    // ---- 3a. IN-GAME canvas hook (preferred path) ----
    //
    // Install the UGameViewportClient::PostRender hook so the ESP draws inside
    // the game's own frame. This has zero compositor latency, unlike the overlay
    // window which DWM composites after the fact.
    //
    // Requires a loaded map (needs LocalPlayer -> ViewportClient), so it retries
    // until the world is up.
    LOG(SBOX("[boot] installing in-game canvas hook", 0x9B));
    {
        int tries = 0;
        while (!canvas::Install() && tries++ < 300)
            Sleep(500);
        if (canvas::Active())
            LOG(SBOX("[boot] in-game canvas ACTIVE (slot %d)", 0x9C), canvas::SlotIndex());
        else
            LOG(SBOX("[boot] in-game canvas unavailable; overlay window will be used", 0x9D));
    }

    LOG("[boot] starting self-owned overlay window");
    CreateThread(nullptr, 0, overlay::ThreadProc, nullptr, 0, nullptr);
    LOG("[boot] overlay thread started");

    // ---- 4. DNS blackhole again, now that the engine has its modules ----
    LOG(SBOX("[boot] net filter (init) installing", 0x4B));
    ace::InstallDnsBlackhole();
    LOG(SBOX("[boot] net filter (init) done", 0x57));

    // ---- 5. periodic refresh ----
    CreateThread(nullptr, 0, PeriodicThread, nullptr, 0, nullptr);

        // start the independent input paths (RegisterHotKey thread etc.)
    input::Init();

    LOG(SBOX("[boot] ready -- INSERT toggles the menu", 0x63));
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point exported for the carrier.
//
// The carrier (zlibwapi.dll) does LoadLibrary on us and then calls this by name,
// so we need no DllMain-side work at all. We still spawn the boot thread and
// return immediately: the carrier's LoadLibrary call must not block for the
// whole boot sequence (it would stall the thread that the game's loader is
// waiting on).
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) int __stdcall ZwCoreStart(void) {
    g_self = nullptr;
    // Resolve our own handle for the log path.
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&ZwCoreStart), &g_self);

    HANDLE t = CreateThread(nullptr, 0, BootThread, g_self, 0, nullptr);
    if (!t) return -1;
    CloseHandle(t);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // No work here: the carrier calls ZwCoreStart explicitly. Doing anything
        // in DllMain would run under the loader lock.
        break;
    case DLL_PROCESS_DETACH:
        break;
    default:
        break;
    }
    return TRUE;
}
