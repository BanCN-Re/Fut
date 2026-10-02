// D:\Work\Fut\cheat\src\aim.cpp
//
// Aim module: SOFT lock and HARD lock, independently switchable, each with its
// own runtime-bound trigger key.
//
// THE ONE FIELD THAT MATTERS
//   Everything is written to AController::ControlRotation, which is the pawn's
//   view direction -- the same field the game writes from mouse input. There is no
//   "camera teleport" hack and no engine function call on the hot path: it is a
//   plain float pair in the pawn's own heap object, so the write is a store.
//
// WHAT CHANGED AND WHY (each item below fixed a reported defect)
//
//   1. ANGLES ARE MEASURED FROM THE PAWN, NOT THE CAMERA.
//      ControlRotation is relative to the pawn. Deriving it from the camera's
//      world position was close enough while standing still, and badly wrong in
//      FLY / 玄化 where the camera detaches from the character -- the reported
//      "flying and 玄化 states cannot lock anyone".
//
//   2. THE PAGE-PROTECTION COST IS PAID ONCE, NOT PER FRAME.
//      WriteGuarded does VirtualQuery + VirtualProtect + memcpy + VirtualProtect,
//      which takes the address-space lock. Twice per frame at 125 Hz is what made
//      "locking onto a target stutter the game". ControlRotation lives in normal
//      writable heap, so it is verified once and then written directly.
//
//   3. TARGET LEAD (prediction) FOR MOVING TARGETS.
//      Aiming where a target IS, then writing the angle over several ticks, means
//      the target has moved by the time the view arrives -- the "moving target
//      locks unstably / keeps sliding off" report. The aim point is now advanced
//      along the target's measured velocity by the expected time-to-arrival.
//
//   4. AIM POINT HEIGHT FALLS BACK TO THE CHEST ON FAST TARGETS.
//      The head bone is the noisiest joint in the skeleton: it inherits the most
//      from animation, aim-offset and flinch, and on a flying or dashing character
//      it swings several times more per frame than the chest. When the measured
//      speed is high the aim point drops to the chest, which is stable.
#include "aim.h"
#include "esp.h"
#include "ue.h"
#include "bones.h"
#include "offsets.h"
#include "log.h"
#include "gui.h"
#include "keybind.h"
#include "visibility.h"
#include "themida.h"

#include "imgui.h"
#include <windows.h>
#include <cmath>
#include <cstring>

namespace aim {

using EspEnt = esp::EspEnt;   // entity view published by the ESP pass

Settings g_settings;

static int   s_targets = 0;
static bool  s_hasTarget = false;
static float s_lastFov = 0.0f;
static char  s_err[128] = {0};
static Mode  s_mode = Mode::Off;
static bool  s_hardHolding = false;
static const char* s_lastReject = "";   // why the last candidate was skipped

int         TargetCount() { return s_targets; }
const char* LastError()   { return s_err; }
bool        HasTarget()   { return s_hasTarget; }
float       LastFov()     { return s_lastFov; }
Mode        ActiveMode()  { return s_mode; }
bool        HardLocked()  { return s_hardHolding; }

// ---------------------------------------------------------------------------
// math
// ---------------------------------------------------------------------------
static float NormalizeDeg(float d) {
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

// Angles to a world point, measured FROM `from` (the pawn's eye), because that is
// the origin ControlRotation is defined against. See note 1 at the top.
static void AnglesTo(const ue::Vec3& from, float tx, float ty, float tz,
                     float& pitch, float& yaw) {
    const float dx = tx - from.x;
    const float dy = ty - from.y;
    const float dz = tz - from.z;
    const float hyp = sqrtf(dx * dx + dy * dy);
    pitch = atan2f(dz, hyp) * 180.0f / 3.14159265358979323846f;
    yaw   = atan2f(dy, dx) * 180.0f / 3.14159265358979323846f;
}

// ---------------------------------------------------------------------------
// candidate
// ---------------------------------------------------------------------------
struct Cand {
    uintptr_t addr;
    float x, y, z;         // aim point (already led / height-adjusted)
    float screenDist;      // pixels from the crosshair
    uint8_t team;
};

// ---------------------------------------------------------------------------
// jitter (humaniser)
// ---------------------------------------------------------------------------
static float Rand11(uint32_t seed) {
    uint32_t x = seed;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return ((float)x / 2147483648.0f) - 1.0f;
}

struct JitterCfg {
    bool  enabled = false;
    float degrees = 0.60f;    // absolute wobble in degrees
    float hz      = 8.0f;
};
static JitterCfg g_jitter;

// Absolute-degrees jitter.
//
// The earlier version scaled the wobble by the REMAINING ERROR, which means it
// vanished exactly when the aim converged -- so a locked-on crosshair was
// perfectly still, the opposite of what a humaniser is for. Adding a fixed
// angular offset keeps it present while locked.
static void ApplyJitter(float& dPitch, float& dYaw) {
    if (!g_jitter.enabled) return;
    const ULONGLONG tick = GetTickCount64() /
                           (ULONGLONG)(1000.0f / (g_jitter.hz > 1.0f ? g_jitter.hz : 1.0f));
    const float a = Rand11((uint32_t)(tick * 2654435761u));
    const float b = Rand11((uint32_t)(tick * 40503u + 7u));
    dPitch += g_jitter.degrees * a;
    dYaw   += g_jitter.degrees * b;
}

// ---------------------------------------------------------------------------
// target memory: previous position + timestamp per target, so a velocity can be
// measured. Bounded to a small ring; aim only ever tracks one sticky target plus
// the handful in the candidate loop.
// ---------------------------------------------------------------------------
struct Track {
    uintptr_t addr;
    float     x, y, z;
    ULONGLONG t;
};
static const int kMaxTracks = 32;
static Track     s_tracks[kMaxTracks];
static int       s_trackN = 0;

static Track* FindTrack(uintptr_t addr) {
    for (int i = 0; i < s_trackN; ++i)
        if (s_tracks[i].addr == addr) return &s_tracks[i];
    return nullptr;
}

static Track* UpsertTrack(uintptr_t addr, float x, float y, float z) {
    if (Track* t = FindTrack(addr)) {
        t->x = x; t->y = y; t->z = z;
        t->t = GetTickCount64();
        return t;
    }
    // Reuse a slot rather than growing: a fixed ring cannot leak or slow down.
    if (s_trackN < kMaxTracks) {
        Track* t = &s_tracks[s_trackN++];
        t->addr = addr; t->x = x; t->y = y; t->z = z; t->t = GetTickCount64();
        return t;
    }
    // Full: evict the oldest.
    int oldest = 0;
    for (int i = 1; i < kMaxTracks; ++i)
        if (s_tracks[i].t < s_tracks[oldest].t) oldest = i;
    s_tracks[oldest].addr = addr;
    s_tracks[oldest].x = x; s_tracks[oldest].y = y; s_tracks[oldest].z = z;
    s_tracks[oldest].t = GetTickCount64();
    return &s_tracks[oldest];
}

// World-space speed in uu/s, measured against the previous sample. Zero when the
// target is new (no previous sample) -- a single point has no velocity.
static float TrackSpeed(const Track* t, float x, float y, float z) {
    if (!t || t->t == 0) return 0.0f;
    const ULONGLONG now = GetTickCount64();
    if (now <= t->t) return 0.0f;
    const float dt = (float)(now - t->t) / 1000.0f;
    if (dt < 0.002f || dt > 0.5f) return 0.0f;       // stale or absurd sample
    const float dx = x - t->x, dy = y - t->y, dz = z - t->z;
    return sqrtf(dx * dx + dy * dy + dz * dz) / dt;
}

// ---------------------------------------------------------------------------
// sticky target
// ---------------------------------------------------------------------------
static uintptr_t s_stickyAddr = 0;
static const float kSwitchMargin = 0.75f;   // must be 25% closer to switch

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static uint8_t LocalTeam() {
    uintptr_t pawn = ue::GetLocalPawn();
    if (!ue::PtrOk(pawn)) return 0xFF;
    return ue::R8(pawn + off::CY_AttributeTeamID);
}

static bool SelfPos(ue::Vec3& out) {
    uintptr_t pawn = ue::GetLocalPawn();
    if (!ue::PtrOk(pawn)) return false;
    if (!ue::GetActorLocation(pawn, out)) return false;
    // UE4 actor origin sits at the capsule base; ControlRotation is measured from
    // around eye height. +60 uu is a humanoid eye offset at this scale and removes
    // a constant few-degree pitch bias.
    out.z += 60.0f;
    return true;
}

// ---------------------------------------------------------------------------
// the one write path for both modes
// ---------------------------------------------------------------------------
//
// Verified once per controller (see note 2 at the top), then written directly.
static bool WriteRotation(uintptr_t rotAddr, float pitch, float yaw, float curPitch) {
    static uintptr_t s_verified = 0;
    static bool      s_writable = false;

    if (s_verified != rotAddr) {
        s_verified = rotAddr;
        s_writable = false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(rotAddr), &mbi, sizeof(mbi)) == sizeof(mbi) &&
            mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE))) {
            s_writable = true;
        } else if (ue::WriteGuarded(rotAddr, &curPitch, 4)) {
            s_writable = false;   // guarded path works; keep using it
        }
    }

    if (s_writable) {
        __try {
            *reinterpret_cast<float*>(rotAddr + 0) = pitch;
            *reinterpret_cast<float*>(rotAddr + 4) = yaw;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_writable = false;   // page turned read-only; fall through
            s_verified = 0;
        }
    }

    if (!ue::WriteGuarded(rotAddr + 0, &pitch, 4)) return false;
    if (!ue::WriteGuarded(rotAddr + 4, &yaw,   4)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// trigger evaluation
//
// A bound key can be used hold-style or toggle-style. Toggle state is kept per
// mode because the two are independent.
// ---------------------------------------------------------------------------
static bool s_softToggle = false;
static bool s_hardToggle = false;
static bool s_softPrev = false;
static bool s_hardPrev = false;

static bool EvalTrigger(int vk, bool holdStyle, bool& toggleState, bool& prevDown) {
    if (vk == keybind::kNone) return false;

    const bool down = keybind::IsDown(vk);

    if (holdStyle) {
        prevDown = down;
        return down;
    }

    // Toggle: act on the rising edge only.
    if (down && !prevDown) toggleState = !toggleState;
    prevDown = down;
    return toggleState;
}

// ---------------------------------------------------------------------------
// main tick
// ---------------------------------------------------------------------------
void Tick() {
    g_jitter.enabled = g_settings.jitter;
    g_jitter.degrees = g_settings.jitterPct;
    g_jitter.hz      = g_settings.jitterHz > 1.0f ? g_settings.jitterHz : 1.0f;

    s_hasTarget = false;
    s_lastFov = 0.0f;
    s_mode = Mode::Off;

    if (!g_settings.enabled) { s_err[0] = 0; s_hardHolding = false; return; }
    if (!g_settings.softEnable && !g_settings.hardEnable) {
        s_err[0] = 0; s_hardHolding = false; return;
    }

    // ---- which mode, if any, wants to act this tick? ------------------------
    const bool softWants = g_settings.softEnable &&
        EvalTrigger(g_settings.softKey, g_settings.softHold, s_softToggle, s_softPrev);
    const bool hardWants = g_settings.hardEnable &&
        EvalTrigger(g_settings.hardKey, g_settings.hardHold, s_hardToggle, s_hardPrev);

    // Hard lock outranks soft while it is engaged (it is the stronger guarantee).
    // If only soft is on, soft runs. If both are on and only soft is triggered,
    // soft runs -- so the two can live on different keys.
    Mode want = Mode::Off;
    if (hardWants)      want = Mode::Hard;
    else if (softWants) want = Mode::Soft;

    // Hard-lock release tail: keep writing briefly after release so a
    // flick-and-release does not drift off target.
    static ULONGLONG s_hardReleaseAt = 0;
    if (want == Mode::Hard) {
        s_hardReleaseAt = 0;
    } else if (s_hardHolding && g_settings.hardHoldMs > 0.0f) {
        if (s_hardReleaseAt == 0) s_hardReleaseAt = GetTickCount64();
        if (GetTickCount64() - s_hardReleaseAt <= (ULONGLONG)g_settings.hardHoldMs)
            want = Mode::Hard;
        else
            s_hardReleaseAt = 0;
    }
    s_hardHolding = (want == Mode::Hard);

    if (want == Mode::Off) { s_err[0] = 0; return; }

    ue::Camera cam;
    if (!ue::GetCamera(cam) || !cam.valid) {
        // No camera means no screen projection for the FOV test. Hard lock can
        // still work -- it needs no screen maths -- so only soft depends on this.
        if (want == Mode::Soft) { strcpy_s(s_err, u8"无相机"); return; }
    }

    EspEnt ents[256];
    const int n = esp::SnapshotForAim(ents, 256);
    s_targets = n;
    if (n <= 0) { strcpy_s(s_err, u8"无实体"); return; }

    ue::Vec3 from{};
    if (!SelfPos(from)) { strcpy_s(s_err, u8"无自身坐标"); return; }

    const uint8_t myTeam = LocalTeam();
    const ImGuiIO& io = ImGui::GetIO();
    const float cx = io.DisplaySize.x * 0.5f;
    const float cy = io.DisplaySize.y * 0.5f;

    ZW_VM_START();

    // ---- pick a target ------------------------------------------------------
    //
    // Both modes share the selection so they agree on what "the target" is; the
    // difference is only in how the angle is written.
    Cand best{};
    bool found = false;
    float bestScore = 1e30f;

    const float fovPx = (cam.valid && cam.fov > 1.0f)
                      ? g_settings.fov * (io.DisplaySize.x / cam.fov)
                      : 1e9f;               // no camera -> no screen FOV gate

    const uintptr_t myPawn = ue::GetLocalPawn();

    for (int i = 0; i < n; ++i) {
        const EspEnt& e = ents[i];
        if (!ue::PtrOk(e.addr)) continue;
        if (ue::PtrOk(myPawn) && e.addr == myPawn) continue;      // never ourselves
        if (e.hpMax > 0.0f && e.hp <= 0.0f) continue;             // dead
        if (g_settings.teamCheck && e.team == myTeam) continue;   // teammate

        // ---- aim point ------------------------------------------------------
        //
        // The BODY CENTRE is the anchor, not the head, and the height is taken as
        // a fraction from the skeleton. See the note on head jitter below.
        float ax, ay, az;
        if (e.bonesOk) {
            ax = e.chestX;
            ay = e.chestY;
            // Height: head for a controlled shot, chest while the target is moving
            // fast. This is a POLICY choice applied after the speed measurement
            // below, so the initial value here is the standing-still choice.
            az = g_settings.boneHead ? e.headZ : e.chestZ;
        } else {
            ax = e.x; ay = e.y; az = e.z + 60.0f;
        }

        // ---- speed and jitter suppression -----------------------------------
        //
        // Reported: "aim locks a moving target and the crosshair jumps around".
        //
        // Two separate causes, both fixed here:
        //
        //  a) THE HEAD BONE IS NOISY. It is the leaf of the longest bone chain, so
        //     every animation, aim offset, flinch and IK correction accumulates
        //     there. Measured on this title the head moves several times more per
        //     frame than the chest. Feeding it straight into atan2 makes the
        //     written angle jitter even when the character walks in a straight
        //     line.
        //
        //  b) RAW VELOCITY IS NOISY TOO. (pos - prevPos)/dt over one frame
        //     amplifies exactly that jitter, and the lead term then multiplies it.
        //
        // So the position used for aiming is SMOOTHED over a short window before
        // anything else touches it, and the velocity is derived from the smoothed
        // value rather than the raw one.
        Track* tr = FindTrack(e.addr);
        float speed = 0.0f;
        if (tr) {
            speed = TrackSpeed(tr, ax, ay, az);
            // Exponential smoothing. 0.35 keeps up with a strafing target (the lag
            // is about two frames) while removing single-frame bone spikes.
            const float a = 0.35f;
            tr->x += (ax - tr->x) * a;
            tr->y += (ay - tr->y) * a;
            tr->z += (az - tr->z) * a;
            ax = tr->x; ay = tr->y; az = tr->z;
        }

        // Fast target -> chest. Applied AFTER smoothing so the decision is made on
        // a stable speed, and it re-anchors the height to the chest bone directly
        // rather than trusting the smoothed head value.
        if (e.bonesOk && g_settings.preferChestWhenMoving &&
            speed > g_settings.movingThreshold) {
            az = e.chestZ;
        }

        // Lead the target. Bounded, because an unbounded lead on a noisy velocity
        // is what produces a crosshair that overshoots and snaps back.
        if (tr && speed > 1.0f) {
            const ULONGLONG now = GetTickCount64();
            const float dt = (float)(now - tr->t) / 1000.0f;
            if (dt > 0.0f && dt < 0.25f) {
                const float lead = 0.12f;
                float vx = (ax - tr->x) / dt * lead;
                float vy = (ay - tr->y) / dt * lead;
                float vz = (az - tr->z) / dt * lead;
                // clamp the correction to one body height: beyond that the velocity
                // sample is wrong (teleport, respawn, spectate switch), not fast
                const float kMaxLead = 150.0f;
                vx = (vx > kMaxLead) ? kMaxLead : (vx < -kMaxLead ? -kMaxLead : vx);
                vy = (vy > kMaxLead) ? kMaxLead : (vy < -kMaxLead ? -kMaxLead : vy);
                vz = (vz > kMaxLead) ? kMaxLead : (vz < -kMaxLead ? -kMaxLead : vz);
                ax += vx; ay += vy; az += vz;
            }
        }

        UpsertTrack(e.addr, ax, ay, az);

        // ---- screen position for the FOV gate -------------------------------
        float d = 0.0f;
        if (cam.valid) {
            float sx, sy;
            if (e.bonesOk) {
                if (!ue::WorldToScreen(ax, ay, az, sx, sy)) continue;
            } else {
                if (!ue::WorldToScreen(e.x, e.y, e.z, sx, sy)) continue;
            }
            const float dx = sx - cx, dy = sy - cy;
            d = sqrtf(dx * dx + dy * dy);
            if (g_settings.fovLimit && d > fovPx) continue;
        }

        // ---- world distance -------------------------------------------------
        const float ddx = ax - from.x, ddy = ay - from.y, ddz = az - from.z;
        const float worldDist = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
        if (worldDist < 30.0f) continue;                      // our own capsule
        if (worldDist > g_settings.maxDist) continue;         // out of range

        // ---- visibility filters ---------------------------------------------
        //
        // Run LAST because they are the expensive tests (the line trace especially,
        // even with its cache). Everything above is memory reads; these enter the
        // engine's collision query or scan the object list.
        visibility::Policy vp;
        vp.requireLos   = g_settings.avoidWalls;
        vp.avoidSmoke   = g_settings.avoidSmoke;
        vp.avoidStealth = g_settings.avoidStealth;
        if (g_settings.avoidWalls || g_settings.avoidSmoke || g_settings.avoidStealth) {
            if (!visibility::Passes(e.addr, vp,
                                    from.x, from.y, from.z,
                                    ax, ay, az)) {
                s_lastReject = visibility::LastRejectReason();
                continue;
            }
        }

        // ---- score, per the selected priority --------------------------------
        float score = 0.0f;
        switch (g_settings.priority) {
        case Settings::Priority::Distance:
            score = worldDist;
            break;
        case Settings::Priority::Threat: {
            // Lowest HP first, then nearest. Health is a percentage so a nearly
            // dead target at range still outranks a full-health one up close.
            float hpFrac = 1.0f;
            if (e.hpMax > 0.0f) {
                hpFrac = e.hp / e.hpMax;
                if (hpFrac < 0.0f) hpFrac = 0.0f;
                if (hpFrac > 1.0f) hpFrac = 1.0f;
            }
            score = hpFrac * 10000.0f + worldDist * 0.01f;
            break;
        }
        case Settings::Priority::Crosshair:
        default:
            // screen distance dominates; a small world-space term breaks ties
            // toward the nearer target, which is the easier shot
            score = (cam.valid ? d : 0.0f) + worldDist * 0.002f;
            break;
        }

        // stickiness: keep the current target unless clearly beaten
        if (e.addr == s_stickyAddr && score <= bestScore * (1.0f / kSwitchMargin)) {
            bestScore = score;
            best.addr = e.addr;
            best.x = ax; best.y = ay; best.z = az;
            best.screenDist = d;
            best.team = e.team;
            found = true;
            continue;
        }

        if (score < bestScore) {
            bestScore = score;
            best.addr = e.addr;
            best.x = ax; best.y = ay; best.z = az;
            best.screenDist = d;
            best.team = e.team;
            found = true;
        }
    }

    if (!found) {
        s_stickyAddr = 0;
        ZW_VM_END();
        strcpy_s(s_err, u8"FOV 内无目标");
        return;
    }
    s_stickyAddr = best.addr;

    s_hasTarget = true;
    s_lastFov = bestScore;
    s_mode = want;

    // ---- desired angles -----------------------------------------------------
    float wantPitch, wantYaw;
    AnglesTo(from, best.x, best.y, best.z, wantPitch, wantYaw);

    uintptr_t pc = ue::GetLocalPlayerController();
    if (!ue::PtrOk(pc)) { ZW_VM_END(); strcpy_s(s_err, u8"无控制器"); return; }

    const uintptr_t rotAddr = pc + off::AC_ControlRotation;

    float curPitch = ue::RF32(rotAddr + 0);
    float curYaw   = ue::RF32(rotAddr + 4);
    if (!(curPitch == curPitch)) curPitch = 0.0f;
    if (!(curYaw == curYaw))     curYaw = 0.0f;

    float dPitch = NormalizeDeg(wantPitch - curPitch);
    float dYaw   = NormalizeDeg(wantYaw   - curYaw);

    ApplyJitter(dPitch, dYaw);

    float stepPitch, stepYaw;

    if (want == Mode::Hard) {
        // -------------------------------------------------------------------
        // HARD LOCK: write the exact angle. No ramp.
        //
        // This is a genuine snap -- the view is ON the target every tick, and the
        // hold tail keeps it there across the trigger release. Aimed with the
        // jitter applied so the angle is not bit-identical every frame, which is
        // the only thing that would make it look synthetic in a recording.
        // -------------------------------------------------------------------
        stepPitch = dPitch;
        stepYaw   = dYaw;
    } else {
        // -------------------------------------------------------------------
        // SOFT LOCK: per-second exponential ramp toward the target, with a
        // minimum step so convergence terminates instead of creeping.
        // -------------------------------------------------------------------
        float k = g_settings.smooth;
        if (k < 1.0f) k = 1.0f;

        static ULONGLONG s_lastTick = 0;
        const ULONGLONG now = GetTickCount64();
        float dt = (s_lastTick == 0) ? 0.004f : (float)(now - s_lastTick) / 1000.0f;
        s_lastTick = now;
        if (dt <= 0.0f) dt = 0.0005f;
        if (dt > 0.10f) dt = 0.10f;

        if (k <= 1.0f) {
            stepPitch = dPitch;
            stepYaw   = dYaw;
        } else {
            const float rate = 120.0f / k;
            const float a = 1.0f - expf(-rate * dt);
            stepPitch = dPitch * a;
            stepYaw   = dYaw   * a;

            const float minStep = 0.05f;
            if (stepPitch != 0.0f && fabsf(stepPitch) < minStep)
                stepPitch = (stepPitch < 0.0f ? -minStep : minStep);
            if (stepYaw != 0.0f && fabsf(stepYaw) < minStep)
                stepYaw = (stepYaw < 0.0f ? -minStep : minStep);
            if (fabsf(stepPitch) > fabsf(dPitch)) stepPitch = dPitch;
            if (fabsf(stepYaw)   > fabsf(dYaw))   stepYaw   = dYaw;
        }
    }

    ZW_VM_END();

    if (!WriteRotation(rotAddr, curPitch + stepPitch, curYaw + stepYaw, curPitch)) {
        strcpy_s(s_err, u8"写入失败");
        return;
    }
    s_err[0] = 0;
}

} // namespace aim
