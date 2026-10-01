#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""imports_of.py - list one module's imports from a specific provider DLL,
expanding ordinals against that provider's export table.

Usage: python imports_of.py <consumer> <provider-substring>
"""
import sys

import pefile

# ws2_32 ordinal map (stable across Windows versions for the entries webio uses)
WS2_ORD = {
    1: "accept", 2: "bind", 3: "closesocket", 4: "connect", 5: "getpeername",
    6: "getsockname", 7: "getsockopt", 8: "htonl", 9: "htons", 10: "ioctlsocket",
    11: "inet_addr", 12: "inet_ntoa", 13: "listen", 14: "ntohl", 15: "ntohs",
    16: "recv", 17: "recvfrom", 18: "select", 19: "send", 20: "sendto",
    21: "setsockopt", 22: "shutdown", 23: "socket", 24: "GetAddrInfoW",
    25: "GetNameInfoW", 26: "WSApSetPostRoutine", 51: "gethostbyaddr",
    52: "gethostbyname", 53: "getprotobyname", 54: "getprotobynumber",
    55: "getservbyname", 56: "getservbyport", 57: "gethostname",
    101: "WSAAsyncSelect", 102: "WSAAsyncGetHostByAddr",
    103: "WSAAsyncGetHostByName", 104: "WSAAsyncGetProtoByNumber",
    105: "WSAAsyncGetProtoByName", 106: "WSAAsyncGetServByPort",
    107: "WSAAsyncGetServByName", 108: "WSACancelAsyncRequest",
    109: "WSASetBlockingHook", 110: "WSAUnhookBlockingHook",
    111: "WSAGetLastError", 112: "WSASetLastError", 113: "WSACancelBlockingCall",
    114: "WSAIsBlocking", 115: "WSAStartup", 116: "WSACleanup",
    151: "__WSAFDIsSet", 500: "WEP",
}


def provider_exports(path):
    pe = pefile.PE(path, fast_load=False)
    m = {}
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            m[e.ordinal] = e.name.decode("latin-1", "replace") if e.name else None
    return m


def main():
    consumer = sys.argv[1]
    prov_sub = sys.argv[2].lower()

    prov_path = None
    for c in ("C:\\Windows\\System32\\", "C:\\Windows\\SysWOW64\\"):
        import os
        p = c + prov_sub
        if os.path.exists(p):
            prov_path = p
            break
    omap = provider_exports(prov_path) if prov_path else {}

    pe = pefile.PE(consumer, fast_load=False)
    print("=== %s imports from %s ===" % (consumer.rsplit("\\", 1)[-1], prov_sub))
    n = 0
    for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = e.dll.decode("latin-1", "replace").lower()
        if prov_sub not in dll:
            continue
        for imp in e.imports:
            if imp.name:
                nm = imp.name.decode("latin-1", "replace")
            else:
                nm = omap.get(imp.ordinal) or WS2_ORD.get(imp.ordinal) or ("ord#%d" % imp.ordinal)
                nm = "%s (ord %d)" % (nm, imp.ordinal)
            print("   %-40s IAT@0x%X" % (nm, imp.address))
            n += 1
    if n == 0:
        print("   (no imports from %s)" % prov_sub)


if __name__ == "__main__":
    main()
