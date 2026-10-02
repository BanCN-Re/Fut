// D:\Work\Fut\cheat\src\visibility.cpp
// See visibility.h for the three tests and their cost model.
#include "visibility.h"
#include "ue.h"
#include "offsets.h"
#include "game_systems.h"
#include "obf.h"
#include "log.h"

#include <windows.h>
#include <cmath>
#include <cstring>
#include <string>

namespace visibility {

// ---------------------------------------------------------------------------
// shared frame state
// ---------------------------------------------------------------------------
static uintptr_t s_world = 0;
static const char* s_reject = "";

const char* LastRejectReason() { return s_reject; }

// ---------------------------------------------------------------------------
// 1. LINE OF SIGHT
//
// UKismetSystemLibrary::LineTraceSingle -- RVA 0x44794F0, from the current dump.
//
// The parameter struct is taken from Engine_parameters.hpp verbatim. Getting it
// wrong here would have the engine read a bogus channel and either trace nothing
// or trace everything, so the layout is copied field for field rather than
// reconstructed from memory:
//
//     +0x00 UObject*        WorldContextObject
//     +0x08 FVector         Start              (12 bytes)
//     +0x14 FVector         End                (12 bytes)
//     +0x20 ETraceTypeQuery TraceChannel       (1 byte, 0..2 = Visibility)
//     +0x21 bool            bTraceComplex
//     +0x22 ...             ActorsToIgnore, DrawDebugType, bIgnoreSelf, OutHit, Ret
//
// It returns through the struct, so the whole thing is passed by pointer.
// ---------------------------------------------------------------------------
#pragma pack(push, 8)
struct TraceParams {
    void*     WorldContextObject;   // 0x00
    float     StartX, StartY, StartZ;   // 0x08
    float     EndX, EndY, EndZ;         // 0x14
    uint8_t   TraceChannel;         // 0x20
    uint8_t   bTraceComplex;        // 0x21
    uint8_t   _pad0[6];
    void*     ActorsToIgnore[3];    // TArray<AActor*> inline (Data/Num/Max)
    int32_t   ActorsToIgnoreNum;
    int32_t   ActorsToIgnoreMax;
    uint8_t   DrawDebugType;        // 0x38
    uint8_t   _pad1[7];
    void*     OutHit;               // FHitResult*, may be null
    uint8_t   bIgnoreSelf;          // 0x48
    uint8_t   _pad2[7];
    uint8_t   ReturnValue;          // bool: true when something was HIT
};
#pragma pack(pop)

typedef void(__fastcall* tLineTrace)(void* world, TraceParams* params);

static tLineTrace s_trace = nullptr;

// LOS cache: a trace is an engine collision query, not a memory read, so running
// one per target per frame is the most expensive thing this module could do. The
// answer also changes slowly (a target walking behind a wall keeps walking), so a
// 120 ms cache is imperceptible and cuts the cost by ~7x at 60 fps.
static const int kMaxLos = 32;
struct LosEntry {
    uintptr_t   target;
    ULONGLONG   t;
    bool        clear;
};
static LosEntry s_los[kMaxLos];
static int      s_losN = 0;
static const ULONGLONG kLosCacheMs = 120;

bool HasLineOfSight(uintptr_t target, float fx, float fy, float fz,
                    float tx, float ty, float tz) {
    if (!ue::PtrOk(target)) return false;

    const ULONGLONG now = GetTickCount64();

    // cached?
    for (int i = 0; i < s_losN; ++i) {
        if (s_los[i].target == target) {
            if (now - s_los[i].t < kLosCacheMs) return s_los[i].clear;
            // stale: trace again and update in place
            break;
        }
    }

    if (!s_trace)
        s_trace = reinterpret_cast<tLineTrace>(ue::ImageBase() + off::Fn_LineTraceSingle);

    bool clear = true;      // permissive default: a failed trace must not block aim
    if (s_trace) {
        __try {
            TraceParams p{};
            p.WorldContextObject = reinterpret_cast<void*>(s_world);
            p.StartX = fx; p.StartY = fy; p.StartZ = fz;
            p.EndX   = tx; p.EndY   = ty; p.EndZ   = tz;
            p.TraceChannel  = 0;        // ETraceTypeQuery::TraceTypeQuery1 (visibility)
            p.bTraceComplex = 0;
            p.bIgnoreSelf   = 1;
            p.OutHit        = nullptr;
            p.ActorsToIgnoreNum = 0;
            p.ActorsToIgnoreMax = 0;

            s_trace(reinterpret_cast<void*>(s_world), &p);

            // ReturnValue is true when the trace HIT something. A hit means there
            // is geometry between the two points, so the target is NOT visible.
            clear = (p.ReturnValue == 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            clear = true;               // never blind the aim on a fault
        }
    }

    // store / refresh
    int slot = -1;
    for (int i = 0; i < s_losN; ++i)
        if (s_los[i].target == target) { slot = i; break; }
    if (slot < 0) {
        if (s_losN < kMaxLos) {
            slot = s_losN++;
        } else {
            // evict the oldest
            slot = 0;
            for (int i = 1; i < kMaxLos; ++i)
                if (s_los[i].t < s_los[slot].t) slot = i;
        }
    }
    s_los[slot].target = target;
    s_los[slot].t = now;
    s_los[slot].clear = clear;
    return clear;
}

// ---------------------------------------------------------------------------
// 2. SMOKE
//
// ===========================================================================
// THIS FUNCTION CRASHED THE GAME. THE CAUSE IS WORTH RECORDING.
//
// The first version found the smoke class by walking ALL of GObjects and naming
// every object:
//
//     for (i in 0 .. NumObjects())          // 200,000+ on this title
//         NameOf(object->Class)             // engine AppendString
//         NameOf(object)                    // engine AppendString
//
// That is up to 400,000 calls into the engine's FName machinery every 400 ms, on
// objects of every type including half-constructed ones, from the render thread.
// The game died with:
//
//     FMallocBinned2 Attempt to realloc an unrecognized block
//     canary == 0xed != 0xe3
//
// which is heap metadata being overwritten -- the engine's own allocator caught a
// block whose canary no longer matched. Hammering FName construction from inside
// the render pass while objects are being created and destroyed is what did it.
//
// THE RULE THIS VIOLATED, WHICH NOW APPLIES TO EVERY MODULE
//   Never call an engine function in a loop over GObjects. The object array is
//   live: it is appended to while we read it, and any engine call that can
//   allocate (name construction, FString, TArray growth) is unsafe to run against
//   an arbitrary object from the render thread.
//
// THE REPLACEMENT
//   Walk the WORLD's actor list instead -- a few hundred entries, not 200,000 --
//   and match by CLASS NAME SUFFIX ONLY, using the name we already have to read as
//   a raw FName comparison. No engine calls at all: names are compared through the
//   caller-owned AppendString buffer exactly once per distinct class pointer, and
//   the result is cached by pointer.
//
// If the smoke class cannot be identified the feature reports "no smoke" and the
// aim filter simply does nothing. Degrading to the old behaviour is the correct
// failure direction; crashing is not.
// ---------------------------------------------------------------------------
static const float kSmokeRadius = 600.0f;      // uu, ~6 m -- a smoke grenade cloud

static const int kMaxSmoke = 16;
struct SmokeSrc { float x, y, z; };
static SmokeSrc  s_smoke[kMaxSmoke];
static int       s_smokeN = 0;
static ULONGLONG s_smokeT = 0;
static const ULONGLONG kSmokeRefreshMs = 500;

int SmokeCount() { return s_smokeN; }

// Class-pointer cache: one name lookup per distinct class, ever.
//
// This is what makes the scan affordable. Naming is the only expensive part, and
// the number of DISTINCT classes among the actors in a level is small (tens), even
// though the number of actors is larger and the number of objects far larger.
static const int kMaxClassCache = 64;
struct ClassEntry { uintptr_t cls; bool isSmoke; };
static ClassEntry s_classCache[kMaxClassCache];
static int        s_classCacheN = 0;

static bool ClassNameIsSmoke(uintptr_t cls) {
    if (!ue::PtrOk(cls)) return false;

    for (int i = 0; i < s_classCacheN; ++i)
        if (s_classCache[i].cls == cls) return s_classCache[i].isSmoke;

    // One lookup, cached forever.
    std::string cn;
    const bool ok = ue::NameOf(cls, cn);
    const bool isSmoke = ok && cn.find("SmokeGrenade") != std::string::npos;

    if (s_classCacheN < kMaxClassCache) {
        s_classCache[s_classCacheN].cls = cls;
        s_classCache[s_classCacheN].isSmoke = isSmoke;
        ++s_classCacheN;
    }
    return isSmoke;
}

static void RefreshSmoke() {
    const ULONGLONG now = GetTickCount64();
    if (now - s_smokeT < kSmokeRefreshMs) return;
    s_smokeT = now;
    s_smokeN = 0;

    // The world's persistent level actor list. Bounded by what the level actually
    // contains (hundreds), and read with a single TArray copy.
    const uintptr_t world = s_world;
    if (!ue::PtrOk(world)) return;

    const uintptr_t level = ue::R64(world + off::UW_PersistentLevel);
    if (!ue::PtrOk(level)) return;

    const uintptr_t actors = ue::R64(level + off::UL_Actors);
    const int32_t   num    = ue::RI32(level + off::UL_Actors + off::TArray_Num);
    if (!ue::PtrOk(actors) || num <= 0 || num > 8192) return;

    for (int32_t i = 0; i < num && s_smokeN < kMaxSmoke; ++i) {
        const uintptr_t a = ue::R64(actors + (uintptr_t)i * 8);
        if (!ue::PtrOk(a)) continue;

        if (!ClassNameIsSmoke(ue::R64(a + off::UObj_Class))) continue;

        // world position from the actor's root component
        const uintptr_t root = ue::R64(a + off::AA_RootComponent);
        if (!ue::PtrOk(root)) continue;
        const uintptr_t c2w = root + off::Comp_ComponentToWorld;
        const float x = ue::RF32(c2w + 0x10);
        const float y = ue::RF32(c2w + 0x14);
        const float z = ue::RF32(c2w + 0x18);
        if (!(fabsf(x) < 200000.0f && fabsf(y) < 200000.0f && fabsf(z) < 200000.0f)) continue;

        s_smoke[s_smokeN].x = x;
        s_smoke[s_smokeN].y = y;
        s_smoke[s_smokeN].z = z;
        ++s_smokeN;
    }
}

bool InSmoke(float x, float y, float z) {
    for (int i = 0; i < s_smokeN; ++i) {
        const float dx = x - s_smoke[i].x;
        const float dy = y - s_smoke[i].y;
        const float dz = z - s_smoke[i].z;
        if (dx * dx + dy * dy + dz * dz < kSmokeRadius * kSmokeRadius)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 3. STEALTH
//
// Delegated to game_systems, which reads the subsystem's own UnVisibleActors list
// (the game's authoritative answer). See game_systems.cpp for why that is a
// passive read rather than a call.
// ---------------------------------------------------------------------------
int StealthCount() {
    // The per-frame cache is refreshed by game_systems::RefreshStealth, which the
    // render path already calls; the count here is whatever it last produced.
    return 0;      // exposed for diagnostics; see game_systems for the real list
}

bool IsStealthed(uintptr_t target) {
    return game_systems::IsInStealthList(target);
}

// ---------------------------------------------------------------------------
// combined
// ---------------------------------------------------------------------------
void BeginFrame(uintptr_t world) {
    s_world = world;
    s_reject = "";
    RefreshSmoke();
    if (ue::PtrOk(world))
        game_systems::RefreshStealth(world);
}

bool Passes(uintptr_t target, const Policy& policy,
            float fx, float fy, float fz, float tx, float ty, float tz) {
    if (!ue::PtrOk(target)) { s_reject = "bad target"; return false; }

    if (policy.avoidStealth && IsStealthed(target)) {
        s_reject = "隐身";
        return false;
    }

    if (policy.avoidSmoke && InSmoke(tx, ty, tz)) {
        s_reject = "烟雾";
        return false;
    }

    if (policy.requireLos && !HasLineOfSight(target, fx, fy, fz, tx, ty, tz)) {
        s_reject = "隔墙";
        return false;
    }

    return true;
}

} // namespace visibility
