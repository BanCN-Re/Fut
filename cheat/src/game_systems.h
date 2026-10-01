// D:\Work\Fut\cheat\src\game_systems.h
#pragma once
//
// Wrappers around the game's OWN highlight and stealth systems.
//
// WHY USE THE GAME'S SYSTEMS INSTEAD OF DRAWING OUR OWN
//
//   The game already contains everything an ESP needs, and it is better than
//   anything we could draw:
//
//     * FCyHighlightInfo / SetRenderOutlineMaterialEnabled -- the outline the game
//       itself puts on enemies for certain abilities. Using it means the "walls"
//       view is produced by the same renderer that produces the real one: correct
//       depth handling, correct material, correct occlusion behaviour, and it
//       updates inside the game frame (no overlay latency).
//
//     * GetStealthActors(UWorld*, TArray<AActor*>*) -- the game's own query for
//       every actor currently in a stealth/invisible state. This is the correct
//       anti-stealth: instead of guessing from opacity or visibility flags (which
//       differ per ability), we ask the system that decides it.
//
//   Both are static functions whose addresses come from the CURRENT dump, so they
//   need no UFunction lookup and no ProcessEvent.
//
// VERIFIED ADDRESSES (FunctionsInfo.json, this build)
//   UCyCachedWorldSubsystem::GetStealthActors   0x2356E60
//   UPMCachedWorldSubsystem::GetStealthActors   0x260FED0
//   USkinnedMeshComponent::SetRenderOutlineMaterialEnabled
//                                               0x25D67B0  (BP wrapper)
//   USkinnedMeshComponent::SetOutlineMaterial   0x44F52C0
//   USkinnedMeshComponent::GetRenderOutlineMaterialEnabled
//                                               0x44F46E0
//   USkinnedMeshComponent::GetSkeletalOutline   0x44F4740
//   USceneComponent::SetHiddenInGame            0x44E6260
#include <cstdint>
#include <windows.h>

namespace game_systems {

// ---------------------------------------------------------------------------
// STEALTH / ANTI-INVISIBILITY
//
// Fills `out` with every actor the game currently considers stealthed and
// returns how many were written.
//
// The engine function signature is
//     static bool GetStealthActors(UWorld* World, TArray<AActor*>* OutActors)
// where TArray is { AActor** Data; int32 Num; int32 Max; }.
//
// We pass a TArray that points at OUR buffer with Max = 0. The engine sees a
// full/allocating case and will allocate its own storage -- so the returned
// buffer must be released afterwards. To avoid that entirely we instead call it
// with Max set to the capacity of our buffer and treat the result as a plain
// read-only list; the game only ever appends within Max when it is non-zero.
// ---------------------------------------------------------------------------
constexpr int kMaxStealth = 64;

// Returns the number of stealthed actors written into `out`.
int GetStealthActors(uintptr_t world, uintptr_t* out, int cap);

// True when this specific actor is currently stealthed.
bool IsStealthed(uintptr_t world, uintptr_t actor);

// Cached per-frame view of the stealth list, rebuilt by RefreshStealth().
void RefreshStealth(uintptr_t world);
bool IsInStealthList(uintptr_t actor);

// ---------------------------------------------------------------------------
// OUTLINE / WALL VIEW
//
// Toggles the game's own skeletal-mesh outline on an actor. This is the same
// effect the game applies for its "see enemies through walls" abilities, so it
// composites correctly instead of being an overlay rectangle.
//
// `enabled` is written straight into the mesh component's outline state.
// ---------------------------------------------------------------------------
void SetActorOutline(uintptr_t actor, bool enabled);
bool IsActorOutlineEnabled(uintptr_t actor);

// ---------------------------------------------------------------------------
// diagnostic
// ---------------------------------------------------------------------------
void LogStealthCensus(uintptr_t world);

} // namespace game_systems
