// D:\Work\Fut\cheat\src\fake_trust.h
// ---------------------------------------------------------------------------
// Certificate-trust bypass for the fake report server.
//
// THE PROBLEM
// ---------------------------------------------------------------------------
// Completing a TLS handshake is only half the job. The reporting client then
// validates our self-signed certificate, and by default that fails. The failure
// happens in one of two places depending on how the module was built:
//
//   CrashSight64.dll / PerfSight.dll
//       link OpenSSL statically and import crypt32 (CertOpenStore,
//       CertEnumCertificatesInStore, CertFindCertificateInStore,
//       CertGetCertificateContextProperty). Those crypt32 calls are how they
//       obtain the trust anchors they feed to OpenSSL, so hooking crypt32 is
//       sufficient and no OpenSSL internals need touching.
//
//   TDataMaster.dll
//       uses the game's libcurl.dll -> libssl-1_1.dll /
//       libcrypto-1_1.dll (separate DLLs, so hookable by name).
//
// We cover the crypt32 route (shared by the two modules that actually talk to
// crash/perf endpoints) plus the OpenSSL verify callback when libcrypto is a
// real DLL. Vendored/static OpenSSL inside CrashSight cannot be hooked by name;
// for that case the crypt32 anchor injection is the lever.
//
// HOW
// ---------------------------------------------------------------------------
// Rather than patching "does this chain verify" to always-true (which is a very
// well-known cheat signature and would also blind the client to genuine
// problems), we ADD our certificate to the store the client reads from. The
// client then performs an entirely normal verification and legitimately
// succeeds. There is no lie in the verification path.
//
// Concretely we hook:
//   CertOpenStore               -> open the real store, then add our cert
//   CertEnumCertificatesInStore -> let the original run; if it is about to
//                                  return "no more", hand out our cert once
//
// The second hook exists because a caller that snapshots the store up-front and
// then never re-opens it would miss an insertion that happened later.
//
// HONEST LIMITS
// ---------------------------------------------------------------------------
//   * If a module pins the server public key (libcurl supports
//     CURLOPT_PINNEDPUBLICKEY, and the string "public key does not match pinned
//     public key" is present in ACE-Base64.dll), trust-store injection does not
//     help: the pin is checked against the leaf key, not the chain. We have NOT
//     verified whether ACE actually pins. If it does, the fake server cannot
//     succeed for that path.
//   * A module that verifies with a compiled-in trust anchor table (rather than
//     reading the Windows store at runtime) is likewise unaffected.
#pragma once

namespace fakesrv {

// Installs the trust hooks. Requires the fake server's certificate to exist
// (it is compiled in via fake_cert.h). Safe to call once.
void InstallTrustHooks();

// True when the hooks are installed.
bool TrustHooksReady();

} // namespace fakesrv
