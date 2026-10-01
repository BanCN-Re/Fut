#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""callgraph.py - build a caller/callee graph for a PE using capstone + .pdata.

Find:
  * all callers of a target function (walk up)
  * all callees of a function (walk down)
  * thread-entry candidates: functions whose address is taken as an immediate
    and passed to CreateThread/_beginthreadex, or stored in a thread-start slot
  * functions containing an infinite loop (jmp self / jmp back with no exit)

Usage:
    python callgraph.py <pe> --callers <hexVA> [depth]
    python callgraph.py <pe> --callees <hexVA>
    python callgraph.py <pe> --threads
    python callgraph.py <pe> --loops
"""
import bisect
import struct
import sys
from collections import defaultdict

import capstone
import pefile

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True


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
    if not d.VirtualAddress:
        return out
    off = rva_off(pe, d.VirtualAddress)
    if off is None:
        return out
    for i in range(d.Size // 12):
        b, e, u = struct.unpack_from("<III", raw, off + i * 12)
        if b:
            out.append((b, e))
    out.sort()
    return out


class Graph:
    def __init__(self, pe, raw):
        self.pe = pe
        self.raw = raw
        self.ib = pe.OPTIONAL_HEADER.ImageBase
        self.funcs = pdata(pe, raw)
        self.starts = [b for b, e in self.funcs]
        self.calls = defaultdict(set)      # caller_rva -> {callee_rva}
        self.imm = defaultdict(set)        # func_rva -> {immediate values}

    def func_at(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0:
            return None
        b, e = self.funcs[i]
        return (b, e) if b <= rva < e else None

    def build(self, verbose=True):
        t = sec(self.pe, ".text")
        code = self.raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
        base_va = self.ib + t.VirtualAddress
        CH = 0x40000
        for off in range(0, len(code), CH):
            chunk = code[off:off + CH]
            if not chunk:
                break
            try:
                for ins in CS.disasm(chunk, base_va + off):
                    f = self.func_at(ins.address - self.ib)
                    if not f:
                        continue
                    if ins.mnemonic == "call":
                        # direct rel32 call
                        for op in ins.operands:
                            if op.type == capstone.x86.X86_OP_IMM:
                                tgt = op.imm - self.ib
                                if self.func_at(tgt):
                                    self.calls[f[0]].add(tgt)
                    elif ins.mnemonic in ("mov", "lea", "push"):
                        # Immediates / RIP-relative addresses that could be a
                        # thread-entry address. On x64 the entry is normally
                        # taken with `lea rcx, [rip+Func]`, which is a MEM
                        # operand, not an IMM -- both forms must be collected.
                        for op in ins.operands:
                            if op.type == capstone.x86.X86_OP_IMM:
                                v = op.imm - self.ib
                                if self.func_at(v):
                                    self.imm[f[0]].add(v)
                            elif (op.type == capstone.x86.X86_OP_MEM and
                                  op.mem.base == capstone.x86.X86_REG_RIP):
                                tgt = ins.address + ins.size + op.mem.disp
                                v = tgt - self.ib
                                if self.func_at(v):
                                    self.imm[f[0]].add(v)
            except Exception:
                pass
        if verbose:
            print("callgraph built: %d functions, %d with calls, %d with code-addr immediates"
                  % (len(self.funcs), len(self.calls), len(self.imm)))

    def callers_of(self, target_rva):
        return sorted(c for c, s in self.calls.items() if target_rva in s)


def name_of(g, rva):
    return "0x%X" % (g.ib + rva)


def main():
    path = sys.argv[1]
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    g = Graph(pe, raw)
    g.build()

    if "--callers" in sys.argv:
        tgt = int(sys.argv[sys.argv.index("--callers") + 1], 0) - g.ib
        depth = int(sys.argv[sys.argv.index("--callers") + 2]) if len(sys.argv) > sys.argv.index("--callers") + 2 else 2
        print()
        print("=== callers of 0x%X (depth %d) ===" % (g.ib + tgt, depth))
        level = {tgt}
        seen = set()
        for d in range(depth):
            nxt = set()
            for t in level:
                for c in g.callers_of(t):
                    if c in seen:
                        continue
                    seen.add(c)
                    nxt.add(c)
                    print("  [d%d] 0x%X  calls 0x%X" % (d + 1, g.ib + c, g.ib + t))
            level = nxt
            if not level:
                break

    if "--callees" in sys.argv:
        tgt = int(sys.argv[sys.argv.index("--callees") + 1], 0) - g.ib
        print()
        print("=== callees of 0x%X ===" % (g.ib + tgt))
        for c in sorted(g.calls.get(tgt, [])):
            print("  0x%X" % (g.ib + c))

    if "--threads" in sys.argv:
        print()
        print("=== thread-entry candidates (address taken as immediate) ===")
        # find CreateThread / _beginthreadex IAT slots
        slots = {}
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = entry.dll.decode("latin-1", "replace").lower()
            for imp in entry.imports:
                nm = (imp.name or b"").decode("latin-1", "replace")
                if nm in ("CreateThread", "_beginthreadex", "_beginthread",
                          "RtlCreateUserThread", "CreateRemoteThread"):
                    slots[imp.address] = nm
        print("  thread APIs: %s" % ", ".join("%s@0x%X" % (v, k) for k, v in slots.items()))
        # for each function that touches a thread API, list code immediates
        t = sec(pe, ".text")
        code = raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
        base_va = g.ib + t.VirtualAddress
        users = set()
        CH = 0x40000
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
                                f = g.func_at(ins.address - g.ib)
                                if f:
                                    users.add((f[0], slots[tgt]))
            except Exception:
                pass
        for frva, api in sorted(users):
            cands = sorted(g.imm.get(frva, []))
            print("  func 0x%X uses %s ; code immediates: %s" % (
                g.ib + frva, api,
                " ".join("0x%X" % (g.ib + c) for c in cands[:8]) or "(none)"))

    if "--loops" in sys.argv:
        print()
        print("=== functions with a tight infinite loop (jmp to self / short back-jmp with no ret before) ===")
        t = sec(pe, ".text")
        for b, e in g.funcs:
            off = rva_off(pe, b)
            if off is None:
                continue
            data = raw[off:off + min(e - b, 0x4000)]
            has_ret = b"\xc3" in data
            try:
                insns = list(CS.disasm(data, g.ib + b))
            except Exception:
                continue
            loop = False
            for ins in insns:
                if ins.mnemonic == "jmp":
                    for op in ins.operands:
                        if op.type == capstone.x86.X86_OP_IMM:
                            if op.imm == ins.address:      # jmp self
                                loop = True
                            elif op.imm < ins.address and (ins.address - op.imm) < 0x200:
                                loop = True
            if loop and not has_ret and (e - b) < 0x600:
                print("  0x%X .. 0x%X  size 0x%X  (no RET, tight loop)" % (g.ib + b, g.ib + e, e - b))


if __name__ == "__main__":
    main()
