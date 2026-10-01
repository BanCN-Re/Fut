// D:\Work\Fut\cheat\src\sdk.cpp
// Implementation of the typed engine layer. See sdk.h for the calling-convention
// rules this file obeys.
#include "sdk.h"
#include "ue.h"
#include "offsets.h"
#include "obf.h"
#include "log.h"

#include <cmath>
#include <cstring>

namespace sdk {

tProcessEvent ProcessEvent = nullptr;

// ---------------------------------------------------------------------------
// UFunction cache
//
// Every ProcessEvent call needs the UFunction object, and looking it up by name
// costs a full GObjects walk (198k objects here). So each one is resolved once
// and cached.
// ---------------------------------------------------------------------------
static uintptr_t s_fnProject      = 0;
static uintptr_t s_fnDrawLine     = 0;
static uintptr_t s_fnDrawBox      = 0;
static uintptr_t s_fnDrawText     = 0;
static uintptr_t s_fnStrLen       = 0;
static uintptr_t s_fnDrawTexture  = 0;
static uintptr_t s_fnSetControlRot = 0;
static uintptr_t s_fnGetCamLoc    = 0;
static uintptr_t s_fnGetCamRot    = 0;
static uintptr_t s_fnGetCtrlRot   = 0;
static uintptr_t s_fnProjWorld    = 0;
static uintptr_t s_fnGetBounds    = 0;
static uintptr_t s_fnSocketXform  = 0;
static uintptr_t s_fnBoneName     = 0;
static uintptr_t s_fnNumBones     = 0;

static bool s_ready = false;
bool Ready() { return s_ready; }

// ---------------------------------------------------------------------------
// name lookup
// ---------------------------------------------------------------------------

// Returns the UFunction whose own name is `leaf` AND whose owner class name
// contains `ownerHint`. Checking the owner matters: this game has several
// same-named functions on different classes (GetControlRotation exists on both
// APawn and AController, for instance) and picking the wrong one silently
// returns the wrong value rather than failing.
static uintptr_t FindFunctionOwned(const char* leaf, const char* ownerHint) {
    if (!ue::GObjectsReady() || !leaf) return 0;
    const int32_t n = ue::NumObjects();
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t o = ue::ObjectAt(i);
        if (!ue::PtrOk(o)) continue;

        std::string nm;
        if (!ue::NameOf(o, nm)) continue;
        if (nm != leaf) continue;
        if (!ue::PtrOk(ue::R64(o + off::UFunction_ExecFunction))) continue;

        if (!ownerHint || !*ownerHint) return o;

        // walk the Outer chain looking for the owning class
        uintptr_t outer = ue::R64(o + off::UObj_Outer);
        int guard = 0;
        while (ue::PtrOk(outer) && guard++ < 8) {
            std::string on;
            if (ue::NameOf(outer, on) && on.find(ownerHint) != std::string::npos)
                return o;
            uintptr_t next = ue::R64(outer + off::UObj_Outer);
            if (next == outer) break;
            outer = next;
        }
    }
    return 0;
}

uintptr_t FindFunction(const char* fullPath) {
    // fullPath form: "Function Owner.Leaf"; split it so we can verify the owner.
    if (!fullPath) return 0;
    const char* dot = strrchr(fullPath, '.');
    if (!dot) return FindFunctionOwned(fullPath, nullptr);
    return FindFunctionOwned(dot + 1, nullptr);
}

bool Init() {
    if (s_ready) return true;
    if (!ue::GObjectsReady() && !ue::Init()) return false;

    ProcessEvent = reinterpret_cast<tProcessEvent>(ue::ImageBase() + off::ProcessEvent);

    s_fnProject       = FindFunctionOwned("K2_Project", "Canvas");
    s_fnDrawLine      = FindFunctionOwned("K2_DrawLine", "Canvas");
    s_fnDrawBox       = FindFunctionOwned("K2_DrawBox", "Canvas");
    s_fnDrawText      = FindFunctionOwned("K2_DrawText", "Canvas");
    s_fnStrLen        = FindFunctionOwned("K2_StrLen", "Canvas");
    s_fnDrawTexture   = FindFunctionOwned("K2_DrawTexture", "Canvas");
    s_fnSetControlRot = FindFunctionOwned("SetControlRotation", "Controller");
    s_fnGetCamLoc     = FindFunctionOwned("GetCameraLocation", "CameraManager");
    s_fnGetCamRot     = FindFunctionOwned("GetCameraRotation", "CameraManager");
    s_fnGetCtrlRot    = FindFunctionOwned("GetControlRotation", "Controller");
    s_fnProjWorld     = FindFunctionOwned("ProjectWorldLocationToScreen", "PlayerController");
    s_fnGetBounds     = FindFunctionOwned("GetActorBounds", "Actor");
    s_fnSocketXform   = FindFunctionOwned("GetSocketTransform", nullptr);
    s_fnBoneName      = FindFunctionOwned("GetBoneName", nullptr);
    s_fnNumBones      = FindFunctionOwned("GetNumBones", nullptr);

    if (!s_fnProject || !s_fnDrawLine) {
        LOG(SBOX("[sdk] init incomplete: project=%d drawline=%d", 0xD0),
            (int)(s_fnProject != 0), (int)(s_fnDrawLine != 0));
        return false;
    }

    s_ready = true;
    LOG(SBOX("[sdk] ready: line=0x%llX box=0x%llX text=0x%llX proj=0x%llX ctrlRot=0x%llX", 0xD1),
        (unsigned long long)s_fnDrawLine, (unsigned long long)s_fnDrawBox,
        (unsigned long long)s_fnDrawText, (unsigned long long)s_fnProject,
        (unsigned long long)s_fnSetControlRot);
    return true;
}

// ---------------------------------------------------------------------------
// FUNC_Native handling
//
// ProcessEvent dispatches on FunctionFlags. A native UFUNCTION needs the
// FUNC_Native bit (0x400) set at call time; without it the VM tries to interpret
// bytecode that does not exist and faults. Dumper-7's generated wrappers do this
// dance around every call (Engine_functions.cpp), and so must we.
//
// UFunction::FunctionFlags is at +0xD8 on this build.
// ---------------------------------------------------------------------------
static const uint32_t kFunctionFlags = 0xD8;
static const uint32_t kFuncNative    = 0x400;

static inline void CallNative(void* self, uintptr_t fn, void* params) {
    if (!ProcessEvent || !self || !fn) return;
    volatile uint32_t* flags = reinterpret_cast<volatile uint32_t*>(fn + kFunctionFlags);
    const uint32_t saved = *flags;
    const bool need = (saved & kFuncNative) == 0;
    if (need) *flags = saved | kFuncNative;
    ProcessEvent(self, reinterpret_cast<void*>(fn), params);
    if (need) *flags = saved;
}

// ---------------------------------------------------------------------------
// canvas
// ---------------------------------------------------------------------------
#pragma pack(push, 8)
struct P_Line { Vec2 A; Vec2 B; float Thickness; LinearColor Color; };
struct P_Box  { Vec2 Pos; Vec2 Size; float Thickness; LinearColor Color; };
struct P_Proj { Vec3 World; Vec3 Out; };
struct P_Text {
    void* Font; FString Text; Vec2 Pos; Vec2 Scale; LinearColor Color;
    float Kerning; LinearColor ShadowColor; Vec2 ShadowOffset;
    bool CentreX; bool CentreY; bool Outlined; LinearColor OutlineColor;
};
struct P_CtrlRot { Rotator Out; };
struct P_SetCtrlRot { Rotator In; };
struct P_Bounds { Vec3 Origin; Vec3 Extent; bool bOnlyColliding; Vec3 OutOrigin; Vec3 OutExtent; };
#pragma pack(pop)

void DrawLine(void* canvas, Vec2 a, Vec2 b, float th, const LinearColor& c) {
    if (!canvas || !s_fnDrawLine) return;
    __try {
        P_Line p{}; p.A = a; p.B = b; p.Thickness = th; p.Color = c;
        CallNative(canvas, s_fnDrawLine, &p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void DrawBox(void* canvas, Vec2 pos, Vec2 size, float th, const LinearColor& c) {
    if (!canvas || !s_fnDrawBox) return;
    __try {
        P_Box p{}; p.Pos = pos; p.Size = size; p.Thickness = th; p.Color = c;
        CallNative(canvas, s_fnDrawBox, &p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------------------
// FILLED RECTANGLE -- via K2_DrawTexture, NOT scanlines.
//
// !!! PERFORMANCE: THIS WAS THE FRAME-RATE KILLER !!!
//
// The first version borrowed the overseas approach and scanlined the rectangle
// with K2_DrawLine. That is survivable for the two or three small bars the
// overseas build draws, but our radar backdrop alone is 180 lines and each health
// bar is another ~100, so one frame issued tens of thousands of ProcessEvent
// calls. ProcessEvent is a full UFunction dispatch, not a blit -- the game
// dropped to single-digit FPS. That is exactly the slowdown observed.
//
// UCanvas::K2_DrawTexture with the canvas's own DefaultTexture (a plain white
// 1x1) and a 1x1 coordinate window paints a solid rectangle in ONE call. This is
// what the engine's own HUD does for panel backgrounds.
//
//   UCanvas::DefaultTexture  @ +0x2B0
//   EBlendMode: BLEND_Opaque=0, BLEND_Masked=1, BLEND_Translucent=2
// ---------------------------------------------------------------------------
void FillRect(void* canvas, float x, float y, float w, float h, const LinearColor& c) {
    if (!canvas || w <= 0.0f || h <= 0.0f) return;

    if (s_fnDrawTexture) {
        struct P_Tex {
            void* Texture; Vec2 Pos; Vec2 Size;
            Vec2 CoordPos; Vec2 CoordSize;
            LinearColor Color; uint8_t BlendMode; uint8_t pad[7];
            float Rotation; Vec2 Pivot;
        };
        __try {
            P_Tex p{};
            p.Texture   = reinterpret_cast<void*>(ue::R64((uintptr_t)canvas + 0x2B0));
            p.Pos       = { x, y };
            p.Size      = { w, h };
            p.CoordPos  = { 0.0f, 0.0f };
            p.CoordSize = { 1.0f, 1.0f };
            p.Color     = c;
            p.BlendMode = 2;                 // BLEND_Translucent
            p.Rotation  = 0.0f;
            p.Pivot     = { 0.0f, 0.0f };
            if (p.Texture) {
                CallNative(canvas, s_fnDrawTexture, &p);
                return;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Fallback only (no texture on this canvas). Stepped and capped so that even
    // this path cannot tank the frame rate.
    const float step = 3.0f;
    int guard = 0;
    for (float yy = y; yy < y + h && guard < 64; yy += step, ++guard)
        DrawLine(canvas, Vec2{ x, yy }, Vec2{ x + w, yy }, step, c);
}

void DrawText(void* canvas, void* font, const wchar_t* text, Vec2 pos, Vec2 scale,
              const LinearColor& color, bool centreX, bool centreY) {
    if (!canvas || !font || !text || !s_fnDrawText) return;
    __try {
        P_Text p{};
        p.Font = font;
        p.Text.Data = const_cast<wchar_t*>(text);
        p.Text.Num = (int32_t)wcslen(text) + 1;
        p.Text.Max = p.Text.Num;
        p.Pos = pos;
        p.Scale = scale;
        p.Color = color;
        p.Kerning = 0.0f;
        p.ShadowColor = LinearColor(0, 0, 0, 1);
        p.ShadowOffset = Vec2{ 1.0f, 1.0f };
        p.CentreX = centreX;
        p.CentreY = centreY;
        p.Outlined = true;
        p.OutlineColor = LinearColor(0, 0, 0, 1);
        CallNative(canvas, s_fnDrawText, &p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool Project(void* canvas, const Vec3& world, Vec2& out) {
    out = Vec2{};
    if (!canvas || !s_fnProject) return false;
    bool ok = false;
    __try {
        P_Proj p{}; p.World = world;
        CallNative(canvas, s_fnProject, &p);
        if (p.Out.Z > 0.0f) { out.X = p.Out.X; out.Y = p.Out.Y; ok = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ok;
}

// ---------------------------------------------------------------------------
// actor / camera
// ---------------------------------------------------------------------------
Vec3 GetActorLocation(uintptr_t actor) {
    // K2_GetActorLocation is a plain native returning a struct by value, which is
    // awkward to model through ProcessEvent. The location is also the root
    // component's world translation, which is what this reads -- cheaper and
    // exact.
    Vec3 v{};
    if (!ue::PtrOk(actor)) return v;
    uintptr_t root = ue::R64(actor + off::AA_RootComponent);
    if (!ue::PtrOk(root)) return v;

    // ComponentToWorld is an FTransform: quat @0x00, translation @0x10
    const uintptr_t c2w = root + off::Comp_ComponentToWorld;
    v.X = ue::RF32(c2w + 0x10);
    v.Y = ue::RF32(c2w + 0x14);
    v.Z = ue::RF32(c2w + 0x18);
    return v;
}

bool ProjectWorldLocation(uintptr_t controller, const Vec3& world, Vec2& out) {
    out = Vec2{};
    if (!ue::PtrOk(controller) || !s_fnProjWorld) return false;
    struct P { Vec3 World; Vec2 Screen; bool bPlayerViewportRelative; bool Ret; };
    bool ok = false;
    __try {
        P p{};
        p.World = world;
        CallNative(reinterpret_cast<void*>(controller), s_fnProjWorld, &p);
        if (p.Ret) { out = p.Screen; ok = true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ok;
}

bool GetActorBounds(uintptr_t actor, Vec3& origin, Vec3& extent) {
    if (!ue::PtrOk(actor) || !s_fnGetBounds) return false;
    bool ok = false;
    __try {
        P_Bounds p{};
        p.bOnlyColliding = false;
        CallNative(reinterpret_cast<void*>(actor), s_fnGetBounds, &p);
        origin = p.OutOrigin;
        extent = p.OutExtent;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ok;
}

bool GetCameraLocation(uintptr_t pcm, Vec3& out) {
    out = Vec3{};
    if (!ue::PtrOk(pcm)) return false;
    uintptr_t fn = s_fnGetCamLoc;
    if (!fn) return false;
    struct P { Vec3 Out; };
    bool ok = false;
    __try {
        P p{};
        CallNative(reinterpret_cast<void*>(pcm), fn, &p);
        out = p.Out; ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ok;
}

bool GetCameraRotation(uintptr_t pcm, Rotator& out) {
    out = Rotator{};
    if (!ue::PtrOk(pcm) || !s_fnGetCamRot) return false;
    struct P { Rotator Out; };
    bool ok = false;
    __try {
        P p{};
        CallNative(reinterpret_cast<void*>(pcm), s_fnGetCamRot, &p);
        out = p.Out; ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ok;
}

void SetControlRotation(uintptr_t controller, const Rotator& r) {
    if (!ue::PtrOk(controller) || !s_fnSetControlRot) return;
    __try {
        P_SetCtrlRot p{}; p.In = r;
        CallNative(reinterpret_cast<void*>(controller), s_fnSetControlRot, &p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool GetControlRotation(uintptr_t controller, Rotator& out) {
    out = Rotator{};
    if (!ue::PtrOk(controller) || !s_fnGetCtrlRot) return false;
    struct P { Rotator Out; };
    bool ok = false;
    __try {
        P p{};
        CallNative(reinterpret_cast<void*>(controller), s_fnGetCtrlRot, &p);
        out = p.Out; ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return ok;
}

// ---------------------------------------------------------------------------
// aim math -- identical formulas to the overseas Aimbot.h so the feel matches
// ---------------------------------------------------------------------------
float NormalizeAngle(float a) {
    while (a > 180.0f)  a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

Rotator CalcAimAngles(const Vec3& from, const Vec3& to) {
    const float dx = to.X - from.X;
    const float dy = to.Y - from.Y;
    const float dz = to.Z - from.Z;

    float dist2D = sqrtf(dx * dx + dy * dy);
    if (dist2D < 0.001f) dist2D = 0.001f;

    const float kRad2Deg = 180.0f / 3.14159265f;
    return Rotator{ atan2f(dz, dist2D) * kRad2Deg,
                    atan2f(dy, dx)   * kRad2Deg,
                    0.0f };
}

Rotator SmoothAngles(const Rotator& current, const Rotator& target, float factor) {
    if (factor <= 1.0f) return target;
    const float dPitch = NormalizeAngle(target.Pitch - current.Pitch);
    const float dYaw   = NormalizeAngle(target.Yaw   - current.Yaw);
    return Rotator{ current.Pitch + dPitch / factor,
                    current.Yaw   + dYaw   / factor,
                    0.0f };
}

} // namespace sdk
