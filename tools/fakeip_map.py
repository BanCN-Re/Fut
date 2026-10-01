#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fakeip_map.py - map Clash/mihomo fake-IPs back to hostnames.

mihomo stores its DNS cache in a bbolt (BoltDB) file. Rather than parse the
full B-tree, we scan the raw pages: keys are hostnames and the values that
follow contain the fake IPv4. We correlate any printable hostname with the
nearest 198.18.x.x / other fake-range IPv4 bytes.

Usage: python fakeip_map.py <cache.db> [ip]
"""
import re
import struct
import sys

HOST = re.compile(rb"[a-z0-9][a-z0-9._\-]{3,120}\.(?:com|net|cn|org|io|top|xyz|moe|"
                  rb"club|site|online|vip|cc|tv|me|info|biz|app|dev|cloud|moe)")
IPV4 = re.compile(rb"(?:\d{1,3}\.){3}\d{1,3}")


def main():
    path = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) > 2 else None
    raw = open(path, "rb").read()
    print("cache: %s  (%d bytes)" % (path, len(raw)))

    # Collect (offset, hostname) and (offset, ip)
    hosts = [(m.start(), m.group().decode("latin-1")) for m in HOST.finditer(raw)]
    ips = [(m.start(), m.group().decode()) for m in IPV4.finditer(raw)]
    print("hostname tokens: %d   ipv4 tokens: %d" % (len(hosts), len(ips)))

    if want:
        # find the requested ip and print nearby hostnames
        print()
        print("=== hostnames within 256 bytes of %s ===" % want)
        wb = want.encode()
        found = 0
        start = 0
        while True:
            i = raw.find(wb, start)
            if i < 0:
                break
            start = i + 1
            near = [h for o, h in hosts if abs(o - i) < 256]
            if near:
                # nearest first
                near.sort(key=lambda h: abs(raw.find(h.encode(), max(0, i - 256)) - i))
                print("  off=0x%-8X  %s" % (i, " | ".join(dict.fromkeys(near[:4]))))
                found += 1
                if found > 25:
                    break
        if not found:
            print("  (no hostname adjacent to that IP in the raw pages)")
        return

    # No specific IP: print the fake-ip range hosts
    print()
    print("=== fake-ip (198.18.x.x) neighbours ===")
    seen = set()
    for o, ip in ips:
        if not ip.startswith("198.18."):
            continue
        near = [h for ho, h in hosts if abs(ho - o) < 256]
        if not near:
            continue
        for h in dict.fromkeys(near):
            key = (ip, h)
            if key in seen:
                continue
            seen.add(key)
            print("  %-16s  %s" % (ip, h))
            if len(seen) > 120:
                return


if __name__ == "__main__":
    main()
