#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fullextract.py - dump ALL strings (ascii+utf16) from a binary to a file.

Used as the input corpus for the "context decryption" step: instead of guessing
what an encrypted name is, we take every string the module contains and look for
the structures around them.

Usage: python fullextract.py <binary> <outfile> [minlen]
"""
import re
import struct
import sys


def load_sections(path):
    raw = open(path, "rb").read()
    if raw[:2] != b"MZ":
        return raw, []
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
    out = sys.argv[2]
    minlen = int(sys.argv[3]) if len(sys.argv) > 3 else 5

    raw, secs = load_sections(path)
    print("file: %s (%d bytes)" % (path, len(raw)))

    n = 0
    with open(out, "w", encoding="utf-8", errors="replace") as f:
        f.write("# strings from %s\n" % path)
        f.write("# format: <kind> 0x<off> 0x<rva> <section> <text>\n")
        # ascii
        for m in re.finditer(rb"[\x20-\x7E]{%d,400}" % minlen, raw):
            s = m.group().decode("latin-1")
            rva, sec = off_to_rva(m.start(), secs)
            f.write("A 0x%08X 0x%08X %-8s %s\n" % (m.start(), rva or 0, sec or "-", s))
            n += 1
        # utf16le
        for m in re.finditer(rb"(?:[\x20-\x7E]\x00){%d,400}" % minlen, raw):
            s = m.group().decode("utf-16-le")
            rva, sec = off_to_rva(m.start(), secs)
            f.write("W 0x%08X 0x%08X %-8s %s\n" % (m.start(), rva or 0, sec or "-", s))
            n += 1
    print("wrote %d strings -> %s" % (n, out))


if __name__ == "__main__":
    main()
