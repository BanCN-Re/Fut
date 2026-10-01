// D:\Work\Fut\cheat\src\d3d.h
#pragma once
#include <cstdint>

namespace d3d {

// Installs a Present hook on the game's swap chain (via a temporary dummy
// device, so no d3d11.dll .text is touched -- only the vtable slot).
bool Init();

// True once the first Present has been seen and ImGui is live.
bool Ready();

// Frame counter, for the GUI.
uint64_t Frames();

void Shutdown();

} // namespace d3d
