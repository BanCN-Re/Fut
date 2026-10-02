// D:\Work\Fut\cheat\src\webui_bridge.h
#pragma once
//
// Marshalling between the settings modules and the shared-memory block that an
// out-of-process UI reads. See webui_shm.h for the architecture and why there is
// no socket in this process.
namespace webui_bridge {

// Create the mapping. Non-fatal if it fails: the feature is optional.
void Init();

// Publish settings + status, and apply any edit the UI made. Once per frame.
void Tick();

} // namespace webui_bridge
