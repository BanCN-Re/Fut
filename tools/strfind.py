#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""strfind.py - extract ASCII/UTF-16 strings from a binary and filter them.

Usage:
    python strfind.py <binary> <pattern> [minlen]
Prints matching strings with their file offsets (and RVA when a section map
can be built). Case-insensitive substring match.
"""
import re
import struct
import sys


def load_sections(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:2] != b"MZ":
        return raw, []
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    nsec = struct.unpack_from("<HH", raw, pe + 4)[1]
    opt_size = struct.unpack_from("<H", raw, pe + 20)[0]
    opt = pe + 24
    sec_off = opt + opt_size
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
    pat = sys.argv[2].lower()
    minlen = int(sys.argv[3]) if len(sys.argv) > 3 else 6

    raw, secs = load_sections(path)
    print("file: %s  (%d bytes, %d sections)" % (path, len(raw), len(secs)))
    print("pattern: %r  minlen=%d" % (pat, minlen))
    print()

    seen = set()
    hits = []

    # ASCII
    for m in re.finditer(rb"[\x20-\x7E]{%d,200}" % minlen, raw):
        s = m.group().decode("latin-1")
        if pat in s.lower():
            off = m.start()
            rva, sec = off_to_rva(off, secs)
            key = (s, rva)
            if key in seen:
                continue
            seen.add(key)
            hits.append((off, rva, sec, s))

    # UTF-16LE
    for m in re.finditer(rb"(?:[\x20-\x7E]\x00){%d,200}" % minlen, raw):
        s = m.group().decode("utf-16-le")
        if pat in s.lower():
            off = m.start()
            rva, sec = off_to_rva(off, secs)
            key = (s, rva)
            if key in seen:
                continue
            seen.add(key)
            hits.append((off, rva, sec, s + "  [utf16]"))

    hits.sort(key=lambda h: (h[1] is None, h[1] or 0))
    for off, rva, sec, s in hits[:120]:
        if rva is not None:
            print("  off=0x%-8X rva=0x%-9X %-8s %s" % (off, rva, sec or "", s))
        else:
            print("  off=0x%-8X (no section)              %s" % (off, s))
    print()
    print("total hits: %d" % len(hits))


if __name__ == "__main__":
    main()
