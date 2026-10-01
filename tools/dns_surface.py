#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dns_surface.py - audit EVERY name-resolution import across a set of modules.

"Complete truncation" requires knowing all the doors, not just the one we
patched. This enumerates, for each module, every import that can resolve a
hostname or reach the network without DNS:

  * ws2_32 : getaddrinfo / GetAddrInfoW / GetAddrInfoExW / gethostbyname /
             WSAAsyncGetHostByName / WSALookupServiceBegin/Next / GetHostNameW
  * dnsapi : DnsQuery_A/W / DnsQueryEx / DnsQueryConfig
  * winhttp: WinHttpConnect / WinHttpOpen / WinHttpCrackUrl
  * wininet: InternetConnectA/W / InternetOpenA/W / InternetOpenUrlA/W
  * direct : connect / send / WSASocket  (raw socket, no name needed)

Usage: python dns_surface.py <module1> [module2 ...]
"""
import sys

import pefile

RESOLVERS = {
    # ws2_32
    "getaddrinfo", "GetAddrInfoW", "GetAddrInfoExW", "GetAddrInfoExA",
    "gethostbyname", "gethostbyname2", "gethostbyaddr",
    "WSAAsyncGetHostByName", "WSAAsyncGetHostByAddr",
    "WSALookupServiceBeginA", "WSALookupServiceBeginW",
    "WSALookupServiceNextA", "WSALookupServiceNextW",
    "GetHostNameW", "gethostname",
    # dnsapi
    "DnsQuery_A", "DnsQuery_W", "DnsQuery_UTF8", "DnsQueryEx",
    "DnsQueryConfig", "DnsFree", "DnsExtractRecordsFromMessage_W",
    # winhttp
    "WinHttpConnect", "WinHttpOpen", "WinHttpCrackUrl", "WinHttpOpenRequest",
    "WinHttpSendRequest", "WinHttpReceiveResponse", "WinHttpReadData",
    # wininet
    "InternetConnectA", "InternetConnectW", "InternetOpenA", "InternetOpenW",
    "InternetOpenUrlA", "InternetOpenUrlW", "InternetCrackUrlA", "InternetCrackUrlW",
    # raw sockets
    "connect", "WSAConnect", "send", "WSASend", "WSASocketA", "WSASocketW",
    "socket", "bind", "sendto", "WSASendTo",
}

# functions our blackhole currently patches
PATCHED = {"getaddrinfo", "GetAddrInfoW", "gethostbyname",
           "GetAddrInfoExW", "WSAAsyncGetHostByName"}


def main():
    paths = sys.argv[1:]
    print("=" * 100)
    print("DNS / NETWORK RESOLUTION SURFACE")
    print("=" * 100)
    print()
    print("Legend: [PATCHED] = our blackhole covers it   [GAP] = NOT covered")
    print()

    all_gaps = {}
    for path in paths:
        try:
            pe = pefile.PE(path, fast_load=False)
        except Exception as e:
            print("  %s : parse error %s" % (path, e))
            continue
        name = path.rsplit("\\", 1)[-1]
        found = []
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = entry.dll.decode("latin-1", "replace")
            for imp in entry.imports:
                fn = (imp.name or b"").decode("latin-1", "replace")
                if fn in RESOLVERS:
                    found.append((dll, fn, imp.address))
        if not found:
            continue
        print("### %s" % name)
        seen = set()
        for dll, fn, addr in sorted(found, key=lambda x: (x[1], x[0])):
            key = (dll, fn)
            if key in seen:
                continue
            seen.add(key)
            tag = "[PATCHED]" if fn in PATCHED else "[GAP]    "
            if fn not in PATCHED:
                all_gaps.setdefault(fn, []).append(name)
            print("   %s %-26s from %-16s IAT@0x%X" % (tag, fn, dll, addr))
        print()

    print("=" * 100)
    print("GAPS (imports that can still resolve or connect without our patch)")
    print("=" * 100)
    if not all_gaps:
        print("  none")
    for fn, mods in sorted(all_gaps.items(), key=lambda kv: -len(kv[1])):
        print("  %-28s used by: %s" % (fn, ", ".join(sorted(set(mods)))))


if __name__ == "__main__":
    main()
