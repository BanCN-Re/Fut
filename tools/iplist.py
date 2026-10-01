#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""iplist.py - extract IP-like strings and DNS-related indicators from a binary.

Answers the question "does a DNS blackhole actually stop this thing?":
  * literal IPv4 / IPv6 endpoints  -> DNS blocking is bypassed
  * DoH / DoT / HTTPS-DNS strings  -> DNS blocking is bypassed
  * plain hostnames                -> DNS blocking works

Usage: python iplist.py <binary>
"""
import re
import struct
import sys

IPV4 = re.compile(rb"(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])")
# ASCII hosts that look like domains
HOST = re.compile(rb"(?<![A-Za-z0-9_.-])(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+"
                  rb"(?:com|net|org|cn|qq|io|top|xyz|info|biz|tv|me|cc|co|vip|club|site|online)(?![A-Za-z0-9-])")

DOH_HINTS = [
    b"dns.google", b"cloudflare-dns", b"1.1.1.1", b"8.8.8.8", b"8.8.4.4",
    b"doh", b"DoH", b"application/dns-message", b"dns-query", b"DNS-over-HTTPS",
    b"https://", b"http://", b"tls", b"SSL", b"openssl", b"mbedtls",
    b"libcurl", b"curl_easy", b"WinHttp", b"InternetOpen",
]


def load_sections(path):
    raw = open(path, "rb").read()
    if raw[:2] != b"MZ":
        return raw, []
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<HH", raw, pe + 4)[1]
    optsz = struct.unpack_from("<H", raw, pe + 20)[0]
    opt = pe + 24
    sec_off = opt + optsz
    secs = []
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return raw, secs


def off_to_rva(off, secs):
    for name, vaddr, vsize, raddr, rsize in secs:
        if raddr <= off < raddr + rsize:
            return vaddr + (off - raddr), name
    return None, None


def interesting(ip: str) -> bool:
    """filter out version-like and obviously-non-routable noise"""
    try:
        parts = [int(x) for x in ip.split(".")]
    except ValueError:
        return False
    if any(p > 255 for p in parts):
        return False
    # drop common version strings / zeros
    if parts[0] == 0 or parts[3] == 0 and parts[2] == 0:
        return False
    if ip in ("127.0.0.1", "0.0.0.0"):
        return False
    # drop things that look like version numbers 1.2.3.4 in .data version blocks
    return True


def main():
    path = sys.argv[1]
    raw, secs = load_sections(path)
    print("file: %s  (%d bytes)" % (path, len(raw)))
    print()

    # ---- IPv4 ----
    ips = {}
    for m in IPV4.finditer(raw):
        s = m.group().decode()
        if not interesting(s):
            continue
        rva, sec = off_to_rva(m.start(), secs)
        # keep the section it came from; .text hits are usually immediates
        ips.setdefault(s, []).append((rva, sec))

    print("=== IPv4 candidates: %d unique ===" % len(ips))
    rows = []
    for ip, locs in ips.items():
        # prefer data-section hits over text hits
        secs_seen = {s for _, s in locs}
        rows.append((ip, len(locs), ",".join(sorted(x for x in secs_seen if x)), locs[0][0]))
    # show data-section ones first
    rows.sort(key=lambda r: (".rdata" not in r[2] and ".data" not in r[2], -r[1]))
    for ip, n, secs_s, first_rva in rows[:60]:
        star = "  <<< DATA SECTION" if (".rdata" in secs_s or ".data" in secs_s) else ""
        print("  %-18s x%-4d %-22s first_rva=0x%-9X%s" % (
            ip, n, secs_s, first_rva or 0, star))

    # ---- hostnames ----
    hosts = {}
    for m in HOST.finditer(raw):
        s = m.group().decode().lower()
        hosts[s] = hosts.get(s, 0) + 1
    print()
    print("=== hostnames: %d unique ===" % len(hosts))
    for h, n in sorted(hosts.items(), key=lambda kv: -kv[1])[:50]:
        tag = ""
        low = h.lower()
        if any(k in low for k in ("anticheat", "tencent", "qq.com", "ace", "sguard",
                                  "crashsight", "perfsight", "bugly", "beacon", "msdk",
                                  "gcloud", "wetest")):
            tag = "   <<< ACE/TENCENT"
        print("  %-52s x%d%s" % (h, n, tag))

    # ---- DoH / transport hints ----
    print()
    print("=== transport / DNS hints ===")
    for k in DOH_HINTS:
        n = raw.count(k)
        if n:
            print("  %-28s x%d" % (k.decode("latin-1"), n))


if __name__ == "__main__":
    main()
