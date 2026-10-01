#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sim_patch.py - SIMULATE our C++ delay-import patcher against the real webio.dll.

This is an independent check of the logic in PatchModuleDelayIATOrd: it walks
the delay-import directory exactly the way the C++ does and reports which slots
WOULD be patched for each (fn, ordinal) request. If a target we depend on does
not show up here, the C++ will not patch it either.

Usage: python sim_patch.py <module>
"""
import struct
import sys

import pefile

DLATTR_RVA = 0x1

# the ws2_32 ordinals our C++ passes explicitly
ORD_TARGETS = {23: "socket", 6: "connect"}
NAME_TARGETS = ["socket", "WSASocketW", "WSASocketA"]


def sec_for(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s
    return None


def off_for(pe, rva):
    s = sec_for(pe, rva)
    if not s:
        return None
    return s.PointerToRawData + (rva - s.VirtualAddress)


def main():
    path = sys.argv[1]
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    ib = pe.OPTIONAL_HEADER.ImageBase
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[13]
    if not d.VirtualAddress:
        print("no delay imports")
        return

    base_off = off_for(pe, d.VirtualAddress)
    print("simulating PatchModuleDelayIATAnyDll* on %s" % path.rsplit("\\", 1)[-1])
    print()

    patched = []

    for i in range(64):
        o = base_off + i * 32
        grAttrs, rvaDLLName, rvaHmod, rvaIAT, rvaINT, rvaB, rvaU, ts = \
            struct.unpack_from("<IIIIIIII", raw, o)
        if rvaDLLName == 0:
            break
        if not (grAttrs & DLATTR_RVA):
            continue
        no = off_for(pe, rvaDLLName)
        dll = raw[no:raw.find(b"\x00", no)].decode("latin-1", "replace")

        intRva = rvaINT or rvaIAT
        thunk_rva = intRva
        iat_rva = rvaIAT
        for k in range(4096):
            to = off_for(pe, thunk_rva)
            if to is None:
                break
            tad = struct.unpack_from("<Q", raw, to)[0]
            if tad == 0:
                break

            is_ord = bool(tad & (1 << 63))
            name = None
            ordv = 0
            if is_ord:
                ordv = tad & 0xFFFF
            else:
                io = off_for(pe, tad)
                if io is not None:
                    name = raw[io + 2:raw.find(b"\x00", io + 2)].decode("latin-1", "replace")

            # replicate the C++ match rule
            hit = None
            for want_ord, label in ORD_TARGETS.items():
                if is_ord and ordv == want_ord:
                    hit = "ord#%d -> %s" % (ordv, label)
            if not is_ord and name in NAME_TARGETS:
                hit = "name %s" % name

            if hit:
                ao = off_for(pe, iat_rva)
                cur = struct.unpack_from("<Q", raw, ao)[0]
                stub = ib <= cur < ib + 0x2000000
                patched.append((dll, iat_rva, hit, cur, stub))
                print("  WOULD PATCH  %-14s IAT VA=0x%-11X %-22s cur=0x%-11X stub=%s"
                      % (dll, ib + iat_rva, hit, cur, stub))

            thunk_rva += 8
            iat_rva += 8

    print()
    print("total slots our patcher would hit: %d" % len(patched))
    bydll = {}
    for dll, _, _, _, _ in patched:
        bydll[dll] = bydll.get(dll, 0) + 1
    for dll, n in sorted(bydll.items()):
        print("   %-16s %d slot(s)" % (dll, n))

    # correctness assertions
    print()
    print("=== assertions ===")
    got_socket = any("socket" in h for _, _, h, _, _ in patched)
    got_connect = any("connect" in h for _, _, h, _, _ in patched)
    got_wsa = any("WSASocketW" in h for _, _, h, _, _ in patched)
    print("  socket  (ord#23)  patched: %s" % got_socket)
    print("  connect (ord#6)   patched: %s" % got_connect)
    print("  WSASocketW (name) patched: %s" % got_wsa)
    allok = got_socket and got_connect and got_wsa
    print()
    print("RESULT: %s" % ("PASS - all critical slots reachable" if allok
                          else "FAIL - some critical slots NOT reachable"))


if __name__ == "__main__":
    main()
