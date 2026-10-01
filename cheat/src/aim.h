// D:\Work\Fut\cheat\src\aim.h
#pragma once
#include <cstdint>

namespace aim {

struct Settings {
    bool  enabled    = false;    // master
    bool  holdKey    = true;     // require the aim key to be held
    int   aimKey     = 0x02;     // VK_RBUTTON (right mouse)
    bool  teamCheck  = true;     // skip teammates
    bool  fovLimit   = true;
    float fov        = 60.0f;    // degrees, half-angle for target selection
    float smooth     = 4.0f;     // lower = faster snap (>=1); rate = 120/smooth per second
    bool  boneHead   = true;     // aim at head, else chest
    bool  drawFov    = true;     // draw the FOV circle
    bool  silentLock = false;    // write ControlRotation directly (server-side
                                 // visible only through the camera; off by default)
    // humaniser: bounded random wobble on the aim error, seeded per tick
    bool  jitter     = false;
    float jitterPct  = 0.60f;    // DEGREES of absolute wobble on the target angle
    float jitterHz   = 8.0f;     // wobble refresh rate
};

extern Settings g_settings;

// Called once per frame from the render thread (after ESP has collected data).
void Tick();

// Result accessors for the GUI.
int         TargetCount();
const char* LastError();
bool        HasTarget();
float       LastFov();

} // namespace aim
