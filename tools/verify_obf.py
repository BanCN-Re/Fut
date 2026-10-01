#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_obf.py - confirm suspicious strings are gone from a build artifact.

This is the acceptance test for P1: scan the object/DLL for the plaintext
markers that were previously readable, and report which survive.

Usage: python verify_obf.py <artifact> [artifact...]
"""
import sys

# markers that must NOT appear in plaintext any more
MUST_BE_GONE = [
    "anticheatexpert.com",
    "tqos.gamesafe.qq.com",
    "crashsight.qq.com",
    "perfsight.qq.com",
    "tdm.qq.com",
    "galileotelemetry.tencent.com",
    "tpstelemetry.tencent.com",
    "sensors.cloud.tencent.com",
    "rsensors.cloud.tencent.com",
    "gcloudcs.com",
    "bugly.qq.com",
    "msdk.qq.com",
    "sguard.qq.com",
    "beacon.qq.com",
    "tgp.qq.com",
]

# markers that are allowed to remain (not yet migrated, or harmless)
STILL_OK = [
    "zlibwapi_orig",
    "ws2_32", "winhttp", "wininet", "webio", "dnsapi", "nrtapi",
    "getaddrinfo", "GetAddrInfoW", "GetAddrInfoExW", "WinHttpConnect",
]


def scan(path):
    with open(path, "rb") as f:
        raw = f.read()
    print("=" * 74)
    print("artifact: %s  (%d bytes)" % (path, len(raw)))
    print("=" * 74)

    leaked = []
    for m in MUST_BE_GONE:
        n = raw.count(m.encode())
        if n:
            leaked.append((m, n))

    print()
    print("--- MUST BE GONE (P1 targets) ---")
    if leaked:
        for m, n in leaked:
            print("  [LEAK] %-40s %d occurrence(s)  <-- STILL PLAINTEXT" % (m, n))
    else:
        print("  [OK]   all %d markers encrypted (0 plaintext occurrences)" % len(MUST_BE_GONE))

    print()
    print("--- still present (expected / harmless) ---")
    for m in STILL_OK:
        n = raw.count(m.encode())
        if n:
            print("  [--]   %-40s %d" % (m, n))

    print()
    if leaked:
        print("RESULT: FAIL - %d marker(s) still readable" % len(leaked))
    else:
        print("RESULT: PASS - no P1 marker is readable in this artifact")
    print()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    for p in sys.argv[1:]:
        try:
            scan(p)
        except OSError as e:
            print("cannot read %s: %s" % (p, e))


if __name__ == "__main__":
    main()
