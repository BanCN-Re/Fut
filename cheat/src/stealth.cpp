// D:\Work\Fut\cheat\src\stealth.cpp
// P4: module concealment -- PEB unlink only (see stealth.h for the rationale
// and the explicit list of what this does NOT hide).
//
// P1 NOTE on string obfuscation:
// Every literal here is bound to a namespace-scope `static constexpr` blob
// before being decrypted. That pattern is what actually keeps plaintext out of
// the image -- feeding an SBOX_BYTES(...) temporary straight into decryption lets
// MSVC /O2 emit the plaintext as a real string constant (verified by finding the
// symbol `??_C@_0BB@...@ZLW_SELF_STEALTH@` in the object file). Do not inline
// these back into the call sites.
#include "stealth.h"
#include "log.h"
#include "obf.h"

#include <windows.h>
#include <cstring>

namespace stealth {

static bool  s_applied = false;
static char  s_result[192] = "n/a";

bool WasApplied() { return s_applied; }
const char* LastResult() { return s_result; }

// ---------------------------------------------------------------------------
// Obfuscated literals (bound to named constexpr blobs -- see the note above).
// ---------------------------------------------------------------------------
namespace lit {

static constexpr auto kEnvName    = SBOX_BYTES("ZLW_SELF_STEALTH", 0x47);
static constexpr auto kDisabled   = SBOX_BYTES("disabled (switch off)", 0x15);
static constexpr auto kLogOff     = SBOX_BYTES("[st] not applied (switch off)", 0x1B);
static constexpr auto kNoHandle   = SBOX_BYTES("failed: no self handle", 0x29);
static constexpr auto kLogNoHnd   = SBOX_BYTES("[st] FAILED: no self handle", 0x2C);
static constexpr auto kNoPeb      = SBOX_BYTES("failed: no PEB", 0x3A);
static constexpr auto kLogNoPeb   = SBOX_BYTES("[st] FAILED: no PEB", 0x3F);
static constexpr auto kNotFound   = SBOX_BYTES("not found in loader list", 0x44);
static constexpr auto kLogNotFnd  = SBOX_BYTES("[st] own entry not in Ldr", 0x52);
static constexpr auto kApplied    = SBOX_BYTES("applied (loader lists unlinked)", 0x6D);
static constexpr auto kLogApplied = SBOX_BYTES("[st] applied: PEB lists unlinked", 0x63);

} // namespace lit

// ---------------------------------------------------------------------------
// Minimal PEB structures (avoid ntdll headers; we only need the loader lists).
// ---------------------------------------------------------------------------
struct LdrUnicodeString { USHORT Length; USHORT MaximumLength; wchar_t* Buffer; };

struct LdrEntry {
    LIST_ENTRY        InLoadOrderLinks;
    LIST_ENTRY        InMemoryOrderLinks;
    LIST_ENTRY        InInitializationOrderLinks;
    void*             DllBase;
    void*             EntryPoint;
    ULONG             SizeOfImage;
    LdrUnicodeString  FullDllName;
    LdrUnicodeString  BaseDllName;
};

struct PebLdrData {
    ULONG      Length;
    BOOLEAN    Initialized;
    HANDLE     SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
};

static PebLdrData* GetPebLdr() {
    BYTE* peb = reinterpret_cast<BYTE*>(__readgsqword(0x60));
    BYTE* ldr = *reinterpret_cast<BYTE**>(peb + 0x18);
    return reinterpret_cast<PebLdrData*>(ldr);
}

// Decrypt a named blob into a local temporary.
#define LIT(blob, key)  obf::DecryptTls((blob).d, sizeof((blob).d), (uint8_t)(key))

// Read the SelfStealth switch from an environment variable. Using the
// environment avoids adding a config file (which would itself be an on-disk
// artifact); the user sets it before launching the game.
static bool SelfStealthEnabled() {
    char buf[8] = {0};
    const char* name = LIT(lit::kEnvName, 0x47);
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return false;
    return buf[0] == '1';
}

void ApplyIfEnabled() {
    if (s_applied) return;

    if (!SelfStealthEnabled()) {
        strcpy_s(s_result, LIT(lit::kDisabled, 0x15));
        LOG(LIT(lit::kLogOff, 0x1B));
        return;
    }

    // Resolve our own loader entry by DllBase. Resolve the module handle BEFORE
    // touching the lists.
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&ApplyIfEnabled), &self);
    if (!self) {
        strcpy_s(s_result, LIT(lit::kNoHandle, 0x29));
        LOG(LIT(lit::kLogNoHnd, 0x2C));
        return;
    }

    PebLdrData* ldr = GetPebLdr();
    if (!ldr) {
        strcpy_s(s_result, LIT(lit::kNoPeb, 0x3A));
        LOG(LIT(lit::kLogNoPeb, 0x3F));
        return;
    }

    LIST_ENTRY* head = &ldr->InLoadOrderModuleList;
    LdrEntry* found = nullptr;
    for (LIST_ENTRY* it = head->Flink; it != head && it != nullptr; it = it->Flink) {
        auto* e = reinterpret_cast<LdrEntry*>(it);
        if (e->DllBase == self) { found = e; break; }
        // Defensive: a malformed list must not send us into a wild walk.
        if (e->InLoadOrderLinks.Flink == nullptr) break;
    }
    if (!found) {
        strcpy_s(s_result, LIT(lit::kNotFound, 0x44));
        LOG(LIT(lit::kLogNotFnd, 0x52));
        return;
    }

    // ---------------------------------------------------------------------
    // Unlink from InLoadOrder and InMemoryOrder ONLY.
    //
    // Deliberately NOT InInitializationOrder: the loader itself walks that list
    // while completing DLL initialization, and our boot thread runs concurrently
    // with that. Unlinking it risks corrupting the loader's own iteration --
    // this is the second half of the crash the Nemesis project hit.
    //
    // Deliberately NOT touching the PE headers: zeroing them or making the
    // header page NOACCESS caused a deterministic crash inside ACE's own
    // enumerator, because ACE read our header page from ITS code path and took
    // the fault itself. Harmless-looking, but it kills the game.
    // ---------------------------------------------------------------------
    found->InLoadOrderLinks.Blink->Flink = found->InLoadOrderLinks.Flink;
    found->InLoadOrderLinks.Flink->Blink = found->InLoadOrderLinks.Blink;
    found->InMemoryOrderLinks.Blink->Flink = found->InMemoryOrderLinks.Flink;
    found->InMemoryOrderLinks.Flink->Blink = found->InMemoryOrderLinks.Blink;

    s_applied = true;
    strcpy_s(s_result, LIT(lit::kApplied, 0x6D));
    LOG(LIT(lit::kLogApplied, 0x63));
}

} // namespace stealth
