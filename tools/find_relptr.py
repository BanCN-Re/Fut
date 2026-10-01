#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""find_relptr.py - find RIP-relative references to a VA (the Rust idiom).

Rust takes a function address as:
    lea  reg, [rip + disp]      ; absolute-address form
or stores it relative to a base:
    .long target - base         ; 32-bit relative in .data

This scans BOTH: every `lea reg,[rip+d]` and `mov reg,[rip+d]` whose target is
the given VA, plus every 32-bit dword in data sections that equals
(target - (dword_va + 4)), which is the classic relative-pointer table form.

Usage: python find_relptr.py <pe> <hexVA>
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


def main():
    path = sys.argv[1]
    va = int(sys.argv[2], 0)
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    def off_to(off):
        for s in pe.sections:
            if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
                return s.Name.rstrip(b"\0").decode("latin-1", "replace"), \
                       s.VirtualAddress + (off - s.PointerToRawData)
        return None, None

    # ---------- 1. code: lea/mov rip-relative ----------
    t = sec(pe, ".text")
    code = raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
    print("=== code refs to 0x%X (lea/mov rip-relative) ===" % va)
    n = 0
    CH = 0x40000
    for off in range(0, len(code), CH):
        chunk = code[off:off + CH]
        if not chunk:
            break
        try:
            for ins in CS.disasm(chunk, ib + t.VirtualAddress + off):
                if ins.mnemonic not in ("lea", "mov", "call", "jmp"):
                    continue
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        tgt = ins.address + ins.size + op.mem.disp
                        if tgt == va:
                            print("  0x%X  %s %s" % (ins.address, ins.mnemonic, ins.op_str))
                            n += 1
        except Exception:
            pass
    print("  %d hit(s)" % n)

    # ---------- 2. data: 32-bit relative pointers ----------
    print()
    print("=== data refs: dword == target - (va+4)  (relative pointer table) ===")
    for s in pe.sections:
        nm = s.Name.rstrip(b"\0").decode("latin-1", "replace")
        if nm not in (".rdata", ".data", ".ddata", "_RDATA"):
            continue
        blob = raw[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        cnt = 0
        for i in range(0, len(blob) - 4, 4):
            dv = struct.unpack_from("<i", blob, i)[0]
            here = ib + s.VirtualAddress + i
            if here + 4 + dv == va:
                print("  %s off=0x%-9X VA=0x%-11X  dword=%d -> 0x%X" % (nm, i, here, dv, va))
                cnt += 1
                if cnt > 20:
                    break
        if cnt:
            print("  (%s: %d)" % (nm, cnt))

    # ---------- 3. data: full qword-relative table (target - table_base) ----------
    print()
    print("=== data refs: qword relative to a base ===")
    for s in pe.sections:
        nm = s.Name.rstrip(b"\0").decode("latin-1", "replace")
        if nm not in (".rdata", ".data", ".ddata", "_RDATA"):
            continue
        blob = raw[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        for i in range(0, len(blob) - 8, 8):
            q = struct.unpack_from("<Q", blob, i)[0]
            here = ib + s.VirtualAddress + i
            if q and abs((here + q) - va) < 0x40:
                print("  %s VA=0x%-11X qword=0x%X -> 0x%X" % (nm, here, q, here + q))


if __name__ == "__main__":
    main()
