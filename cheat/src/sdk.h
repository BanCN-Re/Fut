// D:\Work\Fut\cheat\src\sdk.h
#pragma once
//
// Thin typed layer over the engine, so the rest of the cheat reads like the
// overseas Flavia source instead of raw offsets.
//
// WHY THIS EXISTS
//   The overseas build (D:\xingyun\overseas\flavia-mod) works against a generated
//   CppSDK: `ch->Mesh->GetNumBones()`, `controller->ProjectWorldLocationToScreen(...)`,
//   `canvas->K2_DrawLine(...)`. Reproducing those features here is mostly a matter
//   of re-creating that ergonomics layer on top of the CURRENT dump, because every
//   function address changed in the 2026-09-22 patch.
//
// HOW CALLS ARE MADE
//   Two mechanisms, chosen per function:
//
//   1. ProcessEvent for UFUNCTIONs that have a UFunction object (canvas drawing,
//      ProjectWorldLocationToScreen, GetSocketTransform, ...). This is what the
//      generated SDK does and it is calling-convention safe.
//
//   2. Direct native call through a __fastcall typedef for plain C++ members that
//      are NOT UFUNCTIONs (K2_GetActorLocation is a good example: it is declared
//      BlueprintCallable so it DOES have a UFunction, but the hot ones are also
//      plain natives).
//
//   The rule enforced here: never cast a member function to a FREE function. That
//   mistake left RCX holding garbage and crashed the game with
//   EXCEPTION_ACCESS_VIOLATION reading 0x08000008 earlier in this project.
//
// ALL RVAs BELOW COME FROM THE CURRENT DUMP (2026-09-30, game 1.18.3.7) and were
// re-read out of Dumpspace\FunctionsInfo.json -- not carried over from the old
// project, whose values are all stale.

#include <cstdint>
#include <cmath>
#include <windows.h>

namespace sdk {

// ---------------------------------------------------------------------------
// RVAs (module-relative; add ImageBase())
// ---------------------------------------------------------------------------
namespace fn {
// canvas drawing
constexpr uint32_t K2_DrawLine    = 0x43DDA10;
constexpr uint32_t K2_DrawBox     = 0x43DD8C0;
constexpr uint32_t K2_DrawText    = 0x43DE070;
constexpr uint32_t K2_DrawTexture = 0x43DE430;
constexpr uint32_t K2_StrLen      = 0x43DE8D0;
constexpr uint32_t K2_Project     = 0x43DE820;

// transforms / geometry
constexpr uint32_t K2_GetActorLocation   = 0x43A1830;
constexpr uint32_t GetActorBounds        = 0x439EFA0;
constexpr uint32_t GetSocketLocation     = 0x44E3700;
constexpr uint32_t GetSocketTransform    = 0x44E3900;
constexpr uint32_t GetBoneName           = 0x44F42A0;
constexpr uint32_t GetNumBones           = 0x44F44B0;

// camera / controller
constexpr uint32_t ProjectWorldLocationToScreen = 0x44CA420;
constexpr uint32_t ProjectWorldToScreen         = 0x440CDC0;
constexpr uint32_t SetControlRotation           = 0x43EB250;
constexpr uint32_t GetControlRotation           = 0x44BA750;
constexpr uint32_t GetCameraLocation            = 0x44C0CA0;
constexpr uint32_t GetCameraRotation            = 0x44C0CE0;

// helpers
constexpr uint32_t GetPlayerController   = 0x440A1A0;
constexpr uint32_t GetPlayerPawn         = 0x440A3A0;
constexpr uint32_t LineTraceSingle       = 0x44794F0;
constexpr uint32_t GetInventoryComponent = 0x23782F0;
constexpr uint32_t GetCurrentWeapon      = 0x25DA270;
constexpr uint32_t GetAttributeTeamID    = 0x23441F0;
} // namespace fn

// ---------------------------------------------------------------------------
// structs (layout from the dump)
// ---------------------------------------------------------------------------
struct Vec2 { float X, Y; };

struct Vec3 {
    float X, Y, Z;
    Vec3() : X(0), Y(0), Z(0) {}
    Vec3(float x, float y, float z) : X(x), Y(y), Z(z) {}
};

struct Rotator {
    float Pitch, Yaw, Roll;
    Rotator() : Pitch(0), Yaw(0), Roll(0) {}
    Rotator(float p, float y, float r) : Pitch(p), Yaw(y), Roll(r) {}
};

struct LinearColor {
    float R, G, B, A;
    LinearColor() : R(1), G(1), B(1), A(1) {}
    LinearColor(float r, float g, float b, float a) : R(r), G(g), B(b), A(a) {}
};

// FString: TArray<wchar_t>
struct FString {
    wchar_t* Data;
    int32_t  Num;
    int32_t  Max;
};

// UObject::ProcessEvent
typedef void(__fastcall* tProcessEvent)(void* self, void* func, void* params);
extern tProcessEvent ProcessEvent;

// one-time init: resolves ProcessEvent and the UFunction objects we need
bool Init();
bool Ready();

// map a UFunction by its full path, e.g. "Function Engine.Canvas.K2_DrawLine"
uintptr_t FindFunction(const char* fullPath);

// ---------------------------------------------------------------------------
// canvas drawing (matches the overseas drawing.hpp API)
// ---------------------------------------------------------------------------
void DrawLine(void* canvas, Vec2 a, Vec2 b, float thickness, const LinearColor& c);
void DrawBox(void* canvas, Vec2 pos, Vec2 size, float thickness, const LinearColor& c);

// Filled rectangle. UCanvas has no filled primitive, so this scanlines it --
// the same technique the overseas build uses for health bars and radar backdrops.
void FillRect(void* canvas, float x, float y, float w, float h, const LinearColor& c);

void DrawText(void* canvas, void* font, const wchar_t* text, Vec2 pos, Vec2 scale,
              const LinearColor& color, bool centreX, bool centreY);

// Engine projection. Returns false when the point is behind the camera.
bool Project(void* canvas, const Vec3& world, Vec2& out);

// ---------------------------------------------------------------------------
// actor access
// ---------------------------------------------------------------------------
Vec3  GetActorLocation(uintptr_t actor);
bool  ProjectWorldLocation(uintptr_t controller, const Vec3& world, Vec2& out);
bool  GetActorBounds(uintptr_t actor, Vec3& origin, Vec3& extent);

// camera
bool  GetCameraLocation(uintptr_t pcm, Vec3& out);
bool  GetCameraRotation(uintptr_t pcm, Rotator& out);
void  SetControlRotation(uintptr_t controller, const Rotator& r);
bool  GetControlRotation(uintptr_t controller, Rotator& out);

// geometry helper used all over the cheat
inline float Dist3D(const Vec3& a, const Vec3& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
inline float Dist2D(const Vec3& a, const Vec3& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y;
    return sqrtf(dx * dx + dy * dy);
}

// ---------------------------------------------------------------------------
// projections that the aim module needs
// ---------------------------------------------------------------------------
// Angles from `from` to `to`, matching the overseas CalcAimAngles exactly
// (pitch = atan2(dz, horizontal), yaw = atan2(dy, dx)).
Rotator CalcAimAngles(const Vec3& from, const Vec3& to);

// Shortest-path angle interpolation, normalised to [-180, 180].
float   NormalizeAngle(float a);
Rotator SmoothAngles(const Rotator& current, const Rotator& target, float factor);

} // namespace sdk
