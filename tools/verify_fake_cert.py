#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_fake_cert.py - sanity-check the generated cert header.

Confirms the SAN list actually covers the blocked report hosts, because a bare
"*" does NOT match a multi-label name like pc.crashsight.qq.com under OpenSSL's
default rules -- the exact hostnames must be present or the client's hostname
check fails and the fake server looks like an attack rather than a success.

Usage: python verify_fake_cert.py <fake_cert.h>
"""
import re
import sys

from cryptography import x509


def main():
    path = sys.argv[1]
    with open(path, encoding="utf-8") as f:
        hdr = f.read()

    m = re.search(r'kCertPem\[\] =(.*?);', hdr, re.S)
    if not m:
        print("kCertPem not found")
        return
    # C string literals: "line\n"
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    pem = "".join(p.encode().decode("unicode_escape") for p in parts)

    cert = x509.load_pem_x509_certificate(pem.encode())
    san = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
    names = san.get_values_for_type(x509.DNSName)

    print("subject CN : %s" % cert.subject.get_attributes_for_oid(
        x509.oid.NameOID.COMMON_NAME)[0].value)
    print("SAN entries: %d" % len(names))
    print("valid until: %s" % cert.not_valid_after_utc.date())
    print()

    checks = [
        "crashsight.qq.com", "pc.crashsight.qq.com", "*.crashsight.qq.com",
        "perfsight.qq.com", "tdm.qq.com", "anticheatexpert.com",
    ]
    for c in checks:
        print("  %-30s %s" % (c, "present" if c in names else "MISSING"))

    print()
    for n in sorted(names):
        print("   ", n)


if __name__ == "__main__":
    main()
