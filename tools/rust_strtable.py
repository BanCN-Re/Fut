#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""rust_strtable.py - decode a Rust-style &str table.

Rust binaries store string literals as a table of (pointer, length) pairs in
.rdata/.data, plus the UTF-8 bytes elsewhere. Given a region, this prints every
resolvable (ptr,len) -> string, which reveals the full set of log/ID names a
reporting subsystem uses.

Usage: python rust_strtable.py <pe> <hexVA_start> <hexVA_end>
"""
import struct
import sys

import pefile


def rva_off(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None


def read(pe, raw, va, n):
    off = rva_off(pe, va - pe.OPTIONAL_HEADER.ImageBase)
    if off is None:
        return None
    return raw[off:off + n]


def main():
    path = sys.argv[1]
    lo = int(sys.argv[2], 0)
    hi = int(sys.argv[3], 0)
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    print("scanning 0x%X .. 0x%X for (ptr,len) string descriptors" % (lo, hi))
    print()
    found = []
    va = lo
    while va < hi - 16:
        b = read(pe, raw, va, 16)
        if not b:
            va += 8
            continue
        ptr, ln = struct.unpack_from("<QQ", b, 0)
        if 0 < ln < 300 and ib <= ptr < ib + 0x5000000:
            s = read(pe, raw, ptr, ln)
            if s and all(9 <= c < 127 or c in (10, 13) for c in s):
                txt = s.decode("latin-1")
                found.append((va, ptr, ln, txt))
                va += 16
                continue
        va += 8

    for va, ptr, ln, txt in found:
        print("  desc@0x%-11X ptr=0x%-11X len=%-4d %s" % (va, ptr, ln, txt))
    print()
    print("total: %d descriptors" % len(found))


if __name__ == "__main__":
    main()
