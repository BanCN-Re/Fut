#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""reach.py - which thread entry can REACH a target function?

Breadth-first over the call graph (callee direction). This answers "which
thread entry owns the reporting path" without needing symbols.

Usage: python reach.py <pe> --from A,B,C --to X,Y,Z
"""
import bisect
import struct
import sys
from collections import defaultdict, deque

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

    def func_at(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i < 0:
            return None
        b, e = funcs[i]
        return (b, e) if b <= rva < e else None

    t = sec(pe, ".text")
    code = raw[t.PointerToRawData:t.PointerToRawData + t.SizeOfRawData]
    base_va = ib + t.VirtualAddress

    callees = defaultdict(set)
    callers = defaultdict(set)
    print("building call graph...")
    CH = 0x40000
    for off in range(0, len(code), CH):
        chunk = code[off:off + CH]
        if not chunk:
            break
        try:
            for ins in CS.disasm(chunk, base_va + off):
                if ins.mnemonic != "call":
                    continue
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_IMM:
                        f = func_at(ins.address - ib)
                        tgt = op.imm - ib
                        if f and func_at(tgt):
                            callees[f[0]].add(tgt)
                            callers[tgt].add(f[0])
        except Exception:
            pass
    print("graph: %d nodes with callees" % len(callees))

    if "--from" not in sys.argv or "--to" not in sys.argv:
        print("need --from and --to")
        return
    froms = [int(x, 0) - ib for x in sys.argv[sys.argv.index("--from") + 1].split(",")]
    tos = {int(x, 0) - ib for x in sys.argv[sys.argv.index("--to") + 1].split(",")}

    for f in froms:
        # BFS down, record shortest path
        prev = {f: None}
        q = deque([f])
        hit = None
        while q:
            cur = q.popleft()
            if cur in tos and cur != f:
                hit = cur
                break
            for nxt in callees.get(cur, ()):
                if nxt not in prev:
                    prev[nxt] = cur
                    q.append(nxt)
        print()
        print("=== from 0x%X ===" % (ib + f))
        if hit is None:
            print("   no path to target(s)")
            continue
        path = []
        n = hit
        while n is not None:
            path.append(n)
            n = prev[n]
        path.reverse()
        print("   PATH (%d hops):" % (len(path) - 1))
        for i, p in enumerate(path):
            print("     %s0x%X" % ("  " * min(i, 8), ib + p))


if __name__ == "__main__":
    main()
