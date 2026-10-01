// D:\Work\Fut\cheat\src\bones.cpp
#include "bones.h"
#include "ue.h"
#include "offsets.h"
#include "log.h"
#include "obf.h"

#include <windows.h>
#include <cmath>
#include <cstdio>

namespace bones {

// ---------------------------------------------------------------------------
// Component-space bone transforms
//
// USkeletalMeshComponent: CachedComponentSpaceTransforms is a TArray<FTransform>
// at +0x0800 (0x07F0 is the bone-space mirror). It is filled during
// UpdateTransform and is valid for any character that is being rendered.
//
// FTransform is 0x30 bytes:  FQuat Rotation @0x00 (16B, x,y,z,w)
//                            FVector Translation @0x10 (12B)
//                            FVector Scale3D  @0x20 (12B)
//
// Those translations are in COMPONENT space, so we convert to world with the
// mesh component's own ComponentToWorld (USceneComponent +0x168, 0x30 bytes),
// which Dumper-7 shows as unexplained padding because it is not a UPROPERTY.
// ---------------------------------------------------------------------------

static bool Plausible(float v) {
    if (!(v == v)) return false;
    const float a = v < 0 ? -v : v;
    return a < 500000.0f;
}

// rotate component-space vector by the component's quaternion, then translate
static void ToWorld(const float q[4], const float t[3], const float p[3],
                    float out[3]) {
    const float x = p[0], y = p[1], z = p[2];
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];

    // v' = v + 2*qw*(q x v) + 2*(q x (q x v))
    const float tx = 2.0f * (qy * z - qz * y);
    const float ty = 2.0f * (qz * x - qx * z);
    const float tz = 2.0f * (qx * y - qy * x);

    out[0] = x + qw * tx + (qy * tz - qz * ty) + t[0];
    out[1] = y + qw * ty + (qz * tx - qx * tz) + t[1];
    out[2] = z + qw * tz + (qx * ty - qy * tx) + t[2];
}

static bool GetImpl(uintptr_t character, BoneSet& out);

bool Get(uintptr_t character, BoneSet& out) {
    out = BoneSet{};
    if (!ue::PtrOk(character)) return false;

    // -----------------------------------------------------------------------
    // READ THROTTLE -- the full pass re-reads and re-transforms every bone of
    // every character EVERY FRAME (246 bones x N entities), which is pure
    // waste: bone positions barely move in 100 ms. Cache the last BoneSet per
    // character for 100 ms and hand the caller the cached copy in between.
    // This is the main component of the "drawing makes the game laggy" report.
    // -----------------------------------------------------------------------
    struct Ent { uintptr_t a; ULONGLONG t; BoneSet bs; bool ok; };
    static Ent cache[64];
    static int cacheN = 0;
    {
        const ULONGLONG now = GetTickCount64();
        for (int i = 0; i < cacheN; ++i)
            if (cache[i].a == character) {
                if (now - cache[i].t <= 100) { out = cache[i].bs; return cache[i].ok; }
                cache[i].t = now;                 // refresh this slot
                const bool r = GetImpl(character, cache[i].bs);
                cache[i].ok = r;
                out = cache[i].bs;
                return r;
            }
        if (cacheN < 64) {
            cache[cacheN].a = character; cache[cacheN].t = now;
            cache[cacheN].ok = GetImpl(character, cache[cacheN].bs);
            out = cache[cacheN].bs;
            return cache[cacheN].ok;
        }
        // table full: recycle slot 0 (rare: >64 live characters)
        cache[0].a = character; cache[0].t = now;
        cache[0].ok = GetImpl(character, cache[0].bs);
        out = cache[0].bs;
        return cache[0].ok;
    }
}

static bool GetImpl(uintptr_t character, BoneSet& out) {
    out = BoneSet{};
    if (!ue::PtrOk(character)) return false;

    // -----------------------------------------------------------------------
    // Pick the RIGHT mesh component -- BY SEARCH, NOT BY GUESSED OFFSET.
    //
    // The dump lists several skeletal-mesh fields on this hierarchy
    // (ACharacter::Mesh @0x370, AWFBaseCharacter::MeshFor3PP, PartMeshes,
    // AllSkeletalMeshComponents, ...) and the dump's class attribution does not
    // reliably say which one holds the VISIBLE character. Trying a fixed list
    // FAILED IN PRACTICE: the pointers were valid but their transform arrays
    // were empty, so the probe logged "no usable mesh" while a full bone dump of
    // the very same actor showed 79 perfectly good bones.
    //
    // So instead of trusting an offset, walk the character's first 0x2000 bytes
    // and take the first pointer that looks like a USkeletalMeshComponent with a
    // populated bone array. That is self-locating: it works regardless of which
    // class in the hierarchy owns the mesh, and it survives a game patch that
    // moves the field.
    //
    // Cost: at most 0x2000/8 reads on the first frame for a new actor, and once a
    // component is found we stop immediately.
    // -----------------------------------------------------------------------
    uintptr_t mesh = 0;
    {
        auto boneArrayOk = [](uintptr_t m, uintptr_t& outData, int32_t& outNum) -> bool {
            if (!ue::PtrOk(m)) return false;
            uintptr_t d = ue::R64(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
            int32_t   n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
            if (n <= 0) n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
            if (!ue::PtrOk(d) || n <= 0 || n > 512) {
                d = ue::R64(m + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Data);
                n = ue::RI32(m + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Num);
                if (n <= 0) n = ue::RI32(m + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Max);
            }
            if (!ue::PtrOk(d) || n <= 0 || n > 512) return false;
            outData = d; outNum = n;
            return true;
        };

        // preferred offsets first, then a bounded structural scan
        const uint32_t preferred[] = { off::WF_MeshFor3PP, off::ACh_Mesh, off::WF_PaperMan2DMesh };
        uintptr_t d = 0; int32_t n = 0;
        for (uint32_t o : preferred) {
            if (boneArrayOk(ue::R64(character + o), d, n)) { mesh = ue::R64(character + o); break; }
        }
        if (!mesh) {
            // -------------------------------------------------------------------
            // Pick the component with the MOST bones, not the FIRST one found.
            //
            // A character carries several skeletal meshes: body, weapon,
            // cosmetics, attachments. Taking the first hit grabbed a 30-bone
            // weapon mesh (or a 1-bone attachment) whose world extent is nothing
            // like a humanoid, so the height sanity check then rejected it and the
            // probe logged "no bones" while the real body mesh sat right there.
            //
            // The body skeleton is by far the largest (60-90 bones here), so
            // preferring the maximum is robust and needs no magic bone count.
            // -------------------------------------------------------------------
            uintptr_t bestMesh = 0, bestData = 0;
            int32_t   bestNum = 0;
            uint32_t  bestOff = 0;
            for (uint32_t o = 0; o < 0x2000; o += 8) {
                uintptr_t m = ue::R64(character + o);
                uintptr_t dd = 0; int32_t nn = 0;
                if (boneArrayOk(m, dd, nn) && nn > bestNum) {
                    bestMesh = m; bestData = dd; bestNum = nn; bestOff = o;
                }
            }
            if (bestMesh) {
                mesh = bestMesh; d = bestData; n = bestNum;
                static int s_logged = 0;
                if (s_logged++ < 4)
                    LOG(SBOX("[bones] body mesh at char+0x%X with %d bones", 0xB5), bestOff, bestNum);
            }
        }
    }
    if (!ue::PtrOk(mesh)) return false;

    // bone transform array.
    //
    // USkeletalMeshComponent carries two:
    //   CachedComponentSpaceTransforms @0x0800  (what we want)
    //   CachedBoneSpaceTransforms      @0x07F0  (parent-relative)
    // Either can be the empty one depending on how far the animation update got,
    // so try component-space first and fall back to bone-space. Component-space
    // is preferred because its translations are already in component space,
    // which is what ComponentToWorld expects.
    // -----------------------------------------------------------------------
    // Num IS ZERO HERE -- USE Max.
    //
    // Measured on the live game:
    //     compSpace data=0x9E3660A0 num=0 max=85
    //     boneSpace data=0x9E3650B0 num=0
    //
    // The array IS allocated (85 bones -- exactly right for a humanoid skeleton)
    // and Data is a valid pointer, but Num is never maintained in this build.
    // Requiring num > 0 therefore rejected a perfectly good array, which is why
    // the bone probe failed on every character and the ESP fell back to a
    // capsule-sized box instead of a skeleton.
    //
    // So: prefer Num when set, fall back to Max when it is not. The 0x10000-byte
    // read ceiling further down still guards against a bogus Max.
    // -----------------------------------------------------------------------
    uintptr_t data = ue::R64(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
    int32_t   num  = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
    if (num <= 0)
        num = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
    if (!ue::PtrOk(data) || num <= 0 || num > 512) {
        data = ue::R64(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Data);
        num  = ue::RI32(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Num);
        if (num <= 0)
            num = ue::RI32(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Max);
    }
    if (!ue::PtrOk(data) || num <= 0 || num > 512) {
        // One-shot diagnostic. "bone probe: FAILED" with all-zero values cannot
        // distinguish "mesh pointer wrong" from "array not populated yet" from
        // "count read from the wrong offset". Print the raw facts once per
        // distinct shape so the actual cause is visible.
        static ULONGLONG s_next = 0;
        static int s_lastNum = -999;
        const ULONGLONG n = GetTickCount64();
        if (n >= s_next || num != s_lastNum) {
            s_next = n + 5000;
            s_lastNum = num;
            uintptr_t d1 = ue::R64(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
            int32_t   n1 = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
            int32_t   m1 = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
            uintptr_t d2 = ue::R64(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Data);
            int32_t   n2 = ue::RI32(mesh + off::Skinned_CachedBoneSpaceTransforms + off::TArray_Num);
            LOG(SBOX("[bones] mesh=0x%llX compSpace data=0x%llX num=%d max=%d | boneSpace data=0x%llX num=%d", 0xB2),
                (unsigned long long)mesh,
                (unsigned long long)d1, n1, m1,
                (unsigned long long)d2, n2);
        }
        return false;
    }

    // component -> world transform
    const uintptr_t c2w = mesh + off::Comp_ComponentToWorld;
    float q[4], t[3];
    q[0] = ue::RF32(c2w + 0x00);
    q[1] = ue::RF32(c2w + 0x04);
    q[2] = ue::RF32(c2w + 0x08);
    q[3] = ue::RF32(c2w + 0x0C);
    t[0] = ue::RF32(c2w + 0x10);
    t[1] = ue::RF32(c2w + 0x14);
    t[2] = ue::RF32(c2w + 0x18);

    if (!Plausible(t[0]) || !Plausible(t[1]) || !Plausible(t[2])) { static int c2=0; if(c2++<3) LOG(SBOX("[bones] Get: bad c2w trans (%.1f,%.1f,%.1f)",0xB6), t[0],t[1],t[2]); return false; }
    // a valid quaternion has length ~1
    const float qlen = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    // -----------------------------------------------------------------------
    // A ZERO QUATERNION IS NOT AN ERROR HERE -- TREAT IT AS IDENTITY.
    //
    // Measured on the live game for the character body mesh:
    //     ComponentToWorld quat=(0.000,0.000,0.000,0.000) trans=(-3125.9,9394.3,1202.1)
    //
    // The translation is perfectly valid; this component simply does not carry a
    // rotation in ComponentToWorld (the bone transforms are already resolved).
    // Rejecting qlen==0 therefore threw away good data, which is exactly why the
    // probe kept reporting "no bones" while a raw bone dump of the same actor
    // showed 79 correctly-placed bones.
    //
    // Identity (0,0,0,1) is the correct interpretation of "no rotation", and the
    // ToWorld() rotation term then reduces to the translation alone.
    // -----------------------------------------------------------------------
    if (!(qlen > 0.5f && qlen < 1.5f)) {
        if (qlen < 1e-4f) {
            // identity rotation
            q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f;
        } else {
            static int c3 = 0;
            if (c3++ < 3) LOG(SBOX("[bones] Get: bad quat len=%.3f", 0xB7), qlen);
            return false;
        }
    }

    // ---- read the whole array in one shot ----
    // 0x30 per transform; cap the read so a bogus count cannot ask for GBs.
    const int count = num;
    const size_t bytes = (size_t)count * 0x30;
    // Ceiling matches ue::Read's own limit (0x20000). The two used to disagree:
    // this check allowed 0x10000 while Read() refused anything over 0x1000, so a
    // 308-bone array (14,784 bytes) passed here and was rejected there -- and the
    // skeleton silently never resolved.
    if (bytes > 0x20000) { static int c4=0; if(c4++<3) LOG(SBOX("[bones] Get: count too big %d",0xB8), count); return false; }

    static thread_local uint8_t buf[0x20000];
    if (!ue::Read(data, buf, bytes)) { static int c5=0; if(c5++<3) LOG(SBOX("[bones] Get: read failed (%d bytes)",0xB9), (int)bytes); return false; }

    // -----------------------------------------------------------------------
    // BONE SELECTION -- RESTRICTED TO THE BODY.
    //
    // The mesh this resolves carries 246 bones because the weapon and cosmetics
    // are skinned to the same component. Taking the global highest/lowest bone
    // therefore picked a WEAPON barrel tip as "head" and an attachment as "feet",
    // which is why the skeleton and the box were wrong even though the bone data
    // itself was perfect.
    //
    // Fix: find the body axis first. Most bones in any humanoid skeleton are
    // torso/limb bones clustered around the character's vertical axis, so the
    // MEDIAN of all bone X/Y is a stable estimate of that axis. Then head/feet are
    // the extremes among bones lying WITHIN kBodyRadius of it. A rifle sticking
    // out sideways or a hat floating above is excluded; the skull and the soles
    // are not.
    //
    // This needs no bone names and no per-skeleton index table, so it also
    // survives a game patch that re-orders the skeleton.
    // -----------------------------------------------------------------------
    const float kBodyRadius = 45.0f;   // uu; generous enough for shoulders

    // -----------------------------------------------------------------------
    // BODY VERTICAL AXIS -- FROM THE ACTOR, NOT FROM A BONE MEDIAN.
    //
    // The median of all bone X/Y was meant to find the character's centre line,
    // but this mesh carries 246 bones because the WEAPON and cosmetics are skinned
    // to the same component. Most of those extra bones sit out at the gun, so the
    // median drifted off the body and the 45uu radius filter then threw away the
    // real foot bones. Result: feetZ landed around the knees and the box was short
    // at the bottom and tall at the top -- the "box is too high" report.
    //
    // A UE4 actor's origin IS the centre of its capsule base, i.e. the point
    // between the feet. That is exactly the axis we want, it costs one read, and
    // no amount of weapon geometry can move it.
    // -----------------------------------------------------------------------
    // CAPSULE GEOMETRY -- the box anchor. Read once here, used after pass 2.
    // For ACharacter the RootComponent IS the capsule; its origin sits at the
    // CAPSULE CENTRE, so feet = centre - HalfHeight and head ~= centre + 92%.
    // Bone-scan extremes stay as the fallback when the capsule is unreadable.
    float capHalf = 0.0f;      // 0 = unusable
    float axisZ   = 0.0f;      // capsule centre Z (actor Z)
    {
        float actorX = 0, actorY = 0, actorZ = 0;
        uintptr_t root = ue::R64(character + off::AA_RootComponent);
        if (ue::PtrOk(root)) {
            const uintptr_t c2w = root + off::Comp_ComponentToWorld;
            actorX = ue::RF32(c2w + 0x10);
            actorY = ue::RF32(c2w + 0x14);
            actorZ = ue::RF32(c2w + 0x18);

            const uintptr_t cap = ue::R64(character + off::ACh_CapsuleComponent);
            if (ue::PtrOk(cap)) {
                const float hh = ue::RF32(cap + off::Capsule_HalfHeight);
                if (hh > 40.0f && hh < 130.0f) capHalf = hh;   // sane humanoid range
            }
        }
        if (Plausible(actorZ)) axisZ = actorZ;
        if (Plausible(actorX) && Plausible(actorY)) {
            out.axisX = actorX;
            out.axisY = actorY;
        } else {
            // Fallback only when the root component is unreadable: use the mesh
            // component's own world translation, which is also on the body.
            out.axisX = t[0];
            out.axisY = t[1];
        }
    }

    // pass 2: extremes restricted to the body radius
    {
        float minZ = 1e30f, maxZ = -1e30f;
        float headX = 0, headY = 0, feetX = 0, feetY = 0;
        int used = 0;

        for (int i = 0; i < count; ++i) {
            const uint8_t* f = buf + (size_t)i * 0x30;
            float cs[3];
            memcpy(&cs[0], f + 0x10, 4);
            memcpy(&cs[1], f + 0x14, 4);
            memcpy(&cs[2], f + 0x18, 4);
            if (!Plausible(cs[0]) || !Plausible(cs[1]) || !Plausible(cs[2])) continue;

            float w[3];
            ToWorld(q, t, cs, w);
            if (!Plausible(w[0]) || !Plausible(w[1]) || !Plausible(w[2])) continue;

            // body filter
            const float ddx = w[0] - out.axisX, ddy = w[1] - out.axisY;
            if (ddx * ddx + ddy * ddy > kBodyRadius * kBodyRadius) continue;

            ++used;
            if (w[2] > maxZ) { maxZ = w[2]; headX = w[0]; headY = w[1]; }
            if (w[2] < minZ) { minZ = w[2]; feetX = w[0]; feetY = w[1]; }
        }

        if (used < 6) return false;                 // body not identified
        out.headX = headX; out.headY = headY; out.headZ = maxZ;
        out.feetX = feetX; out.feetY = feetY; out.feetZ = minZ;

        // -------------------------------------------------------------------
        // CAPSULE OVERRIDE -- WHY THE BOX WAS STILL "TOO HIGH".
        //
        // The bone extremes are correct ONLY if every bone inside the 45 uu
        // body radius belongs to the body. It does not: when a bot aims its
        // weapon up, the gun barrel bones sit above the HEAD while still within
        // 45 uu of the body axis. maxZ then lands on the muzzle, the box top
        // floats above the head, and because the box is centred on its own
        // height the whole box shifts up. Same class of bug as the old median
        // drift, just from the gun instead of the cosmetics.
        //
        // The capsule cannot lie: HalfHeight is a plain float on the capsule
        // component and no weapon changes it. Box = capsule span; bones keep
        // their job (chest/pelvis anchors and the skeleton) but no longer own
        // the box.
        // -------------------------------------------------------------------
        float topZ = maxZ, botZ = minZ;
        if (capHalf > 0.0f && axisZ != 0.0f) {
            const float cFeet = axisZ - capHalf;
            // HEAD ANCHOR: the capsule cap sits ABOVE the visual head (the
            // mesh's skull top is inside it), so 0.92*half put the skeleton's
            // head point floating over the body ("骨骼头在身体上方").
            // 0.80*half lands on the skull centre; the box compensates with
            // +16 uu in DrawOneInGame so it still encloses the head.
            const float cHead = axisZ + capHalf * 0.80f;
            if (cHead - cFeet > 100.0f && cHead - cFeet < 260.0f) {
                botZ = cFeet;
                topZ = cHead;
                // anchors on the body axis, not on whichever bone won the max
                out.headX = out.axisX; out.headY = out.axisY;
                out.feetX = out.axisX; out.feetY = out.axisY;
            }
        }

        const float height = topZ - botZ;
        if (!(height > 50.0f && height < 400.0f)) return false;
        out.headZ = topZ;
        out.feetZ = botZ;

        // chest / pelvis as fractions of the BODY height
        const float wantChest  = minZ + height * 0.78f;
        const float wantPelvis = minZ + height * 0.52f;
        float bestChest = 1e30f, bestPelvis = 1e30f;

        for (int i = 0; i < count; ++i) {
            const uint8_t* f = buf + (size_t)i * 0x30;
            float cs[3];
            memcpy(&cs[0], f + 0x10, 4);
            memcpy(&cs[1], f + 0x14, 4);
            memcpy(&cs[2], f + 0x18, 4);
            if (!Plausible(cs[0]) || !Plausible(cs[1]) || !Plausible(cs[2])) continue;
            float w[3];
            ToWorld(q, t, cs, w);
            if (!Plausible(w[0]) || !Plausible(w[1]) || !Plausible(w[2])) continue;

            const float ddx = w[0] - out.axisX, ddy = w[1] - out.axisY;
            if (ddx * ddx + ddy * ddy > kBodyRadius * kBodyRadius) continue;

            const float dc = fabsf(w[2] - wantChest);
            if (dc < bestChest) { bestChest = dc; out.chestX = w[0]; out.chestY = w[1]; out.chestZ = w[2]; }
            const float dp = fabsf(w[2] - wantPelvis);
            if (dp < bestPelvis) { bestPelvis = dp; out.pelvisX = w[0]; out.pelvisY = w[1]; out.pelvisZ = w[2]; }
        }
    }

    out.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// diagnostics: dump every bone with its world position and height
// ---------------------------------------------------------------------------
void DumpBones(uintptr_t character) {
    if (!ue::PtrOk(character)) { LOG("[bones] bad character"); return; }

    // Same mesh selection as Get(), so the dump shows what Get() actually reads.
    uintptr_t mesh = 0;
    for (uint32_t cand : { off::WF_MeshFor3PP, off::ACh_Mesh, off::WF_PaperMan2DMesh }) {
        uintptr_t m = ue::R64(character + cand);
        if (!ue::PtrOk(m)) continue;
        uintptr_t d = ue::R64(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
        int32_t   n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
        if (n <= 0) n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
        if (ue::PtrOk(d) && n > 0) { mesh = m; break; }
    }
    LOG("[bones] char=0x%llX mesh=0x%llX", (unsigned long long)character,
        (unsigned long long)mesh);
    if (!ue::PtrOk(mesh)) { LOG("[bones] no mesh component"); return; }

    uintptr_t data = ue::R64(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
    int32_t   num  = ue::RI32(mesh + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
    LOG("[bones] compSpace.Data=0x%llX Num=%d", (unsigned long long)data, num);
    if (!ue::PtrOk(data) || num <= 0 || num > 512) { LOG("[bones] array unusable"); return; }

    const uintptr_t c2w = mesh + off::Comp_ComponentToWorld;
    float q[4], t[3];
    q[0] = ue::RF32(c2w + 0x00); q[1] = ue::RF32(c2w + 0x04);
    q[2] = ue::RF32(c2w + 0x08); q[3] = ue::RF32(c2w + 0x0C);
    t[0] = ue::RF32(c2w + 0x10); t[1] = ue::RF32(c2w + 0x14); t[2] = ue::RF32(c2w + 0x18);
    LOG("[bones] ComponentToWorld quat=(%.3f,%.3f,%.3f,%.3f) trans=(%.1f,%.1f,%.1f)",
        q[0], q[1], q[2], q[3], t[0], t[1], t[2]);

    const size_t bytes = (size_t)num * 0x30;
    if (bytes > 0x20000) { LOG("[bones] array too large (%zu)", bytes); return; }
    static uint8_t buf[0x20000];
    if (!ue::Read(data, buf, bytes)) { LOG("[bones] read failed"); return; }

    float minZ = 1e30f, maxZ = -1e30f;
    for (int i = 0; i < num; ++i) {
        const uint8_t* f = buf + (size_t)i * 0x30;
        float cs[3];
        memcpy(&cs[0], f + 0x10, 4); memcpy(&cs[1], f + 0x14, 4); memcpy(&cs[2], f + 0x18, 4);
        float w[3];
        ToWorld(q, t, cs, w);
        if (!Plausible(w[2])) continue;
        if (w[2] < minZ) minZ = w[2];
        if (w[2] > maxZ) maxZ = w[2];
    }
    const float h = maxZ - minZ;
    LOG("[bones] height range Z=[%.1f .. %.1f] = %.1f uu", minZ, maxZ, h);

    for (int i = 0; i < num && i < 120; ++i) {
        const uint8_t* f = buf + (size_t)i * 0x30;
        float cs[3];
        memcpy(&cs[0], f + 0x10, 4); memcpy(&cs[1], f + 0x14, 4); memcpy(&cs[2], f + 0x18, 4);
        float w[3];
        ToWorld(q, t, cs, w);
        const float pct = h > 1.0f ? ((w[2] - minZ) / h * 100.0f) : 0.0f;
        LOG("[bones]  [%3d] world=(%.1f, %.1f, %.1f)  %.0f%% up", i, w[0], w[1], w[2], pct);
    }
}

} // namespace bones
