// D:\Work\Fut\cheat\src\overlay.h
#pragma once
#include <cstdint>

namespace overlay {

// Turn screen-capture protection on/off for the overlay window.
// Enabling also switches the ESP from in-game drawing to overlay drawing,
// because WDA is per-window and cannot protect the game's frame.
void ApplyCaptureProtection(bool on);

// Creates our OWN transparent topmost window with its OWN D3D11 device and
// renders ImGui there every frame.
//
// Why not hook the game's swap chain: the game presents through D3D12 (d3d12.dll
// is loaded and its swap chain is a different DXGI object class), so hooking
// IDXGISwapChain v1 Present never fires in-game. A self-owned window does not
// care which API the game uses, and it hooks nothing -- smallest possible
// footprint.
bool Init();
void Shutdown();

// Entry point for the overlay's own thread (owns the window + device).
unsigned long __stdcall ThreadProc(void* param);

bool     Ready();
uint64_t Frames();

// Follow the game window's client rect so the overlay always covers it.
void SyncToGameWindow();

} // namespace overlay
