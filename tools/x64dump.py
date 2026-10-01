#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""x64dump.py - dump and lightly disassemble bytes at a VA inside a PE.

RVA is derived from the PE image base, so the address matches what you see in
IDA. A tiny x64 length-decoder lets us walk instructions without a full
disassembler, plus a call/jmp target resolver.

Usage:
    python x64dump.py <pe> <hexVA> [count]
"""
import struct
import sys

# --- minimal x86-64 instruction length decoder (enough for prologue scanning)
REX = set(range(0x40, 0x50))
ONE_BYTE_NO_MODRM = set(
    list(range(0x50, 0x60)) + [0x90, 0x98, 0x99, 0xC3, 0xC9, 0xCC, 0xF4,
                               0xF8, 0xF9, 0xFC, 0xFD, 0x9C, 0x9D, 0xCB, 0xCE]
    + list(range(0x91, 0x98)) + list(range(0xB0, 0xB8))
)


def load(path):
    raw = open(path, "rb").read()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<HH", raw, pe + 4)[1]
    optsz = struct.unpack_from("<H", raw, pe + 20)[0]
    opt = pe + 24
    plus = struct.unpack_from("<H", raw, opt)[0] == 0x20B
    imgbase = struct.unpack_from("<Q" if plus else "<I", raw, opt + 24)[0]
    sec_off = opt + optsz
    secs = []
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return raw, imgbase, secs


def va_to_off(va, imgbase, secs):
    rva = va - imgbase
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr), name
    return None, None


def decode_len(b, i):
    """Return instruction length or None."""
    start = i
    # prefixes
    while i < len(b) and b[i] in (0x66, 0x67, 0xF0, 0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64, 0x65):
        i += 1
    rex = 0
    if i < len(b) and b[i] in REX:
        rex = b[i]
        i += 1
    if i >= len(b):
        return None
    op = b[i]
    i += 1

    # two-byte opcodes
    if op == 0x0F:
        if i >= len(b):
            return None
        op2 = b[i]
        i += 1
        # 0F 1F /0 = nop Ev
        if op2 == 0x1F:
            modrm = b[i] if i < len(b) else 0
            i += 1
            return (i - start) + modrm_len(op, rex, b, i - 1)
        # 0F B6/B7 BE/BF etc: modrm
        i += modrm_len(op, rex, b, i - 1)
        return i - start

    if op in ONE_BYTE_NO_MODRM:
        # push/pop regs carry an immediate operand
        if 0x50 <= op <= 0x5F:
            return i - start
        if 0xB0 <= op <= 0xB7:
            return (i - start) + 1
        if 0xB8 <= op <= 0xBF:
            return (i - start) + (8 if (rex & 8) else 4)
        return i - start

    if op in (0xE8, 0xE9):
        return (i - start) + 4
    if op == 0xEB:
        return (i - start) + 1
    if 0x70 <= op <= 0x7F:
        return (i - start) + 1
    if op in (0xC2,):
        return (i - start) + 2
    if op == 0xC6:
        i += modrm_len(op, rex, b, i)
        return (i - start) + 1
    if op == 0xC7:
        i += modrm_len(op, rex, b, i)
        return (i - start) + 4

    # generic modrm opcodes
    i += modrm_len(op, rex, b, i)
    return i - start


def modrm_len(op, rex, b, i):
    if i >= len(b):
        return 0
    modrm = b[i]
    i += 1
    mod = (modrm >> 6) & 3
    rm = modrm & 7
    n = 0
    # SIB
    if mod != 3 and rm == 4:
        sib = b[i] if i < len(b) else 0
        n += 1
        i += 1
        if (sib & 7) == 5 and mod == 0:
            n += 4
    if mod == 0 and rm == 5:
        n += 4          # RIP-relative disp32
    elif mod == 1:
        n += 1
    elif mod == 2:
        n += 4
    # immediate for accumulators (best effort)
    return n


def main():
    path = sys.argv[1]
    va = int(sys.argv[2], 0)
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 60

    raw, imgbase, secs = load(path)
    off, sec = va_to_off(va, imgbase, secs)
    if off is None:
        print("VA 0x%X not inside any section (imagebase 0x%X)" % (va, imgbase))
        return

    print("file      : %s" % path)
    print("imagebase : 0x%X" % imgbase)
    print("VA        : 0x%X   RVA 0x%X   section %s" % (va, va - imgbase, sec))
    print()
    data = raw[off:off + 512]
    print("raw hex   : %s" % " ".join("%02X" % x for x in data[:64]))
    print()

    print("linear decode (%d instructions):" % count)
    i = 0
    cur = va
    for _ in range(count):
        if i >= len(data):
            break
        ln = decode_len(data, i)
        if not ln or ln <= 0:
            ln = 1
        chunk = data[i:i + ln]
        note = ""
        # annotate call/jmp rel32
        if chunk and chunk[0] in (0xE8, 0xE9) and len(chunk) >= 5:
            rel = struct.unpack_from("<i", chunk, 1)[0]
            tgt = cur + ln + rel
            note = "  -> 0x%X" % tgt
        print("  0x%-12X %-24s%s" % (cur, " ".join("%02X" % x for x in chunk), note))
        i += ln
        cur += ln


if __name__ == "__main__":
    main()
