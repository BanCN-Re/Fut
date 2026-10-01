// D:\Work\Fut\cheat\loader\loader.cpp
// ===========================================================================
// zlibwapi.dll -- THIN CARRIER (loader)  [architecture: split]
// ===========================================================================
//
// Design (user-specified):
//   * zlibwapi.dll is ONLY a carrier. It forwards the 129 zlib exports to the
//     genuine zlibwapi_orig.dll and does nothing else visible.
//   * The real work (ESP, ImGui, Present hook, network filtering) lives in a
//     separate module loaded at runtime: zw_core.dll.
//   * The carrier applies light obfuscation so it does not stand out.
//
// WHY THE SPLIT
//   The monolithic build was 4,105,216 B against a genuine 133,936 B library --
//   31x too large, which is a glaring size anomaly for "a zlib compression
//   library". The carrier alone is ~20-30 KB, i.e. SMALLER than the original,
//   so the size signature disappears entirely. The bulk (ImGui, the font, the
//   ESP) moves to zw_core.dll where it no longer sits under the zlib name.
//
// HONEST LIMITS (do not oversell this)
//   Splitting hides nothing from an enumerator: zw_core.dll appears in the
//   module list and the VAD just like any other module. What it buys us is:
//     - no size anomaly on the zlibwapi name
//     - the payload can be named and placed independently
//     - the carrier stays small enough to be unremarkable
//   It does NOT make us invisible. ACE-CSI64.dll can enumerate modules
//   (Module32First/Next), walk the VAD (NtQueryVirtualMemory) and read our
//   memory (ReadProcessMemory) -- all verified from its import table.
//
// LOAD ORDER
//   DllMain must not do real work (loader lock). We start one thread that:
//     1. locates zw_core.dll next to us (or from an encrypted resource)
//     2. LoadLibrary's it
//     3. calls its single export ZwCoreStart()
//   Export forwarding is handled entirely by the .def file, so the 129 zlib
//   entries never touch our code.
//
// OBFUSCATION (carrier-level, deliberately light)
//   - the core module name is XOR-encrypted at compile time
//   - we do not export anything except the forwarded zlib entries, so the export
//     table looks exactly like a real zlib proxy
//   - a VERSIONINFO block matching the genuine library
//   Light on purpose: a normal zlib carrier should not look hardened. Heavy
//   obfuscation (VM, encrypted .text, anti-debug) is itself a red flag.
#include <windows.h>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Compile-time string obfuscation (same scheme as the core, kept minimal here).
// ---------------------------------------------------------------------------
namespace obf {
inline constexpr unsigned char KS(unsigned char key, size_t i) {
    return (unsigned char)(key + (unsigned char)(i * 31));
}
template <size_t N> struct Blob { unsigned char d[N]; };
void DecryptStr(const unsigned char* src, size_t n, unsigned char key, char* out);
const char* DecryptTls(const unsigned char* src, size_t n, unsigned char key);
} // namespace obf

#define SBOX_BYTES(str, key)                                                   \
    []() constexpr {                                                           \
        obf::Blob<sizeof(str)> b{};                                            \
        for (size_t i = 0; i < sizeof(str); ++i)                               \
            b.d[i] = (unsigned char)((unsigned char)(str[i]) ^                 \
                                     obf::KS((unsigned char)(key), i));        \
        return b;                                                              \
    }()

// NOTE: bind to a named constexpr before decrypting. Passing an SBOX_BYTES(...)
// temporary directly into the decryptor lets MSVC /O2 emit the PLAINTEXT as a
// string constant (verified in the core module). Keep these names.
static constexpr auto kCoreName = SBOX_BYTES("zw_core.dll", 0x5E);
static constexpr auto kEntryName = SBOX_BYTES("ZwCoreStart", 0x3A);

namespace obf {
void DecryptStr(const unsigned char* src, size_t n, unsigned char key, char* out) {
    if (!src || !out || n == 0) return;
    for (size_t i = 0; i < n; ++i)
        out[i] = (char)(src[i] ^ KS(key, i));
    out[n - 1] = 0;
}
const char* DecryptTls(const unsigned char* src, size_t n, unsigned char key) {
    static thread_local char buf[512];
    if (n > sizeof(buf)) { buf[0] = 0; return buf; }
    DecryptStr(src, n, key, buf);
    return buf;
}
} // namespace obf

// ---------------------------------------------------------------------------
// Core module signature: a single exported entry point.
// ---------------------------------------------------------------------------
typedef int (*ZwCoreStartFn)();

// ---------------------------------------------------------------------------
// Resolve the directory this DLL was loaded from, so we can find the core module
// beside us. Uses the module handle passed to DllMain -- never the PEB, because
// the core may unlink us later and a PEB walk would then fail.
// ---------------------------------------------------------------------------
static void GetOwnDir(HMODULE self, char* out, size_t cap) {
    out[0] = 0;
    if (!self || cap < 4) return;
    if (!GetModuleFileNameA(self, out, (DWORD)cap)) { out[0] = 0; return; }
    char* slash = strrchr(out, '\\');
    if (slash) *(slash + 1) = 0; else out[0] = 0;
}

// ---------------------------------------------------------------------------
// Load the core and hand over control.
// ---------------------------------------------------------------------------
static DWORD WINAPI LoaderThread(LPVOID param) {
    HMODULE self = (HMODULE)param;

    char dir[MAX_PATH] = {0};
    GetOwnDir(self, dir, sizeof(dir));
    if (!dir[0]) return 1;

    const char* coreName = obf::DecryptTls(kCoreName.d, sizeof(kCoreName.d), 0x5E);

    char corePath[MAX_PATH] = {0};
    // Build "<dir><coreName>" without sprintf (keeps the import list minimal).
    size_t dl = strlen(dir);
    size_t cl = strlen(coreName);
    if (dl + cl + 1 >= sizeof(corePath)) return 2;
    memcpy(corePath, dir, dl);
    memcpy(corePath + dl, coreName, cl + 1);

    // Give the loader a moment to finish its own work (we are on our own thread
    // so the loader lock is not held, but the game's loader is still busy at this
    // point and LoadLibrary would serialize behind it anyway).
    Sleep(200);

    { FILE* f = fopen("D:\\Work\\Fut\\evidence\\loader_trace.txt", "a"); if (f) { fprintf(f, "[loader] LoadLibrary %s\n", corePath); fclose(f); } }
    HMODULE core = LoadLibraryA(corePath);
    if (!core) {
        const DWORD le = GetLastError();
        { FILE* f = fopen("D:\\Work\\Fut\\evidence\\loader_trace.txt", "a"); if (f) { fprintf(f, "[loader] LoadLibrary failed err=%lu\n", (unsigned long)le); fclose(f); } }
        // err=183 (ERROR_ALREADY_EXISTS) happens when the DLL is registered but
        // still mid-initialisation on another thread. The module is in the map
        // at that point, so a plain GetModuleHandle succeeds.
        if (le == ERROR_ALREADY_EXISTS)
            core = GetModuleHandleA(corePath);
        if (!core)
            core = LoadLibraryA(coreName);
        if (!core)
            core = GetModuleHandleA(coreName);
        if (!core) return 3;
    }

    ZwCoreStartFn start = (ZwCoreStartFn)GetProcAddress(
        core, obf::DecryptTls(kEntryName.d, sizeof(kEntryName.d), 0x3A));
    { FILE* f = fopen("D:\\\\Work\\\\Fut\\\\evidence\\\\loader_trace.txt", "a"); if (f) { fprintf(f, "[%lu] GetProcAddress %s\n", GetCurrentProcessId(), start ? "OK" : "FAILED"); fclose(f); } }
    if (!start) return 4;

    int rc = start();
    { FILE* f = fopen("D:\\\\Work\\\\Fut\\\\evidence\\\\loader_trace.txt", "a"); if (f) { fprintf(f, "[%lu] ZwCoreStart rc=%d\n", GetCurrentProcessId(), rc); fclose(f); } }
    return rc;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        // No real work here: we hold the loader lock. Just spawn the thread.
        DisableThreadLibraryCalls(hModule);
        HANDLE t = CreateThread(nullptr, 0, LoaderThread, hModule, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
