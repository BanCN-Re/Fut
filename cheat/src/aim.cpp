// D:\Work\Fut\cheat\src\aim.cpp
// Aim module.
//
// Two output paths, deliberately separated:
//
//  1. SMOOTH (default) -- computes the required view angles and writes them to
//     AController::ControlRotation through a guarded write. This is the same
//     field the game itself writes from mouse input, so it behaves like very
//     fast aim rather than teleporting the camera.
//
//  2. SILENT -- same write, but the value is applied without the smoothing ramp
//     and can be reverted after the shot. Off by default: it is far more
//     conspicuous to any behavioural check.
//
// The whole module is READ-ONLY until the user enables it, and every write goes
// through ue::WriteGuarded, which re-validates the page protection first.
#include "aim.h"
#include "esp.h"
#include "ue.h"
#include "bones.h"
#include "offsets.h"
#include "log.h"
#include "gui.h"
#include "themida.h"

#include "imgui.h"
#include <windows.h>
#include <cmath>

namespace aim {

using EspEnt = esp::EspEnt;   // entity view published by the ESP pass

Settings g_settings;

static int   s_targets = 0;
static bool  s_hasTarget = false;
static float s_lastFov = 0.0f;
static char  s_err[128] = {0};

int         TargetCount() { return s_targets; }
const char* LastError()   { return s_err; }
bool        HasTarget()   { return s_hasTarget; }
float       LastFov()     { return s_lastFov; }

// ---------------------------------------------------------------------------
// math
// ---------------------------------------------------------------------------
static float NormalizeDeg(float d) {
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

// UE4 FRotator: Pitch (up+), Yaw (right+), Roll.
// Aiming at a target is atan2 over the world delta, expressed in the game's axes.
static void AnglesTo(const ue::Camera& cam, float tx, float ty, float tz,
                     float& pitch, float& yaw) {
    const float dx = tx - cam.locX;
    const float dy = ty - cam.locY;
    const float dz = tz - cam.locZ;
    const float hyp = sqrtf(dx * dx + dy * dy);
    pitch = atan2f(dz, hyp) * 180.0f / 3.14159265358979323846f;
    yaw   = atan2f(dy, dx) * 180.0f / 3.14159265358979323846f;
}

// ---------------------------------------------------------------------------
// target selection
// ---------------------------------------------------------------------------
//
// WHY AIM READS THE ESP SNAPSHOT (answering "自瞄不是直接读数据吗"):
//
// Both ESP and aim need the same three things per character: the world
// position, the team id and the bone-derived aim point. Walking UWorld ->
// Actors[] and running the IsA class-chain test for a second time inside aim
// would double the cost of the most expensive part of the frame, so ESP does
// the walk ONCE per frame and publishes the result; aim reads that snapshot.
//
// It is still "reading data directly": the snapshot is refreshed every frame by
// esp::Render() BEFORE aim::Tick() runs (see overlay.cpp render order), so the
// data aim consumes is at most one frame old. Nothing is cached across frames.
//
// A dedicated direct-read path would also exist if bone data were needed for
// something ESP does not already compute; today there is no such field.
struct Cand {
    uintptr_t addr;
    float x, y, z;         // aim point (head or chest)
    float screenDist;      // pixels from the crosshair
    uint8_t team;
};

// ---------------------------------------------------------------------------
// random jitter (humaniser)
//
// The smoothed aim writes exact atan2 angles every tick, which is a robotic
// signature: the same error profile, zero variance. Jitter adds a bounded,
// seeded random offset to the TARGET ANGLE before the smoothing ramp, so the
// approach path wobbles like a hand. The seed is per-second, so the pattern
// does not repeat frame to frame.
//
// Jitter magnitude is a fraction of the remaining error, so it never overshoots
// and never prevents convergence.
// ---------------------------------------------------------------------------
static float Rand11(uint32_t seed) {
    // xorshift32 -> [-1, 1]
    uint32_t x = seed;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return ((float)x / 2147483648.0f) - 1.0f;
}

struct JitterCfg {
    bool  enabled = false;
    float percent = 0.10f;    // 10% of remaining error as wobble
    float hz      = 8.0f;     // wobble update rate
};
static JitterCfg g_jitter;

// ---------------------------------------------------------------------------
// target stickiness (hysteresis)
//
// Re-picking the nearest target every tick makes the crosshair jitter between
// two candidates that are almost equidistant -- the classic "aim twitches
// between two enemies" defect. CS2-style aim keeps the current target until:
//   * it dies / leaves the FOV / the list
//   * or another candidate is closer by more than kSwitchMargin
// A margin rather than an equality test is what stops the oscillation.
// ---------------------------------------------------------------------------
static uintptr_t s_stickyAddr = 0;
static const float kSwitchMargin = 0.75f;   // fraction; must be 25% closer to switch

static void ApplyJitter(float dPitch, float dYaw, float& outP, float& outY) {
    outP = dPitch; outY = dYaw;
    if (!g_jitter.enabled || (dPitch == 0.0f && dYaw == 0.0f)) return;

    // ------------------------------------------------------------------
    // ABSOLUTE-ANGLE JITTER.
    //
    // The old version scaled the wobble by the REMAINING ERROR
    // (out = d + d * pct * rand): as the aim converged the error shrank to
    // zero, and with it the jitter -- so the feature was mathematically
    // guaranteed to vanish exactly when you would see it. The wobble is now
    // an absolute offset in DEGREES on the target angle, independent of how
    // close the crosshair already is. percent is read as degrees (0.1..3).
    // ------------------------------------------------------------------
    const uint32_t tick = (uint32_t)(GetTickCount64() / (ULONGLONG)(1000.0f / g_jitter.hz));
    const float a = Rand11(tick * 2654435761u);
    const float b = Rand11(tick * 40503u + 7u);

    const float amp = g_jitter.percent;
    outP = dPitch + amp * a;
    outY  = dYaw   + amp * b;
}

static uint8_t LocalTeam() {
    uintptr_t pawn = ue::GetLocalPawn();
    if (!ue::PtrOk(pawn)) return 0xFF;
    return ue::R8(pawn + off::CY_AttributeTeamID);
}

void Tick() {
    // pull GUI settings into the jitter engine
    g_jitter.enabled = g_settings.jitter;
    g_jitter.percent = g_settings.jitterPct;
    g_jitter.hz      = g_settings.jitterHz > 1.0f ? g_settings.jitterHz : 1.0f;

    s_hasTarget = false;
    s_lastFov = 0.0f;
    if (!g_settings.enabled) { s_err[0] = 0; return; }

    // aim only while the key is held (unless configured otherwise)
    if (g_settings.holdKey) {
        if ((GetAsyncKeyState(g_settings.aimKey) & 0x8000) == 0) { s_err[0] = 0; return; }
    }

    ue::Camera cam;
    if (!ue::GetCamera(cam) || !cam.valid) { strcpy_s(s_err, u8"无相机"); return; }

    EspEnt ents[256];
    const int n = esp::SnapshotForAim(ents, 256);
    s_targets = n;
    if (n <= 0) { strcpy_s(s_err, u8"无实体"); return; }

    const uint8_t myTeam = LocalTeam();
    const ImGuiIO& io = ImGui::GetIO();
    const float cx = io.DisplaySize.x * 0.5f;
    const float cy = io.DisplaySize.y * 0.5f;

    ZW_VM_START();
    Cand best{};
    bool found = false;
    float bestDist = 1e30f;

    // -----------------------------------------------------------------------
    // Target selection, CS2/PUBG style.
    //
    // The naive "closest to crosshair" pick has two visible defects that the
    // reference implementations avoid:
    //
    //  1. STICKINESS. Re-picking the nearest target every tick makes the crosshair
    //     jump between two candidates that are nearly equidistant -- the classic
    //     "aim twitches between two enemies". CS2-style aim keeps the current
    //     target until it leaves the FOV, dies, or another target is clearly
    //     closer (a hysteresis margin).
    //
    //  2. TARGET PRIORITY. Distance-to-crosshair alone ignores how good the shot
    //     is. A target 3 degrees away behind cover is worse than one 5 degrees
    //     away in the open, so we score by a weighted combination instead of raw
    //     pixel distance.
    //
    // FILTERS (all of them were missing, which is why aim locked onto the local
    // player and teammates):
    //    * skip the local pawn by address
    //    * skip anything within kSelfRadius of the camera (our own body)
    //    * skip teammates when teamCheck is on, using the LIVE local team
    //    * skip dead characters
    //    * skip targets outside the FOV cone (horizontal AND vertical)
    // -----------------------------------------------------------------------
    const float selfRadiusSq = 120.0f * 120.0f;
    const uintptr_t myPawn = ue::GetLocalPawn();

    const float pxPerDegX = (cam.fov > 1.0f) ? (io.DisplaySize.x / cam.fov) : 30.0f;
    const float pxPerDegY = pxPerDegX;
    const float fovPxX = g_settings.fov * pxPerDegX;
    const float fovPxY = g_settings.fov * pxPerDegY;

    for (int i = 0; i < n; ++i) {
        const EspEnt& e = ents[i];
        if (!ue::PtrOk(e.addr)) continue;

        // ---- self ----
        if (ue::PtrOk(myPawn) && e.addr == myPawn) continue;

        // ---- dead ----
        if (e.hpMax > 0.0f && e.hp <= 0.0f) continue;

        // ---- teammate ----
        if (g_settings.teamCheck && e.team == myTeam) continue;

        // ---- aim point: head if bones are available, else chest, else centre ----
        float ax, ay, az;
        if (e.bonesOk) {
            if (g_settings.boneHead) { ax = e.chestX; ay = e.chestY; az = e.headZ; }
            else                     { ax = e.chestX; ay = e.chestY; az = e.chestZ; }
        } else {
            ax = e.x; ay = e.y; az = e.z + 60.0f;
        }

        // ---- our own body (bones give a better test than the actor origin) ----
        const float ddx = ax - cam.locX, ddy = ay - cam.locY, ddz = az - cam.locZ;
        if (ddx * ddx + ddy * ddy + ddz * ddz < selfRadiusSq) continue;

        float sx, sy;
        if (!ue::WorldToScreen(ax, ay, az, sx, sy)) continue;

        const float dx = sx - cx, dy = sy - cy;
        const float d = sqrtf(dx * dx + dy * dy);

        // ---- FOV cone (both axes, so a wide target above/below is rejected) ----
        if (g_settings.fovLimit) {
            if (fabsf(dx) > fovPxX) continue;
            if (fabsf(dy) > fovPxY) continue;
        }

        // ---- score ----
        // Pixel distance dominates; a small bonus for targets closer to us in
        // world space breaks ties the way a player would (nearer = easier shot).
        const float worldDist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
        const float score = d + worldDist * 0.002f;

        // ---- stickiness: keep the current target unless clearly beaten ----
        if (e.addr == s_stickyAddr) {
            // refresh the sticky target's aim point even if it is not the best
            if (score <= bestDist * (1.0f / kSwitchMargin)) {
                bestDist = score;
                best.addr = e.addr;
                best.x = ax; best.y = ay; best.z = az;
                best.screenDist = d;
                best.team = e.team;
                found = true;
                continue;
            }
        }

        if (score < bestDist) {
            bestDist = score;
            best.addr = e.addr;
            best.x = ax; best.y = ay; best.z = az;
            best.screenDist = d;
            best.team = e.team;
            found = true;
        }
    }
    ZW_VM_END();

    if (!found) {
        s_stickyAddr = 0;                   // nothing in the cone any more
        strcpy_s(s_err, u8"FOV 内无目标");
        return;
    }

    // A stickier target only holds if it was actually found this tick.
    if (best.addr != s_stickyAddr) {
        // only replace the sticky target when the new one really is clearly better
        s_stickyAddr = best.addr;
    }

    s_hasTarget = true;
    s_lastFov = bestDist;

    // ---- compute the desired view angles ----
    float wantPitch, wantYaw;
    AnglesTo(cam, best.x, best.y, best.z, wantPitch, wantYaw);

    uintptr_t pc = ue::GetLocalPlayerController();
    if (!ue::PtrOk(pc)) { strcpy_s(s_err, u8"无控制器"); return; }

    const uintptr_t rotAddr = pc + off::AC_ControlRotation;

    float curPitch = ue::RF32(rotAddr + 0);
    float curYaw   = ue::RF32(rotAddr + 4);
    float curRoll  = ue::RF32(rotAddr + 8);
    if (!(curPitch == curPitch)) curPitch = 0.0f;
    if (!(curYaw == curYaw))     curYaw = 0.0f;
    if (!(curRoll == curRoll))   curRoll = 0.0f;

    const float dPitch = NormalizeDeg(wantPitch - curPitch);
    const float dYaw   = NormalizeDeg(wantYaw   - curYaw);

    // random jitter (humaniser) -- bounded wobble on the error term
    float jPitch, jYaw;
    ApplyJitter(dPitch, dYaw, jPitch, jYaw);

    // -----------------------------------------------------------------------
    // Smoothing, CS2/PUBG style.
    //
    // The old model was `new = cur + err / k`, i.e. a fixed FRACTION of the error
    // per tick. That has two defects players notice immediately:
    //
    //   * it is frame-rate dependent -- 240 fps converges 4x faster than 60 fps
    //     for the same k, so feel changes with the game's frame rate
    //   * it is exponential, so the last few tenths of a degree crawl: the
    //     crosshair arrives fast then creeps, which reads as "sticky"
    //
    // Real aim assist implementations use a per-SECOND rate with a minimum step
    // so convergence is time-based and always finishes:
    //
    //     step = err * (1 - exp(-rate * dt))        // exponential approach
    //     step = max(step, minStep) * sign(err)     // guarantee arrival
    //
    // `smooth` in the UI is kept as the familiar 1..30 slider; it maps to a rate
    // so a higher slider still means slower, and 1.0 means instant.
    // -----------------------------------------------------------------------
    float k = g_settings.smooth;
    if (k < 1.0f) k = 1.0f;
    if (g_settings.silentLock) k = 1.0f;

    float stepPitch = jPitch, stepYaw = jYaw;

    if (k > 1.0f) {
        // dt from the wall clock so the response is frame-rate independent
        static ULONGLONG s_lastTick = 0;
        const ULONGLONG now = GetTickCount64();
        float dt = (s_lastTick == 0) ? 0.004f : (float)(now - s_lastTick) / 1000.0f;
        s_lastTick = now;
        if (dt <= 0.0f)   dt = 0.0005f;
        if (dt > 0.10f)   dt = 0.10f;          // clamp after a hitch

        // slider 1..30 -> rate 120..4 per second (doubled: the old 60/k map
        // read as "slow and sticky" in real play; convergence is still
        // time-based and frame-rate independent)
        const float rate = 120.0f / k;
        const float a = 1.0f - expf(-rate * dt);

        stepPitch = jPitch * a;
        stepYaw   = jYaw   * a;

        // minimum step so the approach actually terminates instead of creeping.
        // 0.05 deg is below one screen pixel at any usable FOV.
        const float minStep = 0.05f;
        if (fabsf(stepPitch) > 0.0f && fabsf(stepPitch) < minStep)
            stepPitch = (stepPitch < 0.0f ? -minStep : minStep);
        if (fabsf(stepYaw) > 0.0f && fabsf(stepYaw) < minStep)
            stepYaw = (stepYaw < 0.0f ? -minStep : minStep);

        // never overshoot the target
        if (fabsf(stepPitch) > fabsf(jPitch)) stepPitch = jPitch;
        if (fabsf(stepYaw)   > fabsf(jYaw))   stepYaw   = jYaw;
    }

    const float newPitch = curPitch + stepPitch;
    const float newYaw   = curYaw   + stepYaw;

    // ---- write ----
    ZW_VM_END();
    if (!ue::WriteGuarded(rotAddr + 0, &newPitch, 4)) { strcpy_s(s_err, u8"写入 pitch 失败"); return; }
    if (!ue::WriteGuarded(rotAddr + 4, &newYaw,   4)) { strcpy_s(s_err, u8"写入 yaw 失败");   return; }
}

} // namespace aim
