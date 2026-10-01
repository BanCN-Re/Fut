// D:\Work\Fut\cheat\src\d3d.cpp
// Swap-chain hook + ImGui backends.
//
// WHY WE HOOK *TWO* SWAP-CHAIN VARIANTS
// ------------------------------------
// A dummy swap chain is only useful if it has the same vtable as the game's.
// Build 1 used D3D11CreateDeviceAndSwapChain + DXGI_SWAP_EFFECT_DISCARD; the
// trace then showed "Present #1" and nothing after, while the GPU stayed busy
// at ~40% and the game kept rendering. Cause: UE4 4.25 on Windows 10 creates a
// FLIP-MODEL swap chain (CreateSwapChainForHwnd + DXGI_SWAP_EFFECT_FLIP_DISCARD),
// and that object's vtable is a DIFFERENT set of Present/Present1 functions.
// Hooking the legacy vtable therefore patched a function the game never calls.
//
// So we now build both a legacy and a flip-model dummy, and hook Present (slot 8)
// and Present1 (slot 22) in EACH distinct vtable. Whichever the game uses fires;
// the other is harmless.
//
// WHAT IS MODIFIED
// ----------------
// MinHook writes an inline JMP at the head of the target function, so the first
// bytes of the relevant dxgi.dll functions ARE modified. We deliberately do NOT
// touch d3d11.dll, the game's own code, or swap any per-instance vtable (the
// approach that crashed the game in the old project's postmortem: a false
// positive vtable swap made the NVIDIA UMD dereference garbage).
#include "d3d.h"
#include "gui.h"
#include "esp.h"
#include "ue.h"
#include "log.h"
#include "font.h"
#include "input.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <MinHook.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace d3d {

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// one record per vtable variant we manage to hook
struct Variant {
    const char* tag;
    PresentFn   origP;
    Present1Fn  origP1;
    void*       presentAddr;
    void*       present1Addr;
    volatile LONG calls;
};
static Variant s_legacy{ "legacy", nullptr, nullptr, nullptr, nullptr, 0 };
static Variant s_flip  { "flip",   nullptr, nullptr, nullptr, nullptr, 0 };
static ResizeBuffersFn s_origResize = nullptr;
static void*           s_resizeAddr = nullptr;

static bool             s_init = false;
static bool             s_ready = false;
static uint64_t         s_frames = 0;
static ID3D11Device*        s_device = nullptr;
static ID3D11DeviceContext* s_context = nullptr;
static IDXGISwapChain*      s_swap = nullptr;
static ID3D11RenderTargetView* s_rtv = nullptr;
static HWND             s_hwnd = nullptr;
static WNDPROC          s_origWndProc = nullptr;

bool Ready() { return s_ready; }
uint64_t Frames() { return s_frames; }

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static void LogModuleOf(const char* what, void* p) {
    HMODULE m = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(p), &m) && m) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* n = strrchr(path, '\\');
        n = n ? n + 1 : path;
        LOG("[d3d] %s %p  <- %s", what, p, n);
    } else {
        LOG("[d3d] %s %p  <- (unknown module)", what, p);
    }
}

// ---------------------------------------------------------------------------
// window proc detour (input)
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProcDetour(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (input::OnWndProcMessage(hwnd, msg, wp, lp))
        return TRUE;

    if (gui::g_open && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return TRUE;

    if (gui::g_open) {
        switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR:
            return TRUE;
        default: break;
        }
    }
    return CallWindowProcA(s_origWndProc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// render target
// ---------------------------------------------------------------------------
static void CreateRTV() {
    if (!s_swap || !s_device) return;
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(s_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back) {
        s_device->CreateRenderTargetView(back, nullptr, &s_rtv);
        back->Release();
    }
}

static void ReleaseRTV() {
    if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
}

// ---------------------------------------------------------------------------
// ImGui init
// ---------------------------------------------------------------------------
static void InitImGui(IDXGISwapChain* swap) {
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), (void**)&s_device)) || !s_device)
        return;
    s_device->GetImmediateContext(&s_context);
    if (!s_context) return;

    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    s_hwnd = desc.OutputWindow;
    s_swap = swap;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // ---- UI1001 palette / rounded dark theme ----
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding    = 10.0f;
    st.ChildRounding     = 8.0f;
    st.FrameRounding     = 3.0f;
    st.GrabRounding      = 3.0f;
    st.PopupRounding     = 12.0f;
    st.ScrollbarRounding = 0.0f;
    st.ScrollbarSize     = 7.0f;
    st.GrabMinSize       = 5.0f;
    st.WindowPadding     = ImVec2(8, 8);
    st.FramePadding      = ImVec2(6, 4);
    st.ItemSpacing       = ImVec2(6, 6);
    st.WindowBorderSize  = 0.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(18/255.f, 20/255.f, 23/255.f, 0.96f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.09f, 0.09f, 0.09f, 0.00f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.21f, 0.23f, 0.50f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.28f, 0.29f, 0.31f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.35f, 0.36f, 0.38f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.11f, 0.11f, 0.11f, 0.94f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(65/255.f, 63/255.f, 72/255.f, 0.5f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(65/255.f, 63/255.f, 72/255.f, 0.5f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(65/255.f, 63/255.f, 72/255.f, 0.5f);
    c[ImGuiCol_Separator]            = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);

    ImGui_ImplWin32_Init(s_hwnd);
    ImGui_ImplDX11_Init(s_device, s_context);

    if (font::LoadChineseFont(ImGui::GetIO(), 20.0f))
        LOG("[d3d] chinese font loaded");
    else
        LOG("[d3d] WARN: chinese font missing");

    s_origWndProc = (WNDPROC)SetWindowLongPtrA(s_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcDetour);

    CreateRTV();
    s_init = true;
    LOG("[d3d] imgui initialized (hwnd=%p device=%p)", s_hwnd, s_device);
}

// ---------------------------------------------------------------------------
// shared render path
// ---------------------------------------------------------------------------
static void RenderFrame(IDXGISwapChain* swap) {
    if (!s_init) InitImGui(swap);
    if (!s_init || !s_device || !s_context) return;

    if (!s_rtv) CreateRTV();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    if (gui::g_open && s_hwnd) {
        POINT p{};
        if (GetCursorPos(&p) && ScreenToClient(s_hwnd, &p))
            ImGui::GetIO().MousePos = ImVec2((float)p.x, (float)p.y);
    }

    input::Tick();
    ImGui::NewFrame();

    __try {
        esp::Render();
        gui::Render();
        gui::RenderDebugStrip();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG("[d3d] exception in render path (swallowed)");
    }

    ImGui::Render();
    if (s_rtv) {
        s_context->OMSetRenderTargets(1, &s_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    s_ready = true;
    ++s_frames;
    if (s_frames == 1 || (s_frames % 180) == 0) {
        LOG("[d3d] frame %llu  menu=%s  obj=%d  tgt=%d  path=%s  legacy=%ld flip=%ld",
            (unsigned long long)s_frames, gui::g_open ? "OPEN" : "closed",
            ue::NumObjects(), esp::LastTargetCount(), input::ActivePath(),
            s_legacy.calls, s_flip.calls);
    }
}

// ---------------------------------------------------------------------------
// hooks: one pair per variant
// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE Hook_Present_Legacy(IDXGISwapChain* s, UINT a, UINT b) {
    InterlockedIncrement(&s_legacy.calls);
    RenderFrame(s);
    return s_legacy.origP(s, a, b);
}
static HRESULT STDMETHODCALLTYPE Hook_Present_Flip(IDXGISwapChain* s, UINT a, UINT b) {
    InterlockedIncrement(&s_flip.calls);
    RenderFrame(s);
    return s_flip.origP(s, a, b);
}
static HRESULT STDMETHODCALLTYPE Hook_Present1_Legacy(IDXGISwapChain1* s, UINT a, UINT b,
                                                      const DXGI_PRESENT_PARAMETERS* p) {
    InterlockedIncrement(&s_legacy.calls);
    RenderFrame(s);
    return s_legacy.origP1(s, a, b, p);
}
static HRESULT STDMETHODCALLTYPE Hook_Present1_Flip(IDXGISwapChain1* s, UINT a, UINT b,
                                                    const DXGI_PRESENT_PARAMETERS* p) {
    InterlockedIncrement(&s_flip.calls);
    RenderFrame(s);
    return s_flip.origP1(s, a, b, p);
}
static HRESULT STDMETHODCALLTYPE Hook_ResizeBuffers(IDXGISwapChain* s, UINT n, UINT w,
                                                    UINT h, DXGI_FORMAT f, UINT fl) {
    ReleaseRTV();
    HRESULT hr = s_origResize(s, n, w, h, f, fl);
    if (s_init && s_device) CreateRTV();
    return hr;
}

// Hook Present/Present1 in one vtable, filling the matching Variant.
static int HookOneVtable(void** vt, Variant& v) {
    if (!vt) return 0;
    int n = 0;

    void* p8 = vt[8];
    if (p8 && !v.origP) {
        MH_STATUS st = MH_CreateHook(p8, (v.tag[0] == 'l') ? (void*)&Hook_Present_Legacy
                                                           : (void*)&Hook_Present_Flip,
                                     reinterpret_cast<void**>(&v.origP));
        if (st == MH_OK) { v.presentAddr = p8; ++n; LOG("[d3d] %s Present  hooked", v.tag); }
        else LOG("[d3d] %s Present  hook failed st=%d", v.tag, (int)st);
    }

    void* p22 = vt[22];
    if (p22 && !v.origP1) {
        MH_STATUS st = MH_CreateHook(p22, (v.tag[0] == 'l') ? (void*)&Hook_Present1_Legacy
                                                            : (void*)&Hook_Present1_Flip,
                                     reinterpret_cast<void**>(&v.origP1));
        if (st == MH_OK) { v.present1Addr = p22; ++n; LOG("[d3d] %s Present1 hooked", v.tag); }
        else LOG("[d3d] %s Present1 hook failed st=%d", v.tag, (int)st);
    }
    return n;
}

// ---------------------------------------------------------------------------
// install
// ---------------------------------------------------------------------------
static HWND MakeDummyWindow(WNDCLASSEXA& wc) {
    wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "fut_dummy_wc";
    RegisterClassExA(&wc);
    return CreateWindowExA(0, wc.lpszClassName, "fut", WS_OVERLAPPEDWINDOW,
                           0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
}

bool Init() {
    LOG("[d3d] probing swap-chain vtables (legacy + flip-model)...");

    WNDCLASSEXA wc{};
    HWND dummy = MakeDummyWindow(wc);
    if (!dummy) { LOG("[d3d] dummy window failed"); return false; }

    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    // ---- variant A: legacy (DISCARD) ----
    {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = dummy;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
        IDXGISwapChain* sc = nullptr; D3D_FEATURE_LEVEL lvl{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2, D3D11_SDK_VERSION,
            &sd, &sc, &dev, &lvl, &ctx);
        if (SUCCEEDED(hr) && sc) {
            void** vt = *reinterpret_cast<void***>(sc);
            s_legacy.presentAddr = vt[8];
            s_legacy.present1Addr = vt[22];
            LogModuleOf("legacy vt[8] ", vt[8]);
            LogModuleOf("legacy vt[22]", vt[22]);
            if (!s_resizeAddr) { s_resizeAddr = vt[13]; LogModuleOf("legacy vt[13]", vt[13]); }
            sc->Release();
        } else {
            LOG("[d3d] legacy swap chain failed hr=0x%08X", hr);
        }
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }

    // ---- variant B: flip model (what UE4 4.25 on Win10 actually uses) ----
    void** flipVt = nullptr;
    IDXGISwapChain1* flipSc = nullptr;
    {
        ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL lvl{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                       want, 2, D3D11_SDK_VERSION, &dev, &lvl, &ctx);
        if (SUCCEEDED(hr) && dev) {
            IDXGIDevice* dxgiDev = nullptr;
            IDXGIAdapter* ad = nullptr;
            IDXGIFactory2* fac = nullptr;
            if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) && dxgiDev &&
                SUCCEEDED(dxgiDev->GetAdapter(&ad)) && ad &&
                SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac)) && fac) {

                DXGI_SWAP_CHAIN_DESC1 sd1{};
                sd1.Width = 64;
                sd1.Height = 64;
                sd1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                sd1.BufferCount = 2;                 // flip model needs >= 2
                sd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                sd1.SampleDesc.Count = 1;
                sd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

                hr = fac->CreateSwapChainForHwnd(dev, dummy, &sd1, nullptr, nullptr, &flipSc);
                if (SUCCEEDED(hr) && flipSc) {
                    flipVt = *reinterpret_cast<void***>(flipSc);
                    s_flip.presentAddr = flipVt[8];
                    s_flip.present1Addr = flipVt[22];
                    LogModuleOf("flip   vt[8] ", flipVt[8]);
                    LogModuleOf("flip   vt[22]", flipVt[22]);
                } else {
                    LOG("[d3d] flip swap chain failed hr=0x%08X", hr);
                }
            }
            if (fac) fac->Release();
            if (ad) ad->Release();
            if (dxgiDev) dxgiDev->Release();
        }
        if (ctx) ctx->Release();
        if (dev) dev->Release();
    }

    const bool samePresent = (s_legacy.presentAddr && flipVt &&
                              s_legacy.presentAddr == s_flip.presentAddr);
    LOG("[d3d] legacy Present=%p  flip Present=%p  same=%d",
        s_legacy.presentAddr, s_flip.presentAddr, (int)samePresent);

    // clean up dummies BEFORE hooking
    if (flipSc) flipSc->Release();
    DestroyWindow(dummy);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);

    if (!s_legacy.presentAddr && !s_flip.presentAddr) {
        LOG("[d3d] no Present address found at all");
        return false;
    }

    if (MH_Initialize() != MH_OK) { LOG("[d3d] MH_Initialize failed"); return false; }

    int hooked = 0;

    // legacy vtable: rebuild a throwaway object just to read the vtable again
    // (we already released it, so instead hook using the saved addresses)
    if (s_legacy.presentAddr) {
        MH_STATUS st = MH_CreateHook(s_legacy.presentAddr, (void*)&Hook_Present_Legacy,
                                     reinterpret_cast<void**>(&s_legacy.origP));
        if (st == MH_OK) { ++hooked; LOG("[d3d] legacy Present hooked"); }
        else LOG("[d3d] legacy Present failed st=%d", (int)st);
    }
    if (s_legacy.present1Addr && s_legacy.present1Addr != s_legacy.presentAddr) {
        MH_STATUS st = MH_CreateHook(s_legacy.present1Addr, (void*)&Hook_Present1_Legacy,
                                     reinterpret_cast<void**>(&s_legacy.origP1));
        if (st == MH_OK) { ++hooked; LOG("[d3d] legacy Present1 hooked"); }
        else LOG("[d3d] legacy Present1 failed st=%d", (int)st);
    }

    // flip vtable: only hook the addresses that differ from legacy
    if (s_flip.presentAddr && s_flip.presentAddr != s_legacy.presentAddr) {
        MH_STATUS st = MH_CreateHook(s_flip.presentAddr, (void*)&Hook_Present_Flip,
                                     reinterpret_cast<void**>(&s_flip.origP));
        if (st == MH_OK) { ++hooked; LOG("[d3d] flip Present hooked"); }
        else LOG("[d3d] flip Present failed st=%d", (int)st);
    } else if (s_flip.presentAddr) {
        // same function: share the legacy trampoline so both variants work
        s_flip.origP = s_legacy.origP;
        LOG("[d3d] flip Present == legacy Present, sharing trampoline");
    }
    if (s_flip.present1Addr && s_flip.present1Addr != s_legacy.present1Addr &&
        s_flip.present1Addr != s_legacy.presentAddr) {
        MH_STATUS st = MH_CreateHook(s_flip.present1Addr, (void*)&Hook_Present1_Flip,
                                     reinterpret_cast<void**>(&s_flip.origP1));
        if (st == MH_OK) { ++hooked; LOG("[d3d] flip Present1 hooked"); }
        else LOG("[d3d] flip Present1 failed st=%d", (int)st);
    } else if (s_flip.present1Addr) {
        s_flip.origP1 = s_legacy.origP1;
        LOG("[d3d] flip Present1 == legacy Present1, sharing trampoline");
    }

    if (s_resizeAddr) {
        if (MH_CreateHook(s_resizeAddr, (void*)&Hook_ResizeBuffers,
                          reinterpret_cast<void**>(&s_origResize)) == MH_OK)
            LOG("[d3d] ResizeBuffers hooked");
    }

    if (!hooked) { LOG("[d3d] nothing hooked"); return false; }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LOG("[d3d] MH_EnableHook failed");
        return false;
    }

    LOG("[d3d] hooks enabled (%d Present/Present1 hooks across both vtables)", hooked);
    return true;
}

void Shutdown() {
    if (s_origWndProc && s_hwnd)
        SetWindowLongPtrA(s_hwnd, GWLP_WNDPROC, (LONG_PTR)s_origWndProc);
    if (s_init) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        s_init = false;
    }
    ReleaseRTV();
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

} // namespace d3d
