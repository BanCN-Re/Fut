// D:\Work\Fut\cheat\src\ue.cpp
#include "ue.h"
#include "offsets.h"
#include "obf.h"
#include "themida.h"
#include "log.h"

#include <windows.h>
#include <cstring>
#include <cmath>

namespace ue {

// ---------------------------------------------------------------------------
// raw reads
//
// PERFORMANCE: this is the hot path (thousands of reads per frame). The first
// version called VirtualQuery() before every read, which is a kernel transition
// each time -- ~6000 of them per frame, which capped the overlay at 20 FPS.
//
// We now copy directly and let SEH catch a bad address. RawCopy is a leaf
// function with no C++ objects, so __try is legal here (mixing __try with
// objects that need unwinding is a compile error, hence the split).
// ---------------------------------------------------------------------------
static __declspec(noinline) bool RawCopy(void* dst, const void* src, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Read(uintptr_t addr, void* out, size_t size) {
    if (addr < 0x10000 || !out || size == 0) return false;
    if (size > 0x1000) return false;              // guard against absurd sizes
    if (RawCopy(out, reinterpret_cast<const void*>(addr), size)) return true;

    // Slow path: the direct copy faulted. Confirm the page really is unusable
    // so a genuine gap is reported as failure rather than looping.
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

    uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (addr + size > regionEnd) {
        size_t first = static_cast<size_t>(regionEnd - addr);
        if (!RawCopy(out, reinterpret_cast<const void*>(addr), first)) return false;
        return Read(regionEnd, reinterpret_cast<uint8_t*>(out) + first, size - first);
    }
    return false;
}

uint64_t R64(uintptr_t a) { uint64_t v = 0; Read(a, &v, 8); return v; }
uint32_t R32(uintptr_t a) { uint32_t v = 0; Read(a, &v, 4); return v; }
int32_t  RI32(uintptr_t a) { int32_t v = 0; Read(a, &v, 4); return v; }
uint8_t  R8(uintptr_t a)  { uint8_t v = 0; Read(a, &v, 1); return v; }
float    RF32(uintptr_t a) { float v = 0; Read(a, &v, 4); return v; }

// ---------------------------------------------------------------------------
// guarded write
//
// Used only by the aim module. Steps, in order:
//   1. reject implausible addresses
//   2. VirtualQuery the page and require MEM_COMMIT
//   3. require it is not PAGE_NOACCESS / PAGE_GUARD, and that the whole span is
//      inside ONE region (so we never cross into an unmapped page)
//   4. grant PAGE_READWRITE, memcpy, restore the original protection
// Returns false at any failed step instead of raising.
// ---------------------------------------------------------------------------
bool WriteGuarded(uintptr_t addr, const void* src, size_t size) {
    if (addr < 0x10000 || !src || size == 0 || size > 0x1000) return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

    const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (addr + size > regionEnd) return false;      // never span two regions

    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), size, PAGE_READWRITE, &oldProt))
        return false;

    memcpy(reinterpret_cast<void*>(addr), src, size);

    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(addr), size, oldProt, &ignored);
    return true;
}

bool PtrOk(uint64_t p) {
    return p > 0x10000 && p < 0x00007FFFFFFFFFFFULL && (p & 0x3) == 0;
}

// ---------------------------------------------------------------------------
// base / init
// ---------------------------------------------------------------------------
uintptr_t ImageBase() {
    static uintptr_t s_base = 0;
    if (s_base) return s_base;
    s_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!s_base) s_base = 0x140000000ULL;   // documented image base fallback
    return s_base;
}

// ---- GObjects -------------------------------------------------------------
static uintptr_t g_objects = 0;
static int32_t   g_num = 0;
// ---------------------------------------------------------------------------
// GObjects
//
// !!! THE OBJECT COUNT IS NOT CONSTANT -- DO NOT CACHE IT ONCE !!!
//
// This was a real, long-lived bug. Init() used to run once, succeed during
// loading when the game had only ~7,000 objects, and then never run again
// because GObjectsReady() returned true. NumElements therefore stayed frozen at
// 7,006 for the whole session while the game went on to register 198,592.
//
// Consequence: every scan that walks GObjects silently stopped at index 7,006.
// UCanvas::K2_DrawLine lives at index 24,187, so it could never be found no
// matter how the lookup was written -- which is exactly the "draw fn not found"
// wall the canvas work kept hitting. Low-index objects (CyCharacterBase at 1,284,
// Canvas at 5,945) worked, which made the offset look correct.
//
// The fix: RefreshObjects() re-reads the counts every frame (two int32 reads --
// free) and ObjectAt() always uses the fresh value.
// ---------------------------------------------------------------------------
static int32_t   g_chunks = 0;

bool GObjectsReady() { return g_objects != 0 && g_num > 0; }
static void RefreshObjects();   // fwd decl: called by NumObjects below and defined after
int32_t NumObjects() { RefreshObjects(); return g_num; }

static void RefreshObjects() {
    if (!g_objects) return;
    const uintptr_t slot = ImageBase() + off::GObjects;
    const int32_t nume = RI32(slot + 0x14);
    const int32_t numc = RI32(slot + 0x1C);
    if (nume > 0 && nume <= 4000000) {
        // Rate-limit: during streaming the count ticks several times a SECOND
        // and each line is a synchronous file write on the render thread --
        // a measurable slice of the "drawing lags the game" report.
        static ULONGLONG s_nextLog = 0;
        if (nume != g_num && GetTickCount64() >= s_nextLog) {
            s_nextLog = GetTickCount64() + 5000;
            LOG(SBOX("[ue] object count %d -> %d (chunks %d)", 0xA5), g_num, nume, numc);
        }
        g_num = nume;
        g_chunks = numc;
    }
}

bool Init() {
    uintptr_t slot = ImageBase() + off::GObjects;
    uintptr_t objects = R64(slot);
    int32_t   nume = RI32(slot + 0x14);
    int32_t   numc = RI32(slot + 0x1C);

    if (!PtrOk(objects) || nume <= 0 || nume > 4000000) {
        LOG("[ue] GObjects invalid: objects=0x%llX num=%d", (unsigned long long)objects, nume);
        return false;
    }
    if (g_objects != objects || g_num != nume) {
        LOG("[ue] GObjects ok: 0x%llX objects=%d chunks=%d", (unsigned long long)objects, nume, numc);
    }
    g_objects = objects;
    g_num = nume;
    g_chunks = numc;
    return true;
}

uintptr_t ObjectAt(int32_t index) {
    if (index < 0 || index >= g_num || !g_objects) return 0;
    uintptr_t chunk = R64(g_objects + static_cast<uintptr_t>(index / off::ElemsPerChunk) * 8);
    if (!PtrOk(chunk)) return 0;
    uintptr_t item = chunk + static_cast<uintptr_t>(index % off::ElemsPerChunk) * off::FUObjectItemSize;
    return R64(item + 0x00);
}

// ---------------------------------------------------------------------------
// FName -> text, via the game's own FName::AppendString
//
// ===========================================================================
// !!! THIS FUNCTION WAS LEAKING THE ENTIRE HEAP. READ BEFORE EDITING. !!!
// ===========================================================================
//
// The previous version passed a ZEROED FString:
//
//     FStr s{};                 // Data = nullptr, Num = 0, Max = 0
//     fn(name, &s);             // engine sees no buffer -> allocates one
//     ... read s.Data ...
//     return ...;               // never freed
//
// FString::operator+= calls a realloc path when the incoming text does not fit.
// With Data == nullptr and Max == 0 there is nothing to fit into, so the engine
// allocates with its own allocator -- and we never released it.
//
// NameOf() is a HOT PATH: Collect() -> ScanCharacters() runs it once per object
// over the whole GObjects array (198k entries) every 250 ms, plus every
// FindClassByName / ClassChain / IsAOfClassNamed fallback. That is ~198,000
// leaked allocations per scan.
//
// Measured consequence on the live game:
//     object count   202,466  ->  603,464     (3x)
//     private bytes            7.2 GB
//     handles                  3,970
// after ~35 minutes, ending with the overlay dying and the UI becoming unusable.
//
// THE FIX -- exactly what Dumper-7's own generated SDK does (Basic.hpp:384):
//
//     wchar_t buffer[1024];
//     FString TempString(buffer, 0, 1024);     // caller-owned stack buffer
//     AppendString(this, TempString);          // writes INTO it, no allocation
//
// With Data pointing at our buffer and Max = 1024, the engine writes in place and
// allocates nothing. There is then nothing to free, which is the point: the leak
// is removed by construction rather than by pairing a free() we could get wrong.
//
// The buffer is thread_local: NameOf is called from the render thread, the
// PostRender hook and the net-filter thread.
// ---------------------------------------------------------------------------
typedef void (*AppendStringFn)(const void* self, void* outStr);

struct FStr { wchar_t* Data; int32_t Num; int32_t Max; };

bool NameOf(uintptr_t obj, std::string& out) {
    out.clear();
    if (!PtrOk(obj)) return false;

    uint32_t cmp = R32(obj + off::UObj_Name);
    uint32_t num = R32(obj + off::UObj_Name + 4);
    uint32_t name[2] = { cmp, num };

    // Caller-owned buffer: the engine writes here instead of allocating.
    static thread_local wchar_t buf[1024];
    buf[0] = 0;

    FStr s{};
    s.Data = buf;
    s.Num  = 0;
    s.Max  = 1024;

    AppendStringFn fn = reinterpret_cast<AppendStringFn>(ImageBase() + off::AppendString);
    __try {
        fn(name, &s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    // AppendString may still hand back ITS OWN buffer for some FName kinds
    // (e.g. when the name lives in a different table). If it did, that buffer is
    // engine-owned and must NOT be freed here -- we only read it.
    const wchar_t* src = s.Data ? s.Data : buf;
    int n = s.Num;
    if (n <= 0) {
        // some builds leave Num at 0 after an in-place write; fall back to strlen
        n = 1;
        while (n < 1024 && buf[n - 1]) ++n;
    }
    if (n > 0) ++n;                       // include the terminator slot
    if (n > 1024) n = 1024;

    out.reserve(n);
    for (int i = 0; i < n - 1 && src[i]; ++i) {
        const wchar_t w = src[i];
        out.push_back(w < 128 ? static_cast<char>(w) : '?');
    }
    return !out.empty();
}

// ---------------------------------------------------------------------------
// class lookup
// ---------------------------------------------------------------------------
uintptr_t FindClassByName(const char* className) {
    if (!GObjectsReady() || !className) return 0;
    std::string nm;
    for (int32_t i = 0; i < g_num; ++i) {
        uintptr_t o = ObjectAt(i);
        if (!PtrOk(o)) continue;
        if (!PtrOk(R64(o + off::UObj_Class))) continue;   // only UObject-ish entries
        if (!NameOf(o, nm)) continue;
        if (nm == className) return o;
    }
    return 0;
}

uintptr_t FindClassContaining(const char* needle) {
    if (!GObjectsReady() || !needle) return 0;
    std::string nm;
    for (int32_t i = 0; i < g_num; ++i) {
        uintptr_t o = ObjectAt(i);
        if (!PtrOk(o)) continue;
        if (!NameOf(o, nm)) continue;
        if (nm.find(needle) != std::string::npos) return o;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// IsA: walk UStruct::SuperStruct (+0x40)
//
// Spawned actors carry their MOST-DERIVED class (APMCharacter), never the base,
// so comparing UObject::Class directly finds nothing. This mirrors UE4's
// UObject::IsA.
//
// NOTE ON NAMING: the runtime UClass for the character base is registered as
// "CyCharacterBase" in this build (no leading 'A' -- confirmed by the in-process
// verifier), so we try a candidate list instead of one hard-coded name.
// ---------------------------------------------------------------------------
bool IsA(uintptr_t obj, uintptr_t targetClass) {
    ZW_MUTATE_START();
    bool hit = false;
    if (PtrOk(obj) && PtrOk(targetClass)) {
        uintptr_t cls = R64(obj + off::UObj_Class);
        for (int depth = 0; depth < 64 && PtrOk(cls); ++depth) {
            if (cls == targetClass) { hit = true; break; }
            cls = R64(cls + off::UStruct_SuperStruct);
        }
    }
    ZW_MUTATE_END();
    return hit;
}

static uintptr_t s_charClass = 0;
static int       s_charAttempts = 0;

static uintptr_t ResolveCharacterClass() {
    // exact names first (cheap, no full scan)
    const char* kExact[] = {
        SBOX("CyCharacterBase", 0x81),
        SBOX("ACyCharacterBase", 0x82),
        SBOX("CyCharacterParent", 0x83),
        SBOX("ACyCharacterParent", 0x84),
    };
    for (const char* n : kExact) {
        uintptr_t c = FindClassByName(n);
        if (c) {
            LOG("[ue] character class = '%s' @ 0x%llX", n, (unsigned long long)c);
            return c;
        }
    }

    // Fall back to a substring scan, but only for objects that are themselves a
    // class (their Class pointer resolves to a UClass named "Class"), so we do
    // not latch onto a function or property with a similar name.
    uintptr_t uclassClass = FindClassByName("Class");
    if (!uclassClass) uclassClass = FindClassByName("CoreUObject.Class");
    std::string nm;
    for (int32_t i = 0; i < g_num; ++i) {
        uintptr_t o = ObjectAt(i);
        if (!PtrOk(o)) continue;
        if (uclassClass && R64(o + off::UObj_Class) != uclassClass) continue;
        if (!NameOf(o, nm)) continue;
        if (nm.find(SBOX("CyCharacterBase", 0x85)) != std::string::npos ||
            nm.find(SBOX("CyCharacterParent", 0x86)) != std::string::npos) {
            LOG("[ue] character class (substring) = '%s' @ 0x%llX",
                nm.c_str(), (unsigned long long)o);
            return o;
        }
    }
    return 0;
}

bool IsAOfClassNamed(uintptr_t obj, const char* /*className*/) {
    // Resolution is cached; a failed lookup is retried only occasionally, since
    // the substring fallback is a full GObjects scan and must not run per frame.
    if (!s_charClass) {
        ++s_charAttempts;
        if (s_charAttempts <= 3 || (s_charAttempts % 600) == 0) {
            s_charClass = ResolveCharacterClass();
            if (!s_charClass)
                LOG("[ue] character class not resolved yet (attempt %d, %d objects)",
                    s_charAttempts, g_num);
        }
        if (!s_charClass) return false;
    }
    return IsA(obj, s_charClass);
}

bool ClassChain(uintptr_t obj, std::string& out, int maxDepth) {
    out.clear();
    if (!PtrOk(obj)) return false;
    uintptr_t cls = R64(obj + off::UObj_Class);
    for (int d = 0; d < maxDepth && PtrOk(cls); ++d) {
        std::string n;
        if (NameOf(cls, n) && !n.empty()) {
            if (!out.empty()) out += " <- ";
            out += n;
        }
        uintptr_t next = R64(cls + off::UStruct_SuperStruct);
        if (next == cls) break;
        cls = next;
    }
    return !out.empty();
}

// One-shot diagnostic: dump every object name containing `needle`, so we can
// see the real registered class names instead of guessing them.
int DumpNamesContaining(const char* needle, int maxOut) {
    if (!GObjectsReady() || !needle) return 0;
    std::string nm;
    int shown = 0;
    for (int32_t i = 0; i < g_num && shown < maxOut; ++i) {
        uintptr_t o = ObjectAt(i);
        if (!PtrOk(o)) continue;
        if (!NameOf(o, nm)) continue;
        if (nm.find(needle) == std::string::npos) continue;
        uintptr_t cls = R64(o + off::UObj_Class);
        std::string clsName;
        if (PtrOk(cls)) NameOf(cls, clsName);
        LOG("[ue] name[%d] idx=%d addr=0x%llX '%s'  class='%s'",
            shown, i, (unsigned long long)o, nm.c_str(), clsName.c_str());
        ++shown;
    }
    LOG("[ue] DumpNamesContaining('%s') -> %d hit(s)", needle, shown);
    return shown;
}

// ---------------------------------------------------------------------------
// world
//
// TWO PATHS.
//
// The documented GWorld RVA (0x07420710, verified against OffsetsInfo.json) is
// correct and normally works. It was observed reading 0 for a whole session
// though -- during the boot window and while the game sits on menus, the global
// is simply not populated yet. When that happens GetLocalPlayerController()
// returns 0, the camera can never validate, and every downstream consumer (ESP
// projection, aim, skill lock) silently does nothing.
//
// The dump shows the live UWorld is also reachable as an INSTANCE in GObjects:
//
//     [0000B536] {0x122c390e0} World Envi_Train_Aim.Envi_Train_Aim
//
// So when the global reads 0 we locate it there instead. The scan is cached:
// the world object is stable for the lifetime of a level, and walking 198k
// objects every frame would be absurd.
// ---------------------------------------------------------------------------
static uintptr_t s_worldCache = 0;

static uintptr_t FindWorldInGObjects() {
    if (!GObjectsReady()) return 0;

    const int32_t n = NumObjects();
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t o = ObjectAt(i);
        if (!PtrOk(o)) continue;

        // A live UWorld has a valid PersistentLevel; the class default object
        // (Default__World) does not, so this also filters that out.
        uintptr_t uv = R64(o + off::UObj_Class);
        if (!PtrOk(uv)) continue;
        std::string cn;
        if (!NameOf(uv, cn)) continue;
        if (cn != "World") continue;

        uintptr_t lv = R64(o + off::UW_PersistentLevel);
        if (!PtrOk(lv)) continue;

        // must also carry an owning game instance, or it is not the play world
        uintptr_t gi = R64(o + off::UW_OwningGameInstance);
        if (!PtrOk(gi)) continue;

        LOG(SBOX("[ue] UWorld found in GObjects: 0x%llX (idx %d)", 0xB1),
            (unsigned long long)o, i);
        return o;
    }
    return 0;
}

uintptr_t GetWorld() {
    uintptr_t w = R64(ImageBase() + off::GWorld);
    if (PtrOk(w)) {
        s_worldCache = w;
        return w;
    }

    // fast path: reuse the cached world while it still looks valid
    if (PtrOk(s_worldCache)) {
        uintptr_t lv = R64(s_worldCache + off::UW_PersistentLevel);
        if (PtrOk(lv)) return s_worldCache;
        s_worldCache = 0;                 // level torn down; rescan
    }

    // slow path: locate it in GObjects (once per level)
    w = FindWorldInGObjects();
    s_worldCache = w;
    return w;
}

uintptr_t GetPersistentLevel() {
    uintptr_t w = GetWorld();
    if (!w) return 0;
    uintptr_t lv = R64(w + off::UW_PersistentLevel);
    return PtrOk(lv) ? lv : 0;
}

int32_t GetActorCount(uintptr_t level) {
    if (!PtrOk(level)) return 0;
    int32_t n = RI32(level + off::UL_Actors + off::TArray_Num);
    return (n > 0 && n < 2000000) ? n : 0;
}

uintptr_t GetActor(uintptr_t level, int32_t index) {
    if (!PtrOk(level)) return 0;
    uintptr_t data = R64(level + off::UL_Actors + off::TArray_Data);
    if (!PtrOk(data)) return 0;
    uintptr_t a = R64(data + static_cast<uintptr_t>(index) * 8);
    return PtrOk(a) ? a : 0;
}

// ---------------------------------------------------------------------------
// local player / camera
// ---------------------------------------------------------------------------
// forward declarations: GetCamera uses these, but they are defined below it
static bool PlausibleWorld(float v);
static bool PlausibleAngle(float v);
void ScanForCameraPOV(uintptr_t pcm, int range);
bool GetCameraFromPawn(Camera& out);

uintptr_t GetLocalPlayerController() {
    uintptr_t world = GetWorld();
    if (!world) return 0;
    uintptr_t gi = R64(world + off::UW_OwningGameInstance);
    if (!PtrOk(gi)) return 0;
    uintptr_t lpData = R64(gi + off::UGI_LocalPlayers + off::TArray_Data);
    if (!PtrOk(lpData)) return 0;
    uintptr_t lp = R64(lpData);                 // LocalPlayers[0]
    if (!PtrOk(lp)) return 0;
    uintptr_t pc = R64(lp + off::UPlayer_PlayerController);
    return PtrOk(pc) ? pc : 0;
}

uintptr_t GetLocalPawn() {
    uintptr_t pc = GetLocalPlayerController();
    if (!pc) return 0;
    uintptr_t pawn = R64(pc + off::APC_AcknowledgedPawn);
    return PtrOk(pawn) ? pawn : 0;
}

// ---------------------------------------------------------------------------
// Camera POV discovery + hold
//
// The box OFFSET and the camera FLICKER shared one root cause: we were not
// reading the real camera.
//
//  * This game is THIRD-PERSON. The camera sits behind and above the pawn, so a
//    view built from (pawn position, ControlRotation) projects from the wrong
//    origin -- that is the observed box offset, and it drifts with recoil/sway
//    because ControlRotation is the INPUT rotation, not the rendered camera.
//  * APCM_CameraCache (0x3B0) reads back as loc=(0,0,0) fov=90 here, so that
//    slot is not where the live POV lives. When the read failed we fell back to
//    the pawn and flipped between the two every few frames -- the
//    "cam / no-cam 一下黄一下绿" symptom, dragging the boxes with it.
//
// Fix: scan the camera manager once for the real FMinimalViewInfo and VALIDATE
// it against the pawn (the camera must sit 10..3000 uu from the player -- no
// unrelated float triple satisfies that), then keep that offset. A short HOLD of
// the last good value removes the residual one-frame flicker.
// ---------------------------------------------------------------------------
static int       s_povOff     = -1;      // offset from pcm to FMinimalViewInfo
static ULONGLONG s_povRetryMs = 0;       // last failed scan attempt
static const ULONGLONG kPovRetryMs = 1000;
static Camera    s_holdCam{};
static ULONGLONG s_holdMs     = 0;
static const ULONGLONG kHoldMs = 400;

static bool LocatePovOffset(uintptr_t pcm) {
    ZW_MUTATE_START();
    // Preferred: validate against the pawn (the camera must sit near the player).
    // Fallback: if there is no pawn yet (menu / loading), accept the first
    // FMinimalViewInfo-shaped record with a non-default FOV -- the real camera
    // uses the game's actual FOV (e.g. 55.8), whereas the dead slots return the
    // 90.0 default.
    uintptr_t pawn = GetLocalPawn();
    float px = 0, py = 0, pz = 0;
    bool havePawn = false;
    if (PtrOk(pawn)) {
        uintptr_t root = R64(pawn + off::AA_RootComponent);
        if (PtrOk(root)) {
            px = RF32(root + off::USC_RelativeLocation + 0);
            py = RF32(root + off::USC_RelativeLocation + 4);
            pz = RF32(root + off::USC_RelativeLocation + 8);
            havePawn = PlausibleWorld(px) && PlausibleWorld(py) && PlausibleWorld(pz);
        }
    }

    int   best  = -1;
    float bestD = 1e30f;

    for (int off = 0; off < 0x4000; off += 4) {
        const float x = RF32(pcm + off + 0x00);
        const float y = RF32(pcm + off + 0x04);
        const float z = RF32(pcm + off + 0x08);
        if (!PlausibleWorld(x) || !PlausibleWorld(y) || !PlausibleWorld(z)) continue;

        const float p  = RF32(pcm + off + 0x0C);
        const float ya = RF32(pcm + off + 0x10);
        const float r  = RF32(pcm + off + 0x14);
        if (!PlausibleAngle(p) || !PlausibleAngle(ya) || !PlausibleAngle(r)) continue;

        const float fov = RF32(pcm + off + 0x18);
        // Reject the 90.0 default that the dead slots return: the real camera in
        // this game sits well below it.
        if (!(fov >= 30.0f && fov <= 130.0f)) continue;
        if (fov > 88.0f && fov < 92.0f) continue;

        float d = 0.0f;
        if (havePawn) {
            const float dx = x - px, dy = y - py, dz = z - pz;
            d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (!(d > 10.0f && d < 3000.0f)) continue;
        }

        if (best < 0 || d < bestD) { bestD = d; best = off; }
    }

    // -----------------------------------------------------------------------
    // DO NOT ACCEPT AN UNVALIDATED GUESS WHEN A PAWN EXISTS.
    //
    // The old code took the first FMinimalViewInfo-shaped record if there was no
    // pawn. That is fine while loading, but it also ran when the pawn read failed
    // for any transient reason, and it cached whatever it found. Observed result:
    //
    //     [cam] real POV located at pcm+0x2234 (fov=100.0, pawnDist=-1.0)
    //
    // pawnDist=-1 means the distance test never ran, so nothing verified that
    // this record is the player's camera. The cache then pinned a wrong slot and
    // the ESP was dead for the whole session.
    //
    // Rule: accept an unvalidated record ONLY when there is genuinely no pawn
    // (loading/menu), and never overwrite an existing good offset with one.
    // -----------------------------------------------------------------------
    if (best >= 0 && !havePawn && s_povOff >= 0) {
        ZW_MUTATE_END();
        return true;                 // keep the already-validated offset
    }

    const bool ok = (best >= 0);
    if (ok) {
        s_povOff = best;
        LOG("[cam] real POV located at pcm+0x%X (fov=%.1f, pawnDist=%.1f)",
            best, RF32(pcm + best + 0x18), havePawn ? bestD : -1.0f);
    }
    ZW_MUTATE_END();
    return ok;
}

// ---------------------------------------------------------------------------
// POV validation failure counter
//
// WHY THIS EXISTS -- this was a real bug that silently disabled the whole ESP.
//
// s_povOff is a cache: once >= 0, GetCamera never re-runs the locator. If that
// cached offset is a DEAD slot the camera reads garbage (0,0,0 / fov=90) forever
// and NOTHING can ever recover -- which is exactly what happened:
//
//     [cam] using the cache record at pcm+0x1CD0 (fov=90.0)     <- dead slot
//     [cam] real POV located at pcm+0x2234 (fov=100, pawnDist=-1) <- unvalidated
//     [esp/ingame] stage=NO_CAMERA ents=10 valid=0 fov=0.0       <- forever
//
// Two fixes, both below:
//   1. If the cached POV fails to read for kPovFailLimit consecutive frames,
//      drop the cache and let the locator scan again.
//   2. Refuse an unvalidated POV when a pawn EXISTS but the distance check could
//      not run (pawnDist=-1). Accepting a guess is what poisoned the cache.
// ---------------------------------------------------------------------------
static int s_povFailStreak = 0;
static const int kPovFailLimit = 30;    // ~0.5 s at 60 fps, ~0.12 s at 240

bool GetCamera(Camera& out) {
    out = Camera{};

    uintptr_t pc = GetLocalPlayerController();
    if (PtrOk(pc)) {
        uintptr_t pcm = R64(pc + off::APC_PlayerCameraManager);
        if (PtrOk(pcm)) {
            if (s_povOff < 0) {
                // Retry the scan on a timer.
                //
                // The first version set a "scanned, gave up" flag after one
                // failure. That failure happened while the pawn was still null
                // (menu/loading), so the locator never ran again and the tool
                // stayed on the pawn fallback forever -- which is exactly the
                // box offset and the flicker the user kept seeing.
                const ULONGLONG now = GetTickCount64();
                if (now - s_povRetryMs < kPovRetryMs) {
                    // still in the retry cooldown; fall through to the fallback
                } else {
                    s_povRetryMs = now;

                    // try the documented slot first
                    // Try the documented CameraCache slot, but VALIDATE it the same
                    // way the scanner does.
                    //
                    // The old code accepted it on SHAPE alone, and this game's
                    // dumped 0x3B0 slot reads back as loc=(0,0,0) fov=90.0 -- a
                    // dead slot that looks structurally valid. Caching it killed
                    // the camera, and therefore the whole ESP, for the session.
                    //
                    // Two independent checks now:
                    //   * the camera must sit 10..3000 uu from the pawn
                    //   * fov 90.0 exactly is this game's default-FOV signature
                    //     for a slot that is NOT the live camera
                    const uintptr_t cand = off::APCM_CameraCache + off::FCC_POV;
                    const float x = RF32(pcm + cand + 0);
                    const float y = RF32(pcm + cand + 4);
                    const float z = RF32(pcm + cand + 8);
                    const float fov = RF32(pcm + cand + off::FMVI_FOV);

                    const bool shapeOk = PlausibleWorld(x) && PlausibleWorld(y) &&
                                         PlausibleWorld(z) &&
                                         fov >= 30.0f && fov <= 130.0f;
                    const bool isDefaultFov = (fov > 89.5f && fov < 90.5f);

                    bool distOk = true;
                    uintptr_t pawn = GetLocalPawn();
                    if (PtrOk(pawn)) {
                        uintptr_t root = R64(pawn + off::AA_RootComponent);
                        if (PtrOk(root)) {
                            const float px = RF32(root + off::USC_RelativeLocation + 0);
                            const float py = RF32(root + off::USC_RelativeLocation + 4);
                            const float pz = RF32(root + off::USC_RelativeLocation + 8);
                            if (PlausibleWorld(px) && PlausibleWorld(py) && PlausibleWorld(pz)) {
                                const float dx = x - px, dy = y - py, dz = z - pz;
                                const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                                distOk = (d > 10.0f && d < 3000.0f);
                            }
                        }
                    }

                    if (shapeOk && distOk && !isDefaultFov) {
                        s_povOff = (int)cand;
                        LOG(SBOX("[cam] using the cache record at pcm+0x%X (fov=%.1f)", 0x8C),
                            s_povOff, fov);
                    } else {
                        if (shapeOk && isDefaultFov)
                            LOG(SBOX("[cam] cache slot 0x%X has default fov 90.0 -- rejected", 0xAF),
                                (int)cand);
                        LocatePovOffset(pcm);   // may still fail; retried next tick
                    }
                }
            }

            if (s_povOff >= 0) {
                const uintptr_t pov = pcm + (uintptr_t)s_povOff;
                const float x = RF32(pov + 0x00);
                const float y = RF32(pov + 0x04);
                const float z = RF32(pov + 0x08);
                const float p = RF32(pov + 0x0C);
                const float ya = RF32(pov + 0x10);
                const float r = RF32(pov + 0x14);
                const float fov = RF32(pov + 0x18);
                if (PlausibleWorld(x) && PlausibleWorld(y) && PlausibleWorld(z) &&
                    fov >= 40.0f && fov <= 130.0f) {
                    out.locX = x; out.locY = y; out.locZ = z;
                    out.rotPitch = p; out.rotYaw = ya; out.rotRoll = r;
                    out.fov = fov; out.valid = true;
                    s_holdCam = out; s_holdMs = GetTickCount64();
                    s_povFailStreak = 0;
                    return true;
                }

                // The cached offset produced garbage. Count it and, past the
                // limit, throw the cache away so the locator runs again. Without
                // this the ESP stays dead for the whole session (see the note on
                // s_povFailStreak above).
                if (++s_povFailStreak >= kPovFailLimit) {
                    LOG(SBOX("[cam] cached POV pcm+0x%X went stale (%d fails); rescanning", 0xAE),
                        s_povOff, s_povFailStreak);
                    s_povOff = -1;
                    s_povFailStreak = 0;
                    s_povRetryMs = 0;      // no cooldown: rescan immediately
                }
            }
        }
    }

    if (GetCameraFromPawn(out)) {
        s_holdCam = out; s_holdMs = GetTickCount64();
        return true;
    }

    // HOLD the last good camera briefly so one failed read does not flip the
    // projection (and the status colour) off and on.
    if (s_holdCam.valid && GetTickCount64() - s_holdMs < kHoldMs) {
        out = s_holdCam;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// world -> screen, pure math
//
// DO NOT reintroduce a call to the game's ProjectWorldLocationToScreen here.
// It is a member function (this in RCX); calling it with a free-function cast
// makes it dereference a garbage `this` and the game dies with
// EXCEPTION_ACCESS_VIOLATION reading 0x08000008.
//
// Instead we take the camera POV (location + rotation + FOV, all plain data we
// read with guarded reads) and build the view transform ourselves.
//
// UE4 FRotationMatrix for FRotator(Pitch, Yaw, Roll) -- row 0 = forward (X),
// row 1 = right (Y), row 2 = up (Z):
//   Fwd   = ( CP*CY,               CP*SY,               SP      )
//   Right = ( SR*SP*CY - CR*SY,    SR*SP*SY + CR*CY,   -SR*CP   )
//   Up    = ( -(CR*SP*CY + SR*SY), CY*SR - CR*SP*SY,    CR*CP    )
// ---------------------------------------------------------------------------
static int s_screenW = 1920;
static int s_screenH = 1080;

// Camera snapshot for the CURRENT frame.
//
// WorldToScreen used to call GetCamera() for every entity, which walked the
// whole GWorld -> GameInstance -> LocalPlayer -> Controller -> CameraManager
// chain (and previously logged to disk) once per target. With ~200 targets that
// dominated the frame. Now the camera is fetched once per frame by BeginFrame().
static Camera s_frameCam{};
static bool   s_frameCamValid = false;

void SetScreenSize(int w, int h) {
    if (w > 0 && h > 0) { s_screenW = w; s_screenH = h; }
}

void BeginFrame() {
    s_frameCamValid = GetCamera(s_frameCam);
}

bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy) {
    sx = sy = 0.0f;

    if (!s_frameCamValid) return false;
    const Camera& cam = s_frameCam;
    if (!cam.valid) return false;
    if (cam.fov <= 1.0f || cam.fov >= 179.0f) return false;

    if (s_screenW <= 0 || s_screenH <= 0) return false;
    const float W = (float)s_screenW;
    const float H = (float)s_screenH;

    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    const float P = cam.rotPitch * kDeg2Rad;
    const float Y = cam.rotYaw   * kDeg2Rad;
    const float R = cam.rotRoll  * kDeg2Rad;

    const float SP = sinf(P), CP = cosf(P);
    const float SY = sinf(Y), CY = cosf(Y);
    const float SR = sinf(R), CR = cosf(R);

    const float fwdX = CP * CY, fwdY = CP * SY, fwdZ = SP;
    const float rgtX = SR * SP * CY - CR * SY;
    const float rgtY = SR * SP * SY + CR * CY;
    const float rgtZ = -SR * CP;
    const float upX  = -(CR * SP * CY + SR * SY);
    const float upY  = CY * SR - CR * SP * SY;
    const float upZ  = CR * CP;

    // world delta
    const float dx = wx - cam.locX;
    const float dy = wy - cam.locY;
    const float dz = wz - cam.locZ;

    // into camera space
    const float cx = dx * rgtX + dy * rgtY + dz * rgtZ;   // right
    const float cy = dx * upX  + dy * upY  + dz * upZ;    // up
    const float cz = dx * fwdX + dy * fwdY + dz * fwdZ;   // forward (depth)

    if (cz <= 1.0f) return false;                         // behind / too close

    // FMinimalViewInfo::FOV is the HORIZONTAL fov in UE4
    const float tanH = tanf(cam.fov * 0.5f * kDeg2Rad);
    if (tanH <= 0.0001f) return false;
    const float aspect = W / H;
    const float tanV = tanH / aspect;

    sx = W * 0.5f + (cx / cz) * (W * 0.5f) / tanH;
    sy = H * 0.5f - (cy / cz) * (H * 0.5f) / tanV;

    if (!(sx == sx) || !(sy == sy)) return false;         // NaN guard
    return true;
}

bool GetActorLocation(uintptr_t actor, Vec3& out) {
    out = Vec3{0, 0, 0};
    if (!PtrOk(actor)) return false;
    uintptr_t root = R64(actor + off::AA_RootComponent);
    if (!PtrOk(root)) return false;
    float x = RF32(root + off::USC_RelativeLocation + 0);
    float y = RF32(root + off::USC_RelativeLocation + 4);
    float z = RF32(root + off::USC_RelativeLocation + 8);
    if (!(x == x) || !(y == y) || !(z == z)) return false;
    out.x = x; out.y = y; out.z = z;
    return true;
}

// ---------------------------------------------------------------------------
// Scan the PlayerCameraManager for the real FMinimalViewInfo.
//
// The dumped CameraCache offset (0x3B0) reads back as loc=(0,0,0) fov=90.0 here,
// i.e. that slot is not where this build keeps the live POV. Rather than guess
// again, we scan for the structural signature of FMinimalViewInfo:
//   +0x00 FVector Location   (3 plausible world floats, not all zero)
//   +0x0C FRotator Rotation  (3 floats within +-360)
//   +0x18 float FOV          (30..130)
// and log every hit with its offset.
// ---------------------------------------------------------------------------
static bool PlausibleWorld(float v) {
    if (!(v == v)) return false;
    const float a = v < 0 ? -v : v;
    return a > 1.0f && a < 200000.0f;
}
static bool PlausibleAngle(float v) {
    if (!(v == v)) return false;
    return v >= -360.0f && v <= 360.0f;
}

void ScanForCameraPOV(uintptr_t pcm, int range) {
    if (!PtrOk(pcm)) { LOG("[scan] pcm null"); return; }
    LOG(SBOX("[scan] scanning pcm 0x%llX for the view-info record...", 0x8A),
        (unsigned long long)pcm);

    int hits = 0;
    for (int off = 0; off < range && hits < 12; off += 4) {
        float x = RF32(pcm + off + 0x00);
        float y = RF32(pcm + off + 0x04);
        float z = RF32(pcm + off + 0x08);
        float p = RF32(pcm + off + 0x0C);
        float ya = RF32(pcm + off + 0x10);
        float r = RF32(pcm + off + 0x14);
        float fov = RF32(pcm + off + 0x18);

        if (!PlausibleWorld(x) || !PlausibleWorld(y) || !PlausibleWorld(z)) continue;
        if (!PlausibleAngle(p) || !PlausibleAngle(ya) || !PlausibleAngle(r)) continue;
        if (!(fov >= 30.0f && fov <= 130.0f)) continue;

        LOG("[scan]  +0x%03X  loc=(%.1f, %.1f, %.1f) rot=(%.2f, %.2f, %.2f) fov=%.2f",
            off, x, y, z, p, ya, r, fov);
        ++hits;
    }
    LOG("[scan] %d candidate POV offset(s) in 0x%X bytes", hits, range);
}

// ---------------------------------------------------------------------------
// Fallback camera: local pawn eye position + controller ControlRotation.
// Independent of CameraCache, so ESP still works if the manager layout differs.
// ---------------------------------------------------------------------------
bool GetCameraFromPawn(Camera& out) {
    out = Camera{};
    uintptr_t pc = GetLocalPlayerController();
    if (!PtrOk(pc)) return false;

    uintptr_t pawn = R64(pc + off::APC_AcknowledgedPawn);
    if (!PtrOk(pawn)) return false;

    uintptr_t root = R64(pawn + off::AA_RootComponent);
    if (!PtrOk(root)) return false;

    float x = RF32(root + off::USC_RelativeLocation + 0);
    float y = RF32(root + off::USC_RelativeLocation + 4);
    float z = RF32(root + off::USC_RelativeLocation + 8);

    const float p  = RF32(pc + off::AC_ControlRotation + 0);
    const float ya = RF32(pc + off::AC_ControlRotation + 4);
    const float r  = RF32(pc + off::AC_ControlRotation + 8);

    // NOTE: no logging on this path. It runs every frame (and used to run once
    // per entity); each LOG is an fopen/fclose, which was the main cause of the
    // 20 FPS cap. Only log on state transitions.
    if (!PlausibleWorld(x) || !PlausibleWorld(y) || !PlausibleWorld(z)) {
        // The pawn's root may not carry a location in this build; try scanning
        // the pawn for a plausible world position (same signature ESP uses).
        bool found = false;
        for (int off = 0x100; off < 0x600 && !found; off += 4) {
            float ax = RF32(pawn + off + 0), ay = RF32(pawn + off + 4), az = RF32(pawn + off + 8);
            if (!PlausibleWorld(ax) || !PlausibleWorld(ay) || !PlausibleWorld(az)) continue;
            LOG("[pawncam] candidate pawn loc at +0x%X = (%.1f, %.1f, %.1f)", off, ax, ay, az);
            x = ax; y = ay; z = az; found = true;
        }
        if (!found) {
            LOG("[pawncam] FAILED: no plausible pawn location found");
            return false;
        }
    }

    out.locX = x;
    out.locY = y;
    out.locZ = z + 64.0f;      // eye height above the capsule origin
    out.rotPitch = p;
    out.rotYaw = ya;
    out.rotRoll = r;

    uintptr_t pcm = R64(pc + off::APC_PlayerCameraManager);
    float fov = PtrOk(pcm) ? RF32(pcm + off::APCM_DefaultFOV) : 0.0f;
    if (!(fov >= 30.0f && fov <= 130.0f)) fov = 90.0f;
    out.fov = fov;

    out.valid = true;
    return true;
}

void DumpPlayerChain() {
    LOG("[chain] image base = 0x%llX", (unsigned long long)ImageBase());

    uintptr_t pc = GetLocalPlayerController();
    LOG("[chain] PlayerController = 0x%llX", (unsigned long long)pc);
    if (!PtrOk(pc)) return;

    uintptr_t pawn = R64(pc + off::APC_AcknowledgedPawn);
    uintptr_t pcm  = R64(pc + off::APC_PlayerCameraManager);
    LOG("[chain] Pawn=0x%llX  CameraManager=0x%llX", (unsigned long long)pawn,
        (unsigned long long)pcm);

    float px = 0, py = 0, pz = 0;
    if (PtrOk(pawn)) {
        uintptr_t root = R64(pawn + off::AA_RootComponent);
        px = RF32(root + off::USC_RelativeLocation + 0);
        py = RF32(root + off::USC_RelativeLocation + 4);
        pz = RF32(root + off::USC_RelativeLocation + 8);
        LOG("[chain] pawn loc = (%.1f, %.1f, %.1f)", px, py, pz);
    }

    if (!PtrOk(pcm)) { LOG("[chain] no camera manager"); return; }

    // Dump every camera cache the SDK declares, plus the Actor's own transform.
    const uint32_t caches[] = {
        off::APCM_CameraCache,                      // 0x3B0
        0x9E0,                                      // LastFrameCameraCache
        0x1CC0,                                     // CameraCachePrivate
        0x22F0,                                     // LastFrameCameraCachePrivate
    };
    for (uint32_t c : caches) {
        const uintptr_t pov = pcm + c + off::FCC_POV;   // +0x10
        const float x = RF32(pov + 0x00);
        const float y = RF32(pov + 0x04);
        const float z = RF32(pov + 0x08);
        const float p = RF32(pov + 0x0C);
        const float ya = RF32(pov + 0x10);
        const float r = RF32(pov + 0x14);
        const float fov = RF32(pov + 0x18);
        float d = -1.0f;
        if (PtrOk(pawn)) {
            const float dx = x - px, dy = y - py, dz = z - pz;
            d = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        LOG("[chain] cache+0x%04X: loc=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f,%.1f) fov=%.1f  distFromPawn=%.1f",
            c + off::FCC_POV, x, y, z, p, ya, r, fov, d);
    }

    // Wide scan: every float triple 10..5000 uu from the pawn with a plausible
    // FOV within 8 bytes. This is what the auto-locator uses, but with a much
    // larger window and the distance printed for each hit.
    LOG(SBOX("[chain] scanning pcm 0..0x8000 for the view-info record (pawn-relative)...", 0x8B));
    int hits = 0;
    for (int off = 0; off < 0x8000 && hits < 20; off += 4) {
        const float x = RF32(pcm + off + 0x00);
        const float y = RF32(pcm + off + 0x04);
        const float z = RF32(pcm + off + 0x08);
        if (!PlausibleWorld(x) || !PlausibleWorld(y) || !PlausibleWorld(z)) continue;

        float d = -1.0f;
        if (PtrOk(pawn)) {
            const float dx = x - px, dy = y - py, dz = z - pz;
            d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (!(d > 10.0f && d < 5000.0f)) continue;
        }
        const float p = RF32(pcm + off + 0x0C);
        const float ya = RF32(pcm + off + 0x10);
        const float r = RF32(pcm + off + 0x14);
        if (!PlausibleAngle(p) || !PlausibleAngle(ya) || !PlausibleAngle(r)) continue;
        const float fov = RF32(pcm + off + 0x18);
        if (!(fov >= 20.0f && fov <= 150.0f)) continue;

        LOG("[chain]  +0x%04X loc=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f,%.1f) fov=%.1f dist=%.1f",
            off, x, y, z, p, ya, r, fov, d);
        ++hits;
    }
    LOG("[chain] scan hits = %d", hits);
}

// ---------------------------------------------------------------------------
// FName -> text, for a raw name index.
//
// ue::NameOf works on a UObject (it reads the FName straight out of the object).
// The skeleton code needs to resolve an arbitrary FName index, so this exposes
// the same AppendString path for a bare index.
//
// FNameEntry is indexed through GNames' chunked table exactly like GObjects:
//   chunk = GNames[index >> 16];  entry = chunk + (index & 0xFFFF) * stride
// The stride is 2 on this build for a 4-byte header, which is what the working
// NameOf implementation already relies on.
// ---------------------------------------------------------------------------
bool NameFromIndex(uint32_t index, char* out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = 0;

    // -----------------------------------------------------------------------
    // SAME CALLER-OWNED-BUFFER DISCIPLINE AS NameOf -- see the long note above it.
    //
    // This was the SECOND leak site with the identical bug: a zeroed FString made
    // AppendString allocate, and nothing ever freed it. It is called per bone per
    // mesh during skeleton resolve, so it contributed to the same 7.2 GB growth.
    // -----------------------------------------------------------------------
    uint32_t name[2] = { index, 0 };

    static thread_local wchar_t buf[1024];
    buf[0] = 0;

    FStr s{};
    s.Data = buf;
    s.Num  = 0;
    s.Max  = 1024;

    AppendStringFn fn = reinterpret_cast<AppendStringFn>(ImageBase() + off::AppendString);
    __try {
        fn(name, &s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    const wchar_t* src = s.Data ? s.Data : buf;
    int len = s.Num;
    if (len <= 0) {
        len = 1;
        while (len < 1024 && buf[len - 1]) ++len;
    }
    if (len > 1024) len = 1024;

    const int lim = (len < (int)cap) ? len : (int)cap - 1;
    int w = 0;
    for (int i = 0; i + 1 < lim && src[i]; ++i) {
        const wchar_t c = src[i];
        out[w++] = (c < 128) ? static_cast<char>(c) : '?';
    }
    out[w] = 0;
    return w > 0;
}

} // namespace ue
