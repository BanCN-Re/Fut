#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""find_ptr.py - find where a VA is stored as a qword (pointer table / vtable).

Thread entries and Rust trait objects are usually referenced from data tables,
not from code, so a code xref search finds nothing. This scans the whole image
for the 8-byte little-endian pointer.

Usage: python find_ptr.py <pe> <hexVA> [--all]
"""
import struct
import sys

import pefile


def main():
    path = sys.argv[1]
    va = int(sys.argv[2], 0)
    pat = struct.pack("<Q", va)

    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    def off_to_desc(off):
        for s in pe.sections:
            if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
                return s.Name.rstrip(b"\0").decode("latin-1", "replace"), \
                       s.VirtualAddress + (off - s.PointerToRawData)
        return None, None

    print("searching for pointer to 0x%X" % va)
    n = 0
    start = 0
    while True:
        i = raw.find(pat, start)
        if i < 0:
            break
        start = i + 1
        sec, rva = off_to_desc(i)
        print("  off=0x%-9X rva=0x%-9X VA=0x%-11X sec=%s" % (i, rva or 0, ib + (rva or 0), sec))
        n += 1
        if n > 60:
            print("  ... more")
            break
    print("total: %d occurrence(s)" % n)

    # also scan for the value with the low bytes only (relocated forms)
    print()
    print("=== nearby table entries (dump 8 qwords around each hit) ===")
    start = 0
    shown = 0
    while shown < 6:
        i = raw.find(pat, start)
        if i < 0:
            break
        start = i + 1
        base = i - 32
        sec, rva = off_to_desc(base)
        if sec is None:
            continue
        print("  --- around 0x%X (sec %s) ---" % (ib + rva, sec))
        for k in range(0, 80, 8):
            q = struct.unpack_from("<Q", raw, base + k)[0]
            mark = "  <<< TARGET" if q == va else ""
            note = ""
            if ib <= q < ib + 0x5000000:
                note = "(code/data ptr)"
            print("     0x%-11X 0x%016X %s%s" % (ib + rva + k, q, note, mark))
        shown += 1


if __name__ == "__main__":
    main()
