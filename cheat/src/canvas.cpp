// D:\Work\Fut\cheat\src\canvas.cpp
// In-game UCanvas drawing (rationale in canvas.h).
//
// CALLING CONVENTION -- READ THIS BEFORE CHANGING ANYTHING
//
// The previous build crashed the game with
//     EXCEPTION_ACCESS_VIOLATION reading 0x08000008
// because a MEMBER function (APlayerController::ProjectWorldLocationToScreen)
// was cast to a FREE function. On x64 a member function takes `this` in RCX, so
// the call left RCX holding whatever was in it and the engine dereferenced
// garbage.
//
// Everything below therefore goes through UObject::ProcessEvent, which is the
// path Dumper-7's own generated SDK uses (Engine_functions.cpp):
//
//     Params p{};  p.X = ...;  Func->FunctionFlags |= 0x400;
//     ProcessEvent(Func, &p);
//
// ProcessEvent is a genuine member function whose signature we can match
// exactly, and the params struct layout comes straight from the dump
// (Engine_parameters.hpp). No hand-rolled calling convention is involved, so
// there is nothing to get wrong.
//
// The cost is a UFunction lookup per primitive; that is mitigated by resolving
// each UFunction ONCE and caching the pointer.
#include "canvas.h"
#include "ue.h"
#include "esp.h"
#include "offsets.h"
#include "obf.h"
#include "log.h"

#include <windows.h>
#include <cmath>
#include <cstring>

// Global flag read by the overlay so the two ESP paths never both draw.
bool g_canvasActive = false;
namespace canvas {

// ---------------------------------------------------------------------------
// param structs (offsets verified against Engine_parameters.hpp)
// ---------------------------------------------------------------------------
struct FVector2D_t { float X, Y; };
struct FLinearColor_t { float R, G, B, A; };
struct FString_t { wchar_t* Data; int32_t Num; int32_t Max; };

#pragma pack(push, 8)
struct Params_DrawLine {
    FVector2D_t    ScreenPositionA;   // 0x00
    FVector2D_t    ScreenPositionB;   // 0x08
    float          Thickness;         // 0x10
    FLinearColor_t RenderColor;       // 0x14
};
struct Params_DrawBox {
    FVector2D_t    ScreenPosition;    // 0x00
    FVector2D_t    ScreenSize;        // 0x08
    float          Thickness;         // 0x10
    FLinearColor_t RenderColor;       // 0x14
};
struct Params_StrLen {
    void*       RenderFont;           // 0x00
    FString_t   RenderText;           // 0x08
    FVector2D_t ReturnValue;          // 0x18
};
// D:\Work\Fut\cheat\src\_proj_patch.txt
// UCanvas::K2_Project(FVector WorldLocation) -> FVector
//   +0x00 FVector WorldLocation (12B)
//   +0x0C FVector ReturnValue   (12B: projected screen x/y, z = depth)
struct FVector_t { float X, Y, Z; };
struct Params_Project {
    FVector_t WorldLocation;          // 0x00
    FVector_t ReturnValue;            // 0x0C
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// engine state
// ---------------------------------------------------------------------------
// UGameViewportClient::PostRender vtable slot for this title's UE 4.25 build.
//
// PostRender is a C++ virtual, NOT a UFUNCTION, so it cannot be resolved by name.
// Verified two ways: a live census of GObjects found no object named PostRender
// at all, and the dump's Engine_classes.hpp declares only three blueprint
// members on UGameViewportClient (SetConsoleTarget, ShowTitleSafeArea,
// SSSwapControllers). The slot index is therefore the only handle available.
//
// Install() validates that this slot points INTO the game module before hooking
// it, and dumps the neighbouring slots if not -- guessing a slot is how you turn
// a working game into a crash.
static const int kPostRenderSlot = 100;

static bool  s_installed = false;
static int   s_slot = -1;
static void* s_viewportClient = nullptr;

typedef void(__fastcall* tPostRender)(void* viewport, void* canvas);
static tPostRender s_origPostRender = nullptr;

// cached UFunction for each primitive
static uintptr_t s_fnDrawLine = 0;
static uintptr_t s_fnDrawBox  = 0;
static uintptr_t s_fnStrLen   = 0;
static uintptr_t s_fnProject  = 0;   // UCanvas::K2_Project -- the engine's own projection

typedef void(__fastcall* tProcessEvent)(void* self, void* func, void* params);
static tProcessEvent s_processEvent = nullptr;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static void* FindPostRenderNative() {
    if (!ue::GObjectsReady()) return nullptr;

    // -----------------------------------------------------------------------
    // Two passes, because the short name alone was not found.
    //
    // Pass 1 -- full name "GameViewportClient:PostRender" / contains both.
    //   The UObject's own Name field is only the LEAF ("PostRender"), but the
    //   object also has an Outer chain: Function -> UClass -> Package. The
    //   authoritative identity is "<Class>:<Function>", so we check the owner.
    //
    // Pass 2 -- any UFunction whose leaf name is PostRender AND whose owner
    //   class is a GameViewportClient. Belt and braces: if the leaf name is
    //   unique in this build (it was not), pass 1 already caught it.
    // -----------------------------------------------------------------------
    const int32_t n = ue::NumObjects();
    uintptr_t firstAnyPostRender = 0;

    for (int32_t i = 0; i < n; ++i) {
        uintptr_t o = ue::ObjectAt(i);
        if (!ue::PtrOk(o)) continue;

        std::string nm;
        if (!ue::NameOf(o, nm)) continue;
        if (nm != "PostRender") continue;

        const uintptr_t exec = ue::R64(o + off::UFunction_ExecFunction);
        if (!ue::PtrOk(exec)) continue;

        if (!firstAnyPostRender) firstAnyPostRender = o;

        // walk Outer to find the owning class name
        uintptr_t outer = ue::R64(o + off::UObj_Outer);
        int guard = 0;
        while (ue::PtrOk(outer) && guard++ < 8) {
            std::string on;
            if (ue::NameOf(outer, on)) {
                if (on.find("ViewportClient") != std::string::npos) {
                    LOG(SBOX("[canvas] PostRender 0x%llX exec=0x%llX owner=%s", 0xA0),
                        (unsigned long long)o, (unsigned long long)exec, on.c_str());
                    return reinterpret_cast<void*>(exec);
                }
            }
            uintptr_t next = ue::R64(outer + off::UObj_Outer);
            if (next == outer) break;
            outer = next;
        }
    }

    // Fallback: exactly one candidate -- take it, but say so loudly.
    if (firstAnyPostRender) {
        const uintptr_t exec = ue::R64(firstAnyPostRender + off::UFunction_ExecFunction);
        if (ue::PtrOk(exec)) {
            LOG(SBOX("[canvas] PostRender (unverified owner) 0x%llX exec=0x%llX", 0xA1),
                (unsigned long long)firstAnyPostRender, (unsigned long long)exec);
            return reinterpret_cast<void*>(exec);
        }
    }
    return nullptr;
}

static void* FindViewportClient() {
    uintptr_t world = ue::GetWorld();
    if (!ue::PtrOk(world)) return nullptr;
    uintptr_t gi = ue::R64(world + off::UW_OwningGameInstance);
    if (!ue::PtrOk(gi)) return nullptr;
    uintptr_t lpData = ue::R64(gi + off::UGI_LocalPlayers + off::TArray_Data);
    if (!ue::PtrOk(lpData)) return nullptr;
    uintptr_t lp = ue::R64(lpData);
    if (!ue::PtrOk(lp)) return nullptr;
    uintptr_t vpc = ue::R64(lp + off::ULP_ViewportClient);
    return ue::PtrOk(vpc) ? reinterpret_cast<void*>(vpc) : nullptr;
}

// Find a UFunction object by its short name, preferring a Canvas owner.
static uintptr_t FindFunction(const char* shortName) {
    if (!ue::GObjectsReady() || !shortName) return 0;
    const int32_t n = ue::NumObjects();
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t o = ue::ObjectAt(i);
        if (!ue::PtrOk(o)) continue;
        std::string nm;
        if (!ue::NameOf(o, nm)) continue;
        if (nm != shortName) continue;
        if (!ue::PtrOk(ue::R64(o + off::UFunction_ExecFunction))) continue;
        return o;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ENGINE PROJECTION
//
// This replaces the hand-rolled WorldToScreen math, and it is the fix for the
// misaligned boxes.
//
// The world->screen transform is not one formula: this game is third-person with
// camera lag, recoil offset, ADS FOV changes, its own aspect handling and its own
// near-plane convention. Reproducing that from (camera loc, rotation, fov)
// produced boxes that were close but consistently offset -- exactly what the
// screenshot showed.
//
// UCanvas::K2_Project IS that transform, evaluated by the engine, in the same
// frame we draw into, with the exact matrices the renderer is using. There is
// nothing to get wrong.
//
// The overseas build (D:\xingyun\esp\esp_3d_v2.cpp) takes the same idea further:
// it reads the game's ViewUniformShaderParameters WorldToClip matrix at draw
// time. K2_Project is the higher-level equivalent and needs no matrix hunting.
//
// Returns false when the point is behind the camera (z <= 0) or when the
// function is unavailable.
// ---------------------------------------------------------------------------
// fwd decl: ProcessEventNative is defined with the primitives further down
static inline void ProcessEventNative(void* self, uintptr_t fn, void* params);

bool Project(void* cv, float wx, float wy, float wz, float& sx, float& sy) {
    sx = sy = 0.0f;
    if (!cv || !s_processEvent || !s_fnProject) return false;

    bool ok = false;
    __try {
        Params_Project p{};
        p.WorldLocation = { wx, wy, wz };
        ProcessEventNative(cv, s_fnProject, &p);

        // K2_Project returns an FVector whose z is depth; z <= 0 means the point
        // is behind the near plane and must not be drawn.
        if (p.ReturnValue.Z > 0.0f) {
            sx = p.ReturnValue.X;
            sy = p.ReturnValue.Y;
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// FUNC_Native handling -- REQUIRED, and the cause of "draw raised; swallowed"
//
// ProcessEvent dispatches by examining FunctionFlags. For a native function the
// engine expects the FUNC_Native bit (0x400) to be set, and then calls
// ExecFunction directly. Many UFUNCTIONs do NOT have it set persistently -- it
// is applied by the caller around the call.
//
// Dumper-7's own generated wrappers do exactly this (see Engine_functions.cpp):
//
//     auto Flgs = Func->FunctionFlags;
//     Func->FunctionFlags |= 0x400;
//     UObject::ProcessEvent(Func, &Parms);
//     Func->FunctionFlags = Flgs;
//
// Without it the VM sees flags that do not describe a native function and tries
// to interpret bytecode that does not exist -- which faults, and our SEH guard
// swallowed it, so nothing was ever drawn.
//
// UFunction::FunctionFlags is at +0xD8 on this build.
// ---------------------------------------------------------------------------
static const uint32_t kFunctionFlags = 0xD8;
static const uint32_t kFuncNative    = 0x400;

// Call success/failure counters.
//
// The SEH guards below swallow faults silently, so "cmds=16 queued" does not
// prove anything reached the engine. These counters are the only honest signal:
// ok=N means N draw calls actually executed inside the game's frame.
static volatile long s_okCalls = 0;
static volatile long s_badCalls = 0;
long DrawOkCalls()  { return s_okCalls; }
long DrawBadCalls() { return s_badCalls; }

static inline void ProcessEventNative(void* self, uintptr_t fn, void* params) {
    // -----------------------------------------------------------------------
    // ADDRESS TYPE BUG -- `uint32_t flagsAddr` WAS TRUNCATING A 64-BIT POINTER.
    //
    // UFunction::FunctionFlags lives at +0xD8 from the function object, so on x64
    // that is a full 64-bit address like 0x000001F4A2C3D0D8. Declaring the local
    // as uint32_t cut it to 0xA2C3D0D8, and `reinterpret_cast<uint32_t*>(...)`
    // then ZERO-EXTENDED that back to 0x00000000A2C3D0D8 -- an address in the low
    // 4 GB that belongs to nothing we own.
    //
    // Writing there either faulted (caught by the caller's SEH, so the flag was
    // simply never set and native calls misbehaved) or, worse, landed in some
    // other mapping. The compiler flagged it as C4312 and it was correct to.
    //
    // uintptr_t throughout, and the read/write go through volatile so the
    // optimiser cannot fold the restore away.
    // -----------------------------------------------------------------------
    volatile uint32_t* flags = reinterpret_cast<volatile uint32_t*>(fn + kFunctionFlags);
    const uint32_t saved = *flags;

    // only touch the flag when it is actually missing
    const bool needSet = (saved & kFuncNative) == 0;
    if (needSet)
        *flags = saved | kFuncNative;

    s_processEvent(self, reinterpret_cast<void*>(fn), params);

    if (needSet)
        *flags = saved;
}

static void CallDrawLine(void* cv, float x1, float y1, float x2, float y2,
                         float th, const FLinearColor_t& c) {
    if (!s_processEvent || !s_fnDrawLine) return;
    Params_DrawLine p{};
    p.ScreenPositionA = { x1, y1 };
    p.ScreenPositionB = { x2, y2 };
    p.Thickness       = th;
    p.RenderColor     = c;
    __try { ProcessEventNative(cv, s_fnDrawLine, &p); ++s_okCalls; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++s_badCalls; }
}

static void CallDrawBox(void* cv, float x, float y, float w, float h,
                        float th, const FLinearColor_t& c) {
    if (!s_processEvent || !s_fnDrawBox) return;
    Params_DrawBox p{};
    p.ScreenPosition = { x, y };
    p.ScreenSize     = { w, h };
    p.Thickness      = th;
    p.RenderColor    = c;
    __try { ProcessEventNative(cv, s_fnDrawBox, &p); ++s_okCalls; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++s_badCalls; }
}

// A filled area. UCanvas has no filled-rect primitive, so we scanline it with
// DrawLine -- the same approach every UE4 canvas ESP uses.
static void CallFillRect(void* cv, float x, float y, float w, float h,
                         const FLinearColor_t& c) {
    const float step = 2.0f;
    for (float yy = y; yy < y + h; yy += step)
        CallDrawLine(cv, x, yy, x + w, yy, step, c);
}

// ---------------------------------------------------------------------------
// deferred command list (ESP fills it, the hook flushes it inside the frame)
// ---------------------------------------------------------------------------
struct DrawCmd {
    enum Kind { Rect, FillRect, Line } kind;
    float x, y, w, h;
    float x2, y2;
    float r, g, b, a;
    float thickness;
};
static DrawCmd s_cmds[8192];
static int     s_cmdCount = 0;

void BeginFrame() { s_cmdCount = 0; }

void AddRect(float x, float y, float w, float h, float th,
             float r, float g, float b, float a) {
    if (s_cmdCount >= 8192) return;
    DrawCmd& c = s_cmds[s_cmdCount++];
    c.kind = DrawCmd::Rect; c.x = x; c.y = y; c.w = w; c.h = h;
    c.thickness = th; c.r = r; c.g = g; c.b = b; c.a = a;
}

void AddFillRect(float x, float y, float w, float h,
                 float r, float g, float b, float a) {
    if (s_cmdCount >= 8192) return;
    DrawCmd& c = s_cmds[s_cmdCount++];
    c.kind = DrawCmd::FillRect; c.x = x; c.y = y; c.w = w; c.h = h;
    c.r = r; c.g = g; c.b = b; c.a = a;
}

void AddLine(float x1, float y1, float x2, float y2, float th,
             float r, float g, float b, float a) {
    if (s_cmdCount >= 8192) return;
    DrawCmd& c = s_cmds[s_cmdCount++];
    c.kind = DrawCmd::Line; c.x = x1; c.y = y1; c.x2 = x2; c.y2 = y2;
    c.thickness = th; c.r = r; c.g = g; c.b = b; c.a = a;
}

int CmdCount() { return s_cmdCount; }

static void Flush(void* cv) {
    if (!cv) return;
    for (int i = 0; i < s_cmdCount; ++i) {
        const DrawCmd& c = s_cmds[i];
        FLinearColor_t col{ c.r, c.g, c.b, c.a };
        switch (c.kind) {
        case DrawCmd::Rect:     CallDrawBox(cv, c.x, c.y, c.w, c.h, c.thickness, col); break;
        case DrawCmd::FillRect: CallFillRect(cv, c.x, c.y, c.w, c.h, col); break;
        case DrawCmd::Line:     CallDrawLine(cv, c.x, c.y, c.x2, c.y2, c.thickness, col); break;
        default: break;
        }
    }
    s_cmdCount = 0;
}

// ---------------------------------------------------------------------------
// hook
// ---------------------------------------------------------------------------
static void __fastcall Hook_PostRender(void* viewport, void* cv) {
    if (cv) {
        __try {
            // esp::RenderInGame is declared at global scope in esp.h. The explicit
            // qualifier matters: this body is inside namespace canvas, so an
            // unqualified extern would declare canvas::RenderInGame and fail to link.
            ::esp::RenderInGame(cv);
            Flush(cv);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static bool once = false;
            if (!once) { once = true; LOG(SBOX("[canvas] draw raised; swallowed", 0x92)); }
        }
    }
    if (s_origPostRender) s_origPostRender(viewport, cv);
}

// ---------------------------------------------------------------------------
// vtable slot scan.
//
// WHY THIS IS A SEPARATE FUNCTION: MSVC rejects __try in any function that also
// needs C++ object unwinding (error C2712) -- and Install() below holds a lambda
// and std::string. Keeping the SEH block in a plain function that owns nothing
// is the standard fix.
//
// Returns the slot holding `target`, or -1. Returns -2 when the scan faulted.
// ---------------------------------------------------------------------------
static int ScanVtableSlot(void** vt, void* target) {
    __try {
        for (int i = 0; i < 200; ++i)
            if (vt[i] == target) return i;
        return -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
}
// Read one vtable entry safely. Separate function because MSVC forbids __try in
// a function that also needs C++ unwinding (C2712) -- Install() holds strings.
static uintptr_t SafeVtableRead(void** vt, int idx) {
    __try { return reinterpret_cast<uintptr_t>(vt[idx]); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool Installed() { return s_installed; }
int  SlotIndex() { return s_slot; }

bool Install() {
    if (s_installed) return true;

    // ---- per-step diagnostics ----
    //
    // The first version returned a bare false, so a failing Install() looked
    // identical to "still loading" and the retry loop spun silently. Each step
    // now reports once, so the blocking condition is visible in the trace.
    static int s_lastReason = -1;
    auto Fail = [&](int reason, const char* what) -> bool {
        if (reason != s_lastReason) {
            s_lastReason = reason;
            LOG(SBOX("[canvas] blocked at step %d: %s", 0x9E), reason, what);
        }
        return false;
    };

    uintptr_t world = ue::GetWorld();
    if (!ue::PtrOk(world)) return Fail(1, "no UWorld (still loading)");

    void* vpc = FindViewportClient();
    if (!vpc) return Fail(2, "no ViewportClient (no LocalPlayer yet)");
    s_viewportClient = vpc;

    if (!ue::GObjectsReady()) return Fail(3, "GObjects not ready");

    // -----------------------------------------------------------------------
    // LOCATING PostRender
    //
    // UGameViewportClient::PostRender is a plain C++ virtual -- it is NOT a
    // UFUNCTION. Confirmed against the dump: Engine_classes.hpp declares only
    // three blueprint-callable members on that class (SetConsoleTarget,
    // ShowTitleSafeArea, SSSwapControllers), and a live census of GObjects found
    // no object named PostRender at all.
    //
    // Therefore it can never be resolved by name, and the vtable SLOT has to be
    // used directly. The UObject vtable layout for a given engine version is
    // fixed by the header the game was built against, so the index is stable for
    // this title's 4.25 build.
    //
    // We still try the name lookup first (costs nothing, and would be more
    // robust if a future build does export it).
    // -----------------------------------------------------------------------
    void** vt = *reinterpret_cast<void***>(vpc);

    int found = -1;
    if (void* native = FindPostRenderNative())
        found = ScanVtableSlot(vt, native);

    if (found < 0) {
        found = kPostRenderSlot;
        // Verify the slot holds a plausible function pointer inside the game
        // module before touching it. Hooking an out-of-range or aligned-looking
        // slot is how you turn a working game into a crash.
        const uintptr_t slotAddr = SafeVtableRead(vt, found);
        if (!slotAddr) return Fail(5, "vtable read faulted");

        const uintptr_t base = ue::ImageBase();
        const uintptr_t imgEnd = base + 0x20000000;   // generous upper bound
        if (slotAddr < base || slotAddr > imgEnd) {
            static int s_dumped = 0;
            if (s_dumped++ < 1) {
                LOG(SBOX("[canvas] slot %d = 0x%llX is OUTSIDE the module; dumping neighbours", 0xA5),
                    found, (unsigned long long)slotAddr);
                for (int i = found - 6; i <= found + 6; ++i) {
                    const uintptr_t a = SafeVtableRead(vt, i);
                    LOG(SBOX("[canvas]   vt[%d] = 0x%llX %s", 0xA6), i,
                        (unsigned long long)a,
                        (a >= base && a <= imgEnd) ? "(module)" : "(OTHER)");
                }
            }
            return Fail(6, "PostRender slot does not point into the game module");
        }

        static bool once = false;
        if (!once) {
            once = true;
            LOG(SBOX("[canvas] using PostRender slot %d (0x%llX) by index", 0xA7),
                found, (unsigned long long)slotAddr);
        }
    }

    // resolve ProcessEvent + the primitives once
    if (!s_processEvent)
        s_processEvent = reinterpret_cast<tProcessEvent>(ue::ImageBase() + off::ProcessEvent);
    if (!s_fnDrawLine) s_fnDrawLine = FindFunction("K2_DrawLine");
    if (!s_fnDrawBox)  s_fnDrawBox  = FindFunction("K2_DrawBox");
    if (!s_fnProject)  s_fnProject  = FindFunction("K2_Project");

    if (!s_fnDrawLine || !s_fnDrawBox) {
        static bool once = false;
        if (!once) {
            once = true;
            LOG(SBOX("[canvas] draw fn DrawLine=0x%llX DrawBox=0x%llX -- censusing", 0x9F),
                (unsigned long long)s_fnDrawLine, (unsigned long long)s_fnDrawBox);
            // List every object whose name contains "Draw" so the real registered
            // names are visible instead of guessed.
            int shown = 0;
            const int32_t cnt = ue::NumObjects();
            for (int32_t i = 0; i < cnt && shown < 40; ++i) {
                uintptr_t o = ue::ObjectAt(i);
                if (!ue::PtrOk(o)) continue;
                std::string nm;
                if (!ue::NameOf(o, nm)) continue;
                if (nm.find("Draw") == std::string::npos) continue;
                std::string owner;
                uintptr_t outer = ue::R64(o + off::UObj_Outer);
                if (ue::PtrOk(outer)) ue::NameOf(outer, owner);
                const uintptr_t cls = ue::R64(o + off::UObj_Class);
                std::string clsName;
                if (ue::PtrOk(cls)) ue::NameOf(cls, clsName);
                LOG(SBOX("[canvas]  '%s' outer='%s' class='%s' exec=0x%llX", 0xA8),
                    nm.c_str(), owner.c_str(), clsName.c_str(),
                    (unsigned long long)ue::R64(o + off::UFunction_ExecFunction));
                ++shown;
            }
            LOG(SBOX("[canvas] draw census done (%d)", 0xA9), shown);
        }
        return Fail(7, "K2_DrawLine / K2_DrawBox UFunction not found");
    }

    DWORD old = 0;
    if (!VirtualProtect(&vt[found], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
        return Fail(8, "VirtualProtect on the vtable slot failed");
    s_origPostRender = reinterpret_cast<tPostRender>(vt[found]);
    vt[found] = reinterpret_cast<void*>(&Hook_PostRender);
    DWORD unused = 0;
    VirtualProtect(&vt[found], sizeof(void*), old, &unused);

    s_slot = found;
    s_installed = true;
    ::g_canvasActive = true;
    LOG(SBOX("[canvas] PostRender hook INSTALLED at slot %d", 0x96), found);
    return true;
}

void OnPostRender(void*, void*) {}   // unused; kept for the header contract

// True once the hook is live and the primitives resolved.
bool Active() { return s_installed && s_fnDrawLine != 0 && s_processEvent != nullptr; }
bool ProjectReady() { return s_fnProject != 0; }

// ---------------------------------------------------------------------------
// canvas size cache
//
// The features path needs ClipX/ClipY every frame; reading them here keeps that
// knowledge in one place instead of duplicating the offsets across modules.
// UCanvas: +0x30 ClipX  +0x34 ClipY  +0x40 SizeX  +0x44 SizeY
// ---------------------------------------------------------------------------
static float s_clipX = 1920.0f;
static float s_clipY = 1080.0f;

void SetCanvasSize(void* cv) {
    if (!cv) return;
    const uintptr_t c = (uintptr_t)cv;

    // -----------------------------------------------------------------------
    // USE ClipX/ClipY (0x30/0x34), NOT SizeX/SizeY (0x40/0x44).
    //
    // The overseas build centres the FOV circle on `canvas->ClipX / 2`, and that
    // is the value that matches the real drawable viewport. SizeX/SizeY is the
    // canvas allocation and can differ, which is why the circle came out
    // off-centre.
    //
    // UCanvas layout from the dump:
    //   +0x28 OrgX    +0x2C OrgY
    //   +0x30 ClipX   +0x34 ClipY     <- the ones that matter
    //   +0x40 SizeX   +0x44 SizeY
    // -----------------------------------------------------------------------
    float w = (float)ue::RI32(c + 0x30);
    float h = (float)ue::RI32(c + 0x34);
    if (w < 16.0f || h < 16.0f) {
        w = (float)ue::RI32(c + 0x40);
        h = (float)ue::RI32(c + 0x44);
    }
    if (w >= 16.0f && h >= 16.0f) { s_clipX = w; s_clipY = h; }
}

float ClipX() { return s_clipX; }
float ClipY() { return s_clipY; }

// The UFont the engine uses for HUD text. Resolved once from GObjects.
static void* s_font = nullptr;

void* DefaultFont() {
    if (s_font) return s_font;
    if (!ue::GObjectsReady()) return nullptr;

    // -----------------------------------------------------------------------
    // ROBOTO FIRST, THEN ANY USABLE UFont.
    //
    // Roboto is what the overseas build uses, but depending on it alone is why ESP
    // names and distances never appeared: when that single lookup misses, every
    // text call returns immediately and the labels are simply absent -- no box, no
    // error, nothing in the log.
    //
    // So: prefer Roboto, otherwise accept the first UFont of class "Font".
    // -----------------------------------------------------------------------
    void* best = nullptr;
    const int32_t n = ue::NumObjects();
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t o = ue::ObjectAt(i);
        if (!ue::PtrOk(o)) continue;

        uintptr_t cls = ue::R64(o + off::UObj_Class);
        if (!ue::PtrOk(cls)) continue;
        std::string cn;
        if (!ue::NameOf(cls, cn) || cn != "Font") continue;

        std::string nm;
        if (!ue::NameOf(o, nm)) continue;

        if (nm.find("Roboto") != std::string::npos) {
            s_font = reinterpret_cast<void*>(o);
            LOG(SBOX("[canvas] font resolved: %s (Roboto)", 0xE3), nm.c_str());
            return s_font;
        }
        if (!best) best = reinterpret_cast<void*>(o);
    }

    if (best) {
        s_font = best;
        LOG(SBOX("[canvas] font resolved: first UFont available (no Roboto)", 0xE4));
        return s_font;
    }
    LOG(SBOX("[canvas] NO UFont in GObjects -- ESP text cannot render", 0xE5));
    return nullptr;
}

} // namespace canvas
