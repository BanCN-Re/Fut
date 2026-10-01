// D:\Work\Fut\cheat\src\game_systems.cpp
// See game_systems.h for why the game's own highlight/stealth systems are used
// instead of drawing our own.
#include "game_systems.h"
#include "ue.h"
#include "offsets.h"
#include "obf.h"
#include "log.h"

#include <cstring>

namespace game_systems {

// ---------------------------------------------------------------------------
// TArray<AActor*> layout: { T** Data; int32 Num; int32 Max; }
// ---------------------------------------------------------------------------
#pragma pack(push, 8)
struct ActorArray {
    uintptr_t* Data;
    int32_t    Num;
    int32_t    Max;
};
#pragma pack(pop)

typedef bool(__fastcall* tGetStealthActors)(void* world, ActorArray* out);

static tGetStealthActors s_getStealth = nullptr;

// ---------------------------------------------------------------------------
// STEALTH LIST
//
// CALLING DISCIPLINE -- the same lesson as the FString leak:
//
// We hand the engine a ZEROED TArray (Data = nullptr, Num = 0, Max = 0). Seeing
// no capacity, the engine allocates its own storage via its own allocator and
// returns the list in our struct. That storage is OURS to release -- if we simply
// read it and walk away, every call leaks a heap block, which is exactly the bug
// that grew the process to 7.2 GB before.
//
// So this function:
//   1. calls with a zeroed array
//   2. copies the entries into the caller's buffer
//   3. releases the engine allocation through the game's own FMemory::Free path
//      (resolved from GObjects) or, when that is unavailable, does NOT free and
//      instead logs once -- leaking one block is far better than freeing memory
//      we do not own with the wrong allocator, which corrupts the heap.
//
// The call happens at most once per refresh interval, not per actor per frame, so
// even the degraded path is bounded.
// ---------------------------------------------------------------------------
static void* s_fmemFree = nullptr;      // FMemory::Free, resolved lazily
static bool  s_freeWarned = false;

// Resolve FMemory::Free. The game exports it as a plain static function; the dump
// does not list it under a class we can query, so we look for the well-known
// export in the executable first and fall back to nothing.
static void* ResolveFMemoryFree() {
    if (s_fmemFree) return s_fmemFree;

    // UE4 ships FMemory::Free as an exported symbol in Shipping builds only when
    // the project enabled it; try the export table.
    HMODULE exe = GetModuleHandleW(nullptr);
    if (exe) {
        s_fmemFree = reinterpret_cast<void*>(GetProcAddress(exe, "?Free@FMemory@@SAXPEAX@Z"));
        if (!s_fmemFree)
            s_fmemFree = reinterpret_cast<void*>(GetProcAddress(exe, "FMemory_Free"));
    }
    return s_fmemFree;
}

int GetStealthActors(uintptr_t world, uintptr_t* out, int cap) {
    if (!ue::PtrOk(world) || !out || cap <= 0) return 0;

    if (!s_getStealth)
        s_getStealth = reinterpret_cast<tGetStealthActors>(
            ue::ImageBase() + off::Fn_GetStealthActors);

    ActorArray arr{};
    arr.Data = nullptr;
    arr.Num  = 0;
    arr.Max  = 0;

    __try {
        if (!s_getStealth(reinterpret_cast<void*>(world), &arr))
            return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    if (!ue::PtrOk(reinterpret_cast<uintptr_t>(arr.Data)) || arr.Num <= 0) return 0;

    int n = arr.Num;
    if (n > cap) n = cap;
    if (n > 256) n = 256;                 // sanity: never trust an absurd count

    int written = 0;
    for (int i = 0; i < n; ++i) {
        uintptr_t a = 0;
        __try {
            a = arr.Data[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (ue::PtrOk(a)) out[written++] = a;
    }

    // ---- release the engine allocation ----
    if (void* fn = ResolveFMemoryFree()) {
        typedef void(__cdecl* tFree)(void*);
        __try {
            reinterpret_cast<tFree>(fn)(arr.Data);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    } else if (!s_freeWarned) {
        s_freeWarned = true;
        LOG(SBOX("[stealth] FMemory::Free not resolvable; engine list left to the "
                 "allocator (bounded, called once per refresh)", 0x1C0));
    }

    return written;
}

// ---------------------------------------------------------------------------
// per-frame cache
// ---------------------------------------------------------------------------
static uintptr_t s_stealthList[kMaxStealth];
static int       s_stealthN = 0;
static ULONGLONG s_stealthT = 0;
static const ULONGLONG kStealthRefreshMs = 200;

void RefreshStealth(uintptr_t world) {
    const ULONGLONG now = GetTickCount64();
    if (now - s_stealthT < kStealthRefreshMs) return;
    s_stealthT = now;
    s_stealthN = GetStealthActors(world, s_stealthList, kMaxStealth);
}

bool IsInStealthList(uintptr_t actor) {
    if (!ue::PtrOk(actor)) return false;
    for (int i = 0; i < s_stealthN; ++i)
        if (s_stealthList[i] == actor) return true;
    return false;
}

bool IsStealthed(uintptr_t world, uintptr_t actor) {
    uintptr_t list[kMaxStealth];
    const int n = GetStealthActors(world, list, kMaxStealth);
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
    uintptr_t list[kMaxStealth];
    const int n = GetStealthActors(world, list, kMaxStealth);
    LOG(SBOX("[stealth] census: %d actor(s) currently stealthed", 0x1C1), n);
    for (int i = 0; i < n; ++i) {
        std::string nm;
        ue::NameOf(list[i], nm);
        LOG(SBOX("[stealth]   [%d] 0x%llX %s", 0x1C2), i,
            (unsigned long long)list[i], nm.c_str());
    }
}

} // namespace game_systems
