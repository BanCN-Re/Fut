// D:\Work\Fut\cheat\src\bones.h
#pragma once
#include <cstdint>

namespace bones {

struct BoneSet {
    bool  valid = false;
    float headX = 0, headY = 0, headZ = 0;
    float chestX = 0, chestY = 0, chestZ = 0;
    float pelvisX = 0, pelvisY = 0, pelvisZ = 0;
    float feetX = 0, feetY = 0, feetZ = 0;

    // Body vertical axis: the median X/Y of all bones, used to reject weapon and
    // prop bones when picking the head and feet extremes. Exposed for diagnostics;
    // callers normally do not need it.
    float axisX = 0, axisY = 0;
};

// Reads head / chest / pelvis / feet world positions for a character.
//
// Source: USkeletalMeshComponent::CachedComponentSpaceTransforms
//   (a TArray<FTransform>, 0x0800 on USkeletalMeshComponent)
// Each FTransform is 0x30 bytes: FQuat@0x00, FVector Translation@0x10.
// Those transforms are in COMPONENT space, so they are converted to world with
// the mesh component's own ComponentToWorld.
//
// Bone indices are resolved by NAME from the skeleton when available, with a
// fallback to the common UE4 humanoid layout for this title.
// Call once per character per frame; results are cheap cached lookups.
bool Get(uintptr_t character, BoneSet& out);

// Diagnostics: dump the bone name/position table for one character, so the
// indices can be verified against the real rig.
void DumpBones(uintptr_t character);

} // namespace bones
