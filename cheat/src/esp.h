// Defined in overlay.cpp. When true the overlay is excluded from screen capture
// and the ESP is drawn on the overlay instead of in-game (WDA is per-window, so
// an in-game ESP would still be recorded).
extern bool g_captureProtect;

// D:\Work\Fut\cheat\src\esp.h
#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace esp {

struct Settings {
    bool  enabled      = true;    // master switch
    bool  box          = true;    // 2D box
    bool  snapline     = false;   // ray from the top of the screen to the target
    bool  health       = true;    // health bar
    bool  name         = false;   // role id / name label
    bool  distance     = true;    // distance text
    bool  teamColor    = true;    // colour by team
    bool  drawBones    = true;
    bool  cornerBox    = true;
    bool  visibleOnly  = false;   // only draw on-screen targets
    float maxDistance  = 20000.f; // uu
    float boxAlpha     = 1.0f;
    int   thickness    = 2;
    // Draw even when the target is behind geometry. The BOX always draws through
    // (it is on our own overlay layer); the character MODEL needs depth-state
    // control, which this build deliberately does not touch.
    bool  throughWalls = true;
};

extern Settings g_settings;

// Called once per frame from the render thread.
void Render();

// Entity view handed to the aim module, so it does not walk the world again.
struct EspEnt {
    uintptr_t addr = 0;
    float x = 0, y = 0, z = 0, hp = 0, hpMax = 0;
    uint8_t team = 0;
    uint32_t role = 0, skin = 0;
    bool bonesOk = false;
    float headZ = 0, feetZ = 0;
    float chestX = 0, chestY = 0, chestZ = 0;
    float pelvisX = 0, pelvisY = 0, pelvisZ = 0;
};
int SnapshotForAim(EspEnt* out, int maxOut);
// In-game UCanvas render, called from the PostRender hook (game render thread).
void RenderInGame(void* canvasObj);

// Stats for the GUI.
int  LastTargetCount();
int  LastActorCount();
const char* LastError();

} // namespace esp
