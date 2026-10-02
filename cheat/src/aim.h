// D:\Work\Fut\cheat\src\aim.h
#pragma once
#include <cstdint>

namespace aim {

// ---------------------------------------------------------------------------
// LOCK MODES -- HARD AND SOFT, INDEPENDENTLY ENABLEABLE
//
// The two solve the same problem differently, and they are not alternatives to
// each other: you can run either, or both at once.
//
//   SOFT LOCK  ("soft")  -- moves the view gradually toward the target.
//                           Writes a fraction of the remaining error each tick, so
//                           the crosshair visibly travels. Applies only while the
//                           trigger is held. This is the natural one for
//                           "aim assist" behaviour: it looks and feels like the
//                           player is tracking.
//
//   HARD LOCK  ("hard")  -- snaps the view onto the target and HOLDS it there.
//                           Writes the exact angle every tick while the trigger is
//                           held, and (optionally) keeps re-writing for a short
//                           time after release so a flick does not drift. This is
//                           the one that guarantees the shot lands, at the cost of
//                           looking obviously mechanical.
//
// WHEN BOTH ARE ON
//   Hard lock takes precedence while its trigger is held (it is the stronger
//   guarantee). Soft lock still runs on its own trigger, so the two can be bound
//   to different keys -- e.g. soft on right-mouse for tracking, hard on a side
//   button for a guaranteed flick.
//
// Each mode has its own trigger key, its own enable flag, and its own hold-to-
// engage policy, so every combination is expressible.
// ---------------------------------------------------------------------------
enum class Mode { Off = 0, Soft = 1, Hard = 2 };

struct Settings {
    // ---- master ----
    bool  enabled    = false;

    // ---- soft lock ----
    bool  softEnable = false;            // independently switchable
    int   softKey    = 0x02;             // VK_RBUTTON
    bool  softHold   = true;             // true = hold, false = toggle

    // ---- hard lock ----
    bool  hardEnable = false;            // independently switchable
    int   hardKey    = 0;                // VK 0 = 无 (unbound by default)
    bool  hardHold   = true;             // true = hold, false = toggle

    // ---- shared targeting ----
    bool  teamCheck  = true;             // skip teammates
    bool  fovLimit   = true;
    float fov        = 60.0f;            // degrees, half-angle for selection
    float smooth     = 4.0f;             // soft lock rate = 120/smooth per second
    bool  boneHead   = true;             // aim at head, else chest
    bool  drawFov    = true;             // draw the FOV circle

    // Hard lock extra: keep writing the locked angle for this long after the
    // trigger is released, so a flick-and-release stays on target.
    float hardHoldMs = 0.0f;             // 0 = stop immediately on release

    // ---- humaniser ----
    bool  jitter     = false;
    float jitterPct  = 0.60f;            // degrees of absolute wobble
    float jitterHz   = 8.0f;

    // ---- aim point height ----
    // Which bone anchor to aim at. Exposed because on a moving or flying target a
    // lower anchor (chest) is far more stable than the head, whose bone moves
    // several times more per frame.
    bool  preferChestWhenMoving = true;  // auto-fall back to chest on fast targets
    float movingThreshold       = 350.0f;// uu/s above which a target counts as fast
};

extern Settings g_settings;

// Called once per frame from the render thread (after ESP has collected data).
void Tick();

// Result accessors for the GUI / status strip.
int         TargetCount();
const char* LastError();
bool        HasTarget();
float       LastFov();
Mode        ActiveMode();       // which mode acted this frame
bool        HardLocked();       // hard lock currently holding a target

} // namespace aim
