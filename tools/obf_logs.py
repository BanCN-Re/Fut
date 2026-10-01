#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""obf_logs.py - rewrite LOG(" literal ...) call sites to use SBOX().

P1 follow-up: the blocklist domains are encrypted, but the LOG format strings
still contained "DNS-BLACKHOLE" (describes the technique) and one GUI test
string contained "dump.anticheatexpert.com" (the incriminating domain).

This rewrites:
    LOG("[DNS-BLACKHOLE] done: %d", n);
into
    LOG(SBOX("[net] done: %d", 0x37).c_str(), n);

Two things happen at once: the damning prefix text is replaced by something
innocuous, and it is XOR-encrypted so it is not readable in the image.

Usage: python obf_logs.py <file.cpp> [file2.cpp ...]        (in place)
       python obf_logs.py --dry <file.cpp>
"""
import re
import sys

# how each damning token is rewritten (innocuous replacement)
REPLACE = [
    ("[DNS-BLACKHOLE]", "[net]"),
    ("dump.anticheatexpert.com", "example.invalid"),
    ("anticheatexpert.com", "example.invalid"),
]

# distinct keys so identical strings do not produce identical ciphertext
KEYS = [0x37, 0x5D, 0x29, 0x6B, 0x41, 0x73, 0x1F, 0x55, 0x2D, 0x67]

# match  LOG("...")  or  LOG("...", ...)   with a plain string literal first arg
LOG_RE = re.compile(r'LOG\("((?:[^"\\]|\\.)*)"')


def rewrite(text, dry=False):
    counter = [0]
    changed = []

    def repl(m):
        lit = m.group(1)
        orig = lit
        for a, b in REPLACE:
            lit = lit.replace(a, b)
        if lit == orig and "\\" not in lit:
            return m.group(0)          # nothing damning; leave it alone
        key = KEYS[counter[0] % len(KEYS)]
        counter[0] += 1
        changed.append((orig[:60], lit[:60]))
        return 'LOG(SBOX("%s", 0x%02X).c_str()' % (lit, key)

    out = LOG_RE.sub(repl, text)
    return out, changed


def main():
    args = [a for a in sys.argv[1:] if a != "--dry"]
    dry = "--dry" in sys.argv
    for path in args:
        with open(path, encoding="utf-8", errors="replace") as f:
            src = f.read()
        out, changed = rewrite(src, dry)
        print("%s: %d LOG() call(s) rewritten" % (path, len(changed)))
        for o, n in changed:
            print("    %-62s -> %s" % (o, n))
        if not dry and changed:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(out)


if __name__ == "__main__":
    main()
