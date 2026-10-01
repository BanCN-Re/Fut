#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""subsystem_map.py - map a whole subsystem by xref'ing a FAMILY of strings.

Given a string prefix (e.g. "QosId"), find every matching string in the image,
then find every code location that references any of them, and group the results
by enclosing function. That yields the complete implementation surface of the
subsystem without needing symbols.

Usage: python subsystem_map.py <pe> <string-prefix> [--disasm]
"""
import bisect
import re
import struct
import sys
from collections import defaultdict

import capstone
import pefile

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True


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


def main():
    path = sys.argv[1]
    prefix = sys.argv[2]
    want_disasm = "--disasm" in sys.argv

    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    # ---- 1. find all matching strings (ascii) -----------------------------
    strs = {}
    pat = re.compile(re.escape(prefix.encode()) + rb"[A-Za-z0-9_]{2,60}")
    for s in pe.sections:
        blob = raw[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        for m in pat.finditer(blob):
            va = ib + s.VirtualAddress + m.start()
            strs[va] = m.group().decode("latin-1")

    print("file: %s" % path)
    print("strings matching %r: %d" % (prefix, len(strs)))
    for va in sorted(strs):
        print("   0x%X  %s" % (va, strs[va]))
    print()

    if not strs:
        return

    # ---- 2. find code referencing them ------------------------------------
    funcs = pdata(pe, raw)
    starts = [b for b, e in funcs]

    def func_at(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i < 0:
            return None
        b, e = funcs[i]
        return (b, e) if b <= rva < e else None

    t = sec(pe, ".text")
    code = raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
    base_va = ib + t.VirtualAddress

    lo = min(strs) - ib
    hi = max(strs) - ib
    # widen by a safe margin so (ptr,len) descriptors and offsets are caught
    lo -= 0x800
    hi += 0x800

    hits = defaultdict(list)
    CH = 0x40000
    for off in range(0, len(code), CH):
        chunk = code[off:off + CH]
        if not chunk:
            break
        try:
            for ins in CS.disasm(chunk, base_va + off):
                if ins.mnemonic not in ("lea", "mov"):
                    continue
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                        if lo <= tgt - ib <= hi:
                            f = func_at(ins.address - ib)
                            key = f[0] if f else -1
                            hits[key].append((ins.address, tgt))
        except Exception:
            pass

    print("=== functions referencing the %r family ===" % prefix)
    print("total functions: %d" % len(hits))
    for fs, lst in sorted(hits.items(), key=lambda kv: -len(kv[1])):
        star = "  <<< HOT" if len(lst) >= 5 else ""
        print("  func 0x%X   %d ref(s)%s" % (ib + fs if fs > 0 else 0, len(lst), star))
        for va, tgt in lst[:6]:
            s = strs.get(tgt, "")
            print("      0x%X -> 0x%X %s" % (va, tgt, ('"%s"' % s) if s else "(mid-table)"))
    print()

    if want_disasm:
        for fs, lst in sorted(hits.items(), key=lambda kv: -len(kv[1]))[:6]:
            if fs <= 0:
                continue
            f = func_at(fs)
            if not f:
                continue
            print("=" * 78)
            print("DISASM func 0x%X (size 0x%X, %d refs)" % (ib + fs, f[1] - f[0], len(lst)))
            print("=" * 78)
            off = rva_off(pe, fs)
            data = raw[off:off + (f[1] - f[0])]
            n = 0
            for ins in CS.disasm(data, ib + fs):
                ann = ""
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                        if tgt in strs:
                            ann = '   ; "%s"' % strs[tgt]
                        elif lo + ib <= tgt <= hi + ib:
                            ann = "   ; table 0x%X" % tgt
                print("  0x%-12X %-38s%s" % (ins.address, "%s %s" % (ins.mnemonic, ins.op_str), ann))
                n += 1
                if n > 120:
                    print("   ... truncated")
                    break
            print()


if __name__ == "__main__":
    main()
