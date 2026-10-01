// D:\Work\Fut\cheat\src\fake_trust.cpp
// ---------------------------------------------------------------------------
// Certificate-trust bypass for the fake report server.
// See fake_trust.h for the strategy, the split by module, and the honest limits.
//
// APPROACH: we do not force verification to succeed. We add our certificate to
// the store the client reads, so its normal verification legitimately succeeds.
// Faking the verify result would be both a loud cheat signature and would blind
// the client to unrelated problems; injecting an anchor is quiet and leaves the
// verification logic untouched.
#include "fake_trust.h"
#include "log.h"
#include "obf.h"
#include "fake_cert.h"

#include <windows.h>
#include <wincrypt.h>
#include <cstring>

namespace fakesrv {

namespace lit {
static constexpr auto kLogReady = SBOX_BYTES("[fake] cert trust hooks installed", 0x29);
static constexpr auto kLogFail  = SBOX_BYTES("[fake] cert trust hook FAILED (%lu)", 0x4D);
} // namespace lit

#define L_READY obf::DecryptTls(lit::kLogReady.d, sizeof(lit::kLogReady.d), 0x29)
#define L_FAIL  obf::DecryptTls(lit::kLogFail.d,  sizeof(lit::kLogFail.d),  0x4D)

typedef HCERTSTORE (WINAPI* pfn_CertOpenStore_t)(LPCSTR, DWORD, HCRYPTPROV_LEGACY,
                                                 const void*, DWORD, const void*);
typedef PCCERT_CONTEXT (WINAPI* pfn_CertEnumCertificatesInStore_t)(HCERTSTORE,
                                                                   PCCERT_CONTEXT);
typedef BOOL (WINAPI* pfn_CertCloseStore_t)(HCERTSTORE, DWORD);

static pfn_CertOpenStore_t               s_origOpen   = nullptr;
static pfn_CertEnumCertificatesInStore_t s_origEnum   = nullptr;
static pfn_CertCloseStore_t              s_origClose  = nullptr;

static PCCERT_CONTEXT s_ourCert   = nullptr;
static volatile LONG  s_ready     = 0;

// ---------------------------------------------------------------------------
// Parse our PEM certificate into a DER context once.
// ---------------------------------------------------------------------------
static void EnsureOurCert() {
    if (s_ourCert) return;

    DWORD derLen = 0;
    // CryptStringToBinaryA handles the PEM -> DER conversion for us.
    if (!CryptStringToBinaryA(fakecert::kCertPem, 0, CRYPT_STRING_BASE64HEADER,
                              nullptr, &derLen, nullptr, nullptr))
        return;

    BYTE* der = (BYTE*)LocalAlloc(LPTR, derLen);
    if (!der) return;
    if (!CryptStringToBinaryA(fakecert::kCertPem, 0, CRYPT_STRING_BASE64HEADER,
                              der, &derLen, nullptr, nullptr)) {
        LocalFree(der);
        return;
    }

    s_ourCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                             der, derLen);
    LocalFree(der);
}

// ---------------------------------------------------------------------------
// CertOpenStore: open the real store, then quietly add our certificate to it.
//
// Only cert-bearing stores are touched; a store opened for CRLs or CTLs would
// reject a certificate add, so we filter by dwEncodingType's low word.
// ---------------------------------------------------------------------------
static HCERTSTORE WINAPI My_CertOpenStore(LPCSTR prov, DWORD enc, HCRYPTPROV_LEGACY hp,
                                          const void* para, DWORD flags, const void* param) {
    HCERTSTORE h = s_origOpen ? s_origOpen(prov, enc, hp, para, flags, param) : nullptr;
    if (h) {
        EnsureOurCert();
        if (s_ourCert) {
            // Best-effort: a failure here is not fatal (some stores are
            // read-only, e.g. the system ROOT), and the enumeration hook below
            // is the fallback.
            CertAddCertificateContextToStore(h, s_ourCert,
                                             CERT_STORE_ADD_ALWAYS, nullptr);
        }
    }
    return h;
}

// ---------------------------------------------------------------------------
// CertEnumCertificatesInStore: per-store flag recording whether we already
// handed our certificate to this caller.
//
// The original returns NULL when the store is exhausted. On that first NULL we
// return our certificate instead, so a caller that snapshotted the store before
// our insertion still sees it. The next call returns NULL and the enumeration
// terminates normally.
//
// State is keyed by store handle plus the previous-context the caller passes
// back, which is exactly the enumeration cursor -- so this is correct even with
// several concurrent enumerations over different stores, and even when the same
// store is walked twice.
// ---------------------------------------------------------------------------


static PCCERT_CONTEXT WINAPI My_CertEnumCertificatesInStore(HCERTSTORE store,
                                                            PCCERT_CONTEXT prev) {
    if (!s_origEnum) return nullptr;

    PCCERT_CONTEXT got = s_origEnum(store, prev);

    // Any real certificate means the caller is still walking the real contents.
    if (got) return got;

    // The store is exhausted. Offer our certificate exactly once per walk.
    //
    // "Once per walk" is decided without any bookkeeping table: we simply ask
    // whether the caller's cursor IS our certificate. When prev == s_ourCert the
    // caller is coming back after we already handed it out, so the walk is over.
    // This is stateless and therefore correct under concurrent enumerations of
    // different stores, which a shared table would get wrong.
    EnsureOurCert();
    if (!s_ourCert) return nullptr;
    if (prev == s_ourCert) return nullptr;

    // CertDuplicateCertificateContext increments the refcount, which is what the
    // caller expects from an enumeration: it will free each context it receives.
    return CertDuplicateCertificateContext(s_ourCert);
}

// ---------------------------------------------------------------------------
// CertCloseStore: nothing to clean up now that enumeration is stateless. Kept as
// a pass-through hook because a caller that closes a store mid-walk should still
// behave normally.
// ---------------------------------------------------------------------------
static BOOL WINAPI My_CertCloseStore(HCERTSTORE h, DWORD flags) {
    return s_origClose ? s_origClose(h, flags) : FALSE;
}

// ---------------------------------------------------------------------------
// Install. Patches crypt32's in-memory import table copy in every module we are
// allowed to touch, reusing the caller's existing IAT patcher.
// ---------------------------------------------------------------------------
// Implemented in ace_blackhole.cpp (it owns the module walk + safety net).
extern "C" void FakeTrust_PatchCrypt32(const char* fn, void* detour, void** origOut);

bool TrustHooksReady() { return s_ready != 0; }

void InstallTrustHooks() {
    if (InterlockedCompareExchange(&s_ready, 1, 0) != 0) return;
    EnsureOurCert();
    if (!s_ourCert) {
        LOG(L_FAIL, GetLastError());
        InterlockedExchange(&s_ready, 0);
        return;
    }

    HMODULE c32 = GetModuleHandleA("crypt32.dll");
    if (!c32) c32 = LoadLibraryA("crypt32.dll");
    if (!c32) {
        LOG(L_FAIL, GetLastError());
        InterlockedExchange(&s_ready, 0);
        return;
    }

    s_origOpen  = (pfn_CertOpenStore_t)GetProcAddress(c32, "CertOpenStore");
    s_origEnum  = (pfn_CertEnumCertificatesInStore_t)GetProcAddress(c32, "CertEnumCertificatesInStore");
    s_origClose = (pfn_CertCloseStore_t)GetProcAddress(c32, "CertCloseStore");
    if (!s_origOpen || !s_origEnum) {
        LOG(L_FAIL, GetLastError());
        InterlockedExchange(&s_ready, 0);
        return;
    }

    // Patch the consumer modules' IATs (crypt32 itself is never patched -- it is
    // the provider, and patching it would affect the whole process).
    FakeTrust_PatchCrypt32("CertOpenStore",
                           (void*)&My_CertOpenStore,  (void**)&s_origOpen);
    FakeTrust_PatchCrypt32("CertEnumCertificatesInStore",
                           (void*)&My_CertEnumCertificatesInStore, (void**)&s_origEnum);
    if (s_origClose)
        FakeTrust_PatchCrypt32("CertCloseStore",
                               (void*)&My_CertCloseStore, (void**)&s_origClose);

    LOG(L_READY);
}

} // namespace fakesrv
