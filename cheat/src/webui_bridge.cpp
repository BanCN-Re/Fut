// D:\Work\Fut\cheat\src\webui_bridge.cpp
//
// The one place that knows how the settings modules map onto the shared-memory
// block. Keeping the marshalling here means webui_shm.cpp needs no dependency on
// esp/aim/features and can be unit-checked in isolation.
//
// DIRECTION AND OWNERSHIP
//
//   readFromGame  -- game -> block. Runs every frame. Never touches the control
//                    bytes (uiGeneration / gameHeartbeat) because webui_shm owns
//                    those.
//   writeToGame   -- block -> game. Runs ONLY when the UI bumped uiGeneration, so
//                    a stale block cannot fight the in-game ImGui panel.
//
// WHY THE MAPPING IS NOT A FIELD-BY-FIELD MIRROR OF ANY ONE STRUCT
//
// esp::Settings, aim::Settings and features::State overlap (all three carry an
// ESP enable and a box flag). The block holds the union of what the UI needs to
// edit, and writeToGame fans each field out to every module that cares. That way
// one slider moves the real value regardless of which module ended up owning it.
#include "webui_shm.h"
#include "esp.h"
#include "aim.h"
#include "features.h"
#include "keybind.h"
#include "canvas.h"
#include "gui.h"
#include "ue.h"

#include <cstring>
#include <cstdio>

namespace webui_bridge {

// ---------------------------------------------------------------------------
// game -> block
// ---------------------------------------------------------------------------
static void ReadFromGame(webui_shm::Params& p) {
    // ESP
    p.espEnabled   = esp::g_settings.enabled      ? 1 : 0;
    p.espBox       = esp::g_settings.box          ? 1 : 0;
    p.espCornerBox = esp::g_settings.cornerBox    ? 1 : 0;
    p.espBones     = esp::g_settings.drawBones    ? 1 : 0;
    p.espName      = esp::g_settings.name         ? 1 : 0;
    p.espDistance  = esp::g_settings.distance     ? 1 : 0;
    p.espHealth    = esp::g_settings.health       ? 1 : 0;
    p.espSnapline  = esp::g_settings.snapline     ? 1 : 0;
    p.espTeamColor = esp::g_settings.teamColor    ? 1 : 0;
    p.espMaxDist   = esp::g_settings.maxDistance;
    p.boxAlpha     = esp::g_settings.boxAlpha;

    // features-owned (radar, stealth, outline)
    p.radarEnable  = features::g.radarEnable  ? 1 : 0;
    p.antiStealth  = features::g.antiStealth  ? 1 : 0;
    p.outline      = features::g.outline      ? 1 : 0;
    p.radarRange   = features::g.radarRange;
    p.radarSize    = features::g.radarSize;
    p.radarX       = features::g.radarX;
    p.radarY       = features::g.radarY;

    // Aim
    p.aimEnabled   = aim::g_settings.enabled    ? 1 : 0;
    p.softEnable   = aim::g_settings.softEnable ? 1 : 0;
    p.hardEnable   = aim::g_settings.hardEnable ? 1 : 0;
    p.softHold     = aim::g_settings.softHold   ? 1 : 0;
    p.hardHold     = aim::g_settings.hardHold   ? 1 : 0;
    p.teamCheck    = aim::g_settings.teamCheck  ? 1 : 0;
    p.boneHead     = aim::g_settings.boneHead   ? 1 : 0;
    p.preferChestWhenMoving = aim::g_settings.preferChestWhenMoving ? 1 : 0;
    p.jitter       = aim::g_settings.jitter     ? 1 : 0;
    p.softKey      = aim::g_settings.softKey;
    p.hardKey      = aim::g_settings.hardKey;
    p.aimFov       = aim::g_settings.fov;
    p.aimSmooth    = aim::g_settings.smooth;
    p.hardHoldMs   = aim::g_settings.hardHoldMs;
    p.movingThreshold = aim::g_settings.movingThreshold;
    p.jitterDeg    = aim::g_settings.jitterPct;
    p.jitterHz     = aim::g_settings.jitterHz;
}

// ---------------------------------------------------------------------------
// live status
//
// Read-only for the UI. Overwritten every frame, so a UI write here is simply
// lost -- which is the intended behaviour: these are observations, not settings.
// ---------------------------------------------------------------------------
static void ReadStatus(webui_shm::Params& p) {
    p.menuOpen   = gui::g_open ? 1 : 0;

    ue::Camera cam{};
    const bool ok = ue::GetCamera(cam) && cam.valid;
    p.cameraOk = ok ? 1 : 0;
    if (ok) {
        p.camX = cam.locX; p.camY = cam.locY; p.camZ = cam.locZ;
        p.camFov = cam.fov;
    }

    p.objects    = ue::NumObjects();
    p.targets    = esp::LastTargetCount();
    p.hasTarget  = aim::HasTarget()   ? 1 : 0;
    p.hardLocked = aim::HardLocked()  ? 1 : 0;
    p.activeMode = (int32_t)aim::ActiveMode();

    const char* err = aim::LastError();
    if (err && err[0]) {
        strncpy_s(p.lastError, sizeof(p.lastError), err, _TRUNCATE);
    } else {
        p.lastError[0] = 0;
    }
}

// ---------------------------------------------------------------------------
// block -> game
//
// Fans each field out to every module that carries it, so the UI cannot end up
// with a slider that moves a value nobody reads.
// ---------------------------------------------------------------------------
static void WriteToGame(const webui_shm::Params& p) {
    // ---- ESP: esp::Settings is what the box renderer reads --------------
    esp::g_settings.enabled     = p.espEnabled   != 0;
    esp::g_settings.box         = p.espBox       != 0;
    esp::g_settings.cornerBox   = p.espCornerBox != 0;
    esp::g_settings.drawBones   = p.espBones     != 0;
    esp::g_settings.name        = p.espName      != 0;
    esp::g_settings.distance    = p.espDistance  != 0;
    esp::g_settings.health      = p.espHealth    != 0;
    esp::g_settings.snapline    = p.espSnapline  != 0;
    esp::g_settings.teamColor   = p.espTeamColor != 0;
    esp::g_settings.maxDistance = p.espMaxDist;
    esp::g_settings.boxAlpha    = p.boxAlpha;

    // features::State is what the radar / outline / stealth paths read.
    features::g.espEnable    = p.espEnabled   != 0;
    features::g.espBox       = p.espBox       != 0;
    features::g.espCornerBox = p.espCornerBox != 0;
    features::g.espBones     = p.espBones     != 0;
    features::g.espName      = p.espName      != 0;
    features::g.espDistance  = p.espDistance  != 0;
    features::g.espHealth    = p.espHealth    != 0;
    features::g.espSnapline  = p.espSnapline  != 0;
    features::g.espTeamColor = p.espTeamColor != 0;
    // The two modules measure distance in different units: esp uses raw uu, the
    // ported features module uses metres. Convert rather than copy, or the radar
    // and the box disagree about how far 500 means.
    features::g.espMaxDist   = p.espMaxDist / 100.0f;

    features::g.radarEnable = p.radarEnable != 0;
    features::g.antiStealth = p.antiStealth != 0;
    features::g.outline     = p.outline     != 0;
    features::g.radarRange  = p.radarRange;
    features::g.radarSize   = p.radarSize;
    features::g.radarX      = p.radarX;
    features::g.radarY      = p.radarY;

    // ---- Aim ----
    aim::g_settings.enabled     = p.aimEnabled   != 0;
    aim::g_settings.softEnable  = p.softEnable   != 0;
    aim::g_settings.hardEnable  = p.hardEnable   != 0;
    aim::g_settings.softHold    = p.softHold     != 0;
    aim::g_settings.hardHold    = p.hardHold     != 0;
    aim::g_settings.teamCheck   = p.teamCheck    != 0;
    aim::g_settings.boneHead    = p.boneHead     != 0;
    aim::g_settings.preferChestWhenMoving = p.preferChestWhenMoving != 0;
    aim::g_settings.jitter      = p.jitter       != 0;
    aim::g_settings.softKey     = p.softKey;
    aim::g_settings.hardKey     = p.hardKey;
    aim::g_settings.fov         = p.aimFov;
    aim::g_settings.smooth      = p.aimSmooth;
    aim::g_settings.hardHoldMs  = p.hardHoldMs;
    aim::g_settings.movingThreshold = p.movingThreshold;
    aim::g_settings.jitterPct   = p.jitterDeg;
    aim::g_settings.jitterHz    = p.jitterHz;
}

// ---------------------------------------------------------------------------
// install
// ---------------------------------------------------------------------------
static webui_shm::Bridge s_bridge{ &ReadFromGame, &WriteToGame, &ReadStatus };

void Init() {
    if (!webui_shm::Init()) return;      // optional feature; failure is not fatal
}

void Tick() {
    webui_shm::Tick(s_bridge);
}

} // namespace webui_bridge
