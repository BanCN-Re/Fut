#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""charset.py - collect every character the GUI can display, for font subsetting."""
import os, re, sys

PATTERNS = [
    re.compile(r'u8"((?:[^"\\]|\\.)*)"'),
    re.compile(r'L"((?:[^"\\]|\\.)*)"'),
    re.compile(r'"((?:[^"\\]|\\.)*)"'),
]

BASE = (
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    " \t.,:;!?/\\|-_+=()[]{}<>@#$%^&*~`'\""
    "\u00b0\u00d7\u2192\u2190\u2191\u2193\u2022\u2026\u2014"
)

def main():
    src = sys.argv[1]
    prefix = sys.argv[2] if len(sys.argv) > 2 else "charset"
    chars = set(BASE)
    files = 0
    for root, _dirs, names in os.walk(src):
        for n in names:
            if not n.endswith((".cpp", ".h", ".rc")):
                continue
            try:
                with open(os.path.join(root, n), encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            files += 1
            for pat in PATTERNS:
                for m in pat.finditer(text):
                    chars.update(m.group(1))
    ordered = "".join(sorted(chars))
    nonascii = "".join(sorted(c for c in chars if ord(c) > 127))
    for suffix, data in (("_all.txt", ordered), ("_cjk.txt", nonascii), ("_subset.txt", ordered)):
        with open(prefix + suffix, "w", encoding="utf-8") as f:
            f.write(data)
    print("scanned %d source files" % files)
    print("unique characters  : %d" % len(ordered))
    print("non-ASCII (CJK etc): %d" % len(nonascii))

if __name__ == "__main__":
    main()