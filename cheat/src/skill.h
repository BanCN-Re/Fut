// D:\Work\Fut\cheat\src\skill.h
#pragma once
#include <cstdint>

namespace skill {

struct Settings {
    bool enabled   = false;   // master
    bool autoFire  = false;   // fire when the crosshair is on a target
    int  fireKey   = 0x01;    // VK_LBUTTON
    bool trackCd   = true;    // track enemy ability cooldowns
    bool lockTarget= false;   // remember and highlight one target
    int  lockKey   = 0x05;    // VK_XBUTTON2 / mouse4
    float maxTrackDistance = 30000.0f;
};

extern Settings g_settings;

void Tick();
void RenderOverlay();      // draws lock markers / cooldown bars

// status for the GUI
int  LockedIndex();
int  TrackedCount();
const char* LastError();

} // namespace skill
