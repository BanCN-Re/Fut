// D:\Work\Fut\cheat\src\gfxprobe.h
#pragma once
namespace gfxprobe {
// Logs the loaded graphics modules (d3d11/d3d12/dxgi/vulkan/nvapi/...) and, for
// each, whether it is the system copy. Used to identify which graphics API the
// game actually presents through.
void DumpLoadedGraphicsModules();
}
