#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dns_audit.py - enumerate EVERY name-resolution path in a module.

A DNS blackhole is only "complete" if it covers all resolution routes. This
script reports, for the target module:

  1. every imported resolver API (static IAT) and the functions calling it
  2. every dynamically-resolved resolver API string (GetProcAddress route)
  3. async-only APIs (GetAddrInfoExOverlappedResult/Cancel) which imply the
     async GetAddrInfoEx path is in use
  4. other modules in the process that resolve names (winhttp/wininet/dnsapi/
     ws2_32/ncrypt/urlmon) -- patching THEIR iat is what catches dynamic callers

Usage: python dns_audit.py <module> [more modules...]
"""
import re
import struct
import sys
from collections import defaultdict

import capstone
import pefile

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True

RESOLVERS = {
    # ws2_32
    "getaddrinfo", "GetAddrInfoW", "GetAddrInfoExW", "GetAddrInfoExA",
    "GetAddrInfoExOverlappedResult", "GetAddrInfoExCancel",
    "gethostbyname", "gethostbyaddr", "WSAAsyncGetHostByName",
    "getnameinfo", "GetNameInfoW", "WSALookupServiceBeginW",
    "WSALookupServiceNextW", "freeaddrinfo", "FreeAddrInfoExW",
    # dnsapi
    "DnsQuery_A", "DnsQuery_W", "DnsQueryEx", "DnsQueryConfig",
    "DnsFree", "DnsRecordListFree", "DnsExtractRecordsFromMessage_W",
    # winhttp / wininet
    "WinHttpConnect", "InternetConnectA", "InternetConnectW",
    "WinHttpOpenRequest", "WinHttpSendRequest",
    # raw
    "connect", "WSAConnect", "send", "WSASend", "inet_addr", "InetPtonW",
}


def sec(pe, n):
    for s in pe.sections:
        if s.Name.rstrip(b"\0").decode("latin-1", "replace") == n:
            return s
    return None


def rva_off(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None


def pdata(pe, raw):
    out = []
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    off = rva_off(pe, d.VirtualAddress) if d.VirtualAddress else None
    if off is None:
        return out
    for i in range(d.Size // 12):
        b, e, u = struct.unpack_from("<III", raw, off + i * 12)
        if b:
            out.append((b, e))
    out.sort()
    return out


def audit(path):
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase
    print("=" * 78)
    print("MODULE: %s" % path)
    print("  imagebase 0x%X  size %d" % (ib, len(raw)))

    # ---- 1. static imports -------------------------------------------------
    slots = {}
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1", "replace")
        for imp in entry.imports:
            nm = (imp.name or b"").decode("latin-1", "replace")
            if nm in RESOLVERS:
                slots[imp.address] = (nm, dll)

    print()
    print("  -- statically imported resolver APIs (%d) --" % len(slots))
    for a, (n, d) in sorted(slots.items(), key=lambda kv: kv[1][0]):
        print("     %-34s %-14s IAT@0x%X" % (n, d, a))

    # ---- 2. async-only APIs => async path in use ---------------------------
    async_apis = [n for n, d in slots.values()
                  if n in ("GetAddrInfoExOverlappedResult", "GetAddrInfoExCancel",
                           "GetAddrInfoExW", "GetAddrInfoExA")]
    print()
    print("  -- ASYNC resolution indicators --")
    if async_apis:
        for n in sorted(set(async_apis)):
            print("     !! %s present -> async GetAddrInfoEx path likely" % n)
    else:
        print("     none")

    # ---- 3. dynamic resolution strings ------------------------------------
    print()
    print("  -- dynamically-resolved resolver names (string literals) --")
    names = set()
    for s in pe.sections:
        nm = s.Name.rstrip(b"\0").decode("latin-1", "replace")
        if nm not in (".rdata", ".data", ".ddata", ".text"):
            continue
        blob = raw[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        # ascii
        for m in re.finditer(rb"(?:GetAddrInfo|getaddrinfo|gethostby|DnsQuery|"
                             rb"WSALookup|getnameinfo|GetNameInfo|WinHttp|InternetConnect)"
                             rb"[A-Za-z0-9_]{0,32}", blob):
            names.add(("ascii", m.group().decode("latin-1"),
                       ib + s.VirtualAddress + m.start()))
        # utf16
        for m in re.finditer(rb"(?:G\x00e\x00t\x00A\x00d\x00d\x00r\x00I\x00n\x00f\x00o|"
                             rb"g\x00e\x00t\x00a\x00d\x00d\x00r\x00i\x00n\x00f\x00o|"
                             rb"D\x00n\x00s\x00Q\x00u\x00e\x00r\x00y|"
                             rb"W\x00i\x00n\x00H\x00t\x00t\x00p)", blob):
            off = m.start()
            end = off
            while end + 1 < len(blob) and not (blob[end] == 0 and blob[end + 1] == 0):
                end += 2
            try:
                t = blob[off:end].decode("utf-16-le")
                if t and all(32 <= ord(c) < 127 for c in t):
                    names.add(("utf16", t, ib + s.VirtualAddress + off))
            except Exception:
                pass

    for enc, t, va in sorted(names, key=lambda x: x[1]):
        star = "  <<< ASYNC" if ("Overlapped" in t or "Cancel" in t) else ""
        print("     [%-5s] %-40s @0x%X%s" % (enc, t, va, star))

    # ---- 4. who calls the static imports ----------------------------------
    t = sec(pe, ".text")
    if t is None:
        return
    funcs = pdata(pe, raw)
    starts = [b for b, e in funcs]

    import bisect
    def func_at(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i < 0:
            return None
        b, e = funcs[i]
        return (b, e) if b <= rva < e else None

    code = raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
    base_va = ib + t.VirtualAddress
    callers = defaultdict(set)
    CH = 0x40000
    for off in range(0, len(code), CH):
        chunk = code[off:off + CH]
        if not chunk:
            break
        try:
            for ins in CS.disasm(chunk, base_va + off):
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_OP_REG_RIP if False else False:
                        pass
        except Exception:
            pass

    # simpler: scan for RIP-relative refs
    for off in range(0, len(code), CH):
        chunk = code[off:off + CH]
        if not chunk:
            break
        try:
            for ins in CS.disasm(chunk, base_va + off):
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                        if tgt in slots:
                            f = func_at(ins.address - ib)
                            if f:
                                callers[slots[tgt][0]].add(f[0])
        except Exception:
            pass

    print()
    print("  -- functions that call each resolver --")
    for nm in sorted(callers):
        fs = sorted(callers[nm])
        print("     %-34s %s" % (nm, " ".join("0x%X" % (ib + f) for f in fs[:8])))


def main():
    for p in sys.argv[1:]:
        audit(p)
        print()


if __name__ == "__main__":
    main()
