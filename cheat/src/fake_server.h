// D:\Work\Fut\cheat\src\fake_server.h
// ---------------------------------------------------------------------------
// Fake report server (ACE "black hole" = accept + plausible success response).
//
// STRATEGY -- this replaces pure blocking
// ---------------------------------------------------------------------------
// The previous implementation refused connections (WSAECONNREFUSED /
// ERROR_WINHTTP_CANNOT_CONNECT). ACE can SEE a refusal, so it knows the upload
// failed and is free to retry, buffer the payload for later, or treat the
// failure itself as a tamper signal.
//
// What we want instead: the client completes its upload and receives a
// well-formed success response, so it believes reporting succeeded and discards
// the payload. That is a strictly better outcome than a refusal.
//
// WHY THIS NEEDS REAL TLS
// ---------------------------------------------------------------------------
// The three reporting modules do NOT share a network stack (measured):
//
//   TDataMaster.dll   -> libcurl.dll (a real DLL, 74 exports) + libssl-1_1
//   CrashSight64.dll  -> ws2_32 + STATIC OpenSSL (5.6 MB image, no libcurl)
//   PerfSight.dll     -> same as CrashSight
//
// CrashSight and PerfSight link OpenSSL into themselves, so they call
// ws2_32!connect / send / recv directly and do the TLS handshake in-process.
// A plaintext HTTP responder would therefore be rejected at the handshake --
// the client would see a protocol error, which is just a different failure.
//
// So the fake server must genuinely terminate TLS. We do that with the game's
// OWN libssl-1_1.dll (verified: it exports SSL_CTX_new, TLS_server_method,
// SSL_accept, SSL_read, SSL_write, SSL_CTX_use_certificate, ...), loaded at
// runtime. No new dependency, no size cost, and the module is already present
// in the process so loading it is unremarkable.
//
// VERIFICATION IS BYPASSED SEPARATELY
// ---------------------------------------------------------------------------
// Completing a handshake is not enough: the client then validates the chain.
// CrashSight imports CertOpenStore / CertEnumCertificatesInStore (crypt32), so
// the standard Windows trust path is available to it. We therefore also hook
// crypt32's chain verification to accept our certificate. See
// fake_server.cpp:InstallCertTrustHook().
//
// ARCHITECTURE
// ---------------------------------------------------------------------------
// Rather than redirect the client to a different address (which would need a
// connect() redirect plus a listening socket, and would still face the TLS
// problem), we take the socket over IN PLACE:
//
//   connect() to a blocked report server
//     -> we create a LOCAL socketpair instead of dialling out
//     -> the client's fd keeps working for send/recv
//     -> a worker thread runs the TLS server side on the other end
//     -> the worker returns a canned 200 + JSON body, then closes
//
// The payload never leaves the machine, the client sees success, and there is no
// listening port for ACE to notice.
//
// HONEST LIMITS
// ---------------------------------------------------------------------------
//   * The response body must be structurally plausible. ACE parses JSON
//     (serde strings present in ACE-Base64.dll), so a body of pure garbage may
//     itself be flagged. We return a generic ack envelope; see
//     BuildAckBody() for the reasoning and its limits.
//   * This cannot fake a response the client authenticates (signed ack). If the
//     protocol requires a server signature over a client nonce, this yields a
//     well-formed-but-unsigned ack, which is better than a refusal but not
//     necessarily indistinguishable. Not verified either way.
//   * The socket handover changes the fd's peer from a real socket to a local
//     one. Code that later inspects getpeername() will see loopback. CrashSight
//     imports getpeername, so this is a real (if minor) observable.
#pragma once

namespace fakesrv {

// Starts the TLS fake responder. Call once, after the log is open.
// Safe to call repeatedly (later calls are no-ops).
void Install();

// Number of report uploads answered with a fake success so far.
unsigned ServedCount();

// Local port the TLS responder is listening on (0 when not ready).
// Used by connect()/WSAConnect() to redirect blocked destinations here instead
// of refusing them.
unsigned short LocalPort();

// True when libssl was resolved and the TLS context is ready. When false, the
// caller should fall back to blocking (a refused connection beats a broken
// handshake that the client might treat as tampering).
bool Ready();

} // namespace fakesrv
