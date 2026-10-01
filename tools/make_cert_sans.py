#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_cert_sans.py - build the fake cert's SAN list from the blocklist.

For each blocked host we add BOTH the exact name and a wildcard below it:
    crashsight.qq.com  ->  crashsight.qq.com, *.crashsight.qq.com

The wildcard is what covers real report endpoints such as
pc.crashsight.qq.com. Note "*.<domain>" matches exactly ONE extra label, so it
covers pc.crashsight.qq.com but not a.b.crashsight.qq.com. We also add a
single-label wildcard under the registrable domain (*.qq.com) as a catch-all
for endpoints we have not observed.

Usage: python make_cert_sans.py <blocked_domains.txt> <out_sans.txt>
"""
import sys


def main():
    src, out = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as f:
        doms = [l.strip() for l in f if l.strip() and not l.startswith("#")]

    sans = set()
    for d in doms:
        sans.add(d)
        sans.add("*." + d)              # pc.crashsight.qq.com
        parts = d.split(".")
        if len(parts) >= 2:
            sans.add("*." + ".".join(parts[-2:]))   # *.qq.com catch-all

    ordered = sorted(sans)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(ordered))

    print("SAN entries: %d" % len(ordered))
    for s in ordered:
        print("  " + s)


if __name__ == "__main__":
    main()
