// D:\Work\Fut\cheat\src\gui.h
#pragma once
#include <cstdint>

namespace gui {

extern bool g_open;        // menu visibility
extern int  g_menuKey;     // toggle key (default INSERT)
extern bool g_debugStrip;  // always-on diagnostic overlay
extern bool g_opaqueMenu;  // menu open -> solid black backdrop (no see-through)

void Render();                                  // draw the menu (inside a frame)

// Mirror the menu switches into the ported features module. MUST be called
// every frame from the in-game render path -- the menu only renders inside the
// overlay, which does not run while the canvas hook owns the ESP.
void SyncFeatureFlags();
void RenderDebugStrip();                        // always-on overlay + mouse fallback

void ToggleMenu();
void SetMenuOpen(bool open);
void PollEscape();     // ESC closes the menu

// True when the mouse is over our UI, so the caller can swallow clicks.
bool WantsMouse();

} // namespace gui
