// D:\Work\Fut\cheat\src\game_systems.cpp
//
// Wrappers around the game's OWN highlight and stealth systems.
// See game_systems.h for why the game's systems are used instead of our own.
//
// ===========================================================================
// STEALTH: PASSIVE READS ONLY -- THIS FILE NO LONGER CALLS GetStealthActors
//
// The previous version called
//     static bool GetStealthActors(UWorld*, TArray<AActor*>* OutActors)
// with a zeroed TArray, which makes the engine allocate the backing store with
// its own allocator, and then tried to release it via
//     GetProcAddress(exe, "?Free@FMemory@@SAXPEAX@Z")
//
// That export does not exist in this Shipping build, so the release never
// happened and EVERY call leaked an engine heap block. The call ran every 200 ms,
// forever. The consequence was not a slow memory climb: those blocks stay
// registered in the engine's GC reference tables, so once enough had accumulated
// the collector walked references to freed memory, corrupted the heap, and the
// RENDER THREAD died on a wild function pointer:
//
//     EXCEPTION_ACCESS_VIOLATION 0xa9420000
//     0x00007ff9a9420000   <- IP low 32 bits == the faulting address, ends in 0000
//
// i.e. a call through a corrupted pointer into a module header. The user's
// report was "nocamera 跳多了会崩溃" -- the more the camera reset, the more the
// scan path ran, the more blocks leaked, and the sooner the heap broke.
//
// THE FIX IS TO STOP CALLING IT AT ALL.
//
// All of those static accessors (GetStealthActors / GetAllCharacters /
// GetAllActors / GetUnVisibleActor / ...) are thin readers over the arrays the
// subsystem ALREADY MAINTAINS. Reading those arrays directly is:
//   * allocation-free        -> no leak, no GC pressure, no heap corruption
//   * safe to call any time  -> no "is my buffer still valid" question
//   * strictly cheaper       -> a TArray read instead of a function that builds one
//
// The subsystem is found through GObjects once and cached; the arrays are then
// read by offset.
// ===========================================================================
#include "game_systems.h"
#include "ue.h"
#include "offsets.h"
#include "obf.h"
#include "log.h"

#include <cstring>
#include <string>

namespace game_systems {

// ---------------------------------------------------------------------------
// UCachedWorldSubsystem layout (from the current dump, PMGame_classes.hpp)
//
//   TArray<AActor*>          UnVisibleActors          @ 0x0038
//   TArray<AActor*>          VisibleViewBlockActors   @ 0x0048
//   TArray<AActor*>          BuildViewBlockActors     @ 0x0058
//   TArray<AActor*>          PlacementBlockActors     @ 0x0068
//   TArray<ACyCharacterBase*> Characters              @ 0x0078
//
// TArray<AActor*> is { T** Data; int32 Num; int32 Max; } = 0x10 bytes.
//
// UnVisibleActors is the subsystem's own record of actors that are currently not
// visible -- which is exactly the set the stealth query is built from. Reading it
// gives the same answer as calling GetStealthActors, with none of the cost.
// ---------------------------------------------------------------------------
namespace sub {
constexpr uint32_t UnVisibleActors   = 0x0038;
constexpr uint32_t Characters        = 0x0078;
} // namespace sub

// ---------------------------------------------------------------------------
// subsystem instance lookup
//
// The class name is matched against GObjects; the instances live as objects in
// the world's subsystem collection, so a name match on the CLASS then a walk for
// an object whose class is that class is the reliable route without needing the
// collection offsets.
// ---------------------------------------------------------------------------
static uintptr_t s_subClass = 0;
static uintptr_t s_subInst  = 0;

static void ResolveSubsystem() {
    if (!ue::GObjectsReady()) return;

    // 1. the class object
    if (!s_subClass) {
        const int32_t n = ue::NumObjects();
        for (int32_t i = 0; i < n; ++i) {
            uintptr_t o = ue::ObjectAt(i);
            if (!ue::PtrOk(o)) continue;

            uintptr_t cls = ue::R64(o + off::UObj_Class);
            if (!ue::PtrOk(cls)) continue;
            std::string cn;
            if (!ue::NameOf(cls, cn) || cn != "Class") continue;

            std::string nm;
            if (!ue::NameOf(o, nm)) continue;
            // prefer the game-specific subsystem, fall back to the generic one
            if (nm == "PMCachedWorldSubsystem") { s_subClass = o; break; }
            if (nm == "CyCachedWorldSubsystem" && !s_subClass) s_subClass = o;
        }
        if (s_subClass)
            LOG(SBOX("[stealth] subsystem class 0x%llX resolved", 0x1D0),
                (unsigned long long)s_subClass);
    }
    if (!s_subClass) return;

    // 2. a live instance whose UClass is that class (instances carry the
    //    most-derived class, so compare by pointer)
    const int32_t n = ue::NumObjects();
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t o = ue::ObjectAt(i);
        if (!ue::PtrOk(o)) continue;
        if (ue::R64(o + off::UObj_Class) != s_subClass) continue;

        // must look like a real instance, not the CDO
        std::string nm;
        if (!ue::NameOf(o, nm)) continue;
        if (nm.rfind("Default__", 0) == 0) continue;

        s_subInst = o;
        LOG(SBOX("[stealth] subsystem instance 0x%llX ('%s')", 0x1D1),
            (unsigned long long)o, nm.c_str());
        return;
    }
}

// Read a TArray<AActor*> at `base + arrOff` into `out`. No engine calls.
static int ReadActorArray(uintptr_t base, uint32_t arrOff, uintptr_t* out, int cap) {
    if (!ue::PtrOk(base) || !out || cap <= 0) return 0;

    const uintptr_t data = ue::R64(base + arrOff + off::TArray_Data);
    int32_t num = ue::RI32(base + arrOff + off::TArray_Num);
    int32_t max = ue::RI32(base + arrOff + off::TArray_Max);

    if (!ue::PtrOk(data) || num <= 0) return 0;
    // A TArray invariant is Num <= Max. When it does not hold, the struct is not
    // what we think it is, and trusting Num would walk off the allocation.
    if (max > 0 && num > max) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOG(SBOX("[stealth] array at +0x%X violates Num(%d) <= Max(%d); ignoring",
                     0x1D2), arrOff, num, max);
        }
        return 0;
    }
    if (num > 4096) return 0;                 // sanity

    int written = 0;
    for (int32_t i = 0; i < num && written < cap; ++i) {
        uintptr_t a = 0;
        __try {
            a = ue::R64(data + (uintptr_t)i * 8);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (ue::PtrOk(a)) out[written++] = a;
    }
    return written;
}

// ---------------------------------------------------------------------------
// per-frame cache
// ---------------------------------------------------------------------------
static uintptr_t s_stealthList[kMaxStealth];
static int       s_stealthN = 0;
static ULONGLONG s_stealthT = 0;
static ULONGLONG s_resolveT = 0;
static const ULONGLONG kStealthRefreshMs = 200;
static const ULONGLONG kResolveRetryMs   = 3000;

void RefreshStealth(uintptr_t world) {
    (void)world;                       // the instance is found via GObjects

    const ULONGLONG now = GetTickCount64();
    if (now - s_stealthT < kStealthRefreshMs) return;
    s_stealthT = now;

    if (!ue::PtrOk(s_subInst)) {
        // The instance appears once the world is up; retry on a timer rather than
        // every frame (this walks GObjects).
        if (now - s_resolveT < kResolveRetryMs) return;
        s_resolveT = now;
        ResolveSubsystem();
        if (!ue::PtrOk(s_subInst)) return;
    }

    s_stealthN = ReadActorArray(s_subInst, sub::UnVisibleActors,
                                s_stealthList, kMaxStealth);
}

bool IsInStealthList(uintptr_t actor) {
    if (!ue::PtrOk(actor)) return false;
    for (int i = 0; i < s_stealthN; ++i)
        if (s_stealthList[i] == actor) return true;
    return false;
}

int GetStealthActors(uintptr_t world, uintptr_t* out, int cap) {
    (void)world;
    if (!out || cap <= 0) return 0;
    if (!ue::PtrOk(s_subInst)) { ResolveSubsystem(); }
    if (!ue::PtrOk(s_subInst)) return 0;
    return ReadActorArray(s_subInst, sub::UnVisibleActors, out, cap);
}

bool IsStealthed(uintptr_t world, uintptr_t actor) {
    (void)world;
    if (!ue::PtrOk(actor)) return false;
    uintptr_t list[kMaxStealth];
    const int n = GetStealthActors(0, list, kMaxStealth);
    for (int i = 0; i < n; ++i)
        if (list[i] == actor) return true;
    return false;
}

// ---------------------------------------------------------------------------
// OUTLINE / WALL VIEW
//
// USkinnedMeshComponent::SetRenderOutlineMaterialEnabled(bool)
//   RVA 0x25D67B0 is the BlueprintCallable wrapper; it takes (self, bool) and
//   walks the component's own material state, which is why we call it rather than
//   poking a bool at a guessed offset.
//
// The mesh is located the same way bones:: does: the component with the most
// bones (the body), so the outline lands on the character and not on a weapon.
// ---------------------------------------------------------------------------
static uintptr_t BodyMeshOf(uintptr_t actor) {
    if (!ue::PtrOk(actor)) return 0;

    // preferred: the character's own 3rd-person mesh field
    uintptr_t m = ue::R64(actor + off::WF_MeshFor3PP);
    if (ue::PtrOk(m)) {
        int32_t n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
        if (n <= 0) n = ue::RI32(m + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
        if (n > 0) return m;
    }

    // fallback: most bones
    uintptr_t best = 0; int32_t bestN = 0;
    for (uint32_t o = 0; o < 0x2000; o += 8) {
        uintptr_t c = ue::R64(actor + o);
        if (!ue::PtrOk(c)) continue;
        int32_t n = ue::RI32(c + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Num);
        if (n <= 0) n = ue::RI32(c + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Max);
        uintptr_t d = ue::R64(c + off::Skinned_CachedComponentSpaceTransforms + off::TArray_Data);
        if (!ue::PtrOk(d) || n <= 0 || n > 512) continue;
        if (n > bestN) { bestN = n; best = c; }
    }
    return best;
}

typedef void(__fastcall* tSetOutline)(void* self, bool enabled);
typedef bool(__fastcall* tGetOutline)(void* self);

static tSetOutline s_setOutline = nullptr;
static tGetOutline s_getOutline = nullptr;

void SetActorOutline(uintptr_t actor, bool enabled) {
    const uintptr_t mesh = BodyMeshOf(actor);
    if (!ue::PtrOk(mesh)) return;

    if (!s_setOutline)
        s_setOutline = reinterpret_cast<tSetOutline>(
            ue::ImageBase() + off::Fn_SetRenderOutlineMaterialEnabled);

    __try {
        s_setOutline(reinterpret_cast<void*>(mesh), enabled);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool IsActorOutlineEnabled(uintptr_t actor) {
    const uintptr_t mesh = BodyMeshOf(actor);
    if (!ue::PtrOk(mesh)) return false;

    if (!s_getOutline)
        s_getOutline = reinterpret_cast<tGetOutline>(
            ue::ImageBase() + off::Fn_GetRenderOutlineMaterialEnabled);

    bool r = false;
    __try {
        r = s_getOutline(reinterpret_cast<void*>(mesh));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return r;
}

void LogStealthCensus(uintptr_t world) {
    (void)world;
    uintptr_t list[kMaxStealth];
    const int n = GetStealthActors(0, list, kMaxStealth);
    LOG(SBOX("[stealth] census: %d actor(s) currently stealthed (passive read)", 0x1C1), n);
    for (int i = 0; i < n; ++i) {
        std::string nm;
        ue::NameOf(list[i], nm);
        LOG(SBOX("[stealth]   [%d] 0x%llX %s", 0x1C2), i,
            (unsigned long long)list[i], nm.c_str());
    }
}

} // namespace game_systems
