#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fn_table.py - decode a function-pointer / vtable region in a PE.

Rust and C++ both build dispatch tables as arrays of absolute function
pointers, sometimes interleaved with (ptr,len) string descriptors. This walks a
region, classifies each qword as code / string / data, and resolves the string
descriptors so the table's meaning is readable.

Usage: python fn_table.py <pe> <hexVA_start> <hexVA_end>
"""
import struct
import sys

import pefile


def main():
    path = sys.argv[1]
    lo = int(sys.argv[2], 0)
    hi = int(sys.argv[3], 0)
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    secs = []
    for s in pe.sections:
        secs.append((s.Name.rstrip(b"\0").decode("latin-1", "replace"),
                     s.VirtualAddress, s.Misc_VirtualSize, s.SizeOfRawData,
                     s.PointerToRawData))

    def rd(va, n):
        for nm, va0, vs, rs, pr in secs:
            if va0 <= va - ib < va0 + max(vs, rs):
                o = pr + (va - ib - va0)
                return raw[o:o + n], nm
        return None, None

    # code ranges from .pdata
    import bisect
    funcs = []
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    for nm, va0, vs, rs, pr in secs:
        if va0 <= d.VirtualAddress < va0 + max(vs, rs):
            o = pr + (d.VirtualAddress - va0)
            for i in range(d.Size // 12):
                b, e, u = struct.unpack_from("<III", raw, o + i * 12)
                if b:
                    funcs.append((b + ib, e + ib))
    funcs.sort()
    fstarts = [f[0] for f in funcs]

    def is_func(va):
        i = bisect.bisect_right(fstarts, va) - 1
        return i >= 0 and funcs[i][0] <= va < funcs[i][1]

    def is_str(va):
        b, nm = rd(va, 64)
        if not b:
            return None
        t = b.split(b"\x00")[0]
        if len(t) >= 3 and all(9 <= c < 127 for c in t):
            return t.decode("latin-1")
        return None

    print("region 0x%X .. 0x%X  (%d bytes)" % (lo, hi, hi - lo))
    print()
    blob, secname = rd(lo, hi - lo)
    if blob is None:
        print("region not mapped")
        return
    print("section: %s" % secname)
    print()
    print("%-14s %-14s %s" % ("addr", "value", "classification"))
    print("-" * 78)
    for i in range(0, len(blob) - 7, 8):
        va = lo + i
        q = struct.unpack_from("<Q", blob, i)[0]
        note = ""
        cls = "data"
        if ib <= q < ib + 0x5000000:
            if is_func(q):
                cls = "FUNC"
                note = "-> 0x%X" % q
            else:
                s = is_str(q)
                if s:
                    cls = "STR "
                    note = '-> "%s"' % s
                else:
                    r, sn = rd(q, 8)
                    if r:
                        cls = "PTR "
                        note = "-> 0x%X (in %s)" % (q, sn)
        if cls != "data" or q != 0:
            print("0x%-12X 0x%016X  %-5s %s" % (va, q, cls, note))


if __name__ == "__main__":
    main()
