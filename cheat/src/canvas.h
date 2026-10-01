// D:\Work\Fut\cheat\src\canvas.h
#pragma once
#include <cstdint>

namespace canvas {

// ---------------------------------------------------------------------------
// IN-GAME drawing through the engine's own UCanvas.
//
// WHY THIS REPLACES THE OVERLAY WINDOW
//
// The separate overlay window is composited by DWM *after* the game's frame, so
// it always trails by at least one compositor tick. With the game at 239 fps and
// the overlay's DISCARD swap chain capped at the monitor refresh, a box could be
// ~4 game frames behind its target -- the "完全不跟手" the user reported.
//
// Drawing through UCanvas puts our primitives INTO the game's own frame, in the
// same render pass, with zero added latency. It also unlocks what the overlay
// could never do:
//   * it composites with the scene, so "behind a wall" vs "visible" can be
//     decided honestly (see IsVisible below)
//   * no second D3D device competing for the GPU
//   * nothing to hook in d3d11/d3d12/dxgi at all
//
// HOW: hook UGameViewportClient's PostRender vtable slot. PostRender receives a
// ready UCanvas, and every frame the engine calls it exactly once. We locate the
// slot by scanning the vtable for the address of the UE4 function object named
// "Function Engine.GameViewportClient.PostRender" (the technique Flavia uses and
// which is already proven on this title's engine build).
//
// This is the same class of hook that the working Flavia-CN build uses; unlike
// the D3D11 Present hook it modifies no graphics DLL, so it cannot produce the
// dxgi.dll access violations the old build hit.
// ---------------------------------------------------------------------------

// Installs the PostRender hook. Safe to call repeatedly; returns true once
// installed. Requires the game to be in a map (needs LocalPlayer + viewport).
bool Install();

bool Installed();
int  SlotIndex();

// ---- deferred command list ------------------------------------------------
// ESP projects its entities and pushes primitives here; the PostRender hook
// flushes them inside the game's own frame.
void BeginFrame();
void AddRect(float x, float y, float w, float h, float th,
             float r, float g, float b, float a);
void AddFillRect(float x, float y, float w, float h,
                 float r, float g, float b, float a);
void AddLine(float x1, float y1, float x2, float y2, float th,
             float r, float g, float b, float a);
int  CmdCount();
// Honest draw-call accounting: SEH swallow failures silently, so these are the
// only proof that calls actually executed inside the game frame.
long DrawOkCalls();
long DrawBadCalls();

// ENGINE projection (UCanvas::K2_Project). Use this instead of hand-rolled
// world->screen math: the engine's own transform cannot be misaligned by
// camera lag, ADS fov changes or near-plane conventions.
bool Project(void* canvasObj, float wx, float wy, float wz, float& sx, float& sy);
bool ProjectReady();

// Canvas metrics + default font, shared by the features path.
void  SetCanvasSize(void* canvasObj);
float ClipX();
float ClipY();
void* DefaultFont();

// Called from the hook with the live canvas.
struct CanvasApi;
void OnPostRender(void* viewport, void* ucanvas);

// True when the in-game canvas path is live (used by the ESP to decide whether
// to draw in-frame or fall back to the overlay window).
bool Active();

} // namespace canvas
