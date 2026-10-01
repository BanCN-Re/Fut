// D:\Work\Fut\dumper\live_probe.c
// In-process live value reader for the 2026-09-30 Calabiyau (UE4.25.4) build.
//
// Deployed as the zlibwapi proxy (129 forwarded exports + DllMain thread).
// Walks GObjects with the freshly dumped offsets, finds the live UWorld,
// resolves ACyCharacterBase instances, and dumps HP / team / skin / movement
// for every one of them into a text report.
//
// Build: mingw gcc (WinLibs UCRT) -- see build_probe.ps1
// ASCII-only source.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* ---- offsets dumped by Dumper-7 on 2026-09-30 ------------------------- */
#define OFF_GOBJECTS   0x072BE4B0u
#define OFF_GWORLD     0x07420710u
#define ELEMS_PER_CHUNK 0x10000

/* FUObjectItem */
#define FUOBJITEM_SIZE 0x18

/* UObject (UE4.25, as resolved by Dumper-7 for this build) */
#define UOBJ_VTABLE 0x00
#define UOBJ_FLAGS  0x08
#define UOBJ_INDEX  0x0C
#define UOBJ_CLASS  0x10
#define UOBJ_NAME   0x18
#define UOBJ_OUTER  0x20

/* UStruct / UClass name chain */
#define USTRUCT_SUPER 0x40

/* UWorld */
#define UW_PERSISTENTLEVEL     0x30
#define UW_LEVELS              0x148
#define UW_OWNINGGAMEINSTANCE  0x188

/* ULevel */
#define UL_ACTORS 0x98

/* AActor */
#define AA_ROOTCOMPONENT 0x168

/* APawn */
#define AP_PLAYERSTATE 0x330
#define AP_CONTROLLER  0x348

/* ACharacter */
#define AC_MESH 0x370

/* USceneComponent */
#define USC_RELLOC 0x198

/* ACyCharacterBase */
#define CY_INVENTORY   0x7E8
#define CY_SKINCOMP    0x868
#define CY_LIFECOMP    0x870
#define CY_LIFESTATECOMP 0x878
#define CY_ABILITYSYS  0x880
#define CY_TEAMID      0x8B0
#define CY_SKINID      0x8B4

/* UCyBaseAttributeSet (FGameplayAttributeData: 8B pad + BaseValue + CurrentValue) */
#define ATTR_HEALTH      0x78
#define ATTR_HEALTHMAX   0x88
#define ATTR_ARMOR       0x98
#define ATTR_ARMORMAX    0xA8
#define ATTR_EXTRAARMOR  0xB8
#define ATTR_EXTRAARMAX  0xC8

/* APMCharacter */
#define PM_ROLEID 0x17C4

/* UCharacterMovementComponent */
#define CMC_MAXWALKSPEED 0x20C
#define CMC_JUMPZ        0x1D0
#define CMC_MAXACCEL     0x220

static FILE* g_log = NULL;

static void L(const char* fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static int safe_read(uintptr_t addr, void* out, size_t size)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (addr < 0x10000) return 0;
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if ((mbi.Protect & 0xFF) == PAGE_NOACCESS) return 0;

    uintptr_t region_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    if (addr + size > region_end) {
        /* split across regions */
        size_t first = (size_t)(region_end - addr);
        if (!safe_read(addr, out, first)) return 0;
        return safe_read(region_end, (uint8_t*)out + first, size - first);
    }
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, size, &got)) return 0;
    return got == size;
}

static uint64_t R64(uintptr_t a) { uint64_t v = 0; safe_read(a, &v, 8); return v; }
static uint32_t R32(uintptr_t a) { uint32_t v = 0; safe_read(a, &v, 4); return v; }
static int32_t  RI32(uintptr_t a) { int32_t v = 0; safe_read(a, &v, 4); return v; }
static uint8_t  R8(uintptr_t a)  { uint8_t v = 0; safe_read(a, &v, 1); return v; }
static float    RF32(uintptr_t a) { float v = 0; safe_read(a, &v, 4); return v; }

/* portable NaN check (MSVC C mode has no isnan macro) */
static int is_nan_f(float f) { return !(f == f); }

static int ptr_ok(uint64_t p) { return p > 0x10000 && p < 0x00007FFFFFFFFFFFULL; }

/* ---- class name resolution (walk Outer chain) ------------------------- */
static void read_name_index(uintptr_t obj, char* out, size_t n)
{
    uint32_t cmp = R32(obj + UOBJ_NAME);
    uint32_t num = R32(obj + UOBJ_NAME + 4);
    snprintf(out, n, "#%u_%u", cmp, num);
}

/* ---- GObjects walk ---------------------------------------------------- */
typedef struct {
    uintptr_t obj;
    uintptr_t cls;
} ObjEnt;

static uintptr_t g_objects_ptr = 0;
static int32_t   g_num_elems = 0;
static int32_t   g_num_chunks = 0;

static int gobjects_init(uintptr_t base)
{
    uintptr_t slot = base + OFF_GOBJECTS;
    uintptr_t objects = R64(slot);
    int32_t maxe = RI32(slot + 0x10);
    int32_t nume = RI32(slot + 0x14);
    int32_t maxc = RI32(slot + 0x18);
    int32_t numc = RI32(slot + 0x1C);
    L("[gobjects] slot=0x%llX Objects=0x%llX Max=%d Num=%d MaxChunks=%d NumChunks=%d",
      (unsigned long long)slot, (unsigned long long)objects, maxe, nume, maxc, numc);
    if (!ptr_ok(objects) || nume <= 0 || nume > 4000000) return 0;
    g_objects_ptr = objects;
    g_num_elems = nume;
    g_num_chunks = numc;
    return 1;
}

/* item i -> UObject* */
static uintptr_t gobject_at(int32_t idx)
{
    if (idx < 0 || idx >= g_num_elems) return 0;
    int32_t chunk = idx / ELEMS_PER_CHUNK;
    int32_t within = idx % ELEMS_PER_CHUNK;
    uintptr_t chunk_ptr = R64(g_objects_ptr + (uintptr_t)chunk * 8);
    if (!ptr_ok(chunk_ptr)) return 0;
    return R64(chunk_ptr + (uintptr_t)within * FUOBJITEM_SIZE + 0x00);
}

/* ---- scan for a class object by name index is not possible without FName,
        so we identify classes structurally: a UClass whose default object's
        vtable we can match. Instead we collect candidate ACyCharacterBase
        instances by walking the live UWorld -> Levels -> Actors. ---------- */

static int dump_actor(uintptr_t actor, int idx)
{
    uintptr_t root = R64(actor + AA_ROOTCOMPONENT);
    if (!ptr_ok(root)) return 0;

    float x = RF32(root + USC_RELLOC + 0);
    float y = RF32(root + USC_RELLOC + 4);
    float z = RF32(root + USC_RELLOC + 8);
    if (is_nan_f(x) || is_nan_f(y) || is_nan_f(z)) return 0;
    if (fabsf(x) > 200000.f || fabsf(y) > 200000.f) return 0;

    uintptr_t ability = R64(actor + CY_ABILITYSYS);
    uint8_t team = R8(actor + CY_TEAMID);
    uint32_t skin = R32(actor + CY_SKINID);
    uint32_t role = R32(actor + PM_ROLEID);
    uintptr_t mesh = R64(actor + AC_MESH);
    uintptr_t ps = R64(actor + AP_PLAYERSTATE);
    uintptr_t ctrl = R64(actor + AP_CONTROLLER);
    uintptr_t lifecomp = R64(actor + CY_LIFECOMP);
    uintptr_t lifestate = R64(actor + CY_LIFESTATECOMP);

    float hp = 0, hpmax = 0, armor = 0, armax = 0;
    if (ptr_ok(ability)) {
        hp    = RF32(ability + ATTR_HEALTH + 0x0C);
        hpmax = RF32(ability + ATTR_HEALTHMAX + 0x0C);
        armor = RF32(ability + ATTR_ARMOR + 0x0C);
        armax = RF32(ability + ATTR_ARMORMAX + 0x0C);
    }

    float speed = 0, jumpz = 0, accel = 0;
    /* CharMovement is on the pawn; find it by scanning the first 0x1000 for a
       plausible CMC is out of scope here - reported as 0 unless the actor
       carries it directly. Kept as a placeholder field. */
    (void)speed; (void)jumpz; (void)accel;

    L("actor[%04d] addr=0x%llX root=0x%llX loc=(%.1f, %.1f, %.1f) team=%u skin=%u role=%u",
      idx, (unsigned long long)actor, (unsigned long long)root, x, y, z, team, skin, role);
    L("            ability=0x%llX hp=%.2f/%.2f armor=%.2f/%.2f mesh=0x%llX ps=0x%llX ctrl=0x%llX life=0x%llX ls=0x%llX",
      (unsigned long long)ability, hp, hpmax, armor, armax,
      (unsigned long long)mesh, (unsigned long long)ps,
      (unsigned long long)ctrl, (unsigned long long)lifecomp, (unsigned long long)lifestate);
    return 1;
}

static void dump_world(uintptr_t base)
{
    uintptr_t slot = base + OFF_GWORLD;
    uintptr_t world = R64(slot);
    L("[gworld] slot=0x%llX UWorld=0x%llX", (unsigned long long)slot, (unsigned long long)world);
    if (!ptr_ok(world)) { L("[gworld] INVALID"); return; }

    uintptr_t level = R64(world + UW_PERSISTENTLEVEL);
    L("[gworld] PersistentLevel=0x%llX", (unsigned long long)level);

    /* iterate Levels TArray */
    uintptr_t levels_data = R64(world + UW_LEVELS);
    int32_t   levels_num  = RI32(world + UW_LEVELS + 8);
    L("[gworld] Levels.Data=0x%llX Num=%d", (unsigned long long)levels_data, levels_num);
    if (levels_num < 0 || levels_num > 4096) levels_num = 0;

    int total_actors = 0, dumped = 0;
    for (int li = 0; li < levels_num; li++) {
        uintptr_t lv = levels_data ? R64(levels_data + (uintptr_t)li * 8) : 0;
        if (!ptr_ok(lv)) continue;
        uintptr_t actors_data = R64(lv + UL_ACTORS);
        int32_t   actors_num  = RI32(lv + UL_ACTORS + 8);
        if (!ptr_ok(actors_data) || actors_num <= 0 || actors_num > 2000000) continue;
        L("[level %d] 0x%llX actors=%d", li, (unsigned long long)lv, actors_num);
        for (int ai = 0; ai < actors_num; ai++) {
            uintptr_t a = R64(actors_data + (uintptr_t)ai * 8);
            if (!ptr_ok(a)) continue;
            total_actors++;
            dumped += dump_actor(a, ai);
        }
    }
    L("[world] total actor slots=%d  dumped=%d", total_actors, dumped);
}

static DWORD WINAPI ProbeThread(LPVOID mod)
{
    (void)mod;
    Sleep(25000);   /* let the engine finish booting */

    char path[MAX_PATH];
    /* keep the report next to the deployed DLL */
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ProbeThread, &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = 0;
    strncat(path, "fut_probe_report.txt", MAX_PATH - strlen(path) - 1);

    g_log = fopen(path, "w");
    if (!g_log) return 1;

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    L("=== FUT live probe ===");
    L("image base = 0x%llX", (unsigned long long)base);
    L("pid=%lu", GetCurrentProcessId());

    if (!gobjects_init(base)) {
        L("[gobjects] INIT FAILED - offset wrong for this build");
    } else {
        L("[gobjects] OK");
        /* report a few entries */
        for (int i = 0; i < 8; i++) {
            uintptr_t o = gobject_at(i);
            if (!ptr_ok(o)) continue;
            char nm[64]; read_name_index(o, nm, sizeof(nm));
            L("  gobject[%d] = 0x%llX vtable=0x%llX class=0x%llX name=%s",
              i, (unsigned long long)o, (unsigned long long)R64(o + UOBJ_VTABLE),
              (unsigned long long)R64(o + UOBJ_CLASS), nm);
        }
    }

    dump_world(base);

    L("=== done ===");
    fclose(g_log);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, ProbeThread, hModule, 0, NULL);
    }
    return TRUE;
}
