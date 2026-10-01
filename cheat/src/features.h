// D:\Work\Fut\cheat\src\features.h
#pragma once
//
// All gameplay features in one header, ported from the overseas Flavia build
// (D:\xingyun\overseas\flavia-mod\Flavia-Source-main) and re-pointed at this
// game version's offsets.
//
// WHAT CAME FROM WHERE
//   Aimbot.h      -> features::aim      (bone targeting, smoothing, FOV circle, lock)
//   Radar.h       -> features::radar    (2D top-down)
//   TeamUtils.h   -> features::team     (team id + HP through the attribute set)
//   NoRecoil.h    -> features::recoil   (weapon-manager based)
//   drawing.hpp   -> sdk:: + features::draw (corner box, health bar, name, distance)
//
// The overseas implementation reads these fields directly off a generated SDK
// (`ch->AttributeTeamID`, `cySet->Health`). We read the same fields by offset;
// every offset used below is taken from the CURRENT dump and is listed next to
// its use so it can be re-verified after a patch.
#include "sdk.h"
#include "offsets.h"
#include "ue.h"
#include "obf.h"
#include "log.h"

#include <windows.h>
#include <cmath>
#include <string>

namespace features {

// ---------------------------------------------------------------------------
// shared state (mirrors CheatState in the overseas dllmain)
// ---------------------------------------------------------------------------
struct State {
    bool  espEnable     = true;
    bool  espBox        = true;
    bool  espCornerBox  = true;    // corner style, as the reference screenshot
    bool  espName       = true;
    bool  espDistance   = true;
    bool  espHealth     = true;
    bool  espBones      = true;    // skeleton -- the reference draws this
    bool  espSnapline   = false;
    bool  espTeamColor  = true;
    float espMaxDist    = 500.0f;  // metres, matching the overseas default

    // OUR OWN minimap lives top-left (20,20) -- that spot is ours; the game's
    // own map is the top-right one. Range 1500uu (15 m) was so short the radar
    // looked empty -- bots in the training range sit 20-40 m out, so nothing
    // was ever plotted ("直接不显示了").
    bool  radarEnable   = true;
    float radarRange    = 5000.0f; // uu = 50 m
    float radarSize     = 180.0f;
    float radarX        = 20.0f;
    float radarY        = 20.0f;

    bool  aimEnable     = false;
    bool  aimDrawFov    = true;
    bool  aimLockTarget = false;   // stick to one target (hysteresis)
    bool  aimIgnoreTeam = true;
    float aimFov        = 150.0f;  // pixels, as the overseas slider
    float aimSmooth     = 6.0f;
    float aimMaxDist    = 300.0f;  // metres
    int   aimKey        = VK_SHIFT;
    bool  aimBoneHead   = true;
    bool  aimJitter     = false;
    float aimJitterPct  = 0.10f;
    float aimJitterHz   = 8.0f;

    bool  recoilEnable  = false;

    // Uses the GAME'S OWN systems instead of our own drawing.
    //   antiStealth : list actors the game has in a stealth state and keep
    //                 drawing them -- the correct way to defeat invisibility,
    //                 because we ask the system that decides it.
    //   outline     : toggle the game's own skeletal-mesh outline, the same
    //                 effect its "see enemies" abilities use. Composites
    //                 correctly and updates inside the game frame.
    bool  antiStealth   = true;
    bool  outline       = false;

    bool  captureGuard  = false;   // screen-capture exclusion (see capture_guard)
};
extern State g;

// ---------------------------------------------------------------------------
// per-frame scratch, filled by ScanWorld() and consumed by every feature
// ---------------------------------------------------------------------------
struct ActorInfo {
    uintptr_t actor   = 0;
    uintptr_t mesh    = 0;
    sdk::Vec3 loc;
    sdk::Vec3 head;        // best-effort head position
    sdk::Vec3 feet;
    sdk::Vec3 chest;
    sdk::Vec3 pelvis;
    bool      bonesOk = false;
    int       team    = -1;
    float     health  = -1.0f;
    float     healthMax = -1.0f;
    float     distM   = 0.0f;
    bool      isLocal = false;
    char      name[64] = {0};
};
constexpr int kMaxActors = 64;

struct World {
    uintptr_t  world = 0;
    uintptr_t  controller = 0;
    uintptr_t  pcm = 0;
    uintptr_t  localPawn = 0;
    sdk::Vec3  camLoc;
    sdk::Rotator camRot;
    int        localTeam = -1;
    ActorInfo  actors[kMaxActors];
    int        count = 0;
    void*      canvas = nullptr;
    void*      font = nullptr;
    float      clipX = 1920.0f;
    float      clipY = 1080.0f;
};
extern World gWorld;

// ---------------------------------------------------------------------------
// team + health, ported from TeamUtils.h
//
// Field offsets verified against the current dump:
//   ACyCharacterBase::AttributeTeamID  @ 0x8B0  (uint8)
//   ACyCharacterBase::AbilitySystem    @ 0x880
//   UCyBaseAttributeSet::Health        @ 0x78,  HealthMax @ 0x88
//   FGameplayAttributeData::CurrentValue is +0x0C from the attribute's base
// ---------------------------------------------------------------------------
inline int TeamOf(uintptr_t actor) {
    if (!ue::PtrOk(actor)) return -1;
    return (int)ue::R8(actor + off::CY_AttributeTeamID);
}

inline float HealthOf(uintptr_t actor, float* outMax) {
    if (outMax) *outMax = -1.0f;
    if (!ue::PtrOk(actor)) return -1.0f;
    const uintptr_t ability = ue::R64(actor + off::CY_AbilitySystem);
    if (!ue::PtrOk(ability)) return -1.0f;
    const float cur = ue::RF32(ability + off::ATTR_Health + off::ATTR_CurrentValue);
    if (outMax)
        *outMax = ue::RF32(ability + off::ATTR_HealthMax + off::ATTR_CurrentValue);
    return cur;
}

// ---------------------------------------------------------------------------
// bones, via the component the world scan already located
// ---------------------------------------------------------------------------
bool ScoreBones(uintptr_t mesh, sdk::Vec3& head, sdk::Vec3& feet,
                sdk::Vec3& chest, sdk::Vec3& pelvis);

// ---------------------------------------------------------------------------
// world scan -- ONE pass per frame, shared by ESP / radar / aim, exactly like
// the overseas Reader::Update()
// ---------------------------------------------------------------------------
void ScanWorld(void* canvas);

// ---------------------------------------------------------------------------
// draw helpers (drawing.hpp equivalents)
// ---------------------------------------------------------------------------
namespace draw {
void CornerBox(void* cv, float x, float y, float w, float h, const sdk::LinearColor& c, float th);
void HealthBar(void* cv, float x, float y, float h, float hp, float hpMax);
void Text(void* cv, void* font, const std::string& s, float x, float y,
          const sdk::LinearColor& c, bool centre);
}

void DrawEsp(void* canvas);
void DrawRadar(void* canvas);
void DrawSkeleton(void* canvas, const ActorInfo& a, const sdk::LinearColor& c);

// Additive skeleton draw driven by the ESP's entity list.
//
// The ESP owns the geometry; this just renders a named-joint skeleton at that
// entity's mesh so the two never disagree about where the body is.
void DrawSkeletonFor(void* canvasObj, uintptr_t actor);
void DrawFovCircle(void* canvas);

void TickAim(void* canvas);
void TickRecoil(uintptr_t controller);

void Reset();

} // namespace features
