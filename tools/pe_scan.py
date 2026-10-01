#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pe_scan.py - PE section inventory + Shannon entropy, to detect VM/encrypted sections."""
import math
import struct
import sys
import os


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


def scan(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] != b"MZ":
        print("not a PE")
        return
    pe_off = struct.unpack_from("<I", raw, 0x3C)[0]
    if raw[pe_off:pe_off + 4] != b"PE\0\0":
        print("bad PE sig")
        return
    machine, nsec = struct.unpack_from("<HH", raw, pe_off + 4)
    opt_size = struct.unpack_from("<H", raw, pe_off + 20)[0]
    opt_off = pe_off + 24
    magic = struct.unpack_from("<H", raw, opt_off)[0]
    pe32plus = (magic == 0x20B)
    img_base = struct.unpack_from("<Q" if pe32plus else "<I", raw, opt_off + 24)[0]
    sec_off = opt_off + opt_size

    print("file      : %s" % path)
    print("size      : %d bytes (%.1f MB)" % (len(raw), len(raw) / 1048576))
    print("machine   : 0x%04X  (%s)" % (machine, "x64" if machine == 0x8664 else "x86"))
    print("imagebase : 0x%X" % img_base)
    print("sections  : %d" % nsec)
    print()
    print("%-10s %-10s %-10s %-10s %8s  %s" % (
        "name", "vaddr", "vsize", "rawsize", "entropy", "note"))
    print("-" * 74)

    tot = 0
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        chars = struct.unpack_from("<I", raw, o + 36)[0]
        data = raw[raddr:raddr + rsize] if rsize else b""
        ent = entropy(data) if data else 0.0
        tot += rsize
        note = ""
        if ent > 7.9:
            note = "HIGH-ENTROPY (packed/encrypted/compressed)"
        elif ent > 7.5:
            note = "elevated entropy"
        if name.startswith(".tvm") or "vmp" in name.lower() or "themida" in name.lower():
            note = "VM / protector section"
        exec_bit = "X" if chars & 0x20000000 else " "
        rd = "R" if chars & 0x40000000 else " "
        wr = "W" if chars & 0x80000000 else " "
        print("%-10s 0x%-8X 0x%-8X 0x%-8X %8.4f  [%s%s%s] %s" % (
            name, vaddr, vsize, rsize, ent, rd, wr, exec_bit, note))
    print("-" * 74)
    print("sum of raw section sizes: %d (%.1f MB)" % (tot, tot / 1048576))
    print("sum of virtual sizes    : %d (%.1f MB)" % (
        sum(struct.unpack_from("<I", raw, sec_off + i * 40 + 8)[0] for i in range(nsec)),
        sum(struct.unpack_from("<I", raw, sec_off + i * 40 + 8)[0] for i in range(nsec)) / 1048576))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        scan(p)
        print()
