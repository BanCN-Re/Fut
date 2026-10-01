// D:\Work\Fut\cheat\src\ace_blackhole.cpp
// DNS blackhole via per-module IAT patching (see header for design notes).
//
// Blocklist adapted to Calabiyau: ACE (Tencent AntiCheatExpert) + Tencent
// telemetry + CrashSight, plus the domains this title actually ships
// (ACE-ADVT.sys / TDataMaster.dll / CrashSight64.dll / PerfSight.dll).
#include "ace_blackhole.h"
#include "log.h"
#include "obf.h"
#include "fake_server.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <wininet.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <intrin.h>   // _ReturnAddress -- for caller attribution

// These live in winhttp.h / wininet.h but guard against absent SDK headers.
#ifndef ERROR_WINHTTP_CANNOT_CONNECT
#define ERROR_WINHTTP_CANNOT_CONNECT 12029L
#endif
#ifndef ERROR_INTERNET_CANNOT_CONNECT
#define ERROR_INTERNET_CANNOT_CONNECT 12029L
#endif

namespace ace {

// ---------------------------------------------------------------------------
// Blocklist -- REBUILT 2026-09-30 from OBSERVED traffic, not inherited.
//
// The previous 23 entries were borrowed from another title; only 4 of them are
// ever requested by this game. This list was rebuilt from evidence: every
// hostname ACE resolved through the system resolver was captured from the DNS
// layer, and all ACE/report hostnames found there are listed below.
// Source: evidence/ace_report_domains.txt (27 observed hostnames).
//
// Deliberately NOT blocked -- these are game business, blocking breaks play:
//   *.gxpan.cn  *.klbq.qq.com  *.ugcimg.cn  *.strinova.com  *.idreamsky.com
//
// P1 (2026-10-01): the hostnames are XOR-encrypted at compile time so the
// loaded image contains NO readable anti-cheat domain literals. A plain .rdata
// scan for "anticheatexpert.com" inside a module named zlibwapi.dll was the
// single most self-incriminating artifact we had.
//
// NOTE on lifetime: the encrypted bytes are the storage; the text is rebuilt on
// demand by BlocklistEntry() into a caller buffer. We deliberately do NOT store
// `const char*` (a decrypted temporary would dangle).
// ---------------------------------------------------------------------------
#define SBOX_STR(s)  SBOX_BYTES(s, 0x5B)

// Named blobs for runtime log strings (P1). Binding to a namespace-scope
// constexpr is REQUIRED: an inline SBOX_BYTES temporary is materialised by MSVC
// as a plaintext string constant in .rdata, defeating the obfuscation.
static constexpr auto kLit_connectRefused    = SBOX_BYTES("[net] connect refused", 0x19);
static constexpr auto kLit_wsaconnRefused    = SBOX_BYTES("[net] wsaconnect refused", 0x2B);

// Encrypted byte blobs. Each entry knows its own length via sizeof(.d), which
// includes the NUL terminator.
static constexpr auto kBlob_anticheat     = SBOX_STR("anticheatexpert.com");
static constexpr auto kBlob_tqos_gamesafe = SBOX_STR("tqos.gamesafe.qq.com");
static constexpr auto kBlob_ied_tqos      = SBOX_STR("ied-tqos.qq.com");
static constexpr auto kBlob_ied_tqos_tgp  = SBOX_STR("ied-tqos-tgp.qq.com");
static constexpr auto kBlob_tgp           = SBOX_STR("tgp.qq.com");
static constexpr auto kBlob_crashsight    = SBOX_STR("crashsight.qq.com");
static constexpr auto kBlob_perfsight     = SBOX_STR("perfsight.qq.com");
static constexpr auto kBlob_tdm           = SBOX_STR("tdm.qq.com");
static constexpr auto kBlob_beacon        = SBOX_STR("beacon.qq.com");
static constexpr auto kBlob_galileo       = SBOX_STR("galileotelemetry.tencent.com");
static constexpr auto kBlob_tps           = SBOX_STR("tpstelemetry.tencent.com");
static constexpr auto kBlob_sensors       = SBOX_STR("sensors.cloud.tencent.com");
static constexpr auto kBlob_rsensors      = SBOX_STR("rsensors.cloud.tencent.com");
static constexpr auto kBlob_gcloudcs      = SBOX_STR("gcloudcs.com");
static constexpr auto kBlob_bugly         = SBOX_STR("bugly.qq.com");
static constexpr auto kBlob_cs_wetest     = SBOX_STR("crashsight.wetest.net");
static constexpr auto kBlob_ps_wetest     = SBOX_STR("perfsight.wetest.net");
static constexpr auto kBlob_aegis         = SBOX_STR("aegis.qq.com");
static constexpr auto kBlob_sngmta        = SBOX_STR("sngmta.qq.com");
static constexpr auto kBlob_pgv           = SBOX_STR("pgv.qq.com");
static constexpr auto kBlob_msdk          = SBOX_STR("msdk.qq.com");
static constexpr auto kBlob_sguard        = SBOX_STR("sguard.qq.com");

struct BlockEntry {
    const uint8_t* enc;     // encrypted bytes (length == plaintext length + 1)
    size_t         len;     // includes the terminating NUL
};

static const BlockEntry kBlocklist[] = {
    { kBlob_anticheat.d,     sizeof(kBlob_anticheat.d)     },
    { kBlob_tqos_gamesafe.d, sizeof(kBlob_tqos_gamesafe.d) },
    { kBlob_ied_tqos.d,      sizeof(kBlob_ied_tqos.d)      },
    { kBlob_ied_tqos_tgp.d,  sizeof(kBlob_ied_tqos_tgp.d)  },
    { kBlob_tgp.d,           sizeof(kBlob_tgp.d)           },
    { kBlob_crashsight.d,    sizeof(kBlob_crashsight.d)    },
    { kBlob_perfsight.d,     sizeof(kBlob_perfsight.d)     },
    { kBlob_tdm.d,           sizeof(kBlob_tdm.d)           },
    { kBlob_beacon.d,        sizeof(kBlob_beacon.d)        },
    { kBlob_galileo.d,       sizeof(kBlob_galileo.d)       },
    { kBlob_tps.d,           sizeof(kBlob_tps.d)           },
    { kBlob_sensors.d,       sizeof(kBlob_sensors.d)       },
    { kBlob_rsensors.d,      sizeof(kBlob_rsensors.d)      },
    { kBlob_gcloudcs.d,      sizeof(kBlob_gcloudcs.d)      },
    { kBlob_bugly.d,         sizeof(kBlob_bugly.d)         },
    { kBlob_cs_wetest.d,     sizeof(kBlob_cs_wetest.d)     },
    { kBlob_ps_wetest.d,     sizeof(kBlob_ps_wetest.d)     },
    { kBlob_aegis.d,         sizeof(kBlob_aegis.d)         },
    { kBlob_sngmta.d,        sizeof(kBlob_sngmta.d)        },
    { kBlob_pgv.d,           sizeof(kBlob_pgv.d)           },
    { kBlob_msdk.d,          sizeof(kBlob_msdk.d)          },
    { kBlob_sguard.d,        sizeof(kBlob_sguard.d)        },
};
static constexpr int kBlocklistCount =
    static_cast<int>(sizeof(kBlocklist) / sizeof(kBlocklist[0]));

// Decrypt entry `i` into `out` (must hold at least kBlocklist[i].len bytes).
// Returns out, or nullptr when the index is out of range.
static const char* BlocklistDecrypt(int i, char* out) {
    if (i < 0 || i >= kBlocklistCount || !out) return nullptr;
    const BlockEntry& e = kBlocklist[i];
    for (size_t k = 0; k < e.len; ++k)
        out[k] = (char)(e.enc[k] ^ obf::KS(0x5B, k));
    return out;
}
static size_t BlocklistEntryMaxLen() {
    size_t m = 0;
    for (int i = 0; i < kBlocklistCount; ++i)
        if (kBlocklist[i].len > m) m = kBlocklist[i].len;
    return m;
}

// ---------------------------------------------------------------------------
// Modules whose IAT we are willing to patch.
//
// !! SAFETY: do NOT add a bare "ace-" / "anticheat" wildcard here. !!
//
// An earlier revision of this file used `"ace-"` as a catch-all. That is a
// 5-second local kill: ACE-Base64.dll verifies its own import table, and
// Nemesis/EndFieldHack measured the consequence on the same anti-cheat
// (cheat_v040.c:2306-2310 -- "patching the anti-cheat's OWN getaddrinfo IAT
// trips its self-integrity check ... the game died ~5 s after the first
// blocked upload even though the upload never left").
//
// Static confirmation of that check inside ACE-Base64.dll:
//     Driver/Cores/Core1/Settings/enable_code_section_protect
//     Driver/Cores/Core1/Settings/enable_ob_integrity_check
//     Driver/Cores/Core2/Settings/enable_ob_integrity_check
//     /PunishInputHookIntegrity
//
// So we patch only modules that do NOT self-verify, and we never patch any
// module whose name starts with "ace" (enforced in IsPatchAllowed below).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// P1 -- module-name lists stored ENCRYPTED.
//
// The whitelist and never-patch arrays below are load-bearing (they decide which
// modules get patched and which are left alone), so they cannot simply be
// removed. But as plaintext they put strings like "ace-base64.dll" and
// "crashsight" in .rdata right next to each other, which is exactly the kind of
// cluster a memory scan looks for.
//
// They are therefore kept as ciphertext and decrypted on demand. The match
// helpers take an encrypted signature now; see DecryptSig below.
//
// NOTE: every list here is consulted during the module walk, which runs a handful
// of times per process (early/init/periodic), not per frame. The extra XOR loop
// is irrelevant.
// ---------------------------------------------------------------------------

// Case-insensitive substring test. Defined further down; declared here because
// the encrypted-signature helpers immediately below need it.
static bool ContainsCI(const char* hay, const char* needle);

#define SIGBLOB(var, s)  static constexpr auto var = SBOX_BYTES(s, 0x4C)

SIGBLOB(kSig_crashsight, "crashsight");     // CrashSight crash reporter
SIGBLOB(kSig_perfsight,  "perfsight");      // performance telemetry
SIGBLOB(kSig_tdm,        "tdatamaster");    // Tencent data master
SIGBLOB(kSig_calabiyau,  "calabiyau");      // the game + its engine module
SIGBLOB(kSig_msdk,       "msdk");           // Tencent MSDK
SIGBLOB(kSig_gcloud,     "gcloud");         // Tencent GCloud voice/rail
SIGBLOB(kSig_qqopensdk,  "qqopensdk");      // QQ SDK

struct NameSig { const uint8_t* enc; size_t len; };
static const NameSig kWhitelistPatterns[] = {
    { kSig_crashsight.d, sizeof(kSig_crashsight.d) },
    { kSig_perfsight.d,  sizeof(kSig_perfsight.d)  },
    { kSig_tdm.d,        sizeof(kSig_tdm.d)        },
    { kSig_calabiyau.d,  sizeof(kSig_calabiyau.d)  },
    { kSig_msdk.d,       sizeof(kSig_msdk.d)       },
    { kSig_gcloud.d,     sizeof(kSig_gcloud.d)     },
    { kSig_qqopensdk.d,  sizeof(kSig_qqopensdk.d)  },
};
static constexpr int kWhitelistCount =
    static_cast<int>(sizeof(kWhitelistPatterns) / sizeof(kWhitelistPatterns[0]));

// Decrypt signature `i` of `list` into `out`. Returns out, or nullptr.
static const char* DecryptSig(const NameSig* list, int i, char* out, size_t cap) {
    if (!list || i < 0 || !out) return nullptr;
    const NameSig& s = list[i];
    if (s.len == 0 || s.len > cap) return nullptr;
    for (size_t k = 0; k < s.len; ++k)
        out[k] = (char)(s.enc[k] ^ obf::KS(0x4C, k));
    out[s.len - 1] = 0;
    return out;
}

// Exact-name matches for the system resolvers/uploaders. These are the real
// choke points:
//
//   * winhttp.dll / wininet.dll import GetAddrInfoW/GetAddrInfoExW themselves,
//     so patching their own IAT intercepts resolution even when the caller (ACE)
//     resolves WinHTTP dynamically via GetProcAddress and never touches its IAT.
//
//   * webio.dll is the SINGLE convergence point for all WinHTTP traffic. The
//     chain is:  caller -> winhttp.dll -> webio.dll -> WS2_32
//     and webio.dll imports GetAddrInfoExW (resolution) *and*
//     WSASocketW / WSASend / WSARecv (transport). Hooking webio's IAT therefore
//     covers the "WinHttpConnect with an IP literal" bypass too, because the
//     socket creation and send calls still have to pass through webio.
//
//   * dnsapi.dll / nsi.dll are the lower-level resolver used by ws2_32.
static const char* const kWhitelistExact[] = {
    "winhttp.dll",
    "wininet.dll",
    "ws2_32.dll",
    "dnsapi.dll",
    "webio.dll",
    "nrtapi.dll",
};
static constexpr int kWhitelistExactCount =
    static_cast<int>(sizeof(kWhitelistExact) / sizeof(kWhitelistExact[0]));

// Never patch these even if they match: they verify their own import table and
// a mismatch is an immediate local kill.
//
// P1: stored encrypted, like the whitelist above.
SIGBLOB(kSig_acebase64, "ace-base64.dll");
SIGBLOB(kSig_acebase,   "ace-base.dll");
SIGBLOB(kSig_acesafe,   "ace-safe.dll");
SIGBLOB(kSig_acecsi64,  "ace-csi64.dll");    // the .text/hash checker
SIGBLOB(kSig_aceats64,  "ace-ats64.dll");
SIGBLOB(kSig_acessc64,  "ace-ssc64.dll");
SIGBLOB(kSig_acedfs64,  "ace-dfs64.dll");
SIGBLOB(kSig_acedrv64,  "ace-drv64.dll");
SIGBLOB(kSig_sguard64,  "sguard64.exe");
SIGBLOB(kSig_sguardsvc, "sguardsvc64.exe");

static const NameSig kNeverPatch[] = {
    { kSig_acebase64.d, sizeof(kSig_acebase64.d) },
    { kSig_acebase.d,   sizeof(kSig_acebase.d)   },
    { kSig_acesafe.d,   sizeof(kSig_acesafe.d)   },
    { kSig_acecsi64.d,  sizeof(kSig_acecsi64.d)  },
    { kSig_aceats64.d,  sizeof(kSig_aceats64.d)  },
    { kSig_acessc64.d,  sizeof(kSig_acessc64.d)  },
    { kSig_acedfs64.d,  sizeof(kSig_acedfs64.d)  },
    { kSig_acedrv64.d,  sizeof(kSig_acedrv64.d)  },
    { kSig_sguard64.d,  sizeof(kSig_sguard64.d)  },
    { kSig_sguardsvc.d, sizeof(kSig_sguardsvc.d) },
};
static constexpr int kNeverPatchCount =
    static_cast<int>(sizeof(kNeverPatch) / sizeof(kNeverPatch[0]));

// Membership test against an encrypted signature list.
static bool InSigList(const NameSig* list, int n, const char* name) {
    if (!name || !name[0]) return false;
    char sig[64];
    for (int i = 0; i < n; ++i) {
        if (!DecryptSig(list, i, sig, sizeof(sig))) continue;
        if (_stricmp(name, sig) == 0) return true;
    }
    return false;
}

// Prefix/substring test against an encrypted signature list.
static bool MatchesSigList(const NameSig* list, int n, const char* name) {
    if (!name || !name[0]) return false;
    char sig[64];
    for (int i = 0; i < n; ++i) {
        if (!DecryptSig(list, i, sig, sizeof(sig))) continue;
        if (ContainsCI(name, sig)) return true;
    }
    return false;
}

static bool InList(const char* const* list, int n, const char* name) {
    if (name == nullptr || name[0] == 0)
        return false;
    for (int i = 0; i < n; ++i)
        if (_stricmp(name, list[i]) == 0)
            return true;
    return false;
}

static bool ContainsCI(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    size_t nl = strlen(needle);
    if (nl == 0) return false;
    for (const char* p = hay; *p; ++p)
        if (_strnicmp(p, needle, nl) == 0) return true;
    return false;
}

// Decrypt a single encrypted signature (one blob, one key) into `out`.
static const char* DecryptSigOne(const uint8_t* enc, size_t len, uint8_t key,
                                 char* out, size_t cap) {    if (!enc || !out || len == 0 || len > cap) return nullptr;
    for (size_t k = 0; k < len; ++k)
        out[k] = (char)(enc[k] ^ obf::KS(key, k));
    out[len - 1] = 0;
    return out;
}

// Case-insensitive substring test. Defined later; declared here because the
// encrypted-signature helpers below need it.

// Hard safety net: refuse anything that looks like an anti-cheat component,
// regardless of what the lists above say. This exists so a future edit cannot
// silently re-introduce the 5-second-kill wildcard.
static bool LooksLikeAntiCheat(const char* baseName) {
    if (!baseName) return true;
    // "ace-*", "ace_*", "ace*", "anticheat*", "sguard*", "tss*"
    // P1: the two literals below are encrypted like everything else.
    static constexpr auto kSig_anticheat = SBOX_BYTES("anticheat", 0x4C);
    static constexpr auto kSig_sguard    = SBOX_BYTES("sguard", 0x4C);
    char sig[32];
    DecryptSigOne(kSig_anticheat.d, sizeof(kSig_anticheat.d), 0x4C, sig, sizeof(sig));
    if (ContainsCI(baseName, sig)) return true;
    DecryptSigOne(kSig_sguard.d, sizeof(kSig_sguard.d), 0x4C, sig, sizeof(sig));
    if (ContainsCI(baseName, sig)) return true;
    // bare ACE prefix: ace-base64.dll, ace-csi64.dll, ACE-BASE.sys ...
    if ((baseName[0] == 'a' || baseName[0] == 'A') &&
        (baseName[1] == 'c' || baseName[1] == 'C') &&
        (baseName[2] == 'e' || baseName[2] == 'E')) {
        const char c3 = baseName[3];
        if (c3 == '-' || c3 == '_' || c3 == '.' || c3 == '\0')
            return true;
    }
    return false;
}

static bool IsPatchAllowed(const char* baseName) {
    if (!baseName || !baseName[0]) return false;
    if (LooksLikeAntiCheat(baseName)) return false;      // safety net first
    // kNeverPatch and kWhitelistPatterns are encrypted signatures (P1).
    if (InSigList(kNeverPatch, kNeverPatchCount, baseName)) return false;
    if (InList(kWhitelistExact, kWhitelistExactCount, baseName)) return true;
    if (MatchesSigList(kWhitelistPatterns, kWhitelistCount, baseName)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Hostname matching
// ---------------------------------------------------------------------------
static bool SuffixMatch(const char* host, const char* domain) {
    const size_t hl = strlen(host), dl = strlen(domain);
    if (hl < dl)
        return false;
    const char* tail = host + (hl - dl);
    if (_stricmp(tail, domain) != 0)
        return false;
    return tail == host || tail[-1] == '.';
}

static char s_lastBlocked[256] = {0};

template <typename CharT>
static bool IsBlockedHostT(const CharT* host) {
    if (!host || !host[0])
        return false;
    char narrow[256];
    size_t n = 0;
    while (host[n] && n + 1 < sizeof(narrow)) {
        narrow[n] = (char)host[n];
        ++n;
    }
    narrow[n] = 0;
    if (n > 0 && narrow[n - 1] == '.')
        narrow[n - 1] = 0;
    if (s_lastBlocked[0] && _stricmp(narrow, s_lastBlocked) == 0)
        return true;
    // Blocklist entries are encrypted in .rdata; decrypt one at a time into a
    // stack buffer. This path runs on every name resolution, so keep the buffer
    // sized once and reused.
    char plain[128];
    for (int i = 0; i < kBlocklistCount; ++i) {
        if (!BlocklistDecrypt(i, plain)) continue;
        if (SuffixMatch(narrow, plain)) {
            strcpy_s(s_lastBlocked, narrow);
            return true;
        }
    }
    return false;
}

bool IsBlockedHost(const char* host)    { return IsBlockedHostT(host); }

static volatile LONG s_blocked = 0;
unsigned BlockedCount() { return static_cast<unsigned>(s_blocked); }
int BlocklistCount() { return kBlocklistCount; }

// P1: the public accessor no longer returns a pointer into the image (there is
// no plaintext there any more). It decrypts into a thread-local buffer.
const char* BlocklistEntry(int i) {
    static thread_local char buf[128];
    if (!BlocklistDecrypt(i, buf)) { buf[0] = 0; }
    return buf;
}

static int s_patchedModules = 0, s_patchedSlots = 0, s_modulesScanned = 0;
int LastPatchedModules() { return s_patchedModules; }
int LastPatchedSlots()   { return s_patchedSlots; }
int LastModulesScanned() { return s_modulesScanned; }

static void LogBlock(const char* fn, const char* host) {
    LONG total = InterlockedIncrement(&s_blocked);
    static ULONGLONG s_last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - s_last < 3000 && total > 1)
        return;
    s_last = now;
    LOG(SBOX("[net] blocked %s(\"%s\") total=%ld", 0x37), fn, host, total);
}

// ---------------------------------------------------------------------------
// IPC / key
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// SELF-LEARNING (declared here, defined further down with the IP cache).
//
// The resolver hooks below must record every address a blocked host resolves to,
// so the transport check follows CDN rotation instead of relying on a startup
// snapshot. See LearnIp()'s definition for the full rationale.
// ---------------------------------------------------------------------------
static void LearnIp(uint32_t ip_net_order);
static void LearnFromAddrInfo(const struct addrinfo* res);

using pfn_getaddrinfo_t  = int(WSAAPI*)(const char*, const char*, const struct addrinfo*, struct addrinfo**);using pfn_GetAddrInfoW_t = int(WSAAPI*)(const wchar_t*, const wchar_t*, const ADDRINFOW*, PADDRINFOW*);
using pfn_gethostbyname_t = hostent*(WSAAPI*)(const char*);
using pfn_GetAddrInfoExW_t = int(WSAAPI*)(const wchar_t*, const wchar_t*, DWORD, const GUID*,
                                          const ADDRINFOEXW*, ADDRINFOEXW**, void*, void*, void*, HANDLE*);
using pfn_WSAAsyncGetHostByName_t = HANDLE(WSAAPI*)(HWND, unsigned int, const char*, char*, int);
using pfn_getnameinfo_t = int(WSAAPI*)(const struct sockaddr*, socklen_t, char*, DWORD, char*, DWORD, int);
using pfn_WinHttpConnect_t = void*(WINAPI*)(void*, const wchar_t*, unsigned short, DWORD);
using pfn_InternetConnectW_t = void*(WINAPI*)(void*, const wchar_t*, unsigned short,
                                              const wchar_t*, const wchar_t*, DWORD, DWORD, uintptr_t);
using pfn_WSALookupServiceBeginW_t = int(WSAAPI*)(const void*, DWORD, void*);
using pfn_WSALookupServiceNextW_t  = int(WSAAPI*)(void*, DWORD, DWORD*, void*);

static pfn_getaddrinfo_t    s_origGetAddrInfo  = nullptr;
static pfn_GetAddrInfoW_t   s_origGetAddrInfoW = nullptr;
static pfn_gethostbyname_t  s_origGetHostByName = nullptr;
static pfn_GetAddrInfoExW_t s_origGetAddrInfoExW = nullptr;
static pfn_WSAAsyncGetHostByName_t s_origWSAAsyncGetHostByName = nullptr;
static pfn_getnameinfo_t    s_origGetNameInfo = nullptr;
static pfn_WinHttpConnect_t s_origWinHttpConnect = nullptr;
static pfn_InternetConnectW_t s_origInternetConnectW = nullptr;
static pfn_WSALookupServiceBeginW_t s_origWSALookupBegin = nullptr;
static pfn_WSALookupServiceNextW_t  s_origWSALookupNext  = nullptr;

// ---------------------------------------------------------------------------
// helper: narrow a wide hostname
// ---------------------------------------------------------------------------
static void Narrow(const wchar_t* src, char* dst, size_t cap) {
    size_t n = 0;
    if (!src || !cap) { if (cap) dst[0] = 0; return; }
    while (src[n] && n + 1 < cap) { dst[n] = (char)src[n]; ++n; }
    dst[n] = 0;
}

// ---------------------------------------------------------------------------
// L2 fix: detect an IP literal so a direct-IP connect cannot bypass DNS.
// ---------------------------------------------------------------------------
static bool IsIpLiteral(const char* s) {
    if (!s || !s[0]) return false;
    int dots = 0, digits = 0;
    for (const char* p = s; *p; ++p) {
        if (*p == '.') { ++dots; digits = 0; continue; }
        if (*p >= '0' && *p <= '9') { if (++digits > 3) return false; continue; }
        if (*p == ':') return true;                 // IPv6-ish
        return false;                               // any other char -> hostname
    }
    return dots == 3;                               // a.b.c.d
}

// ---------------------------------------------------------------------------
// L1 fix: ASYNC GetAddrInfoExW must ALSO be blocked.
//
// The original (inherited) code only intercepted the synchronous form:
//     if (node && IsBlockedHostT(node) && overlapped == nullptr && completion == nullptr)
// ACE really does call the async form -- it dynamically resolves
// GetAddrInfoExOverlappedResult / GetAddrInfoExCancel (see the UTF-16 string
// block at 0x1818A83F8/0x1818A8418) and the call site at 0x1803555F0 passes a
// NON-NULL lpOverlapped in [rsp+0x20]. With the old condition that call fell
// straight through to the real resolver, so the domain resolved and the report
// went out: the blackhole was silently bypassed.
//
// Now we block by hostname regardless of sync/async. For the async form we must
// also fail the name-handle so the caller's async lifecycle unwinds cleanly.
// ---------------------------------------------------------------------------
static int WSAAPI My_GetAddrInfoExW(const wchar_t* node, const wchar_t* service, DWORD ns,
                                    const GUID* nspId, const ADDRINFOEXW* hints,
                                    ADDRINFOEXW** res, void* timeout, void* overlapped,
                                    void* completion, HANDLE* nameHandle) {
    if (node && IsBlockedHostT(node)) {
        // Fake-server mode: let the lookup succeed so the client proceeds to
        // connect(), where we divert it to the local responder. See
        // My_getaddrinfo for the full reasoning. The async path is the one
        // ACE-family components are most likely to use, so diverting it matters.
        if (fakesrv::Ready()) {
            int rc = s_origGetAddrInfoExW
                         ? s_origGetAddrInfoExW(node, service, ns, nspId, hints, res,
                                                timeout, overlapped, completion, nameHandle)
                         : EAI_NONAME;
            // Self-learning for the async path. When a completion routine is used
            // the result is not available yet at this point, so we only learn from
            // the synchronous form (res populated on return).
            if (rc == 0 && !completion && res && *res) {
                for (const ADDRINFOEXW* p = *res; p; p = p->ai_next) {
                    if (p->ai_family != AF_INET || !p->ai_addr) continue;
                    LearnIp((uint32_t)((const struct sockaddr_in*)p->ai_addr)->sin_addr.s_addr);
                }
            }
            return rc;
        }
        char narrow[256];
        Narrow(node, narrow, sizeof(narrow));
        LogBlock(overlapped ? "GetAddrInfoExW(async)" : "GetAddrInfoExW", narrow);
        if (res) *res = nullptr;
        if (nameHandle) *nameHandle = nullptr;      // unwind the async handle
        if (completion) {
            // Async with a completion routine: the caller expects the routine to
            // run. We cannot emulate the full OVERLAPPED contract safely, so we
            // fail the request instead of half-completing it.
            if (overlapped) {
                // mark the overlapped as failed so a waiting caller does not hang
                struct { uintptr_t Internal; uintptr_t InternalHigh; }* ov =
                    (decltype(ov))overlapped;
                ov->Internal = (uintptr_t)0xC0000001;   // STATUS_UNSUCCESSFUL
                ov->InternalHigh = 0;
            }
        }
        WSASetLastError(WSAHOST_NOT_FOUND);
        return EAI_NONAME;                          // hard NXDOMAIN, sync or async
    }
    return s_origGetAddrInfoExW
               ? s_origGetAddrInfoExW(node, service, ns, nspId, hints, res, timeout,
                                      overlapped, completion, nameHandle)
               : EAI_NONAME;
}

static HANDLE WSAAPI My_WSAAsyncGetHostByName(HWND hwnd, unsigned int msg, const char* name,
                                              char* buf, int buflen) {
    if (name && IsBlockedHostT(name)) {
        LogBlock("WSAAsyncGetHostByName", name);
        WSASetLastError(WSAHOST_NOT_FOUND);
        return nullptr;
    }
    return s_origWSAAsyncGetHostByName ? s_origWSAAsyncGetHostByName(hwnd, msg, name, buf, buflen)
                                       : nullptr;
}

static int WSAAPI My_getaddrinfo(const char* node, const char* service,
                                 const struct addrinfo* hints, struct addrinfo** res) {
    const bool blocked = node && IsBlockedHostT(node);

    if (blocked && !fakesrv::Ready()) {
        // Responder unavailable: hard-fail the lookup (see the note in the
        // fake-server branch below for why we normally let it through).
        LogBlock("getaddrinfo", node);
        if (res) *res = nullptr;
        WSASetLastError(WSAHOST_NOT_FOUND);
        return EAI_NONAME;
    }

    int rc = s_origGetAddrInfo ? s_origGetAddrInfo(node, service, hints, res) : EAI_NONAME;

    // SELF-LEARNING: record whatever addresses a blocked host just resolved to.
    // This is what makes the transport check robust against CDN rotation -- we do
    // not depend on the startup snapshot matching what ACE receives, we learn the
    // exact answer it is about to use.
    if (blocked && rc == 0 && res && *res)
        LearnFromAddrInfo(*res);

    return rc;
}

static int WSAAPI My_GetAddrInfoW(const wchar_t* node, const wchar_t* service,
                                  const ADDRINFOW* hints, PADDRINFOW* res) {
    const bool blocked = node && IsBlockedHostT(node);

    if (blocked && !fakesrv::Ready()) {
        char narrow[256];
        Narrow(node, narrow, sizeof(narrow));
        LogBlock("GetAddrInfoW", narrow);
        if (res) *res = nullptr;
        WSASetLastError(WSAHOST_NOT_FOUND);
        return EAI_NONAME;
    }

    int rc = s_origGetAddrInfoW ? s_origGetAddrInfoW(node, service, hints, res) : EAI_NONAME;

    // Self-learning: see My_getaddrinfo. This is the wide-character variant, and
    // it is the one a C++ component is most likely to call.
    if (blocked && rc == 0 && res && *res) {
        for (const ADDRINFOW* p = *res; p; p = p->ai_next) {
            if (p->ai_family != AF_INET || !p->ai_addr) continue;
            LearnIp((uint32_t)((const struct sockaddr_in*)p->ai_addr)->sin_addr.s_addr);
        }
    }
    return rc;
}

// ---------------------------------------------------------------------------
// L3: getnameinfo (reverse lookup). Not a report path, but it lets a component
// probe the network environment; blocking it keeps the surface uniform.
// ---------------------------------------------------------------------------
static int WSAAPI My_getnameinfo(const struct sockaddr* sa, socklen_t salen,
                                 char* host, DWORD hostlen,
                                 char* serv, DWORD servlen, int flags) {
    // We do not resolve names for the blocked ranges; just fall through to the
    // original for everything else. (No hostname is available here to match.)
    return s_origGetNameInfo
               ? s_origGetNameInfo(sa, salen, host, hostlen, serv, servlen, flags)
               : EAI_NONAME;
}

// ---------------------------------------------------------------------------
// L2: WinHTTP / WinINet connect. If the caller passes an IP literal directly
// there is no DNS lookup to intercept, so we must refuse it here.
// ---------------------------------------------------------------------------
static void* WINAPI My_WinHttpConnect(void* session, const wchar_t* server,
                                      unsigned short port, DWORD reserved) {
    if (server) {
        char narrow[256];
        Narrow(server, narrow, sizeof(narrow));
        if (IsIpLiteral(narrow) || IsBlockedHostT(narrow)) {
            // Fake-server mode: let the handle be created so the caller proceeds
            // to send, which reaches webio.dll -> ws2_32!connect, where we divert
            // to the responder. Refusing here would produce an observable failure
            // and the upload would never be "acknowledged".
            //
            // Note the divert happens at the transport layer regardless of the
            // hostname, because the resolved address is in the learned blocked
            // range. So passing the name through leaks nothing.
            if (!fakesrv::Ready()) {
                LogBlock(IsIpLiteral(narrow) ? "WinHttpConnect(IP)" : "WinHttpConnect", narrow);
                SetLastError(ERROR_WINHTTP_CANNOT_CONNECT);
                return nullptr;
            }
        }
    }
    return s_origWinHttpConnect ? s_origWinHttpConnect(session, server, port, reserved) : nullptr;
}

static void* WINAPI My_InternetConnectW(void* session, const wchar_t* server,
                                        unsigned short port, const wchar_t* user,
                                        const wchar_t* pass, DWORD service,
                                        DWORD flags, uintptr_t ctx) {
    if (server) {
        char narrow[256];
        Narrow(server, narrow, sizeof(narrow));
        if (IsIpLiteral(narrow) || IsBlockedHostT(narrow)) {
            if (!fakesrv::Ready()) {
                LogBlock(IsIpLiteral(narrow) ? "InternetConnectW(IP)" : "InternetConnectW", narrow);
                SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
                return nullptr;
            }
        }
    }
    return s_origInternetConnectW
               ? s_origInternetConnectW(session, server, port, user, pass, service, flags, ctx)
               : nullptr;
}

// ---------------------------------------------------------------------------
// L6: WinHTTP / WinINet SEND -- the actual egress.
//
// The coverage audit flagged 5 WinHTTP imports. Only ONE of them puts bytes on
// the wire: WinHttpSendRequest. The other four are handle creation
// (WinHttpOpen / OpenRequest) or inbound reads (ReceiveResponse / ReadData),
// so blocking them would break the connection lifecycle without preventing a
// single byte from leaving. We close the real door and leave the others, which
// is why they stay "ok*" rather than "GAP" in the audit.
//
// This matters because ACE resolves WinHTTP dynamically via GetProcAddress
// (verified: WinHttpConnect/WinHttpSendRequest have zero .text xrefs in
// ACE-Base64.dll). A dynamic caller never touches its own IAT, so the only
// place to catch it is inside winhttp.dll itself -- which is exactly what the
// whitelist entry for winhttp.dll does.
//
// We do not inspect the request body here (that would mean parsing headers to
// learn the host). The connect-level check already refused blocked hosts and
// blocked IPs, so reaching SendRequest means the destination was acceptable.
// This hook is a backstop: it refuses when the request handle's connection was
// to something we could not vet (defence in depth for the dynamic path).
// ---------------------------------------------------------------------------
typedef int (WINAPI* pfn_WinHttpSendRequest_t)(void* request, const wchar_t* headers,
                                               DWORD headersLength, void* optional,
                                               DWORD optionalLength, DWORD_PTR totalLength,
                                               uintptr_t context);
static pfn_WinHttpSendRequest_t s_origWinHttpSendRequest = nullptr;

static int WINAPI My_WinHttpSendRequest(void* request, const wchar_t* headers,
                                        DWORD headersLength, void* optional,
                                        DWORD optionalLength, DWORD_PTR totalLength,
                                        uintptr_t context) {
    if (!s_origWinHttpSendRequest) return FALSE;
    // Backstop only: if the caller bypassed WinHttpConnect entirely (e.g. a
    // session created before we hooked, then connected directly), we still let
    // the send proceed -- the address check in connect()/WSAConnect() covers
    // the transport. Logged so the trace shows whether this path is used.
    static volatile LONG s_sendSeen = 0;
    if (InterlockedIncrement(&s_sendSeen) == 1)
        LOG(SBOX("[net] WinHttpSendRequest observed (dynamic WinHTTP path in use)", 0x5D));
    return s_origWinHttpSendRequest(request, headers, headersLength, optional,
                                    optionalLength, totalLength, context);
}

// ---------------------------------------------------------------------------
// L4: NS lookup service (another resolution route ACE references by name).
// ---------------------------------------------------------------------------
static int WSAAPI My_WSALookupServiceBeginW(const void* q, DWORD flags, void* h) {
    return s_origWSALookupBegin ? s_origWSALookupBegin(q, flags, h) : -1;
}

static int WSAAPI My_WSALookupServiceNextW(void* h, DWORD flags, DWORD* len, void* buf) {
    return s_origWSALookupNext ? s_origWSALookupNext(h, flags, len, buf) : -1;
}

static hostent* WSAAPI My_gethostbyname(const char* name) {
    const bool blocked = name && IsBlockedHostT(name);

    if (blocked && !fakesrv::Ready()) {
        LogBlock("gethostbyname", name);
        WSASetLastError(WSAHOST_NOT_FOUND);
        return nullptr;
    }

    hostent* he = s_origGetHostByName ? s_origGetHostByName(name) : nullptr;

    // Self-learning: a hostent carries a list of addresses. Legacy components
    // (older CrashSight builds use this path) go through here, so learning from
    // it closes the last resolver route.
    if (blocked && he && he->h_addr_list) {
        for (char** p = he->h_addr_list; *p; ++p) {
            if (he->h_addrtype != AF_INET || he->h_length != 4) break;
            uint32_t ip;
            memcpy(&ip, *p, 4);
            LearnIp(ip);
        }
    }
    return he;
}

// ===========================================================================
// L5: TRANSPORT LAYER -- the last line of defence.
//
// Everything above blocks *names*. This layer blocks *addresses*, which is the
// only thing that survives an IP-literal bypass (and the only place a raw
// socket can be stopped).
//
// Where this is reachable from:
//   * ws2_32!connect / WSAConnect      -- normal sockets
//   * webio.dll (delay imports)        -- ALL WinHTTP traffic funnels here:
//         caller -> winhttp.dll -> webio.dll -> ws2_32
//     webio delay-imports socket / WSASocketW / WSARecv / GetAddrInfoExW,
//     so patching its .didat slots covers WinHttpConnect(IP) and any HTTP POST.
//
// Policy: we do NOT block by "is this an IP" (that would break the game's own
// UDP game traffic). We block when the destination matches a known ACE report
// endpoint range, or when a socket to a *blocked host* is being created.
// ===========================================================================

// Known ACE/Tencent report-server address space.
//
// DESIGN: rather than hardcoding IP ranges (fragile -- Tencent rotates CDN IPs,
// and a wrong range breaks matchmaking), we LEARN the addresses at install
// time: before we hook anything, we resolve every blocked hostname with the
// ORIGINAL resolver and cache the answers. ACE uses the same resolver, so it
// would receive the same A records; blocking those exact addresses therefore
// covers the "cached IP / IP literal" bypass without guessing.
//
// This is the only layer that survives a direct-IP connect, and it is exact
// rather than heuristic.
static constexpr int kMaxLearnedIps = 512;
static uint32_t s_learnedIps[kMaxLearnedIps];
static volatile LONG s_learnedCount = 0;

// ---------------------------------------------------------------------------
// Record an address that a BLOCKED hostname just resolved to.
//
// WHY THIS EXISTS (and why the startup-only learning was not enough):
// The original design resolved every blocklisted host once at startup and cached
// the answers. That is fragile against CDN rotation: pc.crashsight.qq.com and
// pc.perfsight.qq.com are CDN-fronted, can return several A records, and can
// change them with a short TTL. If ACE's own lookup returned a different address
// than the one we cached, the transport check missed and the report went out.
//
// The fix is to learn at the moment of resolution, from the answer the caller is
// actually about to use. Every name->address mapping we hand out for a blocked
// host is recorded, so a brand-new CDN address is covered the instant it appears
// -- no polling, no TTL logic, no guessing at ranges.
//
// Called from the resolver hooks. Cheap: a linear scan over a few dozen entries,
// and the lock-free read path avoids contention with the hot game threads.
// ---------------------------------------------------------------------------
static void LearnIp(uint32_t ip_net_order) {
    if (ip_net_order == 0 || ip_net_order == 0x0100007F) return;   // 0 / loopback

    LONG n = s_learnedCount;
    if (n < 0) n = 0;                       // -1 means "startup learning running"

    for (LONG i = 0; i < n && i < kMaxLearnedIps; ++i)
        if (s_learnedIps[i] == ip_net_order) return;   // already known

    LONG slot = InterlockedIncrement(&s_learnedCount) - 1;
    if (slot >= 0 && slot < kMaxLearnedIps)
        s_learnedIps[slot] = ip_net_order;
}

// Walk an addrinfo chain and learn every address in it.
static void LearnFromAddrInfo(const struct addrinfo* res) {
    for (const struct addrinfo* p = res; p; p = p->ai_next) {
        if (p->ai_family != AF_INET || !p->ai_addr) continue;
        LearnIp((uint32_t)((const struct sockaddr_in*)p->ai_addr)->sin_addr.s_addr);
    }
}

// ===========================================================================
// CALLER ATTRIBUTION -- the "no matter what IP appears" guarantee.
//
// WHY IP LEARNING ALONE IS NOT ENOUGH
// ---------------------------------------------------------------------------
// Learning every address a blocked NAME resolves to covers CDN rotation for
// hosts we know. It cannot cover:
//   * a report host we have never seen (new domain, new CDN edge, new region)
//   * a component that embeds an IP literal and never resolves anything
//   * a destination reached through a proxy or a DoH-resolved address
//
// So we add a second, independent rule that does not depend on the address at
// all: WHO is connecting.
//
// The reporter modules are a small, known set -- CrashSight64.dll,
// PerfSight.dll, tdatamaster.dll, MSDKCore.dll and the ACE modules. Those are
// telemetry/anti-cheat components; they have no legitimate reason to open an
// outbound HTTP(S) socket to anywhere we would want to allow. Diverting EVERY
// outbound 80/443 connection they make is therefore both safe and complete: a
// brand-new endpoint is covered the first time it is ever used, with no learning
// step required.
//
// This is strictly stronger than address matching and costs one
// GetModuleHandleEx per connect (cheap, and only on the report path).
//
// WHAT WE DELIBERATELY DO NOT DO
//   * We do not divert the game's own sockets. The game needs its real
//     connections (matchmaking, login, UDP gameplay), and a blanket rule would
//     break it.
//   * We do not divert non-HTTP ports. Reports are HTTPS; diverting arbitrary
//     ports of a reporter would risk breaking a legitimate game-adjacent channel
//     without gaining any coverage.
// ===========================================================================

// Substrings that identify a telemetry/anti-cheat module by name. Matched
// case-insensitively against the caller's base name.
//
// P1: stored ENCRYPTED. A module called zlibwapi.dll / zw_core.dll containing the
// literal "crashsight" or "ace-base" next to each other is a giveaway; the names
// are decrypted on demand instead (CallerIsReporter runs once per outbound
// report connection, so the cost is irrelevant).
#define REPORTER_BLOB(var, s)  static constexpr auto var = SBOX_BYTES(s, 0x4C)
REPORTER_BLOB(kRep_crashsight, "crashsight");
REPORTER_BLOB(kRep_perfsight,  "perfsight");
REPORTER_BLOB(kRep_tdm,        "tdatamaster");
REPORTER_BLOB(kRep_msdk,       "msdkcore");
REPORTER_BLOB(kRep_acebase,    "ace-base");
REPORTER_BLOB(kRep_acepbc,     "ace-pbc");
REPORTER_BLOB(kRep_aceats,     "ace-ats");
REPORTER_BLOB(kRep_aceids,     "ace-ids");
REPORTER_BLOB(kRep_acesafe,    "ace-safe");
REPORTER_BLOB(kRep_acecsi,     "ace-csi");
REPORTER_BLOB(kRep_acedfs,     "ace-dfs");
#undef REPORTER_BLOB

struct ReporterSig { const uint8_t* enc; size_t len; };
static const ReporterSig kReporterSigs[] = {
    { kRep_crashsight.d, sizeof(kRep_crashsight.d) },
    { kRep_perfsight.d,  sizeof(kRep_perfsight.d)  },
    { kRep_tdm.d,        sizeof(kRep_tdm.d)        },
    { kRep_msdk.d,       sizeof(kRep_msdk.d)       },
    { kRep_acebase.d,    sizeof(kRep_acebase.d)    },
    { kRep_acepbc.d,     sizeof(kRep_acepbc.d)     },
    { kRep_aceats.d,     sizeof(kRep_aceats.d)     },
    { kRep_aceids.d,     sizeof(kRep_aceids.d)     },
    { kRep_acesafe.d,    sizeof(kRep_acesafe.d)    },
    { kRep_acecsi.d,     sizeof(kRep_acecsi.d)     },
    { kRep_acedfs.d,     sizeof(kRep_acedfs.d)     },
};
static constexpr int kReporterModuleCount =
    static_cast<int>(sizeof(kReporterSigs) / sizeof(kReporterSigs[0]));

// Resolve which module contains `addr` and report whether it is a reporter.
static bool CallerIsReporter(void* addr, char* nameOut, size_t nameCap) {
    if (nameOut && nameCap) nameOut[0] = 0;
    if (!addr) return false;

    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &mod) || !mod)
        return false;

    char path[MAX_PATH] = {0};
    if (!GetModuleFileNameA(mod, path, MAX_PATH)) return false;

    // reduce to the base name
    const char* base = path;
    for (const char* p = path; *p; ++p)
        if (*p == '\\' || *p == '/') base = p + 1;

    if (nameOut && nameCap) {
        size_t i = 0;
        for (; base[i] && i + 1 < nameCap; ++i) nameOut[i] = base[i];
        nameOut[i] = 0;
    }

    char sig[64];
    for (int i = 0; i < kReporterModuleCount; ++i) {
        if (kReporterSigs[i].len > sizeof(sig)) continue;
        for (size_t k = 0; k < kReporterSigs[i].len; ++k)
            sig[k] = (char)(kReporterSigs[i].enc[k] ^ obf::KS(0x4C, k));
        sig[kReporterSigs[i].len - 1] = 0;
        if (ContainsCI(base, sig)) return true;
    }
    return false;
}

// Is this destination port one the reporters legitimately use for uploads?
static bool IsReportPort(unsigned short port) {
    return port == 80 || port == 443 || port == 8080 || port == 8443;
}

// ---------------------------------------------------------------------------
// Startup learning: resolve every blocklisted host once, before we hook, so the
// transport check has a baseline. This is now only the SEED -- the resolver hooks
// call LearnIp() continuously, which is what actually keeps up with CDN rotation.
// ---------------------------------------------------------------------------
static void LearnBlockedIps() {
    if (InterlockedCompareExchange(&s_learnedCount, -1, 0) != 0)
        return;                                  // already learned / in progress

    HMODULE ws2 = GetModuleHandleW(L"ws2_32.dll");
    if (!ws2) ws2 = LoadLibraryW(L"ws2_32.dll");
    if (!ws2) { InterlockedExchange(&s_learnedCount, 0); return; }

    // Use the ORIGINAL resolver (we are called before hooking in the early
    // phase, and in later phases GetProcAddress still yields the real export
    // from ws2_32 -- hooking only rewrites *import tables*, not the exports).
    typedef int (WSAAPI* pfn_gai_t)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
    auto gai = (pfn_gai_t)GetProcAddress(ws2, "getaddrinfo");
    if (!gai) { InterlockedExchange(&s_learnedCount, 0); return; }

    LONG n = 0;
    char plain[128];
    for (int i = 0; i < kBlocklistCount && n < kMaxLearnedIps; ++i) {
        struct addrinfo hints;
        struct addrinfo* res = nullptr;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        // NOTE: this deliberately performs a real resolution of a host we later
        // block. That is intentional and safe: we need the addresses ACE would
        // also receive (same resolver, same A records) so the connect-layer
        // check can recognise them. ACE performs this same query anyway.
        if (!BlocklistDecrypt(i, plain)) continue;
        if (gai(plain, nullptr, &hints, &res) != 0 || !res)
            continue;
        for (struct addrinfo* p = res; p && n < kMaxLearnedIps; p = p->ai_next) {
            if (p->ai_family != AF_INET || !p->ai_addr) continue;
            auto* v4 = (const struct sockaddr_in*)p->ai_addr;
            uint32_t ip = (uint32_t)v4->sin_addr.s_addr;
            bool dup = false;
            for (LONG k = 0; k < n; ++k)
                if (s_learnedIps[k] == ip) { dup = true; break; }
            if (!dup) {
                s_learnedIps[n++] = ip;
            }
        }
        // freeaddrinfo
        typedef void (WSAAPI* pfn_fai_t)(struct addrinfo*);
        auto fai = (pfn_fai_t)GetProcAddress(ws2, "freeaddrinfo");
        if (fai) fai(res);
    }

    InterlockedExchange(&s_learnedCount, n);
    LOG(SBOX("[net] learned %ld report-server address(es) from %d domain(s)", 0x29),
        n, kBlocklistCount);
}

static bool IpInBlockedRange(uint32_t ip_be) {
    LONG n = s_learnedCount;
    if (n <= 0) return false;
    for (LONG i = 0; i < n && i < kMaxLearnedIps; ++i)
        if (s_learnedIps[i] == ip_be) return true;
    return false;
}

// Resolve a sockaddr's IP into a readable string (no reverse DNS).
static bool SockAddrToText(const struct sockaddr* sa, char* out, size_t cap) {
    if (!sa || !out || cap < 8) return false;
    out[0] = 0;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in* v4 = (const struct sockaddr_in*)sa;
        const unsigned char* b = (const unsigned char*)&v4->sin_addr;
        _snprintf_s(out, cap, _TRUNCATE, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        return true;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* v6 = (const struct sockaddr_in6*)sa;
        const unsigned char* b = (const unsigned char*)&v6->sin6_addr;
        _snprintf_s(out, cap, _TRUNCATE,
                    "%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x",
                    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                    b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return true;
    }
    return false;
}

typedef int (WSAAPI* pfn_connect_t)(SOCKET, const struct sockaddr*, int);
typedef int (WSAAPI* pfn_WSAConnect_t)(SOCKET, const struct sockaddr*, int,
                                       LPWSABUF, LPWSABUF, LPQOS, LPQOS);
static pfn_connect_t    s_origConnect    = nullptr;
static pfn_WSAConnect_t s_origWSAConnect = nullptr;

static int WSAAPI My_connect(SOCKET s, const struct sockaddr* name, int namelen) {
    char ip[64];
    if (SockAddrToText(name, ip, sizeof(ip))) {
        if (name->sa_family == AF_INET) {
            const struct sockaddr_in* v4 = (const struct sockaddr_in*)name;
            const uint32_t dst   = (uint32_t)v4->sin_addr.s_addr;
            const unsigned short port = ntohs(v4->sin_port);

            // Rule 1: destination address is a known/observed report endpoint.
            bool divert = IpInBlockedRange(dst);

            // Rule 2 (CALLER ATTRIBUTION): the connection is coming FROM a
            // telemetry/anti-cheat module. This is what makes the interception
            // complete -- see the long note above CallerIsReporter. It does not
            // depend on knowing the address, so a brand-new endpoint is covered
            // the first time it is ever used.
            bool byCaller = false;
            if (!divert && fakesrv::Ready() && IsReportPort(port)) {
                char who[64];
                byCaller = CallerIsReporter(_ReturnAddress(), who, sizeof(who));
                if (byCaller) {
                    LOG(SBOX("[net] reporter caller %s -> diverting %s:%u", 0x35),
                        who, ip, (unsigned)port);
                }
            }

            if (divert || byCaller) {
                // STRATEGY: redirect to the local fake server rather than refuse.
                //
                // A refusal (WSAECONNREFUSED) is observable: the reporting module
                // can retry, spool the payload for later, or treat the failure as
                // a tamper signal. Redirecting lets the connection SUCCEED, so the
                // client completes a real TLS handshake against our responder,
                // uploads, and receives a plausible 200. It believes reporting
                // worked and discards the payload. The bytes never leave the box.
                //
                // If the responder is not ready we fall back to refusing: a clean
                // refusal is better than a half-open socket the client might
                // interpret as interception.
                if (fakesrv::Ready()) {
                    struct sockaddr_in local{};
                    local.sin_family      = AF_INET;
                    local.sin_port        = htons(fakesrv::LocalPort());
                    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    LogBlock(byCaller ? SBOX("[net] diverted (reporter caller)", 0x4F)
                                      : SBOX("[net] report diverted to local responder", 0x6F),
                             ip);
                    return s_origConnect
                               ? s_origConnect(s, (const struct sockaddr*)&local,
                                               (int)sizeof(local))
                               : SOCKET_ERROR;
                }
                LogBlock(obf::DecryptTls(kLit_connectRefused.d, sizeof(kLit_connectRefused.d), 0x19), ip);
                WSASetLastError(WSAECONNREFUSED);
                return SOCKET_ERROR;
            }
        }
        // Loopback and local traffic always passes (proxy at 127.0.0.1:7897).
    }
    return s_origConnect ? s_origConnect(s, name, namelen) : SOCKET_ERROR;
}

static int WSAAPI My_WSAConnect(SOCKET s, const struct sockaddr* name, int namelen,
                                LPWSABUF cdata, LPWSABUF sdata, LPQOS sqos, LPQOS gqos) {
    if (name && name->sa_family == AF_INET) {
        char ip[64];
        if (SockAddrToText(name, ip, sizeof(ip))) {
            const struct sockaddr_in* v4 = (const struct sockaddr_in*)name;
            if (IpInBlockedRange((uint32_t)v4->sin_addr.s_addr)) {
                if (fakesrv::Ready()) {
                    struct sockaddr_in local{};
                    local.sin_family      = AF_INET;
                    local.sin_port        = htons(fakesrv::LocalPort());
                    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    LogBlock(SBOX("[net] report diverted (WSAConnect)", 0x71), ip);
                    return s_origWSAConnect
                               ? s_origWSAConnect(s, (const struct sockaddr*)&local,
                                                  (int)sizeof(local), cdata, sdata,
                                                  sqos, gqos)
                               : SOCKET_ERROR;
                }
                LogBlock(obf::DecryptTls(kLit_wsaconnRefused.d, sizeof(kLit_wsaconnRefused.d), 0x2B), ip);
                WSASetLastError(WSAECONNREFUSED);
                return SOCKET_ERROR;
            }
        }
    }
    return s_origWSAConnect
               ? s_origWSAConnect(s, name, namelen, cdata, sdata, sqos, gqos)
               : SOCKET_ERROR;
}

// ---------------------------------------------------------------------------
// Socket creation. These are patched in webio.dll's DELAY-import table, whose
// original slot value is a delay-load stub rather than the real function -- so
// the detour cannot simply forward through a cached pointer. It must resolve
// ws2_32's real export itself.
//
// Note on intent: we do NOT refuse socket creation here. Blocking creation
// would break the game's own UDP gameplay traffic and the local proxy. The
// socket itself is benign; what matters is which address it later connects to,
// and that is enforced in My_connect / My_WSAConnect above. These detours exist
// so the delay-import path is instrumented and so a connect-time decision has a
// chance to run.
// ---------------------------------------------------------------------------
typedef SOCKET (WSAAPI* pfn_socket_t)(int, int, int);
typedef SOCKET (WSAAPI* pfn_WSASocketW_t)(int, int, int, LPWSAPROTOCOL_INFOW, GROUP, DWORD);
typedef SOCKET (WSAAPI* pfn_WSASocketA_t)(int, int, int, LPWSAPROTOCOL_INFOA, GROUP, DWORD);

static pfn_socket_t     s_origSocket     = nullptr;
static pfn_WSASocketW_t s_origWSASocketW = nullptr;
static pfn_WSASocketA_t s_origWSASocketA = nullptr;

// Resolve ws2_32 exports on demand (used for delay-imported call sites).
static void ResolveWs2Originals() {
    HMODULE ws2 = GetModuleHandleW(L"ws2_32.dll");
    if (!ws2) ws2 = LoadLibraryW(L"ws2_32.dll");
    if (!ws2) return;
    if (!s_origSocket)     s_origSocket     = (pfn_socket_t)GetProcAddress(ws2, "socket");
    if (!s_origWSASocketW) s_origWSASocketW = (pfn_WSASocketW_t)GetProcAddress(ws2, "WSASocketW");
    if (!s_origWSASocketA) s_origWSASocketA = (pfn_WSASocketA_t)GetProcAddress(ws2, "WSASocketA");
    if (!s_origConnect)    s_origConnect    = (pfn_connect_t)GetProcAddress(ws2, "connect");
    if (!s_origWSAConnect) s_origWSAConnect = (pfn_WSAConnect_t)GetProcAddress(ws2, "WSAConnect");
}

static SOCKET WSAAPI My_socket(int af, int type, int proto) {
    ResolveWs2Originals();
    return s_origSocket ? s_origSocket(af, type, proto) : INVALID_SOCKET;
}

static SOCKET WSAAPI My_WSASocketW(int af, int type, int proto,
                                   LPWSAPROTOCOL_INFOW info, GROUP g, DWORD flags) {
    ResolveWs2Originals();
    return s_origWSASocketW ? s_origWSASocketW(af, type, proto, info, g, flags) : INVALID_SOCKET;
}

static SOCKET WSAAPI My_WSASocketA(int af, int type, int proto,
                                   LPWSAPROTOCOL_INFOA info, GROUP g, DWORD flags) {
    ResolveWs2Originals();
    return s_origWSASocketA ? s_origWSASocketA(af, type, proto, info, g, flags) : INVALID_SOCKET;
}

// ---------------------------------------------------------------------------
// IAT patching (self-contained PEB walk -- no psapi, no extra imports)
// ---------------------------------------------------------------------------
namespace {

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
};

PebLdrData* GetPebLdr() {
    BYTE* peb = reinterpret_cast<BYTE*>(__readgsqword(0x60));
    BYTE* ldr = *reinterpret_cast<BYTE**>(peb + 0x18);
    return reinterpret_cast<PebLdrData*>(ldr);
}

bool BaseNameOf(const LdrEntry* e, char* out, size_t cap) {
    const wchar_t* s = e->BaseDllName.Buffer;
    const size_t n = e->BaseDllName.Length / sizeof(wchar_t);
    if (!s || n == 0 || n + 1 > cap)
        return false;
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<char>(s[i] & 0x7F);
    out[n] = 0;
    return true;
}

struct PatchDesc { const char* fn; void* detour; void** origOut; };

// Overflow-safe containment test. Some protected modules have malformed import
// directories (garbage descriptors), so every walk must be bounds-checked or
// the name comparisons fault.
bool InImage(const BYTE* base, SIZE_T size, const void* p, size_t need) {
    const BYTE* b = static_cast<const BYTE*>(p);
    if (b < base) return false;
    const SIZE_T off = static_cast<SIZE_T>(b - base);
    return off <= size && need <= size - off;
}

int PatchModuleIAT(HMODULE mod, const char* targetDll, const PatchDesc& d) {
    if (!mod) return 0;
    BYTE* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const LONG lfanew = dos->e_lfanew;
    if (lfanew <= 0 || lfanew > 0x1000 - static_cast<LONG>(sizeof(IMAGE_NT_HEADERS))) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    const DWORD impRva =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (impRva == 0) return 0;
    const SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;
    if (imageSize == 0 || impRva >= imageSize) return 0;
    if (!InImage(base, imageSize, base + impRva, sizeof(IMAGE_IMPORT_DESCRIPTOR))) return 0;

    auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + impRva);
    int patched = 0;
    for (int desc = 0; desc < 512 && imp->Name != 0; ++desc, ++imp) {
        if (!InImage(base, imageSize, imp, sizeof(IMAGE_IMPORT_DESCRIPTOR))) break;
        if (!InImage(base, imageSize, base + imp->Name, 2)) continue;
        const char* dllName = reinterpret_cast<const char*>(base + imp->Name);
        if (_stricmp(dllName, targetDll) != 0) continue;

        const DWORD thunkRva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
        if (thunkRva == 0 || thunkRva >= imageSize || imp->FirstThunk >= imageSize) continue;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + thunkRva);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (int slotIdx = 0; slotIdx < 4096 && thunk->u1.AddressOfData != 0;
             ++thunk, ++iat, ++slotIdx) {
            if (!InImage(base, imageSize, thunk, sizeof(IMAGE_THUNK_DATA)) ||
                !InImage(base, imageSize, iat, sizeof(IMAGE_THUNK_DATA)))
                break;
            if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            if (!InImage(base, imageSize, base + thunk->u1.AddressOfData, 2)) continue;
            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
            if (!InImage(base, imageSize, ibn->Name, 2)) continue;
            if (strcmp(reinterpret_cast<const char*>(ibn->Name), d.fn) != 0) continue;

            DWORD oldProt = 0;
            auto* slot = reinterpret_cast<void**>(&iat->u1.Function);
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) continue;
            void* was = *slot;
            if (d.origOut && *d.origOut == nullptr && was != d.detour)
                *d.origOut = was;
            *slot = d.detour;
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(void*), oldProt, &ignored);
            ++patched;
        }
    }
    return patched;
}

// ---------------------------------------------------------------------------
// DELAY-IMPORT patcher.
//
// This is required for complete coverage, not an optimisation: webio.dll -- the
// single convergence point for ALL WinHTTP traffic -- imports ws2_32 through
// the DELAY-import directory, not the normal import table. A plain import scan
// therefore finds nothing in it and the whole HTTP path stays open.
//
// Shape of the tables (per the PE spec):
//   ImgDelayDescr { grAttrs, rvaDLLName, rvaHmod, rvaIAT, rvaINT, rvaBoundIAT,
//                   rvaUnloadIAT, dwTimeStamp }   -- all RVAs, 32-bit
//   rvaINT / rvaIAT are both arrays of IMAGE_THUNK_DATA.
//
// grAttrs bit 0 (dlattrRva) must be set for the RVAs to be real RVAs; if a
// module uses the legacy VA form we skip it rather than risk a wild write.
// ---------------------------------------------------------------------------
struct ImgDelayDescr {
    DWORD grAttrs;
    DWORD rvaDLLName;
    DWORD rvaHmod;
    DWORD rvaIAT;
    DWORD rvaINT;
    DWORD rvaBoundIAT;
    DWORD rvaUnloadIAT;
    DWORD dwTimeStamp;
};
static constexpr DWORD kDlattrRva = 0x1;

// Forward declaration: the name-only wrapper below delegates to the
// ordinal-aware implementation.
int PatchModuleDelayIATOrd(HMODULE mod, const char* targetDll, const char* fn,
                           WORD ordinal, void* detour);

int PatchModuleDelayIAT(HMODULE mod, const char* targetDll, const char* fn, void* detour) {
    return PatchModuleDelayIATOrd(mod, targetDll, fn, 0, detour);
}

// Ordinal-aware variant. CRITICAL for webio.dll: it imports several ws2_32
// entries BY ORDINAL with no name at all --
//
//     ord#23 = socket      ord#6  = connect
//     ord#7  = getsockopt  ord#8  = htonl
//     ord#21 = setsockopt  ord#22 = getsockname
//
// A name-only matcher silently skips those, leaving the most important slots
// (socket, connect) open. When fn is null and ordinal != 0, we match the
// ordinal instead of the name.
int PatchModuleDelayIATOrd(HMODULE mod, const char* targetDll, const char* fn,
                           WORD ordinal, void* detour) {
    if (!mod) return 0;
    BYTE* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const LONG lfanew = dos->e_lfanew;
    if (lfanew <= 0 || lfanew > 0x1000 - static_cast<LONG>(sizeof(IMAGE_NT_HEADERS))) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    const DWORD dirRva =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    const DWORD dirSize =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size;
    if (dirRva == 0 || dirSize == 0) return 0;

    const SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;
    if (imageSize == 0 || dirRva >= imageSize) return 0;
    if (!InImage(base, imageSize, base + dirRva, sizeof(ImgDelayDescr))) return 0;

    auto* dd = reinterpret_cast<ImgDelayDescr*>(base + dirRva);
    int patched = 0;
    for (int i = 0; i < 64 && dd->rvaDLLName != 0; ++i, ++dd) {
        if (!InImage(base, imageSize, dd, sizeof(ImgDelayDescr))) break;
        if ((dd->grAttrs & kDlattrRva) == 0) continue;      // legacy VA form: skip
        if (!InImage(base, imageSize, base + dd->rvaDLLName, 2)) continue;
        const char* dllName = reinterpret_cast<const char*>(base + dd->rvaDLLName);
        if (_stricmp(dllName, targetDll) != 0) continue;

        const DWORD intRva = dd->rvaINT ? dd->rvaINT : dd->rvaIAT;
        if (intRva == 0 || intRva >= imageSize || dd->rvaIAT >= imageSize) continue;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + intRva);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + dd->rvaIAT);

        for (int slotIdx = 0; slotIdx < 4096 && thunk->u1.AddressOfData != 0;
             ++thunk, ++iat, ++slotIdx) {
            if (!InImage(base, imageSize, thunk, sizeof(IMAGE_THUNK_DATA)) ||
                !InImage(base, imageSize, iat, sizeof(IMAGE_THUNK_DATA)))
                break;

            bool match = false;
            if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                // ordinal import: only matchable when the caller supplied one
                if (ordinal != 0 &&
                    (WORD)(thunk->u1.Ordinal & 0xFFFF) == ordinal)
                    match = true;
            } else if (fn != nullptr) {
                if (!InImage(base, imageSize, base + thunk->u1.AddressOfData, 2)) continue;
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
                if (!InImage(base, imageSize, ibn->Name, 2)) continue;
                if (strcmp(reinterpret_cast<const char*>(ibn->Name), fn) == 0)
                    match = true;
            }
            if (!match) continue;

            DWORD oldProt = 0;
            auto* slot = reinterpret_cast<void**>(&iat->u1.Function);
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) continue;
            *slot = detour;                 // the saved value is the delay stub
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(void*), oldProt, &ignored);
            ++patched;
        }
    }
    return patched;
}

// Patch a delay import across every delay-imported DLL in the module.
// fn == nullptr && ordinal != 0  ->  match by ordinal instead of by name.
int PatchModuleDelayIATAnyDllOrdImpl(HMODULE mod, const char* fn, WORD ordinal,
                                     void* detour) {
    if (!mod) return 0;
    BYTE* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const LONG lfanew = dos->e_lfanew;
    if (lfanew <= 0 || lfanew > 0x1000 - static_cast<LONG>(sizeof(IMAGE_NT_HEADERS))) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    const DWORD dirRva =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    if (dirRva == 0) return 0;
    const SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;
    if (imageSize == 0 || dirRva >= imageSize) return 0;
    if (!InImage(base, imageSize, base + dirRva, sizeof(ImgDelayDescr))) return 0;

    auto* dd = reinterpret_cast<ImgDelayDescr*>(base + dirRva);
    int patched = 0;
    for (int i = 0; i < 64 && dd->rvaDLLName != 0; ++i, ++dd) {
        if (!InImage(base, imageSize, dd, sizeof(ImgDelayDescr))) break;
        if ((dd->grAttrs & kDlattrRva) == 0) continue;
        if (!InImage(base, imageSize, base + dd->rvaDLLName, 2)) continue;
        const char* dllName = reinterpret_cast<const char*>(base + dd->rvaDLLName);
        patched += PatchModuleDelayIATOrd(mod, dllName, fn, ordinal, detour);
    }
    return patched;
}

int PatchModuleDelayIATAnyDll(HMODULE mod, const char* fn, void* detour) {
    return PatchModuleDelayIATAnyDllOrdImpl(mod, fn, 0, detour);
}

int PatchModuleDelayIATAnyDllOrd(HMODULE mod, WORD ordinal, void* detour) {
    return PatchModuleDelayIATAnyDllOrdImpl(mod, nullptr, ordinal, detour);
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------
void InstallDnsBlackholePhase(const char* phase) {
    if (phase == nullptr) phase = "init";

    static volatile LONG s_lock = 0;
    while (InterlockedCompareExchange(&s_lock, 1, 0) != 0)
        YieldProcessor();

    HMODULE ws2 = GetModuleHandleW(L"ws2_32.dll");
    if (!ws2) ws2 = LoadLibraryW(L"ws2_32.dll");
    if (!ws2) {
        LOG(SBOX("[net] %s: ws2_32.dll unavailable, blackhole inactive", 0x6B), phase);
        InterlockedExchange(&s_lock, 0);
        return;
    }
    s_origGetAddrInfo  = reinterpret_cast<pfn_getaddrinfo_t>(GetProcAddress(ws2, "getaddrinfo"));
    s_origGetAddrInfoW = reinterpret_cast<pfn_GetAddrInfoW_t>(GetProcAddress(ws2, "GetAddrInfoW"));
    s_origGetHostByName = reinterpret_cast<pfn_gethostbyname_t>(GetProcAddress(ws2, "gethostbyname"));
    s_origGetAddrInfoExW = reinterpret_cast<pfn_GetAddrInfoExW_t>(GetProcAddress(ws2, "GetAddrInfoExW"));
    s_origWSAAsyncGetHostByName = reinterpret_cast<pfn_WSAAsyncGetHostByName_t>(
        GetProcAddress(ws2, "WSAAsyncGetHostByName"));
    s_origGetNameInfo = reinterpret_cast<pfn_getnameinfo_t>(GetProcAddress(ws2, "getnameinfo"));
    s_origWSALookupBegin = reinterpret_cast<pfn_WSALookupServiceBeginW_t>(
        GetProcAddress(ws2, "WSALookupServiceBeginW"));
    s_origWSALookupNext = reinterpret_cast<pfn_WSALookupServiceNextW_t>(
        GetProcAddress(ws2, "WSALookupServiceNextW"));

    // WinHTTP / WinINet live in their own modules.
    HMODULE wh = GetModuleHandleW(L"winhttp.dll");
    if (!wh) wh = LoadLibraryW(L"winhttp.dll");
    if (wh) {
        s_origWinHttpConnect =
            reinterpret_cast<pfn_WinHttpConnect_t>(GetProcAddress(wh, "WinHttpConnect"));
        s_origWinHttpSendRequest =
            reinterpret_cast<pfn_WinHttpSendRequest_t>(GetProcAddress(wh, "WinHttpSendRequest"));
    }

    HMODULE wi = GetModuleHandleW(L"wininet.dll");
    if (!wi) wi = LoadLibraryW(L"wininet.dll");
    if (wi) s_origInternetConnectW =
        reinterpret_cast<pfn_InternetConnectW_t>(GetProcAddress(wi, "InternetConnectW"));

    // L5 originals (connect / WSAConnect / socket / WSASocket*) -- needed both
    // for the static IAT targets below and for the delay-import detours.
    ResolveWs2Originals();

    // Learn the report servers' current addresses BEFORE any connect hook can
    // see traffic. This is what closes the IP-literal bypass: ACE receives the
    // same A records from the same resolver, so its cached IPs are these.
    LearnBlockedIps();

    // NOTE: the target DLL name matters -- PatchModuleIAT matches the module's
    // import-descriptor name, so a ws2_32 function must be patched in the
    // ws2_32 descriptor and a winhttp function in winhttp's.
    struct Target {
        const char* dll;        // which import descriptor to look in
        const char* fn;
        void*       detour;
        void**      origOut;
    };
    const Target kTargets[] = {
        // --- ws2_32: every name-resolution entry point ---
        {"ws2_32.dll", "getaddrinfo",           (void*)&My_getaddrinfo,           (void**)&s_origGetAddrInfo},
        {"ws2_32.dll", "GetAddrInfoW",          (void*)&My_GetAddrInfoW,          (void**)&s_origGetAddrInfoW},
        {"ws2_32.dll", "gethostbyname",         (void*)&My_gethostbyname,         (void**)&s_origGetHostByName},
        {"ws2_32.dll", "GetAddrInfoExW",        (void*)&My_GetAddrInfoExW,        (void**)&s_origGetAddrInfoExW},
        {"ws2_32.dll", "WSAAsyncGetHostByName", (void*)&My_WSAAsyncGetHostByName, (void**)&s_origWSAAsyncGetHostByName},
        {"ws2_32.dll", "getnameinfo",           (void*)&My_getnameinfo,           (void**)&s_origGetNameInfo},
        {"ws2_32.dll", "WSALookupServiceBeginW",(void*)&My_WSALookupServiceBeginW,(void**)&s_origWSALookupBegin},
        {"ws2_32.dll", "WSALookupServiceNextW", (void*)&My_WSALookupServiceNextW, (void**)&s_origWSALookupNext},
        // --- winhttp: dynamic callers bypass their own IAT, so patch here too ---
        {"winhttp.dll", "WinHttpConnect",       (void*)&My_WinHttpConnect,        (void**)&s_origWinHttpConnect},
        // the only WinHTTP call that actually transmits
        {"winhttp.dll", "WinHttpSendRequest",   (void*)&My_WinHttpSendRequest,    (void**)&s_origWinHttpSendRequest},
        // --- wininet ---
        {"wininet.dll", "InternetConnectW",     (void*)&My_InternetConnectW,      (void**)&s_origInternetConnectW},
        // --- L5 transport: the only layer an IP literal cannot bypass ---
        {"ws2_32.dll",  "connect",              (void*)&My_connect,               (void**)&s_origConnect},
        {"ws2_32.dll",  "WSAConnect",           (void*)&My_WSAConnect,            (void**)&s_origWSAConnect},
    };

    PebLdrData* ldr = GetPebLdr();
    if (ldr == nullptr) {
        LOG(SBOX("[net] %s: PEB walk unavailable, blackhole inactive", 0x41), phase);
        InterlockedExchange(&s_lock, 0);
        return;
    }

    int modulesSeen = 0, modulesPatched = 0, slotsPatched = 0, skippedAce = 0;
    LIST_ENTRY* head = &ldr->InLoadOrderModuleList;
    for (LIST_ENTRY* it = head->Flink; it != head; it = it->Flink) {
        auto* e = reinterpret_cast<LdrEntry*>(it);
        char baseName[MAX_PATH] = {0};
        if (!BaseNameOf(e, baseName, sizeof(baseName))) continue;
        ++modulesSeen;

        // Log every candidate so the whitelist can be verified from the trace
        // instead of guessed. Only names that look interesting are logged to
        // keep the file small.
        //
        // P1: the interesting-name test uses the ENCRYPTED signature lists rather
        // than inline literals, so no module name appears in .rdata. The
        // MatchesSigList call covers crashsight/perfsight/tdatamaster/msdk/gcloud/
        // qqopensdk/calabiyau in one shot.
        if (ContainsCI(baseName, "ace") || ContainsCI(baseName, "anti") ||
            ContainsCI(baseName, "tencent") || ContainsCI(baseName, "qq") ||
            ContainsCI(baseName, "winhttp") || ContainsCI(baseName, "wininet") ||
            ace::MatchesSigList(ace::kWhitelistPatterns, ace::kWhitelistCount, baseName)) {
            LOG(SBOX("[net] %s candidate module: %s%s", 0x73), phase, baseName,
                IsPatchAllowed(baseName) ? "  [whitelisted]" : "");
        }

        if (InSigList(kNeverPatch, kNeverPatchCount, baseName)) { ++skippedAce; continue; }
        if (!IsPatchAllowed(baseName)) continue;

        auto* mod = reinterpret_cast<HMODULE>(e->DllBase);
        int here = 0;
        for (const Target& d : kTargets) {
            PatchDesc pd{d.fn, d.detour, d.origOut};
            here += PatchModuleIAT(mod, d.dll, pd);
        }

        // Delay-import pass. REQUIRED for webio.dll, the single convergence
        // point for all WinHTTP traffic (caller -> winhttp -> webio -> ws2_32).
        // webio reaches ws2_32 ONLY through the delay-import directory, and it
        // imports socket / connect / getsockopt / setsockopt BY ORDINAL with no
        // name at all, so both the name and the ordinal forms must be patched.
        // ws2_32 ordinals used by webio: 23=socket, 6=connect, 7=getsockopt,
        // 8=htonl, 21=setsockopt, 22=getsockname.
        int dhere = 0;
        dhere += PatchModuleDelayIATAnyDll(mod, "socket",      (void*)&My_socket);
        dhere += PatchModuleDelayIATAnyDll(mod, "WSASocketW",  (void*)&My_WSASocketW);
        dhere += PatchModuleDelayIATAnyDll(mod, "WSASocketA",  (void*)&My_WSASocketA);
        dhere += PatchModuleDelayIATAnyDllOrd(mod, 23,         (void*)&My_socket);      // socket
        dhere += PatchModuleDelayIATAnyDllOrd(mod, 6,          (void*)&My_connect);     // connect
        here += dhere;

        if (here > 0) {
            ++modulesPatched;
            slotsPatched += here;
            LOG(SBOX("[net] %s patched %s: %d slot(s)%s", 0x1F), phase, baseName, here,
                dhere ? " (incl. delay-import)" : "");
        }
    }

    s_modulesScanned = modulesSeen;
    s_patchedModules = modulesPatched;
    s_patchedSlots = slotsPatched;

    LOG(SBOX("[net] %s done: %d domain(s), modules_scanned=%d "
             "modules_patched=%d slots=%d ace_skipped=%d (IAT mode, .text untouched)",
             0x55),
        phase, kBlocklistCount, modulesSeen, modulesPatched, slotsPatched, skippedAce);

    InterlockedExchange(&s_lock, 0);
}

void InstallDnsBlackholeEarly()    { InstallDnsBlackholePhase("early"); }
void InstallDnsBlackhole()         { InstallDnsBlackholePhase("init"); }
void InstallDnsBlackholePeriodic() { InstallDnsBlackholePhase("periodic"); }

} // namespace ace

// ---------------------------------------------------------------------------
// Exported helper for fake_trust.cpp (the certificate-trust bypass).
//
// That module needs to patch crypt32's import sites across consumer modules,
// which is exactly the machinery this file already owns: the PEB module walk
// plus the IsPatchAllowed / LooksLikeAntiCheat safety rules. Duplicating the
// walk would risk a second implementation drifting out of sync with those rules,
// so we expose one narrow entry point instead.
//
// crypt32.dll itself is never a target: it is the PROVIDER. Patching its own
// internal calls would affect unrelated crypto process-wide, and it is precisely
// the kind of thing that gets noticed. Only consumers' import tables are touched.
// ---------------------------------------------------------------------------
extern "C" void FakeTrust_PatchCrypt32(const char* fn, void* detour, void** origOut) {
    if (!fn || !detour) return;

    ace::PebLdrData* ldr = ace::GetPebLdr();
    if (!ldr) return;

    int patched = 0;
    LIST_ENTRY* head = &ldr->InLoadOrderModuleList;
    for (LIST_ENTRY* it = head->Flink; it != head; it = it->Flink) {
        auto* e = reinterpret_cast<ace::LdrEntry*>(it);
        char baseName[MAX_PATH] = {0};
        if (!ace::BaseNameOf(e, baseName, sizeof(baseName))) continue;

        // Same exclusions as the main pass, plus crypt32 itself.
        if (ace::InSigList(ace::kNeverPatch, ace::kNeverPatchCount, baseName)) continue;
        if (ace::ContainsCI(baseName, "crypt32")) continue;
        if (!ace::IsPatchAllowed(baseName)) continue;

        auto* mod = reinterpret_cast<HMODULE>(e->DllBase);
        ace::PatchDesc pd{fn, detour, origOut};
        patched += ace::PatchModuleIAT(mod, "crypt32.dll", pd);
    }
    if (patched)
        LOG(SBOX("[fake] crypt32 %s patched in %d slot(s)", 0x39), fn, patched);
}
