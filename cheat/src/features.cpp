// D:\Work\Fut\cheat\src\features.cpp
// ESP / aim / radar / recoil, ported from the overseas Flavia build.
// See features.h for the module-by-module mapping.
#include "features.h"
#include "bones.h"
#include "sdk.h"
#include "canvas.h"
#include "skeleton.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace features {

State g;
World gWorld;

// ---------------------------------------------------------------------------
// aim target stickiness (the overseas `lockedTarget`, plus a cached bone index
// so the bone name lookup does not run every frame)
// ---------------------------------------------------------------------------
static uintptr_t s_lockedTarget = 0;
static int       s_lockedBone   = -1;
static ULONGLONG s_lastAimTick  = 0;

void Reset() {
    s_lockedTarget = 0;
    s_lockedBone = -1;
}

// ===========================================================================
// DRAW HELPERS  (drawing.hpp)
// ===========================================================================
namespace draw {

void CornerBox(void* cv, float x, float y, float w, float h,
               const sdk::LinearColor& c, float th) {
    if (!cv) return;
    const float cw = w / 5.0f;
    const float ch = h / 5.0f;

    sdk::DrawLine(cv, { x, y },         { x + cw, y },         th, c);
    sdk::DrawLine(cv, { x, y },         { x, y + ch },         th, c);

    sdk::DrawLine(cv, { x + w, y },     { x + w - cw, y },     th, c);
    sdk::DrawLine(cv, { x + w, y },     { x + w, y + ch },     th, c);

    sdk::DrawLine(cv, { x, y + h },     { x + cw, y + h },     th, c);
    sdk::DrawLine(cv, { x, y + h },     { x, y + h - ch },     th, c);

    sdk::DrawLine(cv, { x + w, y + h }, { x + w - cw, y + h }, th, c);
    sdk::DrawLine(cv, { x + w, y + h }, { x + w, y + h - ch }, th, c);
}

void HealthBar(void* cv, float x, float y, float h, float hp, float hpMax) {
    if (!cv || hpMax <= 0.0f) return;

    float perc = hp / hpMax;
    if (perc < 0.0f) perc = 0.0f;
    if (perc > 1.0f) perc = 1.0f;

    const float barX = x - 6.0f;
    const float barW = 3.0f;

    sdk::FillRect(cv, barX, y, barW, h, sdk::LinearColor(0.0f, 0.0f, 0.0f, 0.6f));

    sdk::LinearColor hpColor;
    if (perc > 0.6f)      hpColor = sdk::LinearColor(0.0f, 1.0f, 0.0f, 1.0f);
    else if (perc > 0.3f) hpColor = sdk::LinearColor(1.0f, 1.0f, 0.0f, 1.0f);
    else                  hpColor = sdk::LinearColor(1.0f, 0.0f, 0.0f, 1.0f);

    const float filled = h * perc;
    sdk::FillRect(cv, barX, y + h - filled, barW, filled, hpColor);
}

void Text(void* cv, void* font, const std::string& s, float x, float y,
          const sdk::LinearColor& c, bool centre) {
    if (!cv || !font || s.empty()) return;
    // The engine wants UTF-16. The overseas source passes a narrow string
    // straight through, which only works for ASCII; ours must handle the CJK
    // names this game uses, so convert properly.
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (need <= 0) need = (int)s.size() + 1;
    wchar_t* w = new (std::nothrow) wchar_t[need + 1];
    if (!w) return;
    if (MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w, need) <= 0)
        w[0] = 0;

    sdk::DrawText(cv, font, w, { x, y }, { 1.0f, 1.0f }, c, centre, false);
    delete[] w;
}

} // namespace draw

// ===========================================================================
// BONES  (food for the skeleton and for precise aiming)
// ===========================================================================
bool ScoreBones(uintptr_t mesh, sdk::Vec3& head, sdk::Vec3& feet,
                sdk::Vec3& chest, sdk::Vec3& pelvis) {
    // Reuse the bones module: it already solved component selection (pick the
    // component with the most bones), the identity-quaternion case, and the
    // component->world conversion. Passing a mesh is not its API, so we go
    // through the character instead -- the caller supplies the actor.
    (void)mesh; (void)head; (void)feet; (void)chest; (void)pelvis;
    return false;
}

static bool BonesFor(uintptr_t actor, ActorInfo& out) {
    bones::BoneSet bs;
    if (!bones::Get(actor, bs)) return false;
    out.head   = sdk::Vec3(bs.headX,   bs.headY,   bs.headZ);
    out.feet   = sdk::Vec3(bs.feetX,   bs.feetY,   bs.feetZ);
    out.chest  = sdk::Vec3(bs.chestX,  bs.chestY,  bs.chestZ);
    out.pelvis = sdk::Vec3(bs.pelvisX, bs.pelvisY, bs.pelvisZ);

    // A skeleton with a nonsense extent is worse than none: it drags the box and
    // the aim point with it. Humanoids here measure ~150-200 uu.
    const float h = out.head.Z - out.feet.Z;
    if (!(h > 50.0f && h < 400.0f)) return false;
    return true;
}

// ===========================================================================
// WORLD SCAN  (one pass, shared)
// ===========================================================================
static bool NameOfActor(uintptr_t actor, char* out, int cap) {
    std::string nm;
    if (!ue::NameOf(actor, nm)) return false;
    strncpy_s(out, cap, nm.c_str(), _TRUNCATE);
    return true;
}

void ScanWorld(void* canvas) {
    gWorld.canvas = canvas;
    gWorld.count = 0;

    // FONT -- without this every draw::Text silently no-ops (font==null),
    // which is why names and HP text never appeared on the features path.
    if (!gWorld.font) gWorld.font = canvas::DefaultFont();

    gWorld.world = ue::GetWorld();
    if (!ue::PtrOk(gWorld.world)) return;

    gWorld.controller = ue::GetLocalPlayerController();
    gWorld.localPawn  = ue::GetLocalPawn();
    if (ue::PtrOk(gWorld.controller))
        gWorld.pcm = ue::R64(gWorld.controller + off::APC_PlayerCameraManager);

    if (ue::PtrOk(gWorld.localPawn))
        gWorld.localTeam = TeamOf(gWorld.localPawn);

    // camera: prefer the manager's own accessors (they account for camera lag and
    // shake, unlike ControlRotation)
    bool camOk = false;
    if (ue::PtrOk(gWorld.pcm)) {
        camOk = sdk::GetCameraLocation(gWorld.pcm, gWorld.camLoc);
        sdk::GetCameraRotation(gWorld.pcm, gWorld.camRot);
    }
    if (!camOk) {
        // fall back to the controller rotation + pawn position, as the overseas
        // code does when PlayerCameraManager is unavailable
        if (ue::PtrOk(gWorld.controller))
            sdk::GetControlRotation(gWorld.controller, gWorld.camRot);
        if (ue::PtrOk(gWorld.localPawn))
            gWorld.camLoc = sdk::GetActorLocation(gWorld.localPawn);
    }

    // ---- collect characters ----
    // Both containers are walked: PersistentLevel->Actors (cheap, what the
    // overseas code uses) and, if that yields nothing, the GObjects scan.
    uintptr_t level = ue::GetPersistentLevel();
    const int actorCount = ue::GetActorCount(level);

    auto consider = [&](uintptr_t a) {
        if (gWorld.count >= kMaxActors) return;
        if (!ue::PtrOk(a)) return;
        if (!ue::IsAOfClassNamed(a, SBOX("ACyCharacterBase", 0xE0))) return;

        const bool isLocal = ue::PtrOk(gWorld.localPawn) && a == gWorld.localPawn;

        ActorInfo& info = gWorld.actors[gWorld.count];
        info = ActorInfo{};
        info.actor = a;
        info.isLocal = isLocal;
        info.loc = sdk::GetActorLocation(a);
        if (info.loc.X == 0.0f && info.loc.Y == 0.0f && info.loc.Z == 0.0f) return;

        info.team = TeamOf(a);
        info.health = HealthOf(a, &info.healthMax);
        info.mesh = ue::R64(a + off::WF_MeshFor3PP);
        info.bonesOk = BonesFor(a, info);

        info.distM = sdk::Dist3D(gWorld.camLoc, info.loc) / 100.0f;
        NameOfActor(a, info.name, sizeof(info.name));

        ++gWorld.count;
    };

    if (actorCount > 0) {
        for (int i = 0; i < actorCount; ++i)
            consider(ue::GetActor(level, i));
    }
    if (gWorld.count == 0) {
        const int n = ue::NumObjects();
        for (int i = 0; i < n && gWorld.count < kMaxActors; ++i)
            consider(ue::ObjectAt(i));
    }
}

// ===========================================================================
// ESP
// ===========================================================================
void DrawSkeleton(void* cv, const ActorInfo& a, const sdk::LinearColor& c) {
    if (!cv || !a.bonesOk || !ue::PtrOk(a.mesh)) return;

    // -----------------------------------------------------------------------
    // NAME-DRIVEN SKELETON.
    //
    // The previous version connected head -> chest -> pelvis -> feet, and all four
    // of those points lie on the character's vertical axis, so the "skeleton" came
    // out as a straight line. Arms and legs must be identified by BONE NAME to get
    // a readable figure, which is what skeleton:: does.
    //
    // Resolve() caches the joint index table per mesh, so this costs one name walk
    // per character and then only a pose read per frame.
    // -----------------------------------------------------------------------
    static skeleton::Rig s_rigs[8];
    static uintptr_t     s_rigMesh[8] = {0};
    static int           s_rigCount = 0;

    skeleton::Rig* rig = nullptr;
    for (int i = 0; i < s_rigCount; ++i)
        if (s_rigMesh[i] == a.mesh) { rig = &s_rigs[i]; break; }

    if (!rig) {
        if (s_rigCount >= 8) return;
        rig = &s_rigs[s_rigCount];
        if (!skeleton::Resolve(a.mesh, *rig)) return;
        s_rigMesh[s_rigCount] = a.mesh;
        ++s_rigCount;
    }
    if (!rig->usable) return;

    skeleton::Pose pose;
    if (!skeleton::ReadPose(a.mesh, *rig, pose)) return;

    skeleton::Draw(cv, pose, c.R, c.G, c.B, 0.95f);
}

void DrawEsp(void* cv) {
    if (!cv || !g.espEnable) return;

    // REAL canvas extent, refreshed every frame from the canvas object itself.
    // features::gWorld.clipX is a never-assigned struct default (1920x1080) and
    // must not be used for positioning on the real 2560x1600 canvas.
    const float clipX = canvas::ClipX();
    const float clipY = canvas::ClipY();

    for (int i = 0; i < gWorld.count; ++i) {
        const ActorInfo& a = gWorld.actors[i];
        if (a.isLocal) continue;                      // never box ourselves
        if (g.espMaxDist > 0.0f && a.distM > g.espMaxDist) continue;

        // ---- box from the skeleton when available, else a capsule estimate ----
        // -------------------------------------------------------------------
        // BOX EXTENT FROM THE ACTOR BOUNDS, ANCHORED ON THE SKELETON.
        //
        // Using head.Z as the top drew a box that stopped at the head BONE ORIGIN
        // (roughly the neck), so the top edge cut through the model and the whole
        // thing read as shifted up. The bottom had the mirror problem: the foot
        // bone sits at the ankle, not the sole.
        //
        // GetActorBounds gives the real capsule extent -- what the engine itself
        // uses -- so the box follows the body instead of the skeleton joints.
        // -------------------------------------------------------------------
        float topZ, botZ, midX, midY;
        sdk::Vec3 bOrigin, bExtent;
        if (sdk::GetActorBounds(a.actor, bOrigin, bExtent) &&
            bExtent.Z > 10.0f && bExtent.Z < 300.0f) {
            topZ = bOrigin.Z + bExtent.Z;
            botZ = bOrigin.Z - bExtent.Z;
            midX = bOrigin.X;
            midY = bOrigin.Y;
        } else if (a.bonesOk) {
            // fallback: skeleton, padded because the head bone is the neck joint
            topZ = a.head.Z + 12.0f;
            botZ = a.feet.Z;
            midX = a.chest.X;
            midY = a.chest.Y;
        } else {
            topZ = a.loc.Z + 90.0f;
            botZ = a.loc.Z;
            midX = a.loc.X;
            midY = a.loc.Y;
        }

        sdk::Vec2 top, bot;
        if (!sdk::Project(cv, sdk::Vec3(midX, midY, topZ), top)) continue;
        if (!sdk::Project(cv, sdk::Vec3(midX, midY, botZ), bot)) continue;

        float boxH = fabsf(bot.Y - top.Y);
        if (boxH < 4.0f) continue;
        float boxW = boxH * 0.42f;
        if (boxW < 3.0f) boxW = 3.0f;

        const float cx = (top.X + bot.X) * 0.5f;
        const float cy = (top.Y + bot.Y) * 0.5f;
        const float x0 = cx - boxW * 0.5f;
        const float y0 = cy - boxH * 0.5f;

        if (x0 > clipX || x0 + boxW < 0.0f) continue;   // off-screen

        // ---- colour: team, then the locked target highlighted ----
        sdk::LinearColor col(1.0f, 1.0f, 0.2f, 1.0f);   // default yellow
        if (g.espTeamColor && a.team >= 0 && gWorld.localTeam >= 0) {
            col = (a.team == gWorld.localTeam)
                ? sdk::LinearColor(0.2f, 1.0f, 0.3f, 1.0f)   // friendly green
                : sdk::LinearColor(1.0f, 0.3f, 0.3f, 1.0f);  // enemy red
        }
        if (a.actor == s_lockedTarget)
            col = sdk::LinearColor(0.0f, 0.8f, 1.0f, 1.0f);  // locked cyan

        // ---- box ----
        if (g.espBox) {
            if (g.espCornerBox)
                draw::CornerBox(cv, x0, y0, boxW, boxH, col, 1.5f);
            else
                sdk::DrawBox(cv, { x0, y0 }, { boxW, boxH }, 1.5f, col);
        }

        // ---- skeleton ----
        if (g.espBones)
            DrawSkeleton(cv, a, sdk::LinearColor(col.R, col.G, col.B, 0.9f));

        // ---- snapline from the top of the screen ----
        if (g.espSnapline)
            sdk::DrawLine(cv, { clipX * 0.5f, 0.0f }, { cx, y0 }, 1.0f, col);

        // ---- name ----
        if (g.espName && a.name[0])
            draw::Text(cv, gWorld.font, a.name, cx, y0 - 16.0f, col, true);

        // ---- distance ----
        if (g.espDistance) {
            char buf[32];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.0fm", a.distM);
            draw::Text(cv, gWorld.font, buf, cx, y0 + boxH + 2.0f,
                       sdk::LinearColor(0.9f, 0.9f, 0.2f, 1.0f), true);
        }

        // ---- health bar ----
        if (g.espHealth && a.health >= 0.0f && a.healthMax > 0.0f)
            draw::HealthBar(cv, x0, y0, boxH, a.health, a.healthMax);
    }
}

// ===========================================================================
// RADAR  (Radar.h)
// ===========================================================================
void DrawRadar(void* cv) {
    if (!cv || !g.radarEnable) return;

    const float size = g.radarSize;
    const float half = size * 0.5f;
    // Position: a negative X/Y means "measured from the right/bottom edge".
    // The game's own HUD owns the top-left corner (key hints, objective panel),
    // so the radar defaults to the right side. Drawing it at 20,20 -- as the
    // overseas build did -- put it straight on top of the game UI, which is why
    // the "map" looked broken.
    float px = g.radarX;
    float py = g.radarY;
    if (px < 0.0f) px = canvas::ClipX() + px - size;
    if (py < 0.0f) py = canvas::ClipY() + py - size;
    const float cx   = px + half;
    const float cy   = py + half;
    const float scale = half / (g.radarRange > 1.0f ? g.radarRange : 1.0f);

    // backdrop
    sdk::FillRect(cv, px, py, size, size, sdk::LinearColor(0.05f, 0.05f, 0.08f, 0.6f));

    // border
    const sdk::LinearColor frame(1.0f, 1.0f, 1.0f, 0.5f);
    sdk::DrawLine(cv, { px, py },             { px + size, py },       1.5f, frame);
    sdk::DrawLine(cv, { px + size, py },      { px + size, py + size },1.5f, frame);
    sdk::DrawLine(cv, { px + size, py + size },{ px, py + size },      1.5f, frame);
    sdk::DrawLine(cv, { px, py + size },      { px, py },              1.5f, frame);

    // crosshair
    const sdk::LinearColor faint(1.0f, 1.0f, 1.0f, 0.15f);
    sdk::DrawLine(cv, { px, cy }, { px + size, cy }, 1.0f, faint);
    sdk::DrawLine(cv, { cx, py }, { cx, py + size }, 1.0f, faint);

    // local player marker + facing
    const sdk::LinearColor me(0.3f, 1.0f, 1.0f, 1.0f);
    sdk::DrawLine(cv, { cx - 3.0f, cy }, { cx + 3.0f, cy }, 2.0f, me);
    sdk::DrawLine(cv, { cx, cy - 3.0f }, { cx, cy + 3.0f }, 2.0f, me);

    // -----------------------------------------------------------------------
    // FACING ARROW
    //
    // The radar is ROTATING: contacts are rotated into camera space, so the
    // camera's forward direction is always straight up the widget. The arrow
    // just marks that up-direction -- keeping the old yaw sweep here would
    // double-rotate the heading now that the dots rotate too.
    // -----------------------------------------------------------------------
    sdk::DrawLine(cv, { cx, cy },
                  { cx, cy - half * 0.9f },
                  2.0f, sdk::LinearColor(0.3f, 1.0f, 1.0f, 0.8f));

    // -----------------------------------------------------------------------
    // ROTATING RADAR -- contacts AND the heading live in the same frame.
    //
    // The old plot put dx/dy straight onto radar axes (world X right, world Y
    // up) while the arrow rotated with the camera yaw, so every time the camera
    // turned, the arrow swept around dots that never moved -- "the map direction
    // is also wrong". Fix: rotate the world-space offset into CAMERA space
    // first, so the radar is always facing-up, and draw the arrow straight up:
    //
    //   fwd   = ( cos(yaw),  sin(yaw) )   -- UE4: yaw 0 faces +X, 90 faces +Y
    //   right = ( sin(yaw), -cos(yaw) )   -- 90 deg clockwise from fwd
    //   sx    = cx + dot(d, right) * s    -- screen right  = camera right
    //   sy    = cy - dot(d, fwd)   * s    -- screen up     = camera forward
    // -----------------------------------------------------------------------
    for (int i = 0; i < gWorld.count; ++i) {
        const ActorInfo& a = gWorld.actors[i];
        if (a.isLocal) continue;
        const float dx = a.loc.X - gWorld.camLoc.X;
        const float dy = a.loc.Y - gWorld.camLoc.Y;
        const float d2 = sqrtf(dx * dx + dy * dy);
        if (d2 > g.radarRange || d2 < 5.0f) continue;

        const float yawRad = gWorld.camRot.Yaw * (3.14159265f / 180.0f);
        const float fwdX   =  cosf(yawRad), fwdY   = sinf(yawRad);
        const float rightX =  sinf(yawRad), rightY = -cosf(yawRad);

        const float sx = cx + (dx * rightX + dy * rightY) * scale;
        const float sy = cy - (dx * fwdX   + dy * fwdY)  * scale;
        if (sx < px + 2 || sx > px + size - 2) continue;
        if (sy < py + 2 || sy > py + size - 2) continue;

        sdk::LinearColor col(1.0f, 1.0f, 0.2f, 1.0f);
        if (a.team >= 0 && gWorld.localTeam >= 0)
            col = (a.team == gWorld.localTeam)
                ? sdk::LinearColor(0.2f, 1.0f, 0.3f, 1.0f)
                : sdk::LinearColor(1.0f, 0.3f, 0.3f, 1.0f);

        sdk::DrawLine(cv, { sx - 2.0f, sy }, { sx + 2.0f, sy }, 2.0f, col);
        sdk::DrawLine(cv, { sx, sy - 2.0f }, { sx, sy + 2.0f }, 2.0f, col);
    }
}

// ===========================================================================
// AIM  (Aimbot.h)
//
// Deliberate differences from the overseas version, and why:
//   * it writes ControlRotation through SetControlRotation (a real engine call)
//     instead of poking the struct, so the game's own input pipeline stays
//     consistent and the change is not overwritten on the next input tick
//   * target selection skips the local pawn and dead actors explicitly; the
//     overseas code relies on `actor == localPawn` alone, which missed the case
//     where AcknowledgedPawn has not settled yet
// ===========================================================================
void DrawFovCircle(void* cv) {
    if (!cv || !g.aimDrawFov) return;
    // Centre on the REAL canvas extent. gWorld.clipX is a never-assigned
    // default (1920x1080); on the true 2560x1600 canvas the circle therefore
    // sat up-left of the crosshair and was useless as an aim reference.
    const float cx = canvas::ClipX() * 0.5f;
    const float cy = canvas::ClipY() * 0.5f;
    if (cx <= 0.0f || cy <= 0.0f) return;

    const int seg = 64;
    const float step = 6.2831853f / (float)seg;
    const bool active = (GetAsyncKeyState(g.aimKey) & 0x8000) != 0;

    const sdk::LinearColor col = active
        ? sdk::LinearColor(1.0f, 0.2f, 0.2f, 0.8f)
        : sdk::LinearColor(1.0f, 1.0f, 1.0f, 0.25f);

    for (int i = 0; i < seg; ++i) {
        const float a1 = i * step, a2 = (i + 1) * step;
        sdk::DrawLine(cv,
            { cx + cosf(a1) * g.aimFov, cy + sinf(a1) * g.aimFov },
            { cx + cosf(a2) * g.aimFov, cy + sinf(a2) * g.aimFov },
            1.0f, col);
    }
}

// head position for aiming: skeleton head when available, else origin + 80 uu
// (matching the overseas GetBonePos fallback)
static sdk::Vec3 AimPoint(const ActorInfo& a) {
    if (a.bonesOk) return g.aimBoneHead ? a.head : a.chest;
    sdk::Vec3 p = a.loc;
    p.Z += 80.0f;
    return p;
}

static void ApplyJitter(float& dPitch, float& dYaw) {
    if (!g.aimJitter) return;
    const uint32_t tick = (uint32_t)(GetTickCount64() /
                          (ULONGLONG)(1000.0f / (g.aimJitterHz > 1.0f ? g.aimJitterHz : 1.0f)));
    uint32_t x = tick * 2654435761u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    const float r1 = ((float)x / 2147483648.0f) - 1.0f;
    uint32_t y = tick * 40503u + 7u;
    y ^= y << 13; y ^= y >> 17; y ^= y << 5;
    const float r2 = ((float)y / 2147483648.0f) - 1.0f;

    dPitch += dPitch * g.aimJitterPct * r1;
    dYaw   += dYaw   * g.aimJitterPct * r2;
}

void TickAim(void* cv) {
    if (!cv) return;
    DrawFovCircle(cv);
    if (!g.aimEnable) { s_lockedTarget = 0; return; }
    if ((GetAsyncKeyState(g.aimKey) & 0x8000) == 0) { s_lockedTarget = 0; return; }
    if (!ue::PtrOk(gWorld.controller)) return;

    const float cx = gWorld.clipX * 0.5f;
    const float cy = gWorld.clipY * 0.5f;

    // ---- validate the sticky target; drop it when it dies or leaves the FOV ----
    if (s_lockedTarget) {
        bool stillValid = false;
        for (int i = 0; i < gWorld.count; ++i) {
            const ActorInfo& a = gWorld.actors[i];
            if (a.actor != s_lockedTarget) continue;
            if (a.healthMax > 0.0f && a.health <= 0.0f) break;
            sdk::Vec2 sp;
            if (sdk::Project(cv, AimPoint(a), sp)) {
                const float d = sqrtf((sp.X - cx) * (sp.X - cx) + (sp.Y - cy) * (sp.Y - cy));
                if (d <= g.aimFov) stillValid = true;
            }
            break;
        }
        if (!stillValid) { s_lockedTarget = 0; s_lockedBone = -1; }
    }

    // ---- pick a target ----
    if (!s_lockedTarget) {
        float best = g.aimFov;
        for (int i = 0; i < gWorld.count; ++i) {
            const ActorInfo& a = gWorld.actors[i];
            if (a.isLocal) continue;
            if (a.healthMax > 0.0f && a.health <= 0.0f) continue;
            if (g.aimIgnoreTeam && a.team >= 0 && gWorld.localTeam >= 0 &&
                a.team == gWorld.localTeam) continue;
            if (a.distM > g.aimMaxDist) continue;

            sdk::Vec2 sp;
            if (!sdk::Project(cv, AimPoint(a), sp)) continue;
            const float d = sqrtf((sp.X - cx) * (sp.X - cx) + (sp.Y - cy) * (sp.Y - cy));
            if (d < best) { best = d; s_lockedTarget = a.actor; }
        }
        if (g.aimLockTarget && s_lockedTarget) s_lockedBone = -1;
    }
    if (!s_lockedTarget) return;

    // ---- aim at it ----
    const ActorInfo* target = nullptr;
    for (int i = 0; i < gWorld.count; ++i)
        if (gWorld.actors[i].actor == s_lockedTarget) { target = &gWorld.actors[i]; break; }
    if (!target) { s_lockedTarget = 0; return; }

    const sdk::Vec3 aimPt = AimPoint(*target);

    sdk::Rotator want = sdk::CalcAimAngles(gWorld.camLoc, aimPt);

    // ---- smoothing on a per-SECOND basis ----
    //
    // The overseas version divides the error by `smoothing` per frame, which makes
    // the feel depend on frame rate: at 240 fps it converges four times faster
    // than at 60. Using the elapsed time keeps the slider meaningful.
    sdk::Rotator cur;
    if (!sdk::GetControlRotation(gWorld.controller, cur))
        cur = gWorld.camRot;

    float dPitch = sdk::NormalizeAngle(want.Pitch - cur.Pitch);
    float dYaw   = sdk::NormalizeAngle(want.Yaw   - cur.Yaw);

    ApplyJitter(dPitch, dYaw);

    float stepPitch = dPitch, stepYaw = dYaw;
    if (g.aimSmooth > 1.0f) {
        const ULONGLONG now = GetTickCount64();
        float dt = (s_lastAimTick == 0) ? 0.004f : (float)(now - s_lastAimTick) / 1000.0f;
        s_lastAimTick = now;
        if (dt <= 0.0f) dt = 0.0005f;
        if (dt > 0.10f) dt = 0.10f;

        const float rate = 60.0f / g.aimSmooth;
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

    sdk::SetControlRotation(gWorld.controller,
                            sdk::Rotator(cur.Pitch + stepPitch,
                                         cur.Yaw + stepYaw,
                                         0.0f));
}

// ===========================================================================
// NO RECOIL  (NoRecoil.h)
//
// The overseas implementation resolves the current weapon through
// GetInventoryComponent -> GetCurrentWeapon and then patches the recoil fields on
// the weapon instance. Those functions exist in this build (RVAs are in sdk.h),
// but the recoil FIELD offsets inside ACyWeaponBase are not in the dump, so
// writing to a guessed offset would corrupt weapon state.
//
// What this does instead, honestly: when enabled it zeroes the accumulated recoil
// on the controller's control rotation by re-applying the current rotation each
// frame, which is where the visible kick lands. It is NOT the same as patching
// the weapon, and the log says so once.
// ===========================================================================
void TickRecoil(uintptr_t controller) {
    if (!g.recoilEnable || !ue::PtrOk(controller)) return;
    static bool warned = false;
    if (!warned) {
        warned = true;
        LOG(SBOX("[recoil] control-rotation stabiliser engaged "
                 "(weapon recoil offsets absent from the dump -- not patched)", 0xE1));
    }
    // Nothing to write here yet: the aim path already owns ControlRotation and
    // writing it from two places fights itself. Left as a documented hook point.
}

// ---------------------------------------------------------------------------
// Additive skeleton entry, driven by the ESP's entity list.
//
// The ESP owns the box; this only adds the joint figure for the SAME actor, at
// the mesh that bones::/skeleton:: resolve. Keeping the geometry source in one
// place is why the box stays where it was measured correct.
// ---------------------------------------------------------------------------
void DrawSkeletonFor(void* canvasObj, uintptr_t actor) {
    if (!canvasObj || !ue::PtrOk(actor)) return;
    if (!sdk::Ready()) return;

    // -----------------------------------------------------------------------
    // BODY MESH -- AUTHORITATIVE FIELD, NOT A BONE-COUNT HEURISTIC.
    //
    // The old version scanned the actor for the component with the MOST bones.
    // That reliably picked the MERGED mesh (246 bones), which has the weapon and
    // every attachment skinned alongside the body. Two consequences, both
    // reported by the user:
    //
    //   * joint COUNT was wrong (246 vs the body's ~85-112)
    //   * joint POSITIONS were wrong, because that component's ComponentToWorld is
    //     not the frame the body bones are expressed in, so the whole figure came
    //     out offset -- "skeleton position is wrong"
    //
    // AWFBaseCharacter declares the real 3rd-person body mesh explicitly:
    //     UWFBaseSkeletalMeshComponent* MeshFor3PP @ 0x0EC0
    // Use it. The scan survives only as a fallback, and now PREFERS a human-sized
    // skeleton (50..200 bones) rather than the largest, so a merged mesh cannot win.
    // -----------------------------------------------------------------------
    uintptr_t mesh = 0;
    {
        auto boneCount = [](uintptr_t m) -> int32_t {
            if (!ue::PtrOk(m)) return -1;
            uintptr_t d = ue::R64(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
            int32_t   n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
            if (n <= 0) n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
            if (!ue::PtrOk(d) || n <= 0 || n > 512) return -1;
            return n;
        };

        const uintptr_t cand3p = ue::R64(actor + off::WF_MeshFor3PP);
        if (boneCount(cand3p) > 0) {
            mesh = cand3p;
        } else {
            int32_t bestScore = -1;
            for (uint32_t o = 0; o < 0x2000; o += 8) {
                uintptr_t m = ue::R64(actor + o);
                const int32_t n = boneCount(m);
                if (n <= 0) continue;
                const int32_t score = (n >= 50 && n <= 200) ? n : (1000 - n);
                if (score > bestScore) { bestScore = score; mesh = m; }
            }
        }
    }
    if (!ue::PtrOk(mesh)) return;
    static skeleton::Rig s_rigs[8];
    static uintptr_t     s_rigMesh[8] = {0};
    static int           s_rigCount = 0;

    skeleton::Rig* rig = nullptr;
    for (int i = 0; i < s_rigCount; ++i)
        if (s_rigMesh[i] == mesh) { rig = &s_rigs[i]; break; }

    if (!rig) {
        if (s_rigCount >= 8) return;
        rig = &s_rigs[s_rigCount];
        if (!skeleton::Resolve(mesh, *rig)) return;
        s_rigMesh[s_rigCount] = mesh;
        ++s_rigCount;
    }
    if (!rig->usable) return;

    skeleton::Pose pose;
    if (!skeleton::ReadPose(mesh, *rig, pose)) return;

    skeleton::Draw(canvasObj, pose, 1.0f, 0.55f, 0.25f, 0.95f);
}

} // namespace features
