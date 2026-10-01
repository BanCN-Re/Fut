#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""resolve_lea.py - resolve LEA RIP-relative targets inside a function to their
strings, which is how a lazy GetProcAddress resolver names the APIs it loads.

Usage: python resolve_lea.py <pe> <function-VA-hex> [maxinsn]
"""
import struct
import sys

import capstone
import pefile

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True


def rva_off(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None


def read_str(raw, pe, va, maxlen=200):
    off = rva_off(pe, va - pe.OPTIONAL_HEADER.ImageBase)
    if off is None:
        return None
    blob = raw[off:off + maxlen]
    end = blob.find(b"\x00")
    if end <= 0:
        return None
    s = blob[:end]
    if not all(32 <= c < 127 for c in s):
        return None
    return s.decode("latin-1")


def read_wstr(raw, pe, va, maxlen=300):
    off = rva_off(pe, va - pe.OPTIONAL_HEADER.ImageBase)
    if off is None:
        return None
    blob = raw[off:off + maxlen]
    out = []
    for i in range(0, len(blob) - 1, 2):
        if blob[i] == 0 and blob[i + 1] == 0:
            break
        if blob[i + 1] != 0 or not (32 <= blob[i] < 127):
            return None
        out.append(chr(blob[i]))
    return "".join(out) if len(out) >= 4 else None


def func_bounds(pe, raw, va):
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    off = rva_off(pe, d.VirtualAddress)
    rva = va - pe.OPTIONAL_HEADER.ImageBase
    for i in range(d.Size // 12):
        b, e, u = struct.unpack_from("<III", raw, off + i * 12)
        if b <= rva < e:
            return b, e
    return None, None


def main():
    path = sys.argv[1]
    va = int(sys.argv[2], 0)
    maxinsn = int(sys.argv[3]) if len(sys.argv) > 3 else 250

    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    b, e = func_bounds(pe, raw, va)
    if b is None:
        print("VA 0x%X not in .pdata" % va)
        return
    print("func 0x%X .. 0x%X  (size 0x%X)" % (ib + b, ib + e, e - b))
    print()
    off = rva_off(pe, b)
    data = raw[off:off + (e - b)]

    n = 0
    for ins in CS.disasm(data, ib + b):
        note = ""
        if ins.mnemonic == "lea" and "[rip" in ins.op_str:
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    tgt = ins.address + ins.size + op.mem.disp
                    s = read_str(raw, pe, tgt)
                    w = read_wstr(raw, pe, tgt) if not s else None
                    if s:
                        note = "   ; \"%s\"" % s
                    elif w:
                        note = "   ; L\"%s\"" % w
                    else:
                        note = "   ; 0x%X" % tgt
        elif ins.mnemonic == "call" and "[rip" in ins.op_str:
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    tgt = ins.address + ins.size + op.mem.disp
                    note = "   ; IAT 0x%X" % tgt
        print("  0x%-12X %-40s%s" % (ins.address, "%s %s" % (ins.mnemonic, ins.op_str), note))
        n += 1
        if n > maxinsn:
            print("  ... truncated")
            break


if __name__ == "__main__":
    main()
