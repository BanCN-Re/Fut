#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ace_upload.py - find the ACE upload/report path by xref'ing the network APIs.

Strategy: the reporting function MUST call one of
  WinHttpConnect / WinHttpSendRequest / InternetConnect / curl_easy_perform
or resolve a name (getaddrinfo / GetAddrInfoExW / GetAddrInfoW).
We disassemble .text, find every RIP-relative reference to those IAT slots,
group by enclosing .pdata function, and then print each candidate function's
disassembly so the caller chain becomes visible.

Usage: python ace_upload.py <ACE-Base64.dll> [--disasm VA] [--callers]
"""
import struct
import sys
from collections import defaultdict

import capstone
import pefile

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True

NET_IMPORTS = {
    "WinHttpConnect", "WinHttpSendRequest", "WinHttpOpenRequest", "WinHttpOpen",
    "WinHttpReceiveResponse", "WinHttpReadData", "WinHttpWriteData",
    "getaddrinfo", "GetAddrInfoExW", "GetAddrInfoW", "gethostbyname",
    "InternetConnectA", "InternetConnectW", "InternetOpenA", "InternetOpenW",
    "HttpSendRequestA", "HttpSendRequestW", "HttpOpenRequestA", "HttpOpenRequestW",
    "WSAConnect", "connect", "send", "WSASend", "InternetCrackUrlA",
}


def load(path):
    pe = pefile.PE(path, fast_load=False)
    raw = open(path, "rb").read()
    return pe, raw


def sec_of(pe, name):
    for s in pe.sections:
        if s.Name.rstrip(b"\0").decode("latin-1", "replace") == name:
            return s
    return None


def rva_off(pe, rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None


def parse_pdata(pe, raw):
    funcs = []
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[3]
    if not d.VirtualAddress:
        return funcs
    off = rva_off(pe, d.VirtualAddress)
    if off is None:
        return funcs
    for i in range(d.Size // 12):
        b, e, u = struct.unpack_from("<III", raw, off + i * 12)
        if b:
            funcs.append((b, e))
    funcs.sort()
    return funcs


def func_at(funcs, starts, rva):
    import bisect
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0:
        return None
    b, e = funcs[i]
    return (b, e) if b <= rva < e else None


def main():
    path = sys.argv[1]
    pe, raw = load(path)
    ib = pe.OPTIONAL_HEADER.ImageBase
    text = sec_of(pe, ".text")
    funcs = parse_pdata(pe, raw)
    starts = [b for b, e in funcs]

    # ---- collect IAT slots for the network imports
    slots = {}
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1", "replace").lower()
        for imp in entry.imports:
            nm = (imp.name or b"").decode("latin-1", "replace")
            if nm in NET_IMPORTS:
                slots[imp.address] = (nm, dll)

    print("file: %s" % path)
    print("network import slots: %d" % len(slots))
    for a, (n, d) in sorted(slots.items(), key=lambda kv: kv[0]):
        print("   0x%X  %-24s %s" % (a, n, d))

    # ---- scan .text for RIP-relative refs to those slots
    tstart = text.PointerToRawData
    code = raw[tstart:tstart + text.SizeOfRawData]
    text_va = ib + text.VirtualAddress

    by_func = defaultdict(list)
    CH = 0x40000
    for base in range(0, len(code), CH):
        chunk = code[base:base + CH]
        if not chunk:
            break
        try:
            for ins in CS.disasm(chunk, text_va + base):
                for op in ins.operands:
                    if (op.type == capstone.x86.X86_OP_MEM and
                            op.mem.base == capstone.x86.X86_REG_RIP):
                        tgt = ins.address + ins.size + op.mem.disp
                        if tgt in slots:
                            f = func_at(funcs, starts, ins.address - ib)
                            by_func[f[0] if f else -1].append(
                                (ins.address, slots[tgt][0], ins.mnemonic))
        except Exception:
            pass

    print()
    print("=== functions referencing network APIs ===")
    for fs, lst in sorted(by_func.items(), key=lambda kv: kv[0]):
        names = sorted({n for _, n, _ in lst})
        print("  func 0x%X  size 0x%-6X  ->  %s" % (
            ib + fs if fs > 0 else 0, 0, ", ".join(names)))
        for va, n, mn in lst[:8]:
            print("       0x%X  %-6s %s" % (va, mn, n))

    # ---- optional disassembly of a VA
    if "--disasm" in sys.argv:
        va = int(sys.argv[sys.argv.index("--disasm") + 1], 0)
        f = func_at(funcs, starts, va - ib)
        if not f:
            print("VA 0x%X not in a function" % va)
            return
        b, e = f
        print()
        print("=== disasm 0x%X .. 0x%X (size 0x%X) ===" % (ib + b, ib + e, e - b))
        off = rva_off(pe, b)
        data = raw[off:off + (e - b)]
        n = 0
        for ins in CS.disasm(data, ib + b):
            ann = ""
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    tgt = ins.address + ins.size + op.mem.disp
                    if tgt in slots:
                        ann = "   ; %s" % slots[tgt][0]
            print("  0x%-12X %-34s%s" % (ins.address, "%s %s" % (ins.mnemonic, ins.op_str), ann))
            n += 1
            if n > 400:
                print("  ... (truncated)")
                break


if __name__ == "__main__":
    main()
