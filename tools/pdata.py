#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pdata.py - parse the x64 .pdata (RUNTIME_FUNCTION) table to get exact
function boundaries.

For x64 PE images the exception directory lists one RUNTIME_FUNCTION per
function: {BeginAddress, EndAddress, UnwindInfoAddress} as RVAs. That is a
ground-truth function map, far better than scanning for prologues.

Usage:
    python pdata.py <pe> --find <hexRVA>      # function containing an RVA
    python pdata.py <pe> --near <hexRVA> [n]  # n functions around it
    python pdata.py <pe> --stats
"""
import struct
import sys


def load(path):
    raw = open(path, "rb").read()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<HH", raw, pe + 4)[1]
    optsz = struct.unpack_from("<H", raw, pe + 20)[0]
    opt = pe + 24
    plus = struct.unpack_from("<H", raw, opt)[0] == 0x20B
    imgbase = struct.unpack_from("<Q" if plus else "<I", raw, opt + 24)[0]
    # data directory 3 = exception table
    ddir_off = opt + (112 if plus else 96)
    exc_rva, exc_size = struct.unpack_from("<II", raw, ddir_off + 3 * 8)
    sec_off = opt + optsz
    secs = []
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return raw, imgbase, secs, exc_rva, exc_size


def rva_to_off(rva, secs):
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr), name
    return None, None


def parse_funcs(raw, secs, exc_rva, exc_size):
    off, sec = rva_to_off(exc_rva, secs)
    if off is None:
        return []
    funcs = []
    n = exc_size // 12
    for i in range(n):
        b, e, u = struct.unpack_from("<III", raw, off + i * 12)
        if b == 0:
            continue
        funcs.append((b, e, u))
    funcs.sort()
    return funcs


def main():
    path = sys.argv[1]
    raw, imgbase, secs, exc_rva, exc_size = load(path)
    funcs = parse_funcs(raw, secs, exc_rva, exc_size)

    print("file       : %s" % path)
    print("imagebase  : 0x%X" % imgbase)
    print("pdata      : rva 0x%X size 0x%X" % (exc_rva, exc_size))
    print("functions  : %d" % len(funcs))
    print()

    if "--stats" in sys.argv or len(sys.argv) == 2:
        sizes = [e - b for b, e, u in funcs]
        sizes.sort()
        if sizes:
            print("size min/med/max: 0x%X / 0x%X / 0x%X" % (
                sizes[0], sizes[len(sizes) // 2], sizes[-1]))
        return

    if "--find" in sys.argv:
        rva = int(sys.argv[sys.argv.index("--find") + 1], 0)
        for b, e, u in funcs:
            if b <= rva < e:
                print("RVA 0x%X is INSIDE function 0x%X .. 0x%X  (size 0x%X)" % (rva, b, e, e - b))
                print("  start VA 0x%X" % (imgbase + b))
                print("  offset into function: 0x%X" % (rva - b))
                print("  unwind info RVA 0x%X" % u)
                return
        print("RVA 0x%X not inside any .pdata function" % rva)
        return

    if "--near" in sys.argv:
        rva = int(sys.argv[sys.argv.index("--near") + 1], 0)
        n = int(sys.argv[sys.argv.index("--near") + 2]) if len(sys.argv) > sys.argv.index("--near") + 2 else 10
        idx = 0
        for i, (b, e, u) in enumerate(funcs):
            if b <= rva < e:
                idx = i
                break
        lo = max(0, idx - n // 2)
        for b, e, u in funcs[lo:lo + n]:
            mark = "  <<< TARGET" if b <= rva < e else ""
            print("  RVA 0x%-9X .. 0x%-9X  size 0x%-7X VA 0x%X%s" % (
                b, e, e - b, imgbase + b, mark))
        return


if __name__ == "__main__":
    main()
