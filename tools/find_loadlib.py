#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""find_loadlib.py - locate GetProcAddress / LoadLibrary usage and the
name strings passed to them (dynamic API resolution).

If a module imports WinHttpConnect but never calls it via the IAT, it is either
resolving it dynamically or calling through a pointer table. This script finds
   * all GetProcAddress / LoadLibraryA/W IAT slots and their xrefs
   * nearby string immediates (the resolved API names)

Usage: python find_loadlib.py <pe>
"""
import struct
from collections import defaultdict

import capstone
import pefile

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True

TARGETS = {"GetProcAddress", "LoadLibraryA", "LoadLibraryW", "LoadLibraryExA",
           "LoadLibraryExW", "GetModuleHandleA", "GetModuleHandleW",
           "LdrGetProcedureAddress", "LdrLoadDll"}


def sec(pe, name):
    for s in pe.sections:
        if s.Name.rstrip(b"\0").decode("latin-1", "replace") == name:
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


def func_at(funcs, starts, rva):
    import bisect
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0:
        return None
    b, e = funcs[i]
    return (b, e) if b <= rva < e else None


def main():
    path = __import__("sys").argv[1]
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase
    t = sec(pe, ".text")
    funcs = pdata(pe, raw)
    starts = [b for b, e in funcs]

    slots = {}
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1", "replace")
        for imp in entry.imports:
            nm = (imp.name or b"").decode("latin-1", "replace")
            if nm in TARGETS:
                slots[imp.address] = (nm, dll)

    print("file: %s" % path)
    print("loader API slots: %d" % len(slots))
    for a, (n, d) in sorted(slots.items()):
        print("   0x%X  %-24s %s" % (a, n, d))

    code = raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
    text_va = ib + t.VirtualAddress

    hits = defaultdict(list)
    CH = 0x40000
    for base in range(0, len(code), CH):
        chunk = code[base:base + CH]
        if not chunk:
            break
        try:
            for ins in CS.disasm(chunk, text_va + base):
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                        if tgt in slots:
                            f = func_at(funcs, starts, ins.address - ib)
                            hits[f[0] if f else -1].append((ins.address, slots[tgt][0]))
        except Exception:
            pass

    print()
    print("=== functions touching the loader APIs ===")
    for fs, lst in sorted(hits.items()):
        names = sorted({n for _, n in lst})
        print("  func 0x%X  (%d refs)  -> %s" % (ib + fs if fs > 0 else 0, len(lst), ", ".join(names)))

    # ------------------------------------------------------------------
    # collect printable strings that look like API names, and note which
    # function region their address is referenced from
    # ------------------------------------------------------------------
    print()
    print("=== suspicious dynamic-resolution strings ===")
    import re
    api = re.compile(rb"(?:WinHttp|Http|Internet|WSASocket|connect|send|recv|"
                     rb"getaddrinfo|GetAddrInfo|DnsQuery|Crypt|BCrypt|"
                     rb"Rtl|Nt)[A-Za-z0-9_]{2,40}")
    found = {}
    data_secs = [s for s in pe.sections
                 if s.Name.rstrip(b"\0").decode("latin-1", "replace") in (".rdata", ".data", ".ddata")]
    for s in data_secs:
        blob = raw[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        for m in api.finditer(blob):
            nm = m.group().decode("latin-1")
            if len(nm) < 6:
                continue
            rva = s.VirtualAddress + m.start()
            va = ib + rva
            found.setdefault(nm, []).append(va)
    for nm in sorted(found):
        vas = found[nm][:3]
        print("  %-34s %s" % (nm, " ".join("0x%X" % v for v in vas)))
    print()
    print("total distinct API-like strings: %d" % len(found))


if __name__ == "__main__":
    main()
