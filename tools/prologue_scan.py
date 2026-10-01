#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""prologue_scan.py - count x64 function-prologue patterns inside a PE section.

Plaintext compiled code is full of recognizable prologues. Packed/encrypted
code has almost none. This gives a quantitative answer to "is the code
encrypted on disk?".
"""
import struct
import sys

# common x64 function prologues (MSVC + clang)
PROLOGUES = [
    bytes.fromhex("48895C24"),      # mov [rsp+xx], rbx
    bytes.fromhex("48894C24"),      # mov [rsp+xx], rcx
    bytes.fromhex("48895424"),      # mov [rsp+xx], rdx
    bytes.fromhex("4053"),          # push rbx
    bytes.fromhex("4055"),          # push rbp
    bytes.fromhex("4056"),          # push rsi
    bytes.fromhex("4057"),          # push rdi
    bytes.fromhex("4883EC"),        # sub rsp, imm8
    bytes.fromhex("4881EC"),        # sub rsp, imm32
    bytes.fromhex("4C8BDC"),        # mov r11, rsp
    bytes.fromhex("488B C4".replace(" ", "")),  # mov rax, rsp
    bytes.fromhex("F30F1EFA"),      # endbr64
    bytes.fromhex("CCCCCCCC"),      # int3 padding
    bytes.fromhex("488D05"),        # lea rax, [rip+xx]
]


def load(path):
    with open(path, "rb") as f:
        raw = f.read()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<HH", raw, pe + 4)[1]
    opt_size = struct.unpack_from("<H", raw, pe + 20)[0]
    sec_off = pe + 24 + opt_size
    secs = []
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        chars = struct.unpack_from("<I", raw, o + 36)[0]
        secs.append((name, vaddr, vsize, raddr, rsize, chars))
    return raw, secs


def main():
    path = sys.argv[1]
    raw, secs = load(path)
    print("file: %s" % path)
    print()
    for name, vaddr, vsize, raddr, rsize, chars in secs:
        if not (chars & 0x20000000):     # only executable sections
            continue
        data = raw[raddr:raddr + rsize]
        if len(data) < 4096:
            continue
        print("=== exec section %s  RVA 0x%X  size 0x%X (%.1f MB) ===" % (
            name, vaddr, rsize, rsize / 1048576))
        total = 0
        for p in PROLOGUES:
            n = data.count(p)
            total += n
            print("   %-14s %10d occurrences" % (p.hex().upper(), n))
        per_mb = total / (rsize / 1048576)
        print("   -> total %d  = %.0f per MB" % (total, per_mb))
        print()
        print("   reference: normal x64 code has thousands of prologues per MB;")
        print("              encrypted/packed code has near zero.")
        print()


if __name__ == "__main__":
    main()
