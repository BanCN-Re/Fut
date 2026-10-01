#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""qos_table.py - parse the QosIdCsChannel reporting dispatch table.

Layout observed in ACE-Base64.dll at 0x1818A5900:
    each channel entry is a fixed-size record containing
      [0] implementation function pointer (or code pointer)
      [1] auxiliary pointer
      [2] error-string pointer  (e.g. "QosIdCsChannelInitFailed")
      [3] error-string pointer
      [4] more function pointers ...
    followed by the inline UTF-8 name of the NEXT record.

We walk records by locating each inline "QosIdCsChannel*" name and printing the
function pointers that precede/follow it, which identifies the per-channel
implementation block.

Usage: python qos_table.py <pe> <hexVA_start> <hexVA_end>
"""
import re
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

    def rd(va, n):
        for s in pe.sections:
            if s.VirtualAddress <= va - ib < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                o = s.PointerToRawData + (va - ib - s.VirtualAddress)
                return raw[o:o + n]
        return None

    blob = rd(lo, hi - lo)
    if blob is None:
        print("unmapped")
        return

    # locate inline strings
    marks = []
    for m in re.finditer(rb"QosIdCsChannel[A-Za-z]+", blob):
        marks.append((lo + m.start(), m.group().decode("latin-1")))
    # also catch the trailing "2" record separator strings
    for m in re.finditer(rb"[\x20-\x7e]{6,40}", blob):
        s = m.group().decode("latin-1")
        if s.startswith("QosId") and not any(s == t for _, t in marks):
            marks.append((lo + m.start(), s))
    marks.sort()
    marks = list(dict.fromkeys(marks))

    print("inline strings found: %d" % len(marks))
    for va, s in marks:
        print("  0x%X  %s" % (va, s))
    print()

    # For each mark, dump the 6 qwords before it (the entry's function pointers)
    for va, s in marks:
        print("=== %s  @0x%X ===" % (s, va))
        for back in range(-6, 2):
            a = va + back * 8
            b = rd(a, 8)
            if not b:
                continue
            q = struct.unpack("<Q", b)[0]
            note = ""
            if ib <= q < ib + 0x5000000:
                t = rd(q, 48)
                if t:
                    txt = t.split(b"\x00")[0]
                    if len(txt) >= 3 and all(9 <= c < 127 for c in txt):
                        note = 'str "%s"' % txt.decode("latin-1")
                if not note:
                    note = "code/data 0x%X" % q
            print("   [%+d] 0x%-11X 0x%016X  %s" % (back, a, q, note))
        print()


if __name__ == "__main__":
    main()
