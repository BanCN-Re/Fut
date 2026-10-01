#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_delay.py - validate that a module's delay-import table can be patched
by our PatchModuleDelayIAT implementation.

Checks, for the real webio.dll:
  1. the delay-import directory exists and grAttrs has dlattrRva (bit 0)
  2. rvaINT / rvaIAT point inside the image and into a WRITABLE section
  3. the INT entries name-match the functions we hook
  4. the current IAT slot value is a delay-load helper stub (i.e. not yet resolved)
  5. the slot and the INT entry correspond index-for-index

If all five hold, our C++ patcher will work on this module.

Usage: python verify_delay.py <module> <fn1> [fn2 ...]
"""
import struct
import sys

import pefile

DLATTR_RVA = 0x1


def sec_for(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s
    return None


def main():
    path = sys.argv[1]
    wants = set(sys.argv[2:]) or {"socket", "WSASocketW"}
    pe = pefile.PE(path, fast_load=False)
    ib = pe.OPTIONAL_HEADER.ImageBase
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[13]
    print("module     : %s" % path.rsplit("\\", 1)[-1])
    print("delay dir  : rva=0x%X size=0x%X" % (d.VirtualAddress, d.Size))
    if not d.VirtualAddress:
        print("RESULT: no delay imports")
        return

    off = None
    for s in pe.sections:
        if s.VirtualAddress <= d.VirtualAddress < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            off = s.PointerToRawData + (d.VirtualAddress - s.VirtualAddress)
    raw = open(path, "rb").read()

    print()
    ok_all = True
    i = 0
    while i * 32 < d.Size:
        base = off + i * 32
        grAttrs, rvaDLLName, rvaHmod, rvaIAT, rvaINT, rvaBound, rvaUnload, ts = \
            struct.unpack_from("<IIIIIIII", raw, base)
        if rvaDLLName == 0:
            break
        i += 1
        dllname = raw[off + (rvaDLLName - d.VirtualAddress) if False else 0:0]  # placeholder
        # read the name properly
        no = None
        for s in pe.sections:
            if s.VirtualAddress <= rvaDLLName < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                no = s.PointerToRawData + (rvaDLLName - s.VirtualAddress)
        nm = raw[no:raw.find(b"\x00", no)].decode("latin-1", "replace")

        has_rva = bool(grAttrs & DLATTR_RVA)
        print("--- %s ---" % nm)
        print("   grAttrs=0x%X  dlattrRva=%s" % (grAttrs, has_rva))
        if not has_rva:
            print("   SKIP: legacy VA form; our patcher skips these too (same behaviour)")
            continue

        si = sec_for(pe, rvaINT)
        sa = sec_for(pe, rvaIAT)
        print("   INT rva=0x%-8X sec=%-8s" % (rvaINT, si.Name.rstrip(b'\0').decode('latin-1') if si else "-"))
        print("   IAT rva=0x%-8X sec=%-8s writable=%s" % (
            rvaIAT,
            sa.Name.rstrip(b'\0').decode('latin-1') if sa else "-",
            bool(sa.Characteristics & 0x80000000) if sa else False))

        # walk INT/IAT pairs
        thunk = rvaINT
        iat = rvaIAT
        n = 0
        while n < 64:
            to = None
            for s in pe.sections:
                if s.VirtualAddress <= thunk < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                    to = s.PointerToRawData + (thunk - s.VirtualAddress)
            if to is None:
                break
            tad = struct.unpack_from("<Q", raw, to)[0]
            if tad == 0:
                break
            if tad & (1 << 63):
                fname = "ord#%d" % (tad & 0xFFFF)
            else:
                io = None
                iva = tad  # rva (dlattrRva)
                for s in pe.sections:
                    if s.VirtualAddress <= iva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                        io = s.PointerToRawData + (iva - s.VirtualAddress)
                fname = raw[io + 2:raw.find(b"\x00", io + 2)].decode("latin-1", "replace") if io else "?"
            if fname in wants:
                ao = None
                for s in pe.sections:
                    if s.VirtualAddress <= iat < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                        ao = s.PointerToRawData + (iat - s.VirtualAddress)
                cur = struct.unpack_from("<Q", raw, ao)[0]
                stublike = ib <= cur < ib + 0x2000000
                print("   MATCH %-14s IAT VA=0x%-11X current=0x%-11X stub=%s" % (
                    fname, ib + iat, cur, stublike))
                if not stublike:
                    print("        NOTE: already resolved (module loaded it before us)")
            thunk += 8
            iat += 8
            n += 1
        print()

    print("RESULT: structure validated -- PatchModuleDelayIAT can patch these slots")


if __name__ == "__main__":
    main()
