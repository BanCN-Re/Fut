#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ordinals.py - resolve ordinal imports of a module against the exporting DLL.

webio.dll imports many ws2_32 functions BY ORDINAL, so a name-based audit misses
them. This maps each ordinal back to its name by reading the export table of the
provider DLL.

Usage: python ordinals.py <consumer.dll> <provider.dll>
"""
import sys

import pefile


def export_ordinal_map(path):
    pe = pefile.PE(path, fast_load=False)
    m = {}
    if not hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        return m
    base = pe.DIRECTORY_ENTRY_EXPORT.struct.Base
    for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if exp.name:
            m[exp.ordinal] = exp.name.decode("latin-1", "replace")
        else:
            m[exp.ordinal] = "(ordinal %d, unnamed)" % exp.ordinal
    return m


def main():
    consumer = sys.argv[1]
    provider = sys.argv[2]
    omap = export_ordinal_map(provider)
    print("provider %s : %d exports" % (provider.rsplit("\\", 1)[-1], len(omap)))

    pe = pefile.PE(consumer, fast_load=False)
    pname = provider.rsplit("\\", 1)[-1].lower()
    cname = consumer.rsplit("\\", 1)[-1]
    print()
    print("=== %s imports from %s ===" % (cname, pname))
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("latin-1", "replace").lower()
        if dll != pname:
            continue
        for imp in entry.imports:
            if imp.name:
                print("   %-34s %s" % (imp.name.decode("latin-1", "replace"), "IAT@0x%X" % imp.address))
            else:
                nm = omap.get(imp.ordinal, "?")
                print("   %-34s (ordinal %d)   IAT@0x%X" % (nm, imp.ordinal, imp.address))


if __name__ == "__main__":
    main()
