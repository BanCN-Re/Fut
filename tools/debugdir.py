#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""debugdir.py - dump the PE debug directory (PDB path, GUID, age).

If a symbol like "report_bk" does not appear anywhere as a string, the usual
explanation is that it is a *symbol name from a PDB*, not a literal. This
prints the PDB reference stored in the image so that can be confirmed.

Usage: python debugdir.py <pe>
"""
import struct
import sys

import pefile


def main():
    path = sys.argv[1]
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase

    print("file: %s" % path)

    # debug data directory = index 6
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[6]
    print("debug dir: rva 0x%X size 0x%X" % (d.VirtualAddress, d.Size))
    if not d.VirtualAddress:
        print("  (no debug directory)")
        return

    def rd(rva, n):
        for s in pe.sections:
            if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                o = s.PointerToRawData + (rva - s.VirtualAddress)
                return raw[o:o + n]
        return None

    n = d.Size // 28
    for i in range(n):
        b = rd(d.VirtualAddress + i * 28, 28)
        if not b:
            continue
        (chars, ts, major, minor, typ, size, addr, ptr) = struct.unpack("<IIHHIIII", b)
        print()
        print("  entry %d: type=%d size=%d addr=0x%X ptr=0x%X" % (i, typ, size, addr, ptr))
        if ptr and size:
            for s in pe.sections:
                if s.PointerToRawData <= ptr < s.PointerToRawData + s.SizeOfRawData:
                    blob = raw[ptr:ptr + size]
                    break
            else:
                blob = None
            if blob:
                if typ == 2:   # CODEVIEW
                    sig = blob[:4]
                    if sig == b"RSDS":
                        guid = blob[4:20]
                        age = struct.unpack_from("<I", blob, 20)[0]
                        pdb = blob[24:].split(b"\x00")[0].decode("latin-1", "replace")
                        g = "-".join([
                            guid[0:4][::-1].hex(), guid[4:6][::-1].hex(),
                            guid[6:8][::-1].hex(), guid[8:10].hex(),
                            guid[10:16].hex()])
                        print("       PDB (RSDS): %s" % pdb)
                        print("       GUID: %s  Age: %d" % (g, age))
                    elif sig == b"NB10":
                        print("       PDB (NB10): %s" % blob[16:].split(b"\x00")[0].decode("latin-1", "replace"))
                    else:
                        print("       unknown codeview sig %r" % sig)


if __name__ == "__main__":
    main()
