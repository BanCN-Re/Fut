// D:\Work\Fut\cheat\src\fake_server.cpp
// ---------------------------------------------------------------------------
// Fake report server: accept the upload, answer with a plausible success.
//
// See fake_server.h for the strategy (this replaces pure blocking) and the
// limits. This file is the implementation.
//
// MECHANISM, in one paragraph
// ---------------------------------------------------------------------------
// We run a TLS listener on 127.0.0.1:<ephemeral>. When the caller connects to a
// blocklisted report host, My_connect() rewrites the sockaddr to our listener
// instead of refusing. connect() then SUCCEEDS, the client performs a real TLS
// handshake against us, sends its report, and receives a canned HTTP 200 with a
// JSON ack. The client believes it reported successfully; the payload never
// leaves the machine.
//
// Why rewriting beats refusing: a refusal is observable. CrashSight/TDataMaster
// can retry, spool the payload for a later attempt, or treat the failure itself
// as evidence of tampering. A success response ends the transaction.
//
// Why not a plaintext responder: CrashSight64.dll and PerfSight.dll link OpenSSL
// statically and drive ws2_32 directly, so they perform the TLS handshake
// in-process. A plaintext HTTP server would fail the handshake, which is just a
// different kind of failure. The game already ships libssl-1_1.dll and
// libcrypto-1_1.dll, so we bind those at runtime rather than growing our image.
#include "fake_server.h"
#include "log.h"
#include "obf.h"
#include "fake_cert.h"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>   // strtol

namespace fakesrv {

// ---------------------------------------------------------------------------
// Own literals, obfuscated (P1). Bound to named constexpr blobs -- an inline
// SBOX_BYTES temporary gets constant-folded into plaintext in .rdata.
// ---------------------------------------------------------------------------
namespace lit {

static constexpr auto kLibSsl    = SBOX_BYTES("libssl-1_1.dll", 0x2E);
static constexpr auto kLibCrypto = SBOX_BYTES("libcrypto-1_1.dll", 0x6A);
static constexpr auto kLogUp     = SBOX_BYTES("[fake] TLS responder ready on 127.0.0.1:%u", 0x41);
static constexpr auto kLogNoSsl  = SBOX_BYTES("[fake] libssl unavailable -- falling back to refuse", 0x17);
static constexpr auto kLogServed = SBOX_BYTES("[fake] served upload #%u to %s (%u bytes in)", 0x55);
static constexpr auto kLogAccept = SBOX_BYTES("[fake] TLS accept failed (%d)", 0x33);

} // namespace lit

#define LIT(b)  obf::DecryptTls((b).d, sizeof((b).d), (b##_KEY))
// The key must match the SBOX_BYTES call, so spell each out.
#define L_LIBSSL   obf::DecryptTls(lit::kLibSsl.d,    sizeof(lit::kLibSsl.d),    0x2E)
#define L_LIBCRYP  obf::DecryptTls(lit::kLibCrypto.d, sizeof(lit::kLibCrypto.d), 0x6A)
#define L_LOGUP    obf::DecryptTls(lit::kLogUp.d,     sizeof(lit::kLogUp.d),     0x41)
#define L_LOGNO    obf::DecryptTls(lit::kLogNoSsl.d,  sizeof(lit::kLogNoSsl.d),  0x17)
#define L_SERVED   obf::DecryptTls(lit::kLogServed.d, sizeof(lit::kLogServed.d), 0x55)
#define L_ACCEPT   obf::DecryptTls(lit::kLogAccept.d, sizeof(lit::kLogAccept.d), 0x33)

// ---------------------------------------------------------------------------
// libssl / libcrypto subset we actually need.
// ---------------------------------------------------------------------------
typedef struct ssl_ctx_st    SSL_CTX;
typedef struct ssl_st        SSL;
typedef struct ssl_method_st SSL_METHOD;
typedef struct x509_st       X509;
typedef struct evp_pkey_st   EVP_PKEY;
typedef struct bio_st        BIO;

typedef SSL_CTX*   (*pfn_SSL_CTX_new)(const SSL_METHOD*);
typedef void       (*pfn_SSL_CTX_free)(SSL_CTX*);
typedef const SSL_METHOD* (*pfn_TLS_server_method)(void);
typedef int        (*pfn_SSL_CTX_use_certificate)(SSL_CTX*, X509*);
typedef int        (*pfn_SSL_CTX_use_PrivateKey)(SSL_CTX*, EVP_PKEY*);
typedef SSL*       (*pfn_SSL_new)(SSL_CTX*);
typedef void       (*pfn_SSL_free)(SSL*);
typedef int        (*pfn_SSL_set_fd)(SSL*, int);
typedef int        (*pfn_SSL_accept)(SSL*);
typedef int        (*pfn_SSL_read)(SSL*, void*, int);
typedef int        (*pfn_SSL_write)(SSL*, const void*, int);
typedef int        (*pfn_SSL_shutdown)(SSL*);
typedef int        (*pfn_SSL_get_error)(const SSL*, int);

typedef BIO*       (*pfn_BIO_new_mem_buf)(const void*, int);
typedef void       (*pfn_BIO_free)(BIO*);
typedef X509*      (*pfn_PEM_read_bio_X509)(BIO*, X509**, void*, void*);
typedef EVP_PKEY*  (*pfn_PEM_read_bio_PrivateKey)(BIO*, EVP_PKEY**, void*, void*);
typedef void       (*pfn_X509_free)(X509*);
typedef void       (*pfn_EVP_PKEY_free)(EVP_PKEY*);

static struct {
    HMODULE ssl = nullptr, crypto = nullptr;
    pfn_SSL_CTX_new                CTX_new;
    pfn_SSL_CTX_free               CTX_free;
    pfn_TLS_server_method          server_method;
    pfn_SSL_CTX_use_certificate    use_cert;
    pfn_SSL_CTX_use_PrivateKey     use_key;
    pfn_SSL_new                    New;
    pfn_SSL_free                   Free;
    pfn_SSL_set_fd                 set_fd;
    pfn_SSL_accept                 accept;
    pfn_SSL_read                   read;
    pfn_SSL_write                  write;
    pfn_SSL_shutdown               shutdown;
    pfn_SSL_get_error              get_error;
    pfn_BIO_new_mem_buf            bio_mem;
    pfn_BIO_free                   bio_free;
    pfn_PEM_read_bio_X509          read_x509;
    pfn_PEM_read_bio_PrivateKey    read_key;
    pfn_X509_free                  x509_free;
    pfn_EVP_PKEY_free              pkey_free;
} g;

static SSL_CTX*        s_ctx       = nullptr;
static SOCKET          s_listen    = INVALID_SOCKET;
static unsigned short  s_port      = 0;
static volatile LONG   s_ready     = 0;
static volatile LONG   s_served    = 0;

bool Ready() { return s_ready != 0; }
unsigned ServedCount() { return (unsigned)s_served; }

// ---------------------------------------------------------------------------
// Resolve libssl / libcrypto. Both ship with the game, so this is a plain
// LoadLibrary -- no new file appears on disk.
// ---------------------------------------------------------------------------
static bool BindSsl() {
    g.ssl    = LoadLibraryA(L_LIBSSL);
    g.crypto = LoadLibraryA(L_LIBCRYP);
    if (!g.ssl || !g.crypto) return false;

#define BIND(mod, field, name)                                                 \
    g.field = (decltype(g.field))GetProcAddress(g.mod, name);                  \
    if (!g.field) return false;

    BIND(ssl,    CTX_new,       "SSL_CTX_new")
    BIND(ssl,    CTX_free,      "SSL_CTX_free")
    BIND(ssl,    server_method, "TLS_server_method")
    BIND(ssl,    use_cert,      "SSL_CTX_use_certificate")
    BIND(ssl,    use_key,       "SSL_CTX_use_PrivateKey")
    BIND(ssl,    New,           "SSL_new")
    BIND(ssl,    Free,          "SSL_free")
    BIND(ssl,    set_fd,        "SSL_set_fd")
    BIND(ssl,    accept,        "SSL_accept")
    BIND(ssl,    read,          "SSL_read")
    BIND(ssl,    write,         "SSL_write")
    BIND(ssl,    shutdown,      "SSL_shutdown")
    BIND(ssl,    get_error,     "SSL_get_error")
    BIND(crypto, bio_mem,       "BIO_new_mem_buf")
    BIND(crypto, bio_free,      "BIO_free")
    BIND(crypto, read_x509,     "PEM_read_bio_X509")
    BIND(crypto, read_key,      "PEM_read_bio_PrivateKey")
    BIND(crypto, x509_free,     "X509_free")
    BIND(crypto, pkey_free,     "EVP_PKEY_free")
#undef BIND
    return true;
}

// ---------------------------------------------------------------------------
// Build the server TLS context from the embedded PEM material.
// ---------------------------------------------------------------------------
static bool MakeContext() {
    if (!BindSsl()) {
        LOG(L_LOGNO);
        return false;
    }

    const SSL_METHOD* m = g.server_method();
    if (!m) return false;
    s_ctx = g.CTX_new(m);
    if (!s_ctx) return false;

    BIO* cb = g.bio_mem(fakecert::kCertPem, (int)strlen(fakecert::kCertPem));
    BIO* kb = g.bio_mem(fakecert::kKeyPem,  (int)strlen(fakecert::kKeyPem));
    if (!cb || !kb) return false;

    X509*     cert = g.read_x509(cb, nullptr, nullptr, nullptr);
    EVP_PKEY* key  = g.read_key(kb, nullptr, nullptr, nullptr);
    g.bio_free(cb);
    g.bio_free(kb);
    if (!cert || !key) return false;

    // We deliberately do NOT verify the peer: the "peer" is the reporting client
    // and it never presents a certificate. Without this, SSL_accept fails.
    int ok = g.use_cert(s_ctx, cert) && g.use_key(s_ctx, key);
    g.x509_free(cert);
    g.pkey_free(key);
    return ok != 0;
}

// ---------------------------------------------------------------------------
// The canned response.
//
// ACE-Base64.dll contains serde (Rust JSON) error strings, so at least part of
// the reporting path parses JSON and a non-JSON body could itself be flagged.
// We return a generic ack envelope shaped like the Tencent convention
// (ret / msg / data) that appears across the ecosystem.
//
// HONEST LIMIT: this is a plausible envelope, not a verified one. We have not
// observed a real ACE success response, so we cannot prove the client accepts
// it. It is strictly better than a refused connection, which the client can
// definitely interpret as failure -- but "better than refusal" is not the same
// as "indistinguishable". If the protocol signs the ack over a client nonce,
// this will not satisfy it.
// ---------------------------------------------------------------------------
static const char* AckBody(size_t* outLen) {
    // Same obfuscation discipline as everything else in this module.
    static constexpr auto kAck = SBOX_BYTES(
        "{\"ret\":0,\"code\":0,\"msg\":\"ok\",\"data\":{\"status\":1,\"interval\":3600}}",
        0x0B);
    *outLen = sizeof(kAck.d) - 1;
    return obf::DecryptTls(kAck.d, sizeof(kAck.d), 0x0B);
}

// ---------------------------------------------------------------------------
// Read one HTTP request off the TLS stream (headers, then body if announced).
// Returns the number of body bytes consumed. We do not inspect the content --
// only enough structure to know when the client is done talking.
// ---------------------------------------------------------------------------
static size_t DrainRequest(SSL* ssl) {
    char buf[4096];
    size_t total = 0;
    size_t headerEnd = 0;
    long   contentLen = -1;

    // Phase 1: headers.
    while (total < sizeof(buf) - 1) {
        int n = g.read(ssl, buf + total, (int)(sizeof(buf) - 1 - total));
        if (n <= 0) break;
        total += (size_t)n;
        buf[total] = 0;

        if (!headerEnd) {
            char* p = strstr(buf, "\r\n\r\n");
            if (p) {
                headerEnd = (size_t)(p - buf) + 4;
                // Content-Length is case-insensitive in HTTP.
                char* h = buf;
                while ((h = strstr(h, "ontent-Length:")) != nullptr) {
                    contentLen = strtol(h + 15, nullptr, 10);
                    break;
                }
            }
        }
        if (headerEnd && contentLen >= 0) {
            if (total - headerEnd >= (size_t)contentLen) break;
        } else if (headerEnd && contentLen < 0) {
            break;   // no body announced (GET, or chunked which we do not parse)
        }
        if (total >= sizeof(buf) - 1) break;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Serve one accepted connection: TLS handshake, read request, answer 200.
// ---------------------------------------------------------------------------
static void ServeOne(SOCKET c) {
    SSL* ssl = g.New(s_ctx);
    if (!ssl) { closesocket(c); return; }

    g.set_fd(ssl, (int)c);
    if (g.accept(ssl) <= 0) {
        LOG(L_ACCEPT, g.get_error(ssl, -1));
        g.Free(ssl);
        closesocket(c);
        return;
    }

    size_t got = DrainRequest(ssl);

    size_t blen = 0;
    const char* body = AckBody(&blen);
    char resp[512];
    int hlen = _snprintf_s(resp, sizeof(resp), _TRUNCATE,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n", (unsigned)blen);

    if (hlen > 0) {
        g.write(ssl, resp, hlen);
        g.write(ssl, body, (int)blen);
    }

    // Best-effort clean shutdown; the client may just drop the socket.
    g.shutdown(ssl);
    g.Free(ssl);
    closesocket(c);

    InterlockedIncrement(&s_served);
    LOG(L_SERVED, (unsigned)s_served, "report", (unsigned)got);
}

// ---------------------------------------------------------------------------
// Accept loop. One thread for the process lifetime.
// ---------------------------------------------------------------------------
static DWORD WINAPI AcceptThread(LPVOID) {
    for (;;) {
        SOCKET c = accept(s_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            // Listener closed (shutdown) or a transient failure: back off a
            // little rather than spinning.
            Sleep(50);
            continue;
        }
        // Reports are infrequent and can be large; handle synchronously on this
        // thread so we never accumulate unbounded worker threads.
        ServeOne(c);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Bind the listener to an ephemeral loopback port.
// ---------------------------------------------------------------------------
static bool StartListener() {
    s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen == INVALID_SOCKET) return false;

    BOOL yes = TRUE;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only
    a.sin_port = 0;                               // let the OS choose
    if (bind(s_listen, (sockaddr*)&a, sizeof(a)) != 0) return false;
    if (listen(s_listen, 8) != 0) return false;

    int alen = sizeof(a);
    if (getsockname(s_listen, (sockaddr*)&a, &alen) != 0) return false;
    s_port = ntohs(a.sin_port);
    return s_port != 0;
}

void Install() {
    if (InterlockedCompareExchange(&s_ready, 1, 0) != 0) return;   // once

    if (!MakeContext()) {
        LOG(L_LOGNO);
        InterlockedExchange(&s_ready, 0);
        return;
    }
    if (!StartListener()) {
        LOG(L_LOGNO);
        InterlockedExchange(&s_ready, 0);
        return;
    }

    HANDLE t = CreateThread(nullptr, 0, AcceptThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);

    LOG(L_LOGUP, (unsigned)s_port);
}

// ---------------------------------------------------------------------------
// Accessors used by connect()/WSAConnect().
// ---------------------------------------------------------------------------
unsigned short LocalPort() { return s_port; }

} // namespace fakesrv
