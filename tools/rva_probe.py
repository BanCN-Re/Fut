#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""rva_probe.py - dump bytes + local entropy at given RVAs, and detect
whether code at those RVAs is plaintext machine code or encrypted/packed."""
import math
import struct
import sys


def entropy(data):
    if not data:
        return 0.0
    freq = [0] * 256
    for b in data:
        freq[b] += 1
    n = len(data)
    e = 0.0
    for c in freq:
        if c:
            p = c / n
            e -= p * math.log2(p)
    return e


def load_sections(path):
    with open(path, "rb") as f:
        raw = f.read()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<HH", raw, pe + 4)[1]
    opt_size = struct.unpack_from("<H", raw, pe + 20)[0]
    opt = pe + 24
    sec_off = opt + opt_size
    secs = []
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return raw, secs


def rva_to_off(rva, secs):
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr), name
    return None, None


def main():
    path = sys.argv[1]
    rvas = [int(x, 0) for x in sys.argv[2:]]
    raw, secs = load_sections(path)
    print("file: %s" % path)
    print()
    for rva in rvas:
        off, sec = rva_to_off(rva, secs)
        if off is None:
            print("RVA 0x%X -> not in any section" % rva)
            continue
        chunk = raw[off:off + 64]
        e64 = entropy(chunk)
        e4k = entropy(raw[max(0, off - 2048):off + 2048])
        hexs = " ".join("%02X" % b for b in chunk[:32])
        # crude x64 prologue heuristics
        hints = []
        if chunk[:1] in (b"\x48", b"\x4C", b"\x40"):
            hints.append("REX-prefixed (typical x64 prologue)")
        if b"\xC3" in chunk:
            hints.append("contains RET (C3)")
        if b"\xCC" in chunk:
            hints.append("contains INT3 padding")
        if chunk[:1] == b"\xE9" or chunk[:1] == b"\xE8":
            hints.append("starts with JMP/CALL rel32")
        print("RVA 0x%-9X sec=%-6s ent64=%.3f ent4k=%.3f" % (rva, sec, e64, e4k))
        print("   bytes: %s" % hexs)
        if hints:
            print("   hints: %s" % "; ".join(hints))
        print()


if __name__ == "__main__":
    main()
