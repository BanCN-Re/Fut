// live_capture.c -- READ-ONLY external probe for Calabiyau-Win64-Shipping.
// No writes, no injection: OpenProcess(PROCESS_VM_READ) only.
// Captures: player names, dual-path HP ground truth, capsule params,
// and the RefSkeleton array location inside USkeletalMesh (for the rig fix).
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdint.h>

#pragma pack(push,1)
typedef struct { uint64_t ptr; int32_t num; int32_t max; } TArr;
#pragma pack(pop)

static HANDLE g_p = NULL;
static int rd(uint64_t a, void* b, SIZE_T n) {
    SIZE_T got = 0;
    if (!ReadProcessMemory(g_p, (LPCVOID)a, b, n, &got)) return 0;
    return (int)got == (int)n;
}
static uint64_t r64(uint64_t a){ uint64_t v=0; rd(a,&v,8); return v; }
static uint32_t r32(uint64_t a){ uint32_t v=0; rd(a,&v,4); return v; }
static uint8_t  r8 (uint64_t a){ uint8_t  v=0; rd(a,&v,1); return v; }
static float    rf (uint64_t a){ float   v=0; rd(a,&v,4); return v; }

#define GW_RVA 0x07420710ull

static int plausiblef(float f){ return f > -1e7f && f < 1e7f && f == f; }

// Read an FString (ptr, num) as UTF-8-ish ASCII; UE4 FString num INCLUDES the nul.
static int readFStr(uint64_t addr, char* out, int cap) {
    out[0] = 0;
    uint64_t p = r64(addr);
    int32_t  n = (int32_t)r32(addr + 8);
    if (p < 0x10000 || n <= 0 || n > 256) return 0;
    wchar_t w[257];
    if (!rd(p, w, (SIZE_T)(n > 256 ? 256 : n) * 2)) return 0;
    int lim = n > 256 ? 256 : n; int o = 0;
    for (int i = 0; i < lim && w[i]; ++i)
        out[o++] = (w[i] < 128) ? (char)w[i] : '?';
    out[o] = 0;
    return o > 0;
}

int main(int argc, char** argv) {
    FILE* o = fopen("D:\\Work\\Fut\\evidence\\live_capture.txt", "w");
    if (!o) { printf("no out\n"); return 1; }
    fprintf(o, "=== live capture @ boot ===\n");

    DWORD pid = 0;
    if (argc > 1) {
        pid = (DWORD)strtoul(argv[1], NULL, 0);
    } else {
      DWORD pids[2048]; DWORD sz = 0;
      if (!EnumProcesses(pids, sizeof(pids), &sz)) { fprintf(o, "enum fail\n"); fclose(o); return 2; }
      for (DWORD i = 0; i < sz / sizeof(DWORD); ++i) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!h) continue;
        char nm[MAX_PATH];
        if (GetModuleBaseNameA(h, NULL, nm, MAX_PATH) &&
            !_stricmp(nm, "Calabiyau-Win64-Shipping")) { pid = pids[i]; CloseHandle(h); break; }
        CloseHandle(h);
      }
    }
    if (!pid) { fprintf(o, "game not found\n"); fclose(o); return 3; }
    fprintf(o, "pid=%lu\n", pid);

    g_p = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!g_p) { fprintf(o, "OpenProcess failed gle=%lu\n", GetLastError()); fclose(o); return 4; }

    // ASLR: ACE strips module enumeration (gle=5), so walk the PEB instead:
    // NtQueryInformationProcess(ProcessBasicInformation) -> PEB -> Ldr ->
    // InLoadOrderModuleList, matching the shipping exe by name, all via RPM.
    uint64_t BASE = 0;
    {
        typedef LONG (WINAPI *tNtQIP)(HANDLE, int, void*, ULONG, ULONG*);
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        tNtQIP qip = (tNtQIP)(nt ? GetProcAddress(nt, "NtQueryInformationProcess") : NULL);
        if (qip) {
            struct { PVOID Reserved1; PVOID PebBase; PVOID R2[2]; ULONG UniquePid; PVOID R3; } pbi;
            memset(&pbi, 0, sizeof(pbi));
            if (qip(g_p, 0, &pbi, sizeof(pbi), NULL) == 0 && pbi.PebBase) {
                uint64_t peb = (uint64_t)pbi.PebBase;
                uint64_t ldr = r64(peb + 0x18);            // PEB->Ldr
                unsigned char pebhead[8] = {0};
                int rr = rd(peb, pebhead, 8);
                fprintf(o, "dbg peb=0x%llX r-peb-ok=%d head=%02X%02X ldr=0x%llX gle=%lu\n",
                        (unsigned long long)peb, rr, pebhead[0], pebhead[1],
                        (unsigned long long)ldr, GetLastError());
                uint64_t head = ldr + 0x10;                // InLoadOrderModuleList
                uint64_t cur = r64(head);
                int shown = 0;
                for (int i = 0; i < 400 && cur > 0x10000 && cur != head; ++i) {
                    uint64_t dllBase = r64(cur + 0x30);     // LDR_DATA_TABLE_ENTRY.DllBase
                    uint64_t basName = r64(cur + 0x50);     // FullDllName buffer
                    wchar_t wname[280]; wname[0] = 0;
                    if (basName > 0x10000) rd(basName, wname, 278 * 2);
                    char nm[MAX_PATH]; int j = 0;
                    for (; wname[j] && j < MAX_PATH - 1; ++j) nm[j] = (wname[j] < 128) ? (char)wname[j] : '?';
                    nm[j] = 0;
                    if (shown < 6) { fprintf(o, "dbg mod[%d] base=0x%llX '%s'\n", i, (unsigned long long)dllBase, nm); ++shown; }
                    char* base = nm; for (char* c = nm; *c; ++c) if (*c == '\\' || *c == '/') base = c + 1;
                    if (!_stricmp(base, "Calabiyau-Win64-Shipping.exe")) { BASE = dllBase; break; }
                    cur = r64(cur);
                }
            } else {
                fprintf(o, "PBI query failed\n");
            }
        }
    }
    fprintf(o, "shipping base=0x%llX\n", (unsigned long long)BASE);
    if (!BASE) { fclose(o); return 5; }

    uint64_t world = r64(BASE + GW_RVA);
    fprintf(o, "GWorld=0x%llX\n", (unsigned long long)world);
    if (world < 0x10000) { fprintf(o, "bad world\n"); fclose(o); return 5; }

    uint64_t level = r64(world + 0x30);
    fprintf(o, "PersistentLevel=0x%llX\n", (unsigned long long)level);
    if (level < 0x10000) { fclose(o); return 6; }

    uint64_t arrData = r64(level + 0x98);
    int32_t  arrNum  = (int32_t)r32(level + 0x98 + 8);
    if (arrNum < 0 || arrNum > 20000) arrNum = 0;
    fprintf(o, "Actors: num=%d data=0x%llX\n", arrNum, (unsigned long long)arrData);

    int pawns = 0;
    for (int i = 0; i < arrNum && pawns < 24; ++i) {
        uint64_t a = r64(arrData + (uint64_t)i * 8);
        if (a < 0x10000) continue;

        // pawn heuristic: root component + capsule pointer valid
        uint64_t root = r64(a + 0x168);
        uint64_t cap  = r64(a + 0x380);
        if (root < 0x10000 || cap < 0x10000) continue;
        float hh = rf(cap + 0x4C0), cr = rf(cap + 0x4C4);
        if (!(hh > 40.0f && hh < 130.0f) || !(cr > 20.0f && cr < 60.0f)) continue;

        ++pawns;
        uint64_t ps = r64(a + 0x330);
        char nm[257] = {0};
        int hasName = 0;
        if (ps > 0x10000) hasName = readFStr(ps + 0x400, nm, 256);

        float locx = rf(root + 0x178), locy = rf(root + 0x17C), locz = rf(root + 0x188);

        fprintf(o, "\n-- pawn[%d] 0x%llX %s  loc=(%.0f,%.0f,%.0f) cap(hh=%.1f r=%.1f)\n",
                pawns, (unsigned long long)a, hasName ? nm : "(no PS name)",
                locx, locy, locz, hh, cr);
        fprintf(o, "   team=%u PS=0x%llX\n", r8(a + 0x8B0), (unsigned long long)ps);

        // direct-pawn HP candidates (walker_v3 primary/secondary)
        fprintf(o, "   pawn HP candidates: +0x46C=%.1f +0x470=%.1f +0x254=%.1f +0x258=%.1f\n",
                rf(a + 0x46C), rf(a + 0x470), rf(a + 0x254), rf(a + 0x258));
        // dump the whole 0x450..0x4A0 float belt for pattern confirmation
        fprintf(o, "   belt 0x458..0x498:");
        for (uint32_t fo = 0x458; fo < 0x498; fo += 4) {
            float v = rf(a + fo);
            fprintf(o, " %03X=%s%.1f", fo, plausiblef(v) ? "" : "~", v);
        }
        fprintf(o, "\n");

        // ASC path
        uint64_t asc = r64(a + 0x880);
        if (asc > 0x10000) {
            // AttributeSet array: walk the ASC's spawn attributes is complex;
            // the module reads ability+0x78 as if AttributeSet base. Dump floats.
            fprintf(o, "   ASC=0x%llX  +0x78+8=%.1f +0x78+C=%.1f +0x88+8=%.1f +0x88+C=%.1f +0x98+C=%.1f +0xA8+C=%.1f\n",
                    (unsigned long long)asc,
                    rf(asc + 0x78 + 8), rf(asc + 0x78 + 0xC),
                    rf(asc + 0x88 + 8), rf(asc + 0x88 + 0xC),
                    rf(asc + 0x98 + 0xC), rf(asc + 0xA8 + 0xC));
        } else {
            fprintf(o, "   ASC=null(0x%llX)\n", (unsigned long long)asc);
        }

        // mesh -> SkeletalMesh -> RefSkeleton discovery (first pawn only)
        if (pawns == 1) {
            uint64_t mesh = r64(a + 0xEC0);
            if (mesh < 0x10000) mesh = r64(a + 0x370);
            fprintf(o, "   mesh=0x%llX boneMax=%d\n",
                    (unsigned long long)mesh,
                    (int32_t)r32(mesh + 0x800 + 0xC));
            uint64_t skm = r64(mesh + 0x4D0);
            fprintf(o, "   SkeletalMesh=0x%llX\n", (unsigned long long)skm);
            if (skm > 0x10000) {
                // scan for TArray triplets whose count matches the bone count
                int boneN = (int32_t)r32(mesh + 0x800 + 0xC);
                for (uint32_t fo = 0x200; fo < 0x600; fo += 8) {
                    uint64_t p = r64(skm + fo);
                    int32_t  n = (int32_t)r32(skm + fo + 8);
                    int32_t  m = (int32_t)r32(skm + fo + 12);
                    if (p > 0x10000 && n > 4 && n < 1024 && n == m && (boneN <= 0 || n == boneN)) {
                        fprintf(o, "   skm+0x%03X: TArray num=%d  <-- RefSkeleton candidate\n", fo, n);
                    }
                }
            }
        }
    }
    fprintf(o, "\n=== done: %d pawns ===\n", pawns);
    fclose(o);
    printf("done %d pawns\n", pawns);
    return 0;
}
