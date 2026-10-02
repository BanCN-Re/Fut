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
#include <cctype>
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

// ---------------------------------------------------------------------------
// IsGarbageName -- reject entries that are not bone names.
//
// The ReferenceSkeleton array on this title runs past its real bone data into
// unrelated memory, which still resolves through the FName table. Measured
// examples from the census:
//
//     'leCinematic/T_Dynamic_RoleCinematic_940016.T_Dynamic_...'   an asset path
//     'SetActionLongPressTimeThreashold'                            a config key
//     'BasedMatchInterface'                                         an interface name
//     '????y????y??...'                                             raw bytes
//
// A bone name is a short identifier. These rules encode that:
//
//   * length 1..40        (the longest real bone here is 25 chars)
//   * no '/' or '.'       (asset paths carry both)
//   * no run of 4+ identical characters (the '????' runs)
//   * mostly ASCII printable
//
// None of the ~40 real bone names measured on this rig violate any rule, and all
// four garbage classes above violate at least one.
// ---------------------------------------------------------------------------
static bool IsGarbageName(const char* s) {
    if (!s || !s[0]) return true;

    int len = 0;
    int runChar = -1, runLen = 0, maxRun = 0;
    for (const char* p = s; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '.') return true;          // path or dotted name
        if (c < 0x20 || c == 0x7F) return true;         // control byte

        // '+' is not in any bone name; it appears in the FName "Number" suffix
        // format ("Name_1"), so it is allowed, but '?' is not.
        if (c == '?') return true;

        if ((int)c == runChar) { ++runLen; if (runLen > maxRun) maxRun = runLen; }
        else { runChar = (int)c; runLen = 1; }

        if (++len > 40) return true;
    }
    if (maxRun >= 4) return true;
    return len < 1;
}
static void Lower(std::string& s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
}

// ---------------------------------------------------------------------------
// PARENT INFERENCE FROM TRANSFORM MATH
//
// WHY THIS EXISTS: the parent INDEX FIELD COULD NOT BE TRUSTED.
//
// Two attempts to read a parent index out of the ReferenceSkeleton array both
// produced plausible-looking numbers that drew a starburst radiating from the head
// instead of a skeleton. The raw dump showed why -- the array at the discovered
// stride is not a clean FMeshBoneInfo list:
//
//     [0] D1 7D 00 00 00 00 00 00 | FF FF FF FF | 1F AE 7A 01 00 00 00 00
//     [1] 28 3F 70 01 00 00 00 00 | 01 00 00 00 | 62 F9 5E 01 ...
//
// 8-byte values interleaved with small integers. The "parent" read at +0x08 is
// whichever small integer happens to sit there, and it passed a range check while
// pointing at the wrong bone. A range check cannot tell a correct parent from a
// wrong one -- it only rejects nonsense, and this data has no nonsense in it.
//
// THE UE RELATIONSHIP THAT MAKES INFERENCE POSSIBLE
//
// A USkinnedMeshComponent carries TWO transform arrays, and their relationship is
// exact:
//
//     CachedComponentSpaceTransforms[i]  = bone i in COMPONENT space
//     CachedBoneSpaceTransforms[i]       = bone i RELATIVE TO ITS PARENT
//
//     ComponentSpace[i] = BoneSpace[i] * ComponentSpace[parent[i]]
//
// Rearranged, that gives the parent's own component-space translation without
// knowing who the parent is:
//
//     ComponentSpace[parent] = BoneSpace[i]^-1 * ComponentSpace[i]
//
// So for each bone: compute where its parent MUST be, then find the bone whose
// recorded component-space position is closest to that. That bone is the parent.
// It uses only geometry the engine already computed, so it cannot be fooled by a
// field offset or a naming convention -- and it works for every bone including the
// ones whose names never resolve.
//
// Cost: O(n^2) distance checks, ~1700 for a 41-bone rig, computed ONCE per mesh and
// cached with the rig.
// ---------------------------------------------------------------------------
static void QuatRotate(const float q[4], const float v[3], float out[3]) {
    // v' = v + 2*q.w*(q.xyz x v) + 2*(q.xyz x (q.xyz x v))
    const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
    const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
    const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static void QuatConjugate(const float q[4], float out[4]) {
    out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
}

// Returns the number of bones whose parent was inferred.
static int InferParents(uintptr_t mesh, int count, Rig& rig) {
    if (count < 2 || count > Rig::kMaxBones) return 0;

    // ---- read both arrays ---------------------------------------------------
    struct Xf { float q[4]; float t[3]; };
    static thread_local Xf comp[Rig::kMaxBones];
    static thread_local Xf bone[Rig::kMaxBones];

    auto readArray = [&](uint32_t off, Xf* dst) -> bool {
        const uintptr_t d = ue::R64(mesh + off + off::TArray_Data);
        int32_t n = ue::RI32(mesh + off + off::TArray_Num);
        if (n <= 0) n = ue::RI32(mesh + off + off::TArray_Max);
        if (!ue::PtrOk(d) || n < count) return false;

        const size_t bytes = (size_t)count * 0x30;
        static thread_local uint8_t buf[Rig::kMaxBones * 0x30];
        if (bytes > sizeof(buf)) return false;
        if (!ue::Read(d, buf, bytes)) return false;

        for (int i = 0; i < count; ++i) {
            const uint8_t* f = buf + (size_t)i * 0x30;
            memcpy(dst[i].q, f + 0x00, 16);
            memcpy(dst[i].t, f + 0x10, 12);
        }
        return true;
    };

    if (!readArray(off::Skinned_CachedComponentSpaceTransforms, comp)) return 0;
    if (!readArray(off::Skinned_CachedBoneSpaceTransforms,      bone)) return 0;

    // ---- infer ---------------------------------------------------------------
    const float kMaxDistSq = 4.0f * 4.0f;   // 4 uu: bones are cm apart at least

    int inferred = 0;
    rig.parent[0] = -1;                     // the root is its own parent

    for (int i = 1; i < count; ++i) {
        // BoneSpace quaternion must be sane before it can be inverted
        float bl = sqrtf(bone[i].q[0]*bone[i].q[0] + bone[i].q[1]*bone[i].q[1] +
                         bone[i].q[2]*bone[i].q[2] + bone[i].q[3]*bone[i].q[3]);
        if (!(bl > 0.5f && bl < 1.5f)) bl = 1.0f;

        float inv[4];
        QuatConjugate(bone[i].q, inv);

        // parentPos = inv * (comp[i].t - bone[i].t)
        const float d[3] = { comp[i].t[0] - bone[i].t[0],
                             comp[i].t[1] - bone[i].t[1],
                             comp[i].t[2] - bone[i].t[2] };
        float want[3];
        QuatRotate(inv, d, want);

        // nearest bone by component-space position
        int best = -1;
        float bestD = kMaxDistSq;
        for (int j = 0; j < count; ++j) {
            if (j == i) continue;
            const float dx = comp[j].t[0] - want[0];
            const float dy = comp[j].t[1] - want[1];
            const float dz = comp[j].t[2] - want[2];
            const float dd = dx*dx + dy*dy + dz*dz;
            if (dd < bestD) { bestD = dd; best = j; }
        }

        rig.parent[i] = best;                // -1 when nothing was close enough
        if (best >= 0) ++inferred;
    }

    return inferred;
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
    // BONE NAMES -- READ THE REFERENCE SKELETON ARRAY, AND DISCOVER ITS STRIDE.
    //
    // THE BUG THAT MADE EVERY SKELETON WRONG
    //
    // The stride was HARDCODED to 16 (8-byte FName + 4-byte parent + padding).
    // The measured census shows the real FName slots are 48 bytes apart -- every
    // three entries:
    //
    //     [0] Root                    <-- real
    //     [1] None                        garbage
    //     [2] neByteProperty?IntProperty  garbage
    //     [3] Bip001-Spine1           <-- real
    //     [4] None                        garbage
    //     [5] Property?IntProperty        garbage
    //     [6] Bip001-L-Clavicle       <-- real
    //
    // So reading with stride 16 walked into the middle of each record: only one in
    // three reads landed on a name, the rest produced whatever bytes happened to be
    // there, and the JOINT INDICES were three times too small. Both the count and
    // the positions were wrong -- which is exactly the reported "the skeleton has
    // never been right, and moving left/right makes it worse".
    //
    // WHY THE OLD VERIFICATION PASSED ANYWAY
    //
    // It probed only entry 0. Entry 0 IS a real name ("Root") at every stride, so
    // the check could not fail and the wrong stride was accepted silently. A
    // verification that cannot fail is not a verification.
    //
    // THE FIX -- DERIVE THE STRIDE, DO NOT ASSUME IT
    //
    // For each candidate stride, count how many of the slots resolve to a
    // plausible name. The right stride makes nearly ALL of them resolve (bone names
    // are a dense run); a wrong stride makes roughly one in three. Pick the stride
    // with the best score, and require it to be well above the wrong-stride rate
    // before accepting the array at all.
    //
    // This is self-correcting: a patch that changes FMeshBoneInfo is handled
    // automatically, and the log prints what was chosen so it is never a mystery.
    // -------------------------------------------------------------------
    uintptr_t skm = ue::R64(mesh + 0x4D0);
    uintptr_t nameArr = 0;
    int32_t   nameStride = 0;
    int32_t   bestHits = -1;

    // A name is "plausible" when it resolves AND looks like an identifier rather
    // than a property/type dump. The garbage entries in the census
    // ("neByteProperty?IntProperty") come from reading mid-record, so requiring
    // identifier-ish characters rejects them even before the count is compared.
    auto plausibleName = [](const char* s) -> bool {
        if (!s || !s[0]) return false;
        int len = 0, alnum = 0;
        for (const char* p = s; *p; ++p) {
            const unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7F) return false;      // control junk
            if (c >= 0x80) return true;                    // CJK: accept as-is
            if (isalnum(c) || c == '_' || c == '-' || c == '.' || c == ' ') ++alnum;
            if (++len > 96) return false;                  // absurdly long
        }
        // Bone names are mostly alphanumeric; type-dump garbage is not.
        return len > 0 && (alnum * 10) / len >= 8;
    };

    if (ue::PtrOk(skm)) {
        for (uint32_t fo = 0x40; fo < 0x900 && !nameArr; fo += 8) {
            const uintptr_t p = ue::R64(skm + fo);
            const int32_t  n = ue::RI32(skm + fo + 8);
            if (!ue::PtrOk(p) || n != num || n < 2) continue;

            // Try the strides UE actually uses across builds.
            //
            // ===============================================================
            // SCORING BY NAME COUNT ALONE DOES NOT WORK -- MEASURED TWICE.
            //
            // Stride 24 and stride 48 BOTH scored 24/24 on a sample of the first 24
            // entries, because a 24-byte walk lands on real FName slots for the
            // early records before drifting into the middle of them. The sample
            // window was too short to reach the drift, so the wrong stride was
            // selected on two consecutive runs.
            //
            // THE DECISIVE TEST IS THE PARENT FIELD.
            //
            // The raw dump of record 0 shows exactly what a bone record looks like:
            //
            //     +0x00  D1 7D 00 00 00 00 00 00   FName (8 bytes)
            //     +0x08  FF FF FF FF               parent = -1   <- THE ROOT
            //     +0x0C  1F AE 7A 01
            //     +0x10  00 00 00 00 00 00 00 00
            //     +0x18  28 3F 70 01 ...           next record begins
            //
            // So parent is at +0x08 and the true stride is 48 (0x18 was the midline,
            // 0x30 is the next record).
            //
            // WHAT "CONSISTENT" MEANS -- MEASURED, NOT ASSUMED.
            //
            // The first version required parent[i] < i (strict topological order).
            // The same raw dump disproves that for this rig:
            //
            //     [0] 'Root'            parent = -1     root
            //     [1] 'Bip001-Pelvis'   parent =  1     <-- POINTS AT ITSELF
            //     [2] 'Bip001-Spine1'   parent =  0
            //     [3] 'Bip001-Neck'     parent =  0
            //
            // Parent 1 for entry 1 is a SELF-reference, which is how this rig marks a
            // bone whose parent was not exported. Under a strict `< i` rule that one
            // entry rejected the entire array -- every mesh logged "no refskeleton
            // array", which is exactly what happened.
            //
            // The rule that holds is weaker and still discriminating: every parent is
            // a valid INDEX into the same array (-1, self, or 0..n-1). A wrong stride
            // reads pointer fragments in the hundreds of thousands, so it fails that
            // test outright and nothing is lost by relaxing it.
            // ===============================================================
            static const int32_t kStrides[] = { 48, 16, 32, 24, 64, 40 };
            int32_t bestLocal = 0, bestLocalScore = -1;

            for (int32_t st : kStrides) {
                if (st <= 0) continue;

                int named = 0, parentOk = 0;
                for (int32_t k = 0; k < n; ++k) {
                    const uintptr_t rec = p + (size_t)k * st;

                    char tmp[128] = {0};
                    const bool hasName =
                        ue::NameFromIndex((uint32_t)ue::RI32(rec), tmp, sizeof(tmp)) &&
                        tmp[0] != 0 && !IsGarbageName(tmp);
                    if (!hasName) continue;
                    ++named;

                    const int32_t par = ue::RI32(rec + 0x08);
                    if (par == -1 || par == k || (par >= 0 && par < n)) ++parentOk;
                }

                const int32_t score = parentOk * 100 + named;   // consistency dominates
                if (score > bestLocalScore) {
                    bestLocalScore = score;
                    bestLocal = st;
                }
            }

            // Accept when the parent field is index-like for a drawable prefix. A
            // correct stride yields ~40; garbage produces raw pointers and scores 0.
            {
                int32_t parOk = 0;
                for (int32_t k = 0; k < n; ++k) {
                    const uintptr_t rec = p + (size_t)k * bestLocal;
                    const int32_t par = ue::RI32(rec + 0x08);
                    if (par == -1 || par == k || (par >= 0 && par < n)) ++parOk;
                    else break;                       // prefix only, as in Resolve
                }
                if (parOk < 8) continue;
            }

            nameArr    = p;
            nameStride = bestLocal;

            char probe0[64] = {0};
            ue::NameFromIndex((uint32_t)ue::RI32(nameArr), probe0, sizeof(probe0));
            LOG(SBOX("[skel] refskeleton FOUND skm=0x%llX +0x%X num=%d stride=%d score=%d first='%s'", 0x1A2),
                (unsigned long long)skm, fo, n, nameStride, bestLocalScore, probe0);
            break;
        }
    }
    if (!nameArr) {
        LOG(SBOX("[skel] no refskeleton array on mesh 0x%llX (skm=0x%llX num=%d)", 0x1A3),
            (unsigned long long)mesh, (unsigned long long)skm, num);
        if (s_rigCount < kMaxRigs) s_rigs[s_rigCount++] = out;
        return false;
    }
    (void)bestHits;

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

    // ------------------------------------------------------------------
    // HIERARCHY -- READ THE PARENT INDEX FOR EVERY BONE.
    //
    // FMeshBoneInfo layout at the discovered stride:
    //
    //     +0x00  FName ComparisonIndex   (4 bytes)   <- the name we already read
    //     +0x04  FName Number            (4 bytes)
    //     +0x08  int32  ParentIndex                  <- this
    //
    // The parent index is what makes a COMPLETE skeleton possible without names.
    // See the note in skeleton.h: the name table on this title is incomplete (no
    // Head, no Thigh, and garbage past index 40), so a name-driven draw can never
    // be right, while parent links are present and correct for every bone.
    //
    // Validation: every parent must be a valid index into the array (-1, self, or
    // 0..n-1). NOT strictly "parent < i": the measured dump shows this rig uses a
    // SELF-reference for a bone whose parent was not exported
    //
    //     [0] 'Root'            parent = -1
    //     [1] 'Bip001-Pelvis'   parent =  1    <-- self
    //
    // and a strict ordering test rejected the whole array on that single entry.
    // ------------------------------------------------------------------
    out.boneCount = 0;
    out.hierarchyOk = false;
    {
        const int32_t n = (num < Rig::kMaxBones) ? num : Rig::kMaxBones;

        // -------------------------------------------------------------------
        // PARENTS ARE INFERRED FROM GEOMETRY, NOT READ FROM A FIELD.
        //
        // See the long note on InferParents above: the parent index in this
        // ReferenceSkeleton array is not at a trustworthy offset, and two attempts to
        // read it drew a starburst. InferParents derives each bone's parent from the
        // exact UE relationship between the component-space and bone-space transform
        // arrays, which needs no field offset and no bone names.
        //
        // This is the ONLY parent source now. The field read is gone rather than kept
        // as a fallback, because a wrong parent draws confident nonsense -- worse
        // than drawing nothing.
        // -------------------------------------------------------------------
        const int inferred = InferParents(mesh, n, out);

        // A usable skeleton needs the chain, not merely a set of points. Count how
        // many bones got a parent AND how much of the body that represents.
        out.boneCount = n;

        // A starburst is what a WRONG parent set looks like: every bone pointing at
        // one hub. Require the inferred parents to form a spread-out tree instead --
        // no single bone may be the parent of more than a third of the rig, and at
        // least a handful of bones must be parents at all.
        int childCount[Rig::kMaxBones] = {0};
        int used = 0;
        for (int i = 1; i < n; ++i) {
            const int p = out.parent[i];
            if (p >= 0 && p < n) { ++childCount[p]; ++used; }
        }
        int maxChildren = 0, parentsUsed = 0;
        for (int i = 0; i < n; ++i) {
            if (childCount[i] > maxChildren) maxChildren = childCount[i];
            if (childCount[i] > 0) ++parentsUsed;
        }

        const bool spread = (n > 0) && (maxChildren * 3 <= n);
        out.hierarchyOk = (inferred >= 8) && (used >= 8) && spread && (parentsUsed >= 4);

        static int s_hierLogged = 0;
        if (s_hierLogged++ < 4) {
            LOG(SBOX("[skel] hierarchy mesh=0x%llX bones=%d inferred=%d used=%d maxChild=%d parents=%d ok=%d", 0x1A5),
                (unsigned long long)mesh, n, inferred, used, maxChildren, parentsUsed,
                (int)out.hierarchyOk);
        }
    }

    // A real skeleton needs the spine plus at least one full limb chain -- OR a
    // valid hierarchy, which is strictly better evidence than any name match.
    const bool core = out.index[Head] >= 0 && out.index[Pelvis] >= 0;
    const bool limb = (out.index[LHand] >= 0 && out.index[LElbow] >= 0) ||
                      (out.index[RHand] >= 0 && out.index[RElbow] >= 0);
    out.usable = out.hierarchyOk || (core && limb && matched >= 8);

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
        static int s_censusLeft = 1;
        if (s_censusLeft > 0) {
            --s_censusLeft;
            LOG(SBOX("[skel] bone names on mesh 0x%llX (%d total):", 0x1A1),
                (unsigned long long)mesh, num);
            int shown = 0, emptyName = 0;
            for (int32_t i = 0; i < num && shown < 200; ++i) {
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

    // Shared bone->world conversion. Used both for the named joints and for the
    // full hierarchy pass below.
    auto boneWorld = [&](int idx, float out3[3]) -> bool {
        if (idx < 0 || idx >= num) return false;
        const uintptr_t f = data + (uintptr_t)idx * 0x30;   // FTransform, 0x30 bytes
        const float cs0 = ue::RF32(f + 0x10);
        const float cs1 = ue::RF32(f + 0x14);
        const float cs2 = ue::RF32(f + 0x18);
        if (!(fabsf(cs0) < 1e6f && fabsf(cs1) < 1e6f && fabsf(cs2) < 1e6f)) return false;

        const float tx = 2.0f * (q[1]*cs2 - q[2]*cs1);
        const float ty = 2.0f * (q[2]*cs0 - q[0]*cs2);
        const float tz = 2.0f * (q[0]*cs1 - q[1]*cs0);
        out3[0] = cs0 + q[3]*tx + (q[1]*tz - q[2]*ty) + t[0];
        out3[1] = cs1 + q[3]*ty + (q[2]*tx - q[0]*tz) + t[1];
        out3[2] = cs2 + q[3]*tz + (q[0]*ty - q[1]*tx) + t[2];
        return true;
    };

    // ---- named joints (used for the box anchors and the aim point) ----------
    for (int j = 0; j < JointCount; ++j) {
        float w[3];
        if (!boneWorld(rig.index[j], w)) continue;
        out.pos[j][0] = w[0]; out.pos[j][1] = w[1]; out.pos[j][2] = w[2];
        out.ok[j] = true;
        ++found;
    }

    // ---- full hierarchy ------------------------------------------------------
    //
    // Every bone, in world space, so the skeleton can be drawn from parent links
    // instead of from a name table that this title does not fully expose. See the
    // note in skeleton.h.
    //
    // Reading all ~79 transforms costs one 0x30-byte read each, and they are
    // contiguous, so this is a single sequential walk rather than a scattered one.
    int bonesOk = 0;
    if (rig.hierarchyOk) {
        const int n = (rig.boneCount < Pose::kMaxBones) ? rig.boneCount : Pose::kMaxBones;
        out.boneCount = n;
        for (int i = 0; i < n; ++i) {
            out.parent[i] = rig.parent[i];
            float w[3];
            if (!boneWorld(i, w)) continue;
            out.bonePos[i][0] = w[0];
            out.bonePos[i][1] = w[1];
            out.bonePos[i][2] = w[2];
            out.boneOk[i] = true;
            ++bonesOk;
        }
    }

    out.any = found > 0 || bonesOk > 0;
    out.full = out.ok[Head] && out.ok[Pelvis];

    // The skeleton is drawable when EITHER source produced enough joints. The
    // hierarchy is preferred by Draw() because it is complete.
    return (bonesOk >= 8) || out.full;
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void Draw(void* canvas, const Pose& pose, float r, float g, float b, float a) {
    if (!canvas) return;

    const sdk::LinearColor col(r, g, b, a);

    // -----------------------------------------------------------------------
    // PREFERRED PATH: PARENT LINKS.
    //
    // Draws a segment for every bone that has a parent. This is the complete
    // skeleton -- every limb, every finger -- and it needs no bone names, so it
    // cannot be broken by the incomplete name array this title exposes.
    //
    // A minimum segment LENGTH is applied: bones that sit on top of each other
    // (fingers, twists, sockets) project to almost the same pixel and would
    // otherwise produce a dense blob rather than a readable figure. 2 px keeps the
    // limbs and the spine while dropping the noise.
    // -----------------------------------------------------------------------
    {
        int drawn = 0;
        const int n = (pose.boneCount < Pose::kMaxBones) ? pose.boneCount : Pose::kMaxBones;
        for (int i = 1; i < n; ++i) {
            if (!pose.boneOk[i]) continue;
            const int p = pose.parent[i];
            if (p < 0 || p >= n || !pose.boneOk[p]) continue;

            sdk::Vec2 a, c;
            if (!sdk::Project(canvas, sdk::Vec3(pose.bonePos[p][0], pose.bonePos[p][1], pose.bonePos[p][2]), a))
                continue;
            if (!sdk::Project(canvas, sdk::Vec3(pose.bonePos[i][0], pose.bonePos[i][1], pose.bonePos[i][2]), c))
                continue;

            const float dx = c.X - a.X, dy = c.Y - a.Y;
            if (dx * dx + dy * dy < 4.0f) continue;      // < 2 px: skip the clutter

            sdk::DrawLine(canvas, { a.X, a.Y }, { c.X, c.Y }, 1.4f, col);
            ++drawn;
        }
        if (drawn > 0) {
            // Head marker: the aim point, so it is readable at a glance.
            if (pose.ok[Head]) {
                sdk::Vec2 h;
                if (sdk::Project(canvas, sdk::Vec3(pose.pos[Head][0], pose.pos[Head][1], pose.pos[Head][2]), h))
                    sdk::DrawLine(canvas, { h.X - 3.0f, h.Y }, { h.X + 3.0f, h.Y }, 1.6f, col);
            }
            return;
        }
    }

    // -----------------------------------------------------------------------
    // FALLBACK: THE NAMED JOINT TABLE.
    //
    // Reached only when the hierarchy is unavailable. Kept because it still yields
    // a correct spine and box anchors, which is better than nothing.
    // -----------------------------------------------------------------------
    if (!pose.full) return;

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

    auto link = [&](Joint A, Joint B) {
        if (!ok[A] || !ok[B]) return;
        sdk::DrawLine(canvas, { sx[A], sy[A] }, { sx[B], sy[B] }, 1.6f, col);
    };

    link(Head, Neck);
    link(Neck, Chest);
    link(Chest, Spine);
    link(Spine, Pelvis);

    if (ok[LShoulder] && ok[RShoulder]) {
        link(Chest, LShoulder);
        link(Chest, RShoulder);
    }
    link(LShoulder, LElbow); link(LElbow, LHand);
    link(RShoulder, RElbow); link(RElbow, RHand);

    link(Pelvis, LHip); link(LHip, LKnee); link(LKnee, LFoot);
    link(Pelvis, RHip); link(RHip, RKnee); link(RKnee, RFoot);

    if (ok[Head])
        sdk::DrawLine(canvas, { sx[Head] - 3.0f, sy[Head] }, { sx[Head] + 3.0f, sy[Head] }, 1.6f, col);
}

} // namespace skeleton
