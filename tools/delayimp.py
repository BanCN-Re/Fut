#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""delayimp.py - dump a module's DELAY imports and resolve their IAT slots.

Delay-loaded imports are invisible to a plain import-table scan, which is
exactly how webio.dll hides its ws2_32 usage. This prints, per delay-imported
DLL, every function and the address the module will patch at first use -- which
is a valid hook target for an IAT patcher.

Usage: python delayimp.py <module>
"""
import sys

import pefile


def main():
    path = sys.argv[1]
    pe = pefile.PE(path, fast_load=False)
    print("=== DELAY imports of %s ===" % path.rsplit("\\", 1)[-1])

    if not hasattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT"):
        print("   (none)")
        return

    for e in pe.DIRECTORY_ENTRY_DELAY_IMPORT:
        dll = e.dll.decode("latin-1", "replace")
        print()
        print("--- %s ---" % dll)
        for imp in e.imports:
            nm = imp.name.decode("latin-1", "replace") if imp.name else ("ord#%d" % imp.ordinal)
            print("   %-40s IAT@0x%X" % (nm, imp.address))

    # A delay-imported function resolves to a stub first; the stub is what is
    # stored in the IAT until first call. Show where those stubs live.
    print()
    print("=== note ===")
    print("A delay-import IAT slot initially points to a __delayLoadHelper2 stub.")
    print("Patching the slot with our detour works, but the slot's ORIGINAL value")
    print("is the stub -- so the detour MUST resolve the real function itself")
    print("(LoadLibrary + GetProcAddress) instead of calling the saved pointer.")


if __name__ == "__main__":
    main()
