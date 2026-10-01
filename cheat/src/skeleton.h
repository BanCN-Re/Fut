// D:\Work\Fut\cheat\src\skeleton.h
#pragma once
//
// Name-driven bone skeleton.
//
// WHY NOT THE AXIS-EXTREMES APPROACH
//   Picking head/chest/pelvis/feet as "the highest and lowest bones on the body
//   axis" produces four points that all lie ON that axis. Connecting them draws a
//   vertical line, which is exactly what the user saw. A readable skeleton needs
//   the LEFT and RIGHT limb bones, and those cannot be inferred from a bounding
//   box -- they have to be identified.
//
// HOW THEY ARE IDENTIFIED
//   USkinnedMeshComponent::GetBoneName(index) (RVA 0x44F42A0) returns the bone's
//   FName. This game uses the Unreal/Biped convention ("Bip001-Head",
//   "Bip001-L-Clavicle", ...) as the overseas build's default `targetBoneName =
//   "Bip001-Head"` implies. Names are enumerated ONCE per mesh, matched by
//   substring, and the resulting index table is cached; after that a frame costs
//   one array read per joint.
//
//   Matching is substring-based and case-insensitive so it survives the L/R
//   prefix variants ("Bip001-L-Hand", "Bip001_L_Hand", "LeftHand", ...).
//
// FALLBACK
//   When no names match (different rig, stripped names) the caller falls back to
//   the axis method, which at least yields a correct BOX even though the skeleton
//   is a line. The two are reported differently in the log so the difference is
//   never hidden.
#include <cstdint>

namespace skeleton {

// Joint slots, filled by Resolve().
enum Joint {
    Head = 0, Neck, Chest, Spine, Pelvis,
    LShoulder, LElbow, LHand,
    RShoulder, RElbow, RHand,
    LHip, LKnee, LFoot,
    RHip, RKnee, RFoot,
    JointCount
};

// Bone index per joint; -1 when unresolved. Cached per mesh component.
struct Rig {
    uintptr_t mesh = 0;
    int       index[JointCount];
    bool      resolved = false;
    bool      usable = false;      // enough joints to draw a real skeleton
    char      firstName[64] = {0}; // diagnostics: the first bone seen
};

// Per-character world positions for one frame.
struct Pose {
    float pos[JointCount][3];
    bool  ok[JointCount];
    bool  any = false;
    bool  full = false;   // limbs resolved -> draw a real skeleton
};

// Resolve (and cache) the joint table for a mesh component found via
// bones::Get's component selection. Returns false when the rig is not usable.
bool Resolve(uintptr_t mesh, Rig& rig);

// Read the world positions for `rig` from the mesh's transform arrays.
bool ReadPose(uintptr_t mesh, const Rig& rig, Pose& out);

// Draw the skeleton into the canvas (lines between joints).
void Draw(void* canvas, const Pose& pose, float r, float g, float b, float a);

} // namespace skeleton
