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

        // -------------------------------------------------------------------
        // COMPONENT VALIDATION -- BY UCLASS POINTER, NOT BY NAME
        //
        // WHY NAME MATCHING WAS THE WRONG TOOL
        //
        // Two attempts at a name test both failed, in opposite directions:
        //
        //   a) "ends with SkeletalMeshComponent"  -> rejected the CORRECT component.
        //      Measured runtime names: 'PMSkelMeshComp', 'PMPaper2DSkeleltalMeshComp'.
        //      This build abbreviates, so the C++ type UPMSkeletalMeshComponent is
        //      never spelled out at runtime.
        //
        //   b) "contains Skel and Comp"           -> accepted 'SkeletalMesh', which
        //      is USkeletalMesh: the ASSET, not a component. It carries 13 bones for
        //      a prop, so the fallback scan happily picked it over the character's
        //      real 79-bone rig.
        //
        // A name is a string that a patch, a locale or a build flag can change. The
        // CLASS POINTER cannot be faked and does not depend on spelling at all.
        //
        // THE CORRECT TEST
        //
        //   Find the UClass object for USkeletalMeshComponent ONCE, then ask
        //   ue::IsA(candidate, thatClass). IsA walks UStruct::SuperStruct (+0x40),
        //   so every subclass in the chain matches automatically:
        //
        //       UPMSkeletalMeshComponent      -> derives from USkeletalMeshComponent
        //       UWFBaseSkeletalMeshComponent  -> derives from USkeletalMeshComponent
        //       UCyPendantSkeletalMeshComponent -> derives from USkeletalMeshComponent
        //       USkeletalMesh (the asset)     -> does NOT; it is not a component
        //
        // It is also CHEAPER than the name version: pointer comparison along the
        // super chain, with zero calls into the engine's FName machinery -- which is
        // exactly the kind of call that corrupted the heap earlier in this session.
        //
        // The class is resolved lazily and cached, and a failed resolution is
        // retried on a timer rather than every frame (FindClassByName walks all of
        // GObjects).
        // -------------------------------------------------------------------
        auto skeletalMeshClass = []() -> uintptr_t {
            static uintptr_t s_cls = 0;
            static ULONGLONG s_retry = 0;
            if (s_cls) return s_cls;

            const ULONGLONG now = GetTickCount64();
            if (s_retry != 0 && now - s_retry < 3000) return 0;
            s_retry = now;

            // The engine base class. Every game-specific mesh component derives from
            // it, so this one lookup covers all of them.
            s_cls = ue::FindClassByName(SBOX("SkeletalMeshComponent", 0xC0));
            if (s_cls) {
                LOG(SBOX("[bones] SkeletalMeshComponent UClass = 0x%llX", 0xC1),
                    (unsigned long long)s_cls);
            }
            return s_cls;
        };

        auto isSkeletalMesh = [&skeletalMeshClass](uintptr_t m) -> bool {
            if (!ue::PtrOk(m)) return false;
            const uintptr_t target = skeletalMeshClass();
            if (!target) return false;                 // not resolved yet: reject, retry later
            return ue::IsA(m, target);
        };

        // -------------------------------------------------------------------
        // ONE-SHOT DIAGNOSTIC: WHAT IS ACTUALLY AT THE DECLARED OFFSETS?
        //
        // The class-name check rejected every candidate on the first run, which
        // means the guess about the naming convention was wrong -- not that the
        // offsets are wrong. Printing the real names is the only way to tell the
        // two apart, so this runs once per process and names every candidate
        // regardless of whether it passes the test.
        // -------------------------------------------------------------------
        {
            static int s_dumped = 0;
            if (s_dumped++ < 3) {
                const uint32_t probe[] = { off::WF_MeshFor3PP, off::ACh_Mesh, off::WF_PaperMan2DMesh };
                for (uint32_t po : probe) {
                    const uintptr_t c = ue::R64(character + po);
                    char line[192];
                    if (!ue::PtrOk(c)) {
                        _snprintf_s(line, sizeof(line), _TRUNCATE,
                                    "[bones] char+0x%X -> null/invalid", po);
                    } else {
                        std::string cn;
                        const uintptr_t cls = ue::R64(c + off::UObj_Class);
                        const bool named = ue::PtrOk(cls) && ue::NameOf(cls, cn);
                        _snprintf_s(line, sizeof(line), _TRUNCATE,
                                    "[bones] char+0x%X -> cls=0x%llX name='%s'",
                                    po, (unsigned long long)cls,
                                    named ? cn.c_str() : "<naming failed>");
                    }
                    LOG(SBOX("%s", 0xB7), line);
                }
            }
        }

        // preferred offsets first, validated
        const uint32_t preferred[] = { off::WF_MeshFor3PP, off::ACh_Mesh, off::WF_PaperMan2DMesh };
        uintptr_t d = 0; int32_t n = 0;
        for (uint32_t o : preferred) {
            const uintptr_t cand = ue::R64(character + o);
            if (isSkeletalMesh(cand) && boneArrayOk(cand, d, n)) {
                mesh = cand;
                static int s_p1 = 0;
                if (s_p1++ < 4) {
                    std::string cn;
                    const uintptr_t cls = ue::R64(cand + off::UObj_Class);
                    if (ue::PtrOk(cls)) ue::NameOf(cls, cn);
                    LOG(SBOX("[bones] declared field char+0x%X: %d bones, class='%s'", 0xB6),
                        o, n, cn.c_str());
                }
                break;
            }
        }
        if (!mesh) {
            // -------------------------------------------------------------------
            // Bounded scan, but ONLY over pointers that pass the class test.
            //
            // Without that test this loop is exactly what produced the 308-bone
            // data asset, so the test is the load-bearing part, not the range.
            //
            // Among valid components, prefer a HUMAN-SIZED rig: the body skeleton
            // here is 60-90 bones, while the merged mesh that carries weapon and
            // cosmetics is larger and has a different ComponentToWorld, which is
            // what made the drawn skeleton sit offset from the model.
            // -------------------------------------------------------------------
            uintptr_t bestMesh = 0, bestData = 0;
            int32_t   bestNum = 0;
            int32_t   bestScore = -1;
            uint32_t  bestOff = 0;
            for (uint32_t o = 0; o < 0x2000; o += 8) {
                const uintptr_t m = ue::R64(character + o);
                if (!isSkeletalMesh(m)) continue;
                uintptr_t dd = 0; int32_t nn = 0;
                if (!boneArrayOk(m, dd, nn)) continue;

                // 50..200 bones scores by count (bigger humanoid wins);
                // anything outside that range is penalised so a merged mesh or a
                // prop cannot outrank a real body.
                const int32_t score = (nn >= 50 && nn <= 200) ? nn : (1000 - nn);
                if (score > bestScore) {
                    bestScore = score;
                    bestMesh = m; bestData = dd; bestNum = nn; bestOff = o;
                }
            }
            if (bestMesh) {
                mesh = bestMesh; d = bestData; n = bestNum;
                static int s_logged = 0;
                if (s_logged++ < 6) {
                    // Log the CLASS NAME, not just the offset. The offset alone was
                    // what hid the bug for two rounds: 0x118 with 308 bones looked
                    // like a perfectly good body mesh until the dump showed it was
                    // an EntranceEffectsDataAsset. A name makes that impossible to
                    // miss.
                    std::string cn;
                    const uintptr_t cls = ue::R64(bestMesh + off::UObj_Class);
                    if (ue::PtrOk(cls)) ue::NameOf(cls, cn);
                    LOG(SBOX("[bones] picked char+0x%X: %d bones, class='%s'", 0xB5),
                        bestOff, bestNum, cn.c_str());
                }
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
    // BONE -> WORLD, WITHOUT THE WEAPON BONES.
    //
    // THE BUG THAT PRODUCED "chestZ ABOVE headZ":
    //
    // The previous version picked head = highest bone and feet = lowest bone,
    // both filtered to a 45 uu cylinder around the body axis. It then OVERRODE
    // head/feet with the capsule span but left chest/pelvis searching the BONE
    // array against a target height computed from a DIFFERENT origin:
    //
    //     wantChest = minZ + height * 0.78f    // minZ from bones
    //                                          // height from the CAPSULE
    //
    // Two different baselines in one expression. minZ is the lowest body bone
    // (around the ankle), while height is the full capsule span (feet to skull).
    // Adding 78% of the capsule height to an ankle position lands ABOVE the head,
    // which is exactly what the log showed:
    //
    //     headZ=1276.9  chestZ=1334.6  pelvisZ=1288.6
    //                     ^ above head    ^ above head
    //
    // The fix is to stop mixing sources. The capsule owns the vertical span
    // (nothing a weapon does can change HalfHeight), so chest and pelvis are
    // derived from the SAME capsule-derived feet/head that the box uses:
    //
    //     feet  = axisZ - capHalf
    //     head  = axisZ + capHalf * 0.80      (skull centre, not the capsule cap)
    //     chest = feet + (head - feet) * 0.78
    //     pelvis= feet + (head - feet) * 0.52
    //
    // Their X/Y come from the body axis, and the Z is taken from the NEAREST BONE
    // to that height so the anchors still follow the pose (lean, crouch) instead
    // of being a rigid column. Bones only ever SUPPLY a refinement here; they can
    // no longer move an anchor past a sanity bound.
    // -----------------------------------------------------------------------
    const float kBodyRadius = 60.0f;   // uu; shoulders + a little slack

    // ---- 1. capsule span: authoritative for the vertical extent -------------
    float capHalf = 0.0f;
    float axisZ   = 0.0f;
    {
        uintptr_t root = ue::R64(character + off::AA_RootComponent);
        if (ue::PtrOk(root)) {
            const uintptr_t c2w = root + off::Comp_ComponentToWorld;
            const float ax = ue::RF32(c2w + 0x10);
            const float ay = ue::RF32(c2w + 0x14);
            const float az = ue::RF32(c2w + 0x18);
            if (Plausible(az)) axisZ = az;
            if (Plausible(ax) && Plausible(ay)) { out.axisX = ax; out.axisY = ay; }
        }
        if (!Plausible(out.axisX) || !Plausible(out.axisY)) {
            out.axisX = t[0];          // mesh world translation as the fallback
            out.axisY = t[1];
        }

        const uintptr_t cap = ue::R64(character + off::ACh_CapsuleComponent);
        if (ue::PtrOk(cap)) {
            const float hh = ue::RF32(cap + off::Capsule_HalfHeight);
            if (hh > 40.0f && hh < 130.0f) capHalf = hh;    // sane humanoid range
        }
    }

    // ---- 2. collect the body bones once (world space) -----------------------
    // A bone belongs to the body when it is close to the vertical axis. Bones are
    // already converted to world, so this also rejects the weapon, which is
    // skinned to the same component but hangs off to one side.
    struct WBone { float x, y, z; };
    static thread_local WBone wb[512];
    int wbn = 0;
    for (int i = 0; i < count && wbn < 512; ++i) {
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

        wb[wbn].x = w[0]; wb[wbn].y = w[1]; wb[wbn].z = w[2];
        ++wbn;
    }
    if (wbn < 6) return false;                      // body not identifiable

    // ---- 3. vertical span: capsule if available, else the body bones --------
    float feetZ, headZ;
    bool fromCapsule = false;
    if (capHalf > 0.0f && axisZ != 0.0f) {
        const float cFeet = axisZ - capHalf;
        // 0.80 puts the anchor at the SKULL CENTRE; the box adds +16 uu so it
        // still encloses the top of the head without floating.
        const float cHead = axisZ + capHalf * 0.80f;
        if (cHead - cFeet > 100.0f && cHead - cFeet < 260.0f) {
            feetZ = cFeet; headZ = cHead; fromCapsule = true;
        }
    }
    if (!fromCapsule) {
        // Bone-only fallback. The body filter already removed the weapon, so the
        // extremes are trustworthy here.
        feetZ = wb[0].z; headZ = wb[0].z;
        for (int i = 1; i < wbn; ++i) {
            if (wb[i].z < feetZ) feetZ = wb[i].z;
            if (wb[i].z > headZ) headZ = wb[i].z;
        }
    }

    const float height = headZ - feetZ;
    if (!(height > 50.0f && height < 400.0f)) return false;

    out.headZ = headZ;
    out.feetZ = feetZ;
    out.headX = out.axisX; out.headY = out.axisY;
    out.feetX = out.axisX; out.feetY = out.axisY;

    // ---- 4. chest / pelvis: fractions of the SAME span ----------------------
    //
    // Z comes from the span; X/Y are snapped to the nearest body bone at that
    // height so the anchors follow a lean or a crouch. The snap is bounded: a
    // candidate more than 25 uu away vertically is ignored, which keeps a stray
    // bone from dragging an anchor off the torso.
    {
        const float wantChest  = feetZ + height * 0.78f;
        const float wantPelvis = feetZ + height * 0.52f;

        const float kSnapMax = 25.0f;

        float bestChest = 1e30f, bestPelvis = 1e30f;
        out.chestX  = out.axisX; out.chestY  = out.axisY; out.chestZ  = wantChest;
        out.pelvisX = out.axisX; out.pelvisY = out.axisY; out.pelvisZ = wantPelvis;

        for (int i = 0; i < wbn; ++i) {
            const float dc = fabsf(wb[i].z - wantChest);
            if (dc < bestChest && dc <= kSnapMax) {
                bestChest = dc;
                out.chestX = wb[i].x; out.chestY = wb[i].y; out.chestZ = wb[i].z;
            }
            const float dp = fabsf(wb[i].z - wantPelvis);
            if (dp < bestPelvis && dp <= kSnapMax) {
                bestPelvis = dp;
                out.pelvisX = wb[i].x; out.pelvisY = wb[i].y; out.pelvisZ = wb[i].z;
            }
        }
    }

    out.mesh = mesh;
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
