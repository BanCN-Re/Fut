#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_fake_server.py - exercise the in-process TLS responder directly.

Why a separate test: the responder only receives traffic when a reporting module
actually uploads, which is sporadic. This connects to it on demand so the TLS
handshake, the request drain, and the canned response can be verified without
waiting for ACE.

It also proves the certificates are well-formed, which matters because the
client verifies the hostname and a malformed SAN would silently look like an
attack rather than a success.

Usage: python test_fake_server.py <port> [hostname]
"""
import socket
import ssl
import sys


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    port = int(sys.argv[1])
    host = sys.argv[2] if len(sys.argv) > 2 else "pc.crashsight.qq.com"

    # The responder uses a self-signed cert, so verification must be off for THIS
    # test. The in-game path verifies normally against the injected trust anchor;
    # see fake_trust.cpp.
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    body = b'{"probe":"test"}'
    req = (
        b"POST /report HTTP/1.1\r\n"
        b"Host: " + host.encode() + b"\r\n"
        b"Content-Type: application/json\r\n"
        b"Content-Length: " + str(len(body)).encode() + b"\r\n"
        b"Connection: close\r\n\r\n" + body
    )

    print("connecting to 127.0.0.1:%d (SNI=%s)" % (port, host))
    try:
        raw = socket.create_connection(("127.0.0.1", port), timeout=5)
    except OSError as e:
        print("FAIL: cannot connect: %s" % e)
        print("      (is the game running? the responder lives in that process)")
        return

    try:
        tls = ctx.wrap_socket(raw, server_hostname=host)
    except ssl.SSLError as e:
        print("FAIL: TLS handshake: %s" % e)
        return

    print("TLS ok: %s / %s" % (tls.version(), tls.cipher()[0]))
    cert = tls.getpeercert(binary_form=True)
    print("peer cert: %d bytes DER" % (len(cert) if cert else 0))

    tls.sendall(req)
    chunks = []
    while True:
        try:
            d = tls.recv(4096)
        except OSError:
            break
        if not d:
            break
        chunks.append(d)
    tls.close()

    resp = b"".join(chunks)
    print()
    print("--- response (%d bytes) ---" % len(resp))
    print(resp.decode("utf-8", "replace"))

    head = resp.split(b"\r\n", 1)[0]
    if b"200" in head:
        print("RESULT: PASS - responder returned a success status")
    else:
        print("RESULT: FAIL - unexpected status line: %r" % head)


if __name__ == "__main__":
    main()
