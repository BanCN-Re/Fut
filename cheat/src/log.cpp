// D:\Work\Fut\cheat\src\log.cpp
#include "log.h"
#include "obf.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <share.h>   // _SH_DENYNO for _fsopen

namespace logutil {

static char  s_path[MAX_PATH] = {0};
static CRITICAL_SECTION s_cs;
static bool  s_ready = false;

// ---------------------------------------------------------------------------
// Obfuscated literals (P1).
// Bound to namespace-scope constexpr blobs so MSVC cannot turn them back into
// plaintext string constants. See obf.h for why the binding matters.
// ---------------------------------------------------------------------------
namespace lit {
static constexpr auto kLogPart1   = SBOX_BYTES("fut", 0x3D);
static constexpr auto kLogPart2   = SBOX_BYTES("_cheat.trace.log", 0x71);
static constexpr auto kTraceStart = SBOX_BYTES("=== proxy trace start ===", 0x22);
} // namespace lit

// Decrypt a named blob with its matching key.
#define LIT_LOG1  obf::DecryptTls(lit::kLogPart1.d,   sizeof(lit::kLogPart1.d),   0x3D)
#define LIT_LOG2  obf::DecryptTls(lit::kLogPart2.d,   sizeof(lit::kLogPart2.d),   0x71)
#define LIT_TRACE obf::DecryptTls(lit::kTraceStart.d, sizeof(lit::kTraceStart.d), 0x22)

void Init(void* moduleHandle) {
    if (s_ready) return;
    InitializeCriticalSection(&s_cs);

    // Resolve our own path from the module handle. This must happen BEFORE any
    // stealth work, otherwise resolving the path would walk a PEB chain we may
    // have already unlinked.
    char dll[MAX_PATH] = {0};
    HMODULE self = reinterpret_cast<HMODULE>(moduleHandle);
    if (!self) {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&Init), &self);
    }
    if (!GetModuleFileNameA(self, dll, MAX_PATH)) {
        dll[0] = 0;
    }
    char* slash = strrchr(dll, '\\');
    if (slash) *(slash + 1) = 0; else dll[0] = 0;

    // P1: the log filename is built from obfuscated fragments so the loaded
    // image does not contain "fut_cheat.trace.log" (the word "cheat" next to a
    // zlib proxy name is a strong signal). The on-disk name is unchanged so
    // existing tooling keeps working.
    //
    // The fragments are named constexpr blobs, not inline SBOX_BYTES temporaries:
    // MSVC /O2 materialises a temporary as a plaintext string constant in .rdata
    // (verified), which would defeat the whole point. See obf.h.
    {
        char part1[16], part2[24];
        strcpy_s(part1, LIT_LOG1);
        strcpy_s(part2, LIT_LOG2);
        _snprintf_s(s_path, sizeof(s_path), _TRUNCATE, "%s%s%s", dll, part1, part2);
    }
    s_ready = true;

    TraceLine(LIT_TRACE);
}

void TraceLine(const char* line) {
    if (!s_ready || !s_path[0]) return;
    EnterCriticalSection(&s_cs);
    // ------------------------------------------------------------------
    // PERSISTENT HANDLE -- this used to fopen/fclose PER LINE: a kernel
    // handle create+destroy for every trace line, many times a second on
    // the RENDER THREAD once the ESP was on. A measurable slice of the
    // "enabling the ESP lags the game" report. Keep the file open and
    // fflush per line: near-crash-safe, zero handle churn.
    // ------------------------------------------------------------------
    static FILE* s_out = nullptr;
    if (!s_out) {
        // ------------------------------------------------------------------
        // _fsopen with _SH_DENYNO, NOT fopen_s.
        //
        // fopen_s opens with a DENY-WRITE share mode, which makes the trace file
        // UNREADABLE while the game runs: `type`, `Get-Content`, Notepad and
        // anything else wanting a peek all fail with "being used by another
        // process".
        //
        // That matters because this log is the only live diagnostic channel. When
        // a bug is reported mid-session, reading it WHILE THE GAME IS UP is
        // exactly what is needed -- and the exclusive lock made it impossible.
        //
        // _fsopen(..., _SH_DENYNO) keeps one persistent append handle (so there is
        // still no per-line handle churn) while letting other processes read.
        // ------------------------------------------------------------------
        s_out = _fsopen(s_path, "a", _SH_DENYNO);
        if (!s_out) {
            LeaveCriticalSection(&s_cs);
            return;
        }
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(s_out, "%02d:%02d:%02d.%03d  %s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            line ? line : "");
    fflush(s_out);
    LeaveCriticalSection(&s_cs);
}

} // namespace logutil
