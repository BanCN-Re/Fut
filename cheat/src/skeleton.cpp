// D:\Work\Fut\cheat\src\skeleton.cpp
// See skeleton.h for why the joints are resolved by name.
#include "skeleton.h"
#include "ue.h"
#include "offsets.h"
#include "sdk.h"
#include "obf.h"
#include "log.h"

#include <windows.h>
#include <cmath>
#include <cstring>
#include <string>

namespace skeleton {

// ---------------------------------------------------------------------------
// WHY GetBoneName IS NOT USED (kept as a note so nobody re-adds it)
//
// The obvious way to read bone names is
//     USkinnedMeshComponent::GetBoneName(int32) -> FName    RVA 0x44F42A0
// and it was tried two ways:
//
//   1. modelled as (self, outFName, index)  -- WRONG. FName is 8 bytes and comes
//      back in RAX, so the callee read RCX as a hidden sret slot and every name
//      resolved empty.
//   2. modelled as (self, index) -> uint64   -- ABI-correct, but the call still
//      faults inside the engine on this build (the BP wrapper expects a
//      UFunction invocation, not a bare C++ call).
//
// Both were dead ends, and a third path works and is used instead: the mesh's
// USkeletalMesh (mesh + 0x4D0) carries RawRefBoneInfo, a TArray<FMeshBoneInfo>
// where each entry's first 4 bytes are the FName ComparisonIndex. That array is
// read directly and the index is resolved with ue::NameFromIndex -- no engine
// call at all, so it cannot fault and costs a single cached walk per mesh.
// ---------------------------------------------------------------------------

// Names to look for, in priority order per joint. Matching is case-insensitive
// substring; the first hit wins.
struct JointName {
    Joint       joint;
    const char* patterns[4];   // null-terminated list
};

// The pattern sets cover the three conventions seen across UE titles: Biped
// ("Bip001-L-Hand"), Unreal Mannequin ("lowerarm_l") and plain English.
static const JointName kNames[] = {
    { Head,      { "head",        "skull",       nullptr, nullptr } },
    { Neck,      { "neck",        "neck_01",     nullptr, nullptr } },
    { Chest,     { "spine_03",    "spine3",      "upperchest", "chest" } },
    { Spine,     { "spine_01",    "spine1",      "spine",     nullptr } },
    { Pelvis,    { "pelvis",      "hips",        "bip001-pelvis", nullptr } },

    { LShoulder, { "l-clavicle",  "clavicle_l",  "shoulder_l", "l-shoulder" } },
    { LElbow,    { "l-upperarm",  "upperarm_l",  "l-arm",      "upperarm_l" } },
    { LHand,     { "l-hand",      "hand_l",      "l-forearm",  "lowerarm_l" } },

    { RShoulder, { "r-clavicle",  "clavicle_r",  "shoulder_r", "r-shoulder" } },
    { RElbow,    { "r-upperarm",  "upperarm_r",  "r-arm",      "upperarm_r" } },
    { RHand,     { "r-hand",      "hand_r",      "r-forearm",  "lowerarm_r" } },

    { LHip,      { "l-thigh",     "thigh_l",     "l-leg",      "thigh_l" } },
    { LKnee,     { "l-calf",      "calf_l",      "l-shin",     "calf_l" } },
    { LFoot,     { "l-foot",      "foot_l",      "l-ankle",    "foot_l" } },

    { RHip,      { "r-thigh",     "thigh_r",     "r-leg",      "thigh_r" } },
    { RKnee,     { "r-calf",      "calf_r",      "r-shin",     "calf_r" } },
    { RFoot,     { "r-foot",      "foot_r",      "r-ankle",    "foot_r" } },
};

static void Lower(std::string& s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
}

// Does this bone name satisfy any pattern for this joint?
static bool Matches(const std::string& lowered, const char* const* pats) {
    for (int i = 0; i < 4 && pats[i]; ++i)
        if (lowered.find(pats[i]) != std::string::npos) return true;
    return false;
}

// Cache: one rig per mesh component. Meshes are stable for a character's life.
static const int kMaxRigs = 64;
static Rig s_rigs[kMaxRigs];
static int s_rigCount = 0;

static Rig* FindRig(uintptr_t mesh) {
    for (int i = 0; i < s_rigCount; ++i)
        if (s_rigs[i].mesh == mesh) return &s_rigs[i];
    return nullptr;
}

bool Resolve(uintptr_t mesh, Rig& out) {
    out = Rig{};
    out.mesh = mesh;
    if (!ue::PtrOk(mesh)) return false;

    if (Rig* cached = FindRig(mesh)) { out = *cached; return out.usable; }

    // bone count: prefer the array Max (Num is not maintained in this build --
    // see bones.cpp for the measurement)
    int32_t num = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
    if (num <= 0)
        num = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
    if (num <= 0 || num > 512) {
        num = ue::RI32(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Max);
    }
    if (num <= 0 || num > 512) return false;

    for (int i = 0; i < JointCount; ++i) out.index[i] = -1;

    // -------------------------------------------------------------------
    // BONE NAMES -- READ THE REFERENCE SKELETON ARRAY FROM MEMORY.
    //
    // Calling USkinnedMeshComponent::GetBoneName produced nothing but SEH
    // faults on this build (0 joints, first bone='' on every mesh, both under
    // the old sret ABI and under the corrected RAX one), so stop calling it.
    // The same names live in plain memory:
    //
    //   mesh +0x4D0               -> USkeletalMesh*          (SDK: 0x04D0)
    //   SkeletalMesh + ???        -> FReferenceSkeleton {
    //       TArray<FMeshBoneInfo> RawRefBoneInfo;   // FName(8) + parent(4)
    //       ...
    //   }
    //
    // Dumper-7 does not print FReferenceSkeleton members, so DISCOVER the
    // array: scan the SkeletalMesh object for a TArray whose count matches
    // the bone count, then verify the first entries resolve to real names via
    // the engine's own AppendString path (ue::NameFromIndex -- proven by
    // ue::NameOf). FMeshBoneInfo stride is 16 (8 FName + 4 parent + pad).
    // -------------------------------------------------------------------
    uintptr_t skm = ue::R64(mesh + 0x4D0);
    uintptr_t nameArr = 0;
    int32_t   nameStride = 16;
    if (ue::PtrOk(skm)) {
        for (uint32_t fo = 0x40; fo < 0x900 && !nameArr; fo += 8) {
            const uintptr_t p = ue::R64(skm + fo);
            const int32_t  n = ue::RI32(skm + fo + 8);
            if (!ue::PtrOk(p) || n != num) continue;

            // verify: entry 0 and entry min(1,n-1) must resolve to names
            char probe[2][64] = {{0}, {0}};
            bool ok = true;
            for (int k = 0; k < 2 && n > k; ++k) {
                const uint32_t idx = (uint32_t)ue::RI32(p + (size_t)k * 16);
                if (!ue::NameFromIndex(idx, probe[k], sizeof(probe[0]))) { ok = false; break; }
            }
            if (!ok) continue;

            nameArr = p;
            LOG(SBOX("[skel] refskeleton FOUND skm=0x%llX +0x%X num=%d first='%s'", 0x1A2),
                (unsigned long long)skm, fo, n, probe[0]);
            break;
        }
    }
    if (!nameArr) {
        LOG(SBOX("[skel] no refskeleton array on mesh 0x%llX (skm=0x%llX num=%d)", 0x1A3),
            (unsigned long long)mesh, (unsigned long long)skm, num);
        if (s_rigCount < kMaxRigs) s_rigs[s_rigCount++] = out;
        return false;
    }

    int matched = 0;
    for (int32_t i = 0; i < num; ++i) {
        // FMeshBoneInfo: FName ComparisonIndex @ +0 (4B). Read straight from
        // the discovered RawRefBoneInfo array -- no engine calls at all.
        const uint32_t nameIdx = (uint32_t)ue::RI32(nameArr + (size_t)i * nameStride);
        std::string name;
        {
            char tmp[128] = {0};
            if (!ue::NameFromIndex(nameIdx, tmp, sizeof(tmp))) continue;
            name = tmp;
        }
        if (name.empty()) continue;
        if (out.firstName[0] == 0)
            strncpy_s(out.firstName, sizeof(out.firstName), name.c_str(), _TRUNCATE);

        std::string low = name;
        Lower(low);

        for (const JointName& jn : kNames) {
            if (out.index[jn.joint] >= 0) continue;      // already found
            if (Matches(low, jn.patterns)) {
                out.index[jn.joint] = i;
                ++matched;
                break;
            }
        }
        if (matched >= JointCount) break;
    }

    out.resolved = true;
    // A real skeleton needs the spine plus at least one full limb chain.
    const bool core = out.index[Head] >= 0 && out.index[Pelvis] >= 0;
    const bool limb = (out.index[LHand] >= 0 && out.index[LElbow] >= 0) ||
                      (out.index[RHand] >= 0 && out.index[RElbow] >= 0);
    out.usable = core && limb && matched >= 8;

    // ------------------------------------------------------------------
    // ONE-SHOT NAME CENSUS -- reads the refskeleton array, not GetBoneName.
    //
    // The rig on this title starts with "Root" and matches none of the Biped
    // ("Bip001-*") or Mannequin ("*_l") patterns in kNames, which is why only a
    // handful of joints ever resolve and the skeleton degrades to a stick figure.
    //
    // Guessing more patterns would be blind work, so the first two resolved meshes
    // dump their real bone names and the table is corrected from that evidence.
    //
    // IMPORTANT: this reads `nameArr`, the same FName array the matching loop
    // above uses, because that is the path that WORKS. The earlier version called
    // SafeGetBoneName/GetBoneName, which faults on this build, so the census
    // printed nothing at all and the fix could never be derived.
    // ------------------------------------------------------------------
    {
        static int s_censusLeft = 2;
        if (s_censusLeft > 0) {
            --s_censusLeft;
            LOG(SBOX("[skel] bone names on mesh 0x%llX (%d total):", 0x1A1),
                (unsigned long long)mesh, num);
            int shown = 0, emptyName = 0;
            for (int32_t i = 0; i < num && shown < 48; ++i) {
                const uint32_t idx = (uint32_t)ue::RI32(nameArr + (size_t)i * nameStride);
                char tmp[128] = {0};
                if (!ue::NameFromIndex(idx, tmp, sizeof(tmp))) {
                    ++emptyName;
                    continue;
                }
                LOG(SBOX("[skel]   [%d] %s", 0x1A2), i, tmp);
                ++shown;
            }
            LOG(SBOX("[skel] census end: shown=%d emptyName=%d", 0x1A4), shown, emptyName);
        }
    }

    if (s_rigCount < kMaxRigs) s_rigs[s_rigCount++] = out;

    LOG(SBOX("[skel] rig on mesh 0x%llX: %d joints matched, usable=%d, first bone='%s'", 0x1A0),
        (unsigned long long)mesh, matched, (int)out.usable, out.firstName);
    return out.usable;
}

// ---------------------------------------------------------------------------
// read the pose
// ---------------------------------------------------------------------------
bool ReadPose(uintptr_t mesh, const Rig& rig, Pose& out) {
    out = Pose{};
    if (!ue::PtrOk(mesh) || !rig.usable) return false;

    uintptr_t data = ue::R64(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
    int32_t   num  = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
    if (num <= 0) num = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
    if (!ue::PtrOk(data) || num <= 0 || num > 512) {
        data = ue::R64(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Data);
        num  = ue::RI32(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Num);
        if (num <= 0) num = ue::RI32(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Max);
    }
    if (!ue::PtrOk(data) || num <= 0 || num > 512) return false;

    // component -> world
    const uintptr_t c2w = mesh + off::Comp_ComponentToWorld;
    float q[4], t[3];
    q[0] = ue::RF32(c2w + 0x00); q[1] = ue::RF32(c2w + 0x04);
    q[2] = ue::RF32(c2w + 0x08); q[3] = ue::RF32(c2w + 0x0C);
    t[0] = ue::RF32(c2w + 0x10); t[1] = ue::RF32(c2w + 0x14); t[2] = ue::RF32(c2w + 0x18);

    float qlen = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (qlen < 1e-4f) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; }   // identity
    else if (!(qlen > 0.5f && qlen < 1.5f)) return false;

    int found = 0;
    for (int j = 0; j < JointCount; ++j) {
        const int idx = rig.index[j];
        if (idx < 0 || idx >= num) continue;

        // FTransform is 0x30; translation at +0x10
        const uintptr_t f = data + (uintptr_t)idx * 0x30;
        float cs[3];
        cs[0] = ue::RF32(f + 0x10);
        cs[1] = ue::RF32(f + 0x14);
        cs[2] = ue::RF32(f + 0x18);

        // rotate by the component quaternion, then translate
        const float x = cs[0], y = cs[1], z = cs[2];
        const float tx = 2.0f * (q[1]*z - q[2]*y);
        const float ty = 2.0f * (q[2]*x - q[0]*z);
        const float tz = 2.0f * (q[0]*y - q[1]*x);
        out.pos[j][0] = x + q[3]*tx + (q[1]*tz - q[2]*ty) + t[0];
        out.pos[j][1] = y + q[3]*ty + (q[2]*tx - q[0]*tz) + t[1];
        out.pos[j][2] = z + q[3]*tz + (q[0]*ty - q[1]*tx) + t[2];
        out.ok[j] = true;
        ++found;
    }

    out.any = found > 0;
    out.full = out.ok[Head] && out.ok[Pelvis];
    return out.full;
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void Draw(void* canvas, const Pose& pose, float r, float g, float b, float a) {
    if (!canvas || !pose.full) return;

    // Project each joint once; joints are shared between bones so projecting in
    // the draw loop would do the work two or three times per frame.
    float sx[JointCount], sy[JointCount];
    bool  ok[JointCount];
    for (int j = 0; j < JointCount; ++j) {
        ok[j] = false;
        if (!pose.ok[j]) continue;
        sdk::Vec2 sp;
        if (!sdk::Project(canvas, sdk::Vec3(pose.pos[j][0], pose.pos[j][1], pose.pos[j][2]), sp))
            continue;
        sx[j] = sp.X; sy[j] = sp.Y; ok[j] = true;
    }

    const sdk::LinearColor col(r, g, b, a);

    auto link = [&](Joint A, Joint B) {
        if (!ok[A] || !ok[B]) return;
        sdk::DrawLine(canvas, { sx[A], sy[A] }, { sx[B], sy[B] }, 1.6f, col);
    };

    // ---- spine ----
    link(Head, Neck);
    link(Neck, Chest);
    link(Chest, Spine);
    link(Spine, Pelvis);

    // ---- arms ----
    // The shoulder joint is anchored to the chest so the arm does not float when
    // the clavicle bone is missing.
    if (ok[LShoulder] && ok[RShoulder]) {
        link(Chest, LShoulder);
        link(Chest, RShoulder);
    }
    link(LShoulder, LElbow); link(LElbow, LHand);
    link(RShoulder, RElbow); link(RElbow, RHand);

    // ---- legs ----
    link(Pelvis, LHip); link(LHip, LKnee); link(LKnee, LFoot);
    link(Pelvis, RHip); link(RHip, RKnee); link(RKnee, RFoot);

    // ---- head marker so the aim point is readable ----
    if (ok[Head])
        sdk::DrawLine(canvas, { sx[Head] - 3.0f, sy[Head] }, { sx[Head] + 3.0f, sy[Head] }, 1.6f, col);
}

} // namespace skeleton
