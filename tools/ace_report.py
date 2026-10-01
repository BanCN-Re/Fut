#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ace_report.py - locate the ACE reporting path with real disassembly.

Uses capstone + pefile + the .pdata function table. Targets:
  1. the function that calls GetAddrInfoExW / getaddrinfo / WinHttpConnect
     (that is the upload path, regardless of what it is named)
  2. xrefs to the DoH strings
  3. obfuscated 'report_bk' reconstructions (stack-immediate dwords)

Usage: python ace_report.py <ACE-Base64.dll> [--va 0x...]
"""
import struct
import sys
from collections import defaultdict

import capstone
import pefile

# capstone 5.x renamed the arch constants to CS_ARCH_X86 / CS_MODE_64
_ARCH = getattr(capstone, "CS_ARCH_X86", None) or capstone.CS_ARCH_X86
CS = capstone.Cs(_ARCH, capstone.CS_MODE_64)
CS.detail = True


def load(path):
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    return pe, raw


def rva_to_off(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None


def off_to_rva(pe, off):
    for s in pe.sections:
        if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
            return s.VirtualAddress + (off - s.PointerToRawData)
    return None


def parse_pdata(pe, raw):
    """return list of (begin, end) RVAs"""
    funcs = []
    try:
        ddir = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    except IndexError:
        return funcs
    if not ddir.VirtualAddress:
        return funcs
    off = rva_to_off(pe, ddir.VirtualAddress)
    if off is None:
        return funcs
    n = ddir.Size // 12
    for i in range(n):
        b, e, u = struct.unpack_from("<III", raw, off + i * 12)
        if b:
            funcs.append((b, e))
    funcs.sort()
    return funcs


def build_func_index(funcs):
    starts = [b for b, e in funcs]
    return starts


def func_at(funcs, starts, rva):
    import bisect
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0:
        return None
    b, e = funcs[i]
    if b <= rva < e:
        return (b, e)
    return None


def main():
    path = sys.argv[1]
    pe, raw = load(path)
    ib = pe.OPTIONAL_HEADER.ImageBase

    text = None
    for s in pe.sections:
        nm = s.Name.rstrip(b"\0").decode("latin-1", "replace")
        if nm == ".text":
            text = s
            break
    if text is None:
        print("no .text")
        return

    funcs = parse_pdata(pe, raw)
    starts = build_func_index(funcs)
    print("file       : %s" % path)
    print("imagebase  : 0x%X" % ib)
    print(".text      : rva 0x%X size 0x%X" % (text.VirtualAddress, text.Misc_VirtualSize))
    print("functions  : %d (from .pdata)" % len(funcs))

    # ------------------------------------------------------------------
    # 1. resolve the relevant import addresses
    # ------------------------------------------------------------------
    want_imports = {}
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1", "replace").lower()
        for imp in entry.imports:
            nm = (imp.name or b"").decode("latin-1", "replace")
            if nm in ("getaddrinfo", "GetAddrInfoExW", "GetAddrInfoW",
                      "gethostbyname", "WinHttpConnect", "WinHttpSendRequest",
                      "WinHttpOpen", "WinHttpOpenRequest", "InternetConnectA",
                      "InternetConnectW", "InternetOpenA", "InternetOpenW",
                      "curl_easy_perform", "curl_easy_init"):
                want_imports.setdefault(nm, []).append((dll, imp.address))

    print()
    print("=== relevant imports (IAT slot VA) ===")
    iat_slots = {}
    for nm, locs in sorted(want_imports.items()):
        for dll, addr in locs:
            print("  %-22s from %-14s IAT@0x%X" % (nm, dll, addr))
            iat_slots[addr] = nm

    # ------------------------------------------------------------------
    # 2. scan .text for RIP-relative references to those IAT slots
    # ------------------------------------------------------------------
    tstart = text.PointerToRawData
    tend = tstart + text.SizeOfRawData
    code = raw[tstart:tend]
    text_va = ib + text.VirtualAddress

    print()
    print("=== code references to those IAT slots ===")
    refs = defaultdict(list)   # func_start -> [(va, import)]
    CHUNK = 0x40000
    for base in range(0, len(code), CHUNK):
        chunk = code[base:base + CHUNK]
        if not chunk:
            break
        chunk_va = text_va + base
        try:
            for ins in CS.disasm(chunk, chunk_va):
                if ins.id == 0:
                    continue
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                        if tgt in iat_slots:
                            f = func_at(funcs, starts, ins.address - ib)
                            fs = f[0] if f else None
                            refs[fs].append((ins.address, iat_slots[tgt], ins.mnemonic))
        except Exception:
            pass

    for fs, lst in sorted(refs.items(), key=lambda kv: -len(kv[1])):
        if fs is None:
            print("  (outside any function) 0x%X  %d ref(s)" % (0, len(lst)))
            continue
        importers = sorted({nm for _, nm, _ in lst})
        print("  func VA 0x%X  (%d refs)  -> %s" % (ib + fs, len(lst), ", ".join(importers)))
        for va, nm, mn in lst[:6]:
            print("      0x%X  %s  %s" % (va, mn, nm))

    # ------------------------------------------------------------------
    # 3. obfuscated report_bk reconstruction
    # ------------------------------------------------------------------
    print()
    print("=== obfuscated 'report_bk' hunt (imm32 chunks) ===")
    pats = {
        "repo": 0x6F706572,
        "rt_b": 0x625F7472,
        "t_bk": 0x6B625F74,
        "_bk\0": 0x00006B62,
        "k\0\0\0": 0x0000006B,
    }
    hits = defaultdict(list)
    n = len(code)
    for i in range(0, n - 4, 4):
        v = struct.unpack_from("<I", code, i)[0]
        for nm, pv in pats.items():
            if v == pv:
                va = text_va + i
                f = func_at(funcs, starts, va - ib)
                hits[nm].append((va, f[0] if f else None))
    for nm, lst in hits.items():
        print("  %-8s %d hit(s)" % (nm, len(lst)))
        for va, fs in lst[:10]:
            print("      0x%X  in func 0x%X" % (va, ib + fs if fs else 0))

    # ------------------------------------------------------------------
    # 4. DoH string xrefs
    # ------------------------------------------------------------------
    print()
    print("=== DoH string location ===")
    doh = raw.find(b"application/dns-message")
    if doh >= 0:
        rva = off_to_rva(pe, doh)
        print("  'application/dns-message' off=0x%X rva=0x%X VA=0x%X" % (doh, rva, ib + rva))
    doh2 = raw.find(b"Could not DoH-resolve")
    if doh2 >= 0:
        rva = off_to_rva(pe, doh2)
        print("  'Could not DoH-resolve'   off=0x%X rva=0x%X VA=0x%X" % (doh2, rva, ib + rva))


if __name__ == "__main__":
    main()
