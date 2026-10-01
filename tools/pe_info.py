#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pe_info.py - print image base, entry point, and section map for a PE."""
import struct
import sys


def info(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] != b"MZ":
        print("not a PE: %s" % path)
        return
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    machine, nsec = struct.unpack_from("<HH", raw, pe + 4)
    optsz = struct.unpack_from("<H", raw, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", raw, opt)[0]
    plus = (magic == 0x20B)
    imgbase = struct.unpack_from("<Q" if plus else "<I", raw, opt + 24)[0]
    entry = struct.unpack_from("<I", raw, opt + 16)[0]
    sec_off = opt + optsz

    print("file       : %s" % path)
    print("size       : %d bytes" % len(raw))
    print("machine    : 0x%04X" % machine)
    print("magic      : 0x%X (%s)" % (magic, "PE32+" if plus else "PE32"))
    print("imagebase  : 0x%X" % imgbase)
    print("entry RVA  : 0x%X" % entry)
    print("entry VA   : 0x%X" % (imgbase + entry))
    print()
    print("%-10s %-12s %-12s %-12s %-12s" % ("name", "vaddr", "vsize", "rawptr", "rawsize"))
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        print("%-10s 0x%-10X 0x%-10X 0x%-10X 0x%-10X" % (name, vaddr, vsize, raddr, rsize))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        info(p)
        print()
