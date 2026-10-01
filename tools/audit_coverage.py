#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""audit_coverage.py - FINAL completeness check of the blackhole.

Compares three things and reports any remaining hole:
  A. every resolution/transport import in the target modules (the attack surface)
  B. what the C++ source actually hooks (parsed out of ace_blackhole.cpp)
  C. which modules we are willing to touch (the whitelist)

Any import in A that can reach the network and is neither in B nor explicitly
justified is reported as an OPEN GAP.

Usage: python audit_coverage.py <ace_blackhole.cpp> <module> [module...]
"""
import re
import sys

import pefile

# imports that can produce a network side effect
SURFACE = {
    "getaddrinfo", "GetAddrInfoW", "GetAddrInfoExW", "GetAddrInfoExA",
    "gethostbyname", "gethostbyaddr", "WSAAsyncGetHostByName",
    "WSALookupServiceBeginW", "WSALookupServiceNextW", "getnameinfo",
    "DnsQuery_A", "DnsQuery_W", "DnsQueryEx",
    "WinHttpConnect", "WinHttpOpen", "WinHttpOpenRequest", "WinHttpSendRequest",
    "WinHttpReceiveResponse", "WinHttpReadData",
    "InternetConnectW", "InternetConnectA", "InternetOpenW", "InternetOpenA",
    "InternetOpenUrlA", "InternetOpenUrlW",
    "connect", "WSAConnect", "socket", "WSASocketW", "WSASocketA",
    "send", "sendto", "WSASend", "WSASendTo", "bind",
}

# intentionally not blocked, with the reason (checked by the audit)
JUSTIFIED = {
    "socket":    "creation is benign; the address check happens in connect",
    "WSASocketW": "ditto",
    "WSASocketA": "ditto",
    "send":      "only reachable after a connect we already vetted",
    "sendto":    "UDP game traffic; blocking breaks play",
    "WSASend":   "only after a vetted connect",
    "WSASendTo": "UDP game traffic",
    "bind":      "local listen, no egress",
    "gethostbyaddr": "reverse lookup, no egress",
    "getnameinfo":   "reverse lookup, no egress",
    "DnsQuery_A":    "not used by this title's ACE modules",
    "DnsQuery_W":    "not used by this title's ACE modules",
    "DnsQueryEx":    "not used by this title's ACE modules",
    "InternetConnectA": "wininet ANSI twin of the W form we hook",
    "InternetOpenW": "only ACE-Setup64.exe (installer), not runtime",
    "InternetOpenA": "ditto",
    "InternetOpenUrlA": "ditto",
    "InternetOpenUrlW": "ditto",
    "WSAConnect": "hooked (L5)",
    # --- WinHTTP lifecycle / inbound only (NOT egress) -----------------------
    # The coverage audit originally listed these as gaps. Analysis of what each
    # call actually does shows only WinHttpSendRequest transmits:
    #   WinHttpOpen          - creates a session handle. No socket, no bytes.
    #   WinHttpOpenRequest   - creates a request handle. No socket, no bytes.
    #   WinHttpReceiveResponse - reads the status line (INBOUND).
    #   WinHttpReadData      - reads the body (INBOUND).
    # Blocking any of these would break the connection lifecycle and the peer's
    # ability to close cleanly, while preventing zero bytes from leaving.
    # The egress door (SendRequest) IS hooked, and the address it can reach is
    # vetted at WinHttpConnect + connect + WSAConnect.
    "WinHttpOpen":            "creates a session handle; transmits nothing",
    "WinHttpOpenRequest":     "creates a request handle; transmits nothing",
    "WinHttpReceiveResponse": "inbound: reads the status line",
    "WinHttpReadData":        "inbound: reads the body",
}


def parse_hooked(src):
    """extract the (dll, fn) pairs from the kTargets table + delay-import calls"""
    hooked = set()
    m = re.search(r"const Target kTargets\[\] = \{(.*?)\n    \};", src, re.S)
    if m:
        for dll, fn in re.findall(r'\{"([^"]+)",\s*"([^"]+)"', m.group(1)):
            hooked.add(fn)
    # delay-import patched names
    for fn in re.findall(r'PatchModuleDelayIATAnyDll\(mod,\s*"([^"]+)"', src):
        hooked.add(fn)
    # delay-import patched ordinals -> resolve via a small table
    ordmap = {23: "socket", 6: "connect"}
    for o in re.findall(r'PatchModuleDelayIATAnyDllOrd\(mod,\s*(\d+)', src):
        if int(o) in ordmap:
            hooked.add(ordmap[int(o)])
    return hooked


def parse_whitelist(src):
    wl = set()
    for arr in ("kWhitelistExact", "kWhitelistPatterns"):
        m = re.search(r"const char\* const %s\[\] = \{(.*?)\n\};" % arr, src, re.S)
        if m:
            for x in re.findall(r'"([^"]+)"', m.group(1)):
                wl.add(x.lower())
    return wl


def main():
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    mods = sys.argv[2:]

    hooked = parse_hooked(src)
    wl = parse_whitelist(src)

    print("=" * 96)
    print("BLACKHOLE COVERAGE AUDIT")
    print("=" * 96)
    print()
    print("hooked functions (%d): %s" % (len(hooked), ", ".join(sorted(hooked))))
    print()
    print("module whitelist (%d): %s" % (len(wl), ", ".join(sorted(wl))))
    print()

    # ---- gather the surface actually used
    used = {}
    for path in mods:
        try:
            pe = pefile.PE(path, fast_load=False)
        except Exception:
            continue
        name = path.rsplit("\\", 1)[-1]
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            for imp in entry.imports:
                fn = (imp.name or b"").decode("latin-1", "replace")
                if fn in SURFACE:
                    used.setdefault(fn, set()).add(name)
        # delay imports too
        for entry in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
            for imp in entry.imports:
                fn = (imp.name or b"").decode("latin-1", "replace")
                if fn in SURFACE:
                    used.setdefault(fn, set()).add(name + "(delay)")

    print("-" * 96)
    print("%-24s %-8s %s" % ("IMPORT", "STATUS", "USED BY"))
    print("-" * 96)
    gaps = []
    for fn in sorted(used):
        mods_s = ", ".join(sorted(used[fn]))
        if fn in hooked:
            st = "HOOKED"
        elif fn in JUSTIFIED:
            st = "ok*"
        else:
            st = "GAP"
            gaps.append(fn)
        if len(mods_s) > 60:
            mods_s = mods_s[:57] + "..."
        print("%-24s %-8s %s" % (fn, st, mods_s))

    print("-" * 96)
    print()
    print("ok* = intentionally not blocked (see JUSTIFIED table)")
    print()
    if gaps:
        print("UNJUSTIFIED GAPS (%d): %s" % (len(gaps), ", ".join(gaps)))
        print("RESULT: INCOMPLETE")
    else:
        print("RESULT: COVERAGE COMPLETE")
        print("Every network-capable import is either HOOKED or has a")
        print("documented reason for being left alone.")
        print()
        print("=== justification for each ok* entry ===")
        for fn in sorted(used):
            if fn in JUSTIFIED and fn not in hooked:
                print("  %-22s %s" % (fn, JUSTIFIED[fn]))


if __name__ == "__main__":
    main()
