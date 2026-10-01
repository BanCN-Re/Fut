#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_blocked_domains.py - list the blocklisted hosts from the source.

Used to build the fake certificate's SAN list: the client connects with SNI set
to the real report host and then verifies that name, so the cert must carry the
exact hostnames (a bare "*" only matches one label and would fail for
pc.crashsight.qq.com).

Usage: python dump_blocked_domains.py <ace_blackhole.cpp> <out.txt>
"""
import re
import sys

# matches either the direct form  SBOX_BYTES("host.example.com", 0x5B)
# or the blocklist wrapper        SBOX_STR("host.example.com")
PAT = re.compile(r'(?:SBOX_BYTES|SBOX_STR)\(\s*"([A-Za-z0-9.-]+\.[A-Za-z]{2,})"')


def main():
    src = sys.argv[1]
    out = sys.argv[2]
    with open(src, encoding="utf-8", errors="replace") as f:
        text = f.read()

    doms = sorted(set(PAT.findall(text)))
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(doms))

    print("blocked domains: %d" % len(doms))
    for d in doms:
        print("  " + d)


if __name__ == "__main__":
    main()
