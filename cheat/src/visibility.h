// D:\Work\Fut\cheat\src\visibility.h
#pragma once
//
// Target visibility: line of sight, smoke, and stealth.
//
// WHY THIS IS A SEPARATE MODULE
//   "Do not lock through a wall / through smoke / onto someone invisible" is a
//   question about the WORLD, not about aiming. Keeping it here means aim stays a
//   pure angle solver and the three tests can be tuned, cached and reasoned about
//   independently.
//
// THE THREE TESTS, AND WHAT EACH ONE ACTUALLY COSTS
//
//   1. LINE OF SIGHT -- a real trace from the shooter's eye to the target point.
//      This is the only honest answer to "is there a wall in the way", and it is
//      also by far the most expensive: each call enters the engine's collision
//      query. So it is cached per (target, ~120 ms) rather than run every frame,
//      and it is skipped entirely when the caller has asked for through-wall aim.
//
//   2. SMOKE -- the game spawns ABP_Summon_SmokeGrenade_C actors for smoke. A
//      target inside one cannot be seen through it. Checked by enumerating the
//      live smoke actors (cached) and testing a radius, which is far cheaper than
//      tracing against the smoke volumes themselves.
//
//   3. STEALTH -- the game's own answer, from the subsystem's UnVisibleActors list
//      (see game_systems). This is authoritative: it is the same list the game
//      uses to decide whether a character renders.
//
// All three are opt-in and default to permissive (visible), so a failure inside
// any of them degrades to the old behaviour rather than blinding the aim.
#include <cstdint>

namespace visibility {

// Per-frame cache of the world state the three tests need. Call once per frame
// before any IsVisible() query.
void BeginFrame(uintptr_t world);

// ---------------------------------------------------------------------------
// individual tests
// ---------------------------------------------------------------------------

// Real line trace from `from` to `to`. Expensive: the result is cached by
// (target address) for kLosCacheMs.
bool HasLineOfSight(uintptr_t target, float fx, float fy, float fz,
                    float tx, float ty, float tz);

// True when the point sits inside any active smoke volume.
bool InSmoke(float x, float y, float z);

// The game's own invisibility verdict.
bool IsStealthed(uintptr_t target);

// ---------------------------------------------------------------------------
// combined, with the caller's policy
// ---------------------------------------------------------------------------
struct Policy {
    bool requireLos    = false;   // do not lock through walls
    bool avoidSmoke    = false;   // do not lock through smoke
    bool avoidStealth  = false;   // do not lock onto invisible targets
};

// Applies every enabled test. Returns true when the target is acceptable.
bool Passes(uintptr_t target, const Policy& policy,
            float fx, float fy, float fz, float tx, float ty, float tz);

// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------
int  SmokeCount();
int  StealthCount();
const char* LastRejectReason();

} // namespace visibility
