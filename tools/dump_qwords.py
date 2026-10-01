#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_qwords.py - dump qwords around a VA and resolve any that point to strings.

Rust stores &str as (ptr, len) pairs; the pointer is absolute. This walks a
window around a known string and labels every qword that resolves to text, which
reveals the layout of the surrounding table.

Usage: python dump_qwords.py <pe> <hexVA> [before] [after]
"""
import struct
import sys

import pefile


def main():
    path = sys.argv[1]
    va = int(sys.argv[2], 0)
    before = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    after = int(sys.argv[4]) if len(sys.argv) > 4 else 160

    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase
    img_hi = ib + 0x5000000

    def rd(v, n):
        for s in pe.sections:
            if s.VirtualAddress <= v - ib < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                o = s.PointerToRawData + (v - ib - s.VirtualAddress)
                return raw[o:o + n]
        return None

    start = va - before
    blob = rd(start, before + after)
    if blob is None:
        print("VA not mapped")
        return

    print("window 0x%X .. 0x%X" % (start, start + len(blob)))
    print()
    for i in range(0, len(blob) - 7, 8):
        q = struct.unpack_from("<Q", blob, i)[0]
        cur = start + i
        note = ""
        if ib <= q < img_hi:
            s = rd(q, 64)
            if s:
                t = s.split(b"\x00")[0]
                if len(t) >= 3 and all(9 <= c < 127 for c in t):
                    note = '-> "%s"' % t.decode("latin-1")
        print("  0x%-11X %016X  %s" % (cur, q, note))


if __name__ == "__main__":
    main()
