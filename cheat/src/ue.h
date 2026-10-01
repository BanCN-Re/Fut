// D:\Work\Fut\cheat\src\ue.h
// Minimal in-process UE4 reader: module base, GObjects walk, FName resolution,
// GWorld -> Actors, and the player/camera chain.
//
// Everything here reads the CURRENT process (we are injected), so no
// OpenProcess/ReadProcessMemory-to-another-process is involved -- which is the
// only way to work here, since ACE blocks external RPM (err=5).
#pragma once
#include <cstdint>
#include <string>

namespace ue {

// ---- init ---------------------------------------------------------------
uintptr_t ImageBase();
bool      Init();                 // caches base + GObjects; safe to call again

// ---- raw reads (SEH-guarded, never fault) ------------------------------
bool     Read(uintptr_t addr, void* out, size_t size);
uint64_t R64(uintptr_t addr);
uint32_t R32(uintptr_t addr);
int32_t  RI32(uintptr_t addr);
uint8_t  R8(uintptr_t addr);
float    RF32(uintptr_t addr);
bool     PtrOk(uint64_t p);

// ---- guarded write -----------------------------------------------------
// Validates that the target page is committed and writable BEFORE writing, then
// writes under a temporary PAGE_READWRITE grant and restores the old protection.
// Returns false rather than faulting on a bad address.
bool WriteGuarded(uintptr_t addr, const void* src, size_t size);

// ---- GObjects -----------------------------------------------------------
bool      GObjectsReady();
int32_t   NumObjects();
uintptr_t ObjectAt(int32_t index);

// ---- FName --------------------------------------------------------------
bool NameOf(uintptr_t obj, std::string& out);

// Resolve a bare FName index to text (used for bone names).
bool NameFromIndex(uint32_t index, char* out, size_t cap);   // resolves via AppendString

// ---- class matching -----------------------------------------------------
uintptr_t FindClassByName(const char* className);        // exact UClass object
uintptr_t FindClassContaining(const char* needle);       // substring match

// True when obj's class is, or derives from, targetClass.
// Spawned actors carry their MOST-DERIVED class (e.g. APMCharacter), never the
// base (ACyCharacterBase), so exact pointer comparison finds nothing. This walks
// UStruct::SuperStruct (+0x40) like UE4's own UObject::IsA.
bool IsA(uintptr_t obj, uintptr_t targetClass);
// Convenience: resolve the class by name once, then IsA against instances.
bool IsAOfClassNamed(uintptr_t obj, const char* className);
// Debug: append the object's class chain ("APMCharacter <- AWFBaseCharacter <- ...")
bool ClassChain(uintptr_t obj, std::string& out, int maxDepth = 12);
// Debug: log every object name containing `needle` (returns how many were logged).
int DumpNamesContaining(const char* needle, int maxOut = 60);

// ---- world --------------------------------------------------------------
uintptr_t GetWorld();
uintptr_t GetPersistentLevel();
int32_t   GetActorCount(uintptr_t level);
uintptr_t GetActor(uintptr_t level, int32_t index);

// ---- local player / camera ---------------------------------------------
struct Camera {
    float locX = 0, locY = 0, locZ = 0;
    float rotPitch = 0, rotYaw = 0, rotRoll = 0;
    float fov = 0;
    bool  valid = false;
};
uintptr_t GetLocalPlayerController();
uintptr_t GetLocalPawn();
bool      GetCamera(Camera& out);

// One-shot diagnostic: logs every link of the GWorld -> camera chain with live
// values, so a failing link is visible instead of silently returning null.
void DumpPlayerChain();

// ---- world -> screen ----------------------------------------------------
// Projects a world point to screen coordinates using PURE MATH from the camera
// POV we already read.
//
// We deliberately do NOT call the game's
// APlayerController::ProjectWorldLocationToScreen. That is a C++ MEMBER
// function, so `this` must be passed in RCX; the first attempt called it as a
// free function, leaving RCX as garbage, and the game crashed with
// EXCEPTION_ACCESS_VIOLATION reading 0x08000008 (twice, on entering the
// training range). Reading POV data and doing the transform ourselves touches
// no game code at all.
bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy);

// Screen size used by the projection (set each frame from the overlay).
void SetScreenSize(int w, int h);

// Snapshot the camera once per frame. WorldToScreen reads this snapshot, so it
// stays cheap when called for hundreds of entities.
void BeginFrame();

// ---- vector helper ------------------------------------------------------
struct Vec3 { float x, y, z; };
bool GetActorLocation(uintptr_t actor, Vec3& out);

} // namespace ue
