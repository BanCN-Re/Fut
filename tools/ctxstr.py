#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ctxstr.py - print strings with surrounding context around a pattern match.

Usage: python ctxstr.py <binary> <pattern> [window] [maxhits]
"""
import re
import struct
import sys


def load_sections(path):
    raw = open(path, "rb").read()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<HH", raw, pe + 4)[1]
    optsz = struct.unpack_from("<H", raw, pe + 20)[0]
    opt = pe + 24
    sec_off = opt + optsz
    secs = []
    for i in range(nsec):
        o = sec_off + i * 40
        name = raw[o:o + 8].rstrip(b"\0").decode("latin-1", "replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", raw, o + 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return raw, secs


def off_to_rva(off, secs):
    for name, vaddr, vsize, raddr, rsize in secs:
        if raddr <= off < raddr + rsize:
            return vaddr + (off - raddr), name
    return None, None


def main():
    path = sys.argv[1]
    pat = sys.argv[2].encode("latin-1")
    window = int(sys.argv[3]) if len(sys.argv) > 3 else 220
    maxhits = int(sys.argv[4]) if len(sys.argv) > 4 else 25

    raw, secs = load_sections(path)
    print("file: %s" % path)
    print("pattern: %r  window=%d" % (pat, window))
    print("=" * 100)

    shown = 0
    start = 0
    while shown < maxhits:
        i = raw.find(pat, start)
        if i < 0:
            break
        start = i + 1
        lo = max(0, i - window // 2)
        hi = min(len(raw), i + window // 2)
        chunk = raw[lo:hi]
        # render printable, dots for non-printable
        rend = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
        rva, sec = off_to_rva(i, secs)
        print("[%d] off=0x%X rva=0x%X sec=%s" % (shown, i, rva or 0, sec))
        print("    %s" % rend)
        print()
        shown += 1
    print("shown %d hit(s)" % shown)


if __name__ == "__main__":
    main()
