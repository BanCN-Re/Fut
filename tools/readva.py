#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""readva.py - read bytes/strings at a VA inside a PE (ASCII + UTF-16).

Usage: python readva.py <pe> <hexVA> [n]
"""
import struct
import sys

import pefile


def main():
    path = sys.argv[1]
    va = int(sys.argv[2], 0)
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 64

    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    off = None
    secname = None
    for s in pe.sections:
        if s.VirtualAddress <= va - ib < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            off = s.PointerToRawData + (va - ib - s.VirtualAddress)
            secname = s.Name.rstrip(b"\0").decode("latin-1", "replace")
            break
    if off is None:
        print("VA 0x%X not mapped" % va)
        return

    b = raw[off:off + n]
    print("VA 0x%X  section %s" % (va, secname))
    print("  hex   : %s" % b.hex())
    try:
        a = b.split(b"\x00")[0].decode("latin-1")
        print("  ascii : %r" % a)
    except Exception:
        pass
    try:
        u = b.decode("utf-16-le")
        u = u.split("\x00")[0]
        if u and all(32 <= ord(c) < 127 for c in u):
            print("  utf16 : %r" % u)
    except Exception:
        pass


if __name__ == "__main__":
    main()
