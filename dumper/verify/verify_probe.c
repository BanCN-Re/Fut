// D:\Work\Fut\dumper\verify\verify_probe.c
// In-process verifier v2 for the 2026-09-30 Calabiyau (UE4.25.4) build.
//
// Answers, with hard bytes:
//   1. are function bodies plaintext in memory?          (yes/no + entropy)
//   2. is GObjects plaintext and walkable?               (count + samples)
//   3. is GWorld non-null (retried until a map loads)?
//   4. can we resolve FName via AppendString?            (real class names)
//   5. are ACTOR COORDINATES plaintext?                  (find characters, read loc)
//
// Deployed as the zlibwapi proxy. Writes fut_verify_report.txt next to the DLL.

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define OFF_GOBJECTS     0x072BE4B0u
#define OFF_GWORLD       0x07420710u
#define OFF_APPENDSTRING 0x0288FBB0u
#define ELEMS_PER_CHUNK  0x10000
#define FUOBJITEM_SIZE   0x18

#define UOBJ_VTABLE 0x00
#define UOBJ_CLASS  0x10
#define UOBJ_NAME   0x18
#define UOBJ_OUTER  0x20

#define UW_PERSISTENTLEVEL 0x30
#define UW_LEVELS          0x148
#define UL_ACTORS          0x98
#define AA_ROOTCOMPONENT   0x168
#define USC_RELLOC         0x198

/* ACyCharacterBase */
#define CY_ABILITYSYS 0x880
#define CY_TEAMID     0x8B0
#define CY_SKINID     0x8B4
/* UCyBaseAttributeSet */
#define ATTR_HEALTH    0x78
#define ATTR_HEALTHMAX 0x88
/* APMCharacter */
#define PM_ROLEID 0x17C4

static FILE* g_log = NULL;
static void L(const char* fmt, ...)
{
    va_list ap; if (!g_log) return;
    va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

static int safe_read(uintptr_t addr, void* out, size_t size)
{
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T got = 0;
    if (addr < 0x10000) return 0;
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, size, &got)) return 0;
    return got == size;
}
static uint64_t R64(uintptr_t a){ uint64_t v=0; safe_read(a,&v,8); return v; }
static uint32_t R32(uintptr_t a){ uint32_t v=0; safe_read(a,&v,4); return v; }
static int32_t  RI32(uintptr_t a){ int32_t v=0; safe_read(a,&v,4); return v; }
static float    RF32(uintptr_t a){ float v=0; safe_read(a,&v,4); return v; }
static int ptr_ok(uint64_t p){ return p > 0x10000 && p < 0x00007FFFFFFFFFFFULL; }
static int is_nan_f(float f){ return !(f == f); }

static double entropy(const uint8_t* d, size_t n)
{
    size_t freq[256]; size_t i; double e = 0.0;
    if (!n) return 0.0;
    memset(freq, 0, sizeof(freq));
    for (i = 0; i < n; i++) freq[d[i]]++;
    for (i = 0; i < 256; i++) if (freq[i]) {
        double p = (double)freq[i] / (double)n;
        e -= p * (log(p) / log(2.0));
    }
    return e;
}

/* ---------- FName resolution through the game's own AppendString -------- */
typedef struct { wchar_t* Data; int32_t Num; int32_t Max; } FStr;

static uintptr_t g_base = 0;
static int g_append_ok = 0;

static int fname_to_str(uint32_t cmp_index, uint32_t number, char* out, size_t outn)
{
    typedef void (*AppendFn)(const void* self, void* outStr);
    AppendFn fn;
    FStr s;
    uint32_t name[2];

    out[0] = 0;
    if (!g_append_ok) return 0;

    fn = (AppendFn)(g_base + OFF_APPENDSTRING);
    memset(&s, 0, sizeof(s));
    name[0] = cmp_index;
    name[1] = number;

    __try {
        fn(name, &s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    if (!s.Data || s.Num <= 0) return 0;
    {
        int i, n = s.Num < 200 ? s.Num : 200;
        for (i = 0; i < n - 1 && s.Data[i]; i++)
            out[i] = (char)(s.Data[i] < 128 ? s.Data[i] : '?');
        out[i] = 0;
    }
    return 1;
}

/* ---------- GObjects walk ---------------------------------------------- */
static uintptr_t g_objects = 0;
static int32_t   g_num = 0;

static uintptr_t gobject_at(int32_t idx)
{
    uintptr_t chunk;
    if (idx < 0 || idx >= g_num || !g_objects) return 0;
    chunk = R64(g_objects + (uintptr_t)(idx / ELEMS_PER_CHUNK) * 8);
    if (!ptr_ok(chunk)) return 0;
    return R64(chunk + (uintptr_t)(idx % ELEMS_PER_CHUNK) * FUOBJITEM_SIZE);
}

static void obj_name(uintptr_t obj, char* out, size_t n)
{
    uint32_t cmp = R32(obj + UOBJ_NAME);
    uint32_t num = R32(obj + UOBJ_NAME + 4);
    if (!fname_to_str(cmp, num, out, n))
        snprintf(out, n, "#%u", cmp);
}

/* ---------- function table --------------------------------------------- */
typedef struct { const char* name; uint32_t rva; } FnEnt;
static const FnEnt kFns[] = {
    { "AActor::K2_GetActorLocation",                     0x43A1830 },
    { "AActor::K2_GetActorRotation",                     0x43A1910 },
    { "AActor::GetActorBounds",                          0x439EFA0 },
    { "AActor::GetTransform",                            0x43A0310 },
    { "AActor::GetVelocity",                             0x43A03C0 },
    { "AActor::GetActorForwardVector",                   0x439F240 },
    { "AActor::K2_GetRootComponent",                     0x23909A0 },
    { "USceneComponent::K2_GetComponentLocation",        0x44E4EA0 },
    { "USceneComponent::GetSocketLocation",              0x44E3700 },
    { "APlayerController::ProjectWorldLocationToScreen", 0x44CA420 },
    { "APlayerCameraManager::GetCameraLocation",         0x44C0CA0 },
    { "UGameplayStatics::GetPlayerCharacter",            0x440A0E0 },
    { NULL, 0 }
};

static void dump_actor_chain(uintptr_t actor, const char* cls)
{
    uintptr_t root = R64(actor + AA_ROOTCOMPONENT);
    float x, y, z, hp = 0, hpmax = 0;
    uint32_t team, skin, role;
    uintptr_t ability;

    if (!ptr_ok(root)) return;
    x = RF32(root + USC_RELLOC + 0);
    y = RF32(root + USC_RELLOC + 4);
    z = RF32(root + USC_RELLOC + 8);
    if (is_nan_f(x) || is_nan_f(y) || is_nan_f(z)) return;

    ability = R64(actor + CY_ABILITYSYS);
    if (ptr_ok(ability)) {
        hp    = RF32(ability + ATTR_HEALTH + 0x0C);
        hpmax = RF32(ability + ATTR_HEALTHMAX + 0x0C);
    }
    team = R32(actor + CY_TEAMID) & 0xFF;
    skin = R32(actor + CY_SKINID);
    role = R32(actor + PM_ROLEID);

    L("    %-20s addr=0x%llX root=0x%llX  LOC=(%.2f, %.2f, %.2f)  team=%u skin=%u role=%u  hp=%.1f/%.1f",
      cls, (unsigned long long)actor, (unsigned long long)root,
      x, y, z, team, skin, role, hp, hpmax);
}

static DWORD WINAPI ProbeThread(LPVOID mod)
{
    (void)mod;
    int i;
    char path[MAX_PATH];
    HMODULE self = NULL;
    int found_classes = 0;
    uintptr_t class_ptrs[8]; char class_names[8][64]; int nclass = 0;
    const char* want[] = { "CyCharacterBase", "PMCharacter", "Character", "PMPlayerState", NULL };

    Sleep(20000);

    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ProbeThread, &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    { char* s = strrchr(path, '\\'); if (s) *(s + 1) = 0; }
    strncat(path, "fut_verify_report.txt", MAX_PATH - strlen(path) - 1);

    g_log = fopen(path, "w");
    if (!g_log) return 1;

    g_base = (uintptr_t)GetModuleHandleA(NULL);
    L("=== FUT verify v2  (pid %lu, base 0x%llX) ===", GetCurrentProcessId(), (unsigned long long)g_base);
    L("");

    /* ---- 1. function bodies ---- */
    L("--- 1. function bodies at dumped RVAs (read from INSIDE the process) ---");
    for (i = 0; kFns[i].name; i++) {
        uint8_t b[32]; char hex[128]; size_t o = 0; int k;
        if (!safe_read(g_base + kFns[i].rva, b, 32)) { L("  %-46s READ FAILED", kFns[i].name); continue; }
        for (k = 0; k < 16 && o + 4 < sizeof(hex); k++)
            o += (size_t)snprintf(hex + o, sizeof(hex) - o, "%02X ", b[k]);
        L("  %-46s ent=%.2f  %s", kFns[i].name, entropy(b, 32), hex);
    }
    L("");

    /* ---- 2. AppendString usable? ---- */
    L("--- 2. FName resolution via AppendString (RVA 0x%X) ---", OFF_APPENDSTRING);
    {
        char nm[200];
        g_append_ok = 1;
        if (fname_to_str(0, 0, nm, sizeof(nm)))
            L("  index 0 -> \"%s\"   (expect \"None\")", nm);
        else
            L("  AppendString call FAILED / returned nothing");
    }
    L("");

    /* ---- 3. GObjects ---- */
    L("--- 3. GObjects ---");
    {
        uintptr_t slot = g_base + OFF_GOBJECTS;
        g_objects = R64(slot);
        g_num = RI32(slot + 0x14);
        L("  slot=0x%llX Objects=0x%llX NumElements=%d NumChunks=%d",
          (unsigned long long)slot, (unsigned long long)g_objects, g_num, RI32(slot + 0x1C));
        if (ptr_ok(g_objects) && g_num > 0) {
            L("  => GObjects PLAINTEXT and walkable");
            L("  scanning for character classes...");
            for (i = 0; i < g_num && nclass < 8; i++) {
                uintptr_t o = gobject_at(i);
                uintptr_t cl;
                char nm[200];
                int w;
                if (!ptr_ok(o)) continue;
                cl = R64(o + UOBJ_CLASS);
                if (!ptr_ok(cl)) continue;
                obj_name(o, nm, sizeof(nm));
                for (w = 0; want[w]; w++) {
                    if (strstr(nm, want[w])) {
                        int dup = 0, j2;
                        for (j2 = 0; j2 < nclass; j2++) if (class_ptrs[j2] == cl) dup = 1;
                        if (!dup) {
                            class_ptrs[nclass] = cl;
                            snprintf(class_names[nclass], 64, "%s", nm);
                            L("    class[%d] = 0x%llX  \"%s\"  (obj idx %d)",
                              nclass, (unsigned long long)cl, nm, i);
                            nclass++;
                        }
                        break;
                    }
                }
            }
            found_classes = nclass;
        } else {
            L("  => GObjects NOT readable");
        }
    }
    L("");

    /* ---- 4. GWorld retry loop ---- */
    L("--- 4. GWorld (retried every 5s for up to 4 min) ---");
    {
        int attempt;
        uintptr_t world = 0;
        for (attempt = 0; attempt < 48; attempt++) {
            world = R64(g_base + OFF_GWORLD);
            if (ptr_ok(world)) {
                uintptr_t level, actors_data; int32_t actors_num;
                L("  [t=%3ds] UWorld* = 0x%llX  <-- NON-NULL", attempt * 5, (unsigned long long)world);
                level = R64(world + UW_PERSISTENTLEVEL);
                actors_data = R64(level + UL_ACTORS);
                actors_num = RI32(level + UL_ACTORS + 8);
                L("           PersistentLevel=0x%llX  Actors.Data=0x%llX Num=%d",
                  (unsigned long long)level, (unsigned long long)actors_data, actors_num);
                if (ptr_ok(actors_data) && actors_num > 0 && actors_num < 2000000) {
                    int n = 0;
                    for (i = 0; i < actors_num && n < 15; i++) {
                        uintptr_t a = R64(actors_data + (uintptr_t)i * 8);
                        uintptr_t root;
                        float x, y, z;
                        if (!ptr_ok(a)) continue;
                        root = R64(a + AA_ROOTCOMPONENT);
                        if (!ptr_ok(root)) continue;
                        x = RF32(root + USC_RELLOC + 0);
                        y = RF32(root + USC_RELLOC + 4);
                        z = RF32(root + USC_RELLOC + 8);
                        if (is_nan_f(x) || is_nan_f(y) || is_nan_f(z)) continue;
                        if (x == 0.0f && y == 0.0f && z == 0.0f) continue;
                        L("           actor[%5d] 0x%llX  LOC=(%.2f, %.2f, %.2f)",
                          i, (unsigned long long)a, x, y, z);
                        n++;
                    }
                    L("  => COORDINATE CHAIN PLAINTEXT (GWorld -> Level -> Actors -> RootComponent -> +0x198)");
                }
                break;
            }
            if (attempt % 6 == 0)
                L("  [t=%3ds] UWorld* = 0x0 (still booting / not in a map)", attempt * 5);
            Sleep(5000);
        }
        if (!ptr_ok(world))
            L("  => GWorld still 0 after 4 min (use UEngine fallback)");
    }
    L("");

    /* ---- 5+6. live characters + raw actors, retried until a map is loaded ---- */
    L("--- 5. live ACyCharacterBase instances + raw Actors (retry up to 9 min) ---");
    {
        uintptr_t cy_cls = 0;
        int attempt;
        int reported_chain = 0;

        /* locate the UClass object for ACyCharacterBase (once) */
        for (i = 0; i < g_num && !cy_cls; i++) {
            uintptr_t o = gobject_at(i);
            char nm[200];
            if (!ptr_ok(o)) continue;
            if (!ptr_ok(R64(o + UOBJ_CLASS))) continue;
            obj_name(o, nm, sizeof(nm));
            if (strcmp(nm, "ACyCharacterBase") == 0) cy_cls = o;
        }
        if (!cy_cls) {
            for (i = 0; i < g_num && !cy_cls; i++) {
                uintptr_t o = gobject_at(i);
                char nm[200];
                if (!ptr_ok(o)) continue;
                obj_name(o, nm, sizeof(nm));
                if (strstr(nm, "CyCharacterBase") && !strstr(nm, "Default__")) cy_cls = o;
            }
        }
        L("  ACyCharacterBase UClass = 0x%llX", (unsigned long long)cy_cls);

        for (attempt = 0; attempt < 108; attempt++) {
            uintptr_t world = R64(g_base + OFF_GWORLD);
            int live = 0;

            /* (a) raw actor chain from UWorld */
            if (ptr_ok(world) && !reported_chain) {
                uintptr_t level = R64(world + UW_PERSISTENTLEVEL);
                uintptr_t adata = R64(level + UL_ACTORS);
                int32_t anum = RI32(level + UL_ACTORS + 8);
                L("  [t=%3ds] UWorld=0x%llX PersistentLevel=0x%llX Actors.Data=0x%llX Num=%d",
                  attempt * 5, (unsigned long long)world, (unsigned long long)level,
                  (unsigned long long)adata, anum);
                if (ptr_ok(adata) && anum > 0 && anum < 200000) {
                    int k, n = 0;
                    L("  --- raw Actors (first 25) ---");
                    for (k = 0; k < anum && n < 25; k++) {
                        uintptr_t a = R64(adata + (uintptr_t)k * 8);
                        uintptr_t root; float x, y, z; char nm[200];
                        if (!ptr_ok(a)) continue;
                        obj_name(a, nm, sizeof(nm));
                        root = R64(a + AA_ROOTCOMPONENT);
                        x = RF32(root + USC_RELLOC + 0);
                        y = RF32(root + USC_RELLOC + 4);
                        z = RF32(root + USC_RELLOC + 8);
                        L("    [%3d] actor=0x%llX '%.48s' raw=(%.2f, %.2f, %.2f)",
                          k, (unsigned long long)a, nm, x, y, z);
                        n++;
                    }
                    reported_chain = 1;
                    L("  => COORDINATE CHAIN IS PLAINTEXT: UWorld -> PersistentLevel -> Actors[] ->");
                    L("     AActor::RootComponent(+0x168) -> USceneComponent::RelativeLocation(+0x198)");
                }
            }

            /* (b) live character instances with real coordinates */
            if (cy_cls) {
                for (i = 0; i < g_num; i++) {
                    uintptr_t o = gobject_at(i);
                    uintptr_t root;
                    float x, y, z;
                    if (!ptr_ok(o)) continue;
                    if (R64(o + UOBJ_CLASS) != cy_cls) continue;
                    if (o == cy_cls) continue;                 /* skip the CDO itself */
                    root = R64(o + AA_ROOTCOMPONENT);
                    if (!ptr_ok(root)) continue;
                    x = RF32(root + USC_RELLOC + 0);
                    y = RF32(root + USC_RELLOC + 4);
                    z = RF32(root + USC_RELLOC + 8);
                    if (is_nan_f(x) || is_nan_f(y) || is_nan_f(z)) continue;
                    if (x == 0.0f && y == 0.0f && z == 0.0f) continue;
                    dump_actor_chain(o, "ACyCharacterBase");
                    live++;
                    if (live >= 30) break;
                }
            }
            if (live) {
                L("  => %d LIVE character(s) with real coordinates at t=%ds", live, attempt * 5);
                L("  => COORDINATES ARE PLAINTEXT (no encryption, no obfuscation)");
                break;
            }
            if (attempt % 6 == 0)
                L("  [t=%3ds] no live characters yet (world=%s)",
                  attempt * 5, ptr_ok(world) ? "loaded" : "null");
            Sleep(5000);
        }
    }
    L("");
    (void)found_classes; (void)class_ptrs; (void)class_names; (void)nclass;

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
