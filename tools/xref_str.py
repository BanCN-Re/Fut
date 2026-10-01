#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xref_str.py - find code that references a given string VA (RIP-relative).

This is what locates the reporting function when the endpoint is looked up via
an ID table rather than an inline literal: the "QosId..." names cluster, and the
code that walks that cluster is the reporting state machine.

Usage: python xref_str.py <pe> <hexVA> [radius]
       python xref_str.py <pe> --scan-range <hexStartVA> <hexEndVA>
"""
import struct
import sys

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
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase
    funcs = pdata(pe, raw)
    starts = [b for b, e in funcs]

    import bisect
    def func_at(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i < 0:
            return None
        b, e = funcs[i]
        return (b, e) if b <= rva < e else None

    if sys.argv[2] == "--scan-range":
        lo = int(sys.argv[3], 0) - ib
        hi = int(sys.argv[4], 0) - ib
        targets = set(range(lo, hi, 8))   # align to 8 for LEA hits
    else:
        va = int(sys.argv[2], 0)
        rad = int(sys.argv[3]) if len(sys.argv) > 3 else 0x400
        targets = set(range(va - ib - rad, va - ib + rad))

    t = sec(pe, ".text")
    code = raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
    base_va = ib + t.VirtualAddress

    hits = {}
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
                        if (tgt - ib) in targets:
                            f = func_at(ins.address - ib)
                            key = f[0] if f else -1
                            hits.setdefault(key, []).append((ins.address, tgt))
        except Exception:
            pass

    print("=== code referencing the string range ===")
    for fs, lst in sorted(hits.items()):
        print("  func 0x%X  (%d refs)" % (ib + fs if fs > 0 else 0, len(lst)))
        for va, tgt in lst[:10]:
            print("      0x%X  ->  0x%X" % (va, tgt))


if __name__ == "__main__":
    main()
