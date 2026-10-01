#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""detect_surface.py - enumerate what an anti-cheat module can DETECT.

For each module it classifies every import into detection categories, so we can
see (a) what it is capable of and (b) whether our in-process DLL is exposed.

Usage: python detect_surface.py <binary> [binary...]
"""
import sys

import pefile

CATEGORIES = {
    "模块枚举 (能看到我们的DLL)": [
        "Module32First", "Module32FirstW", "Module32Next", "Module32NextW",
        "EnumProcessModules", "EnumProcessModulesEx", "CreateToolhelp32Snapshot",
        "LdrEnumerateLoadedModules", "GetModuleFileNameA", "GetModuleFileNameW",
        "GetModuleHandleA", "GetModuleHandleW", "GetModuleHandleExA", "GetModuleHandleExW",
    ],
    "进程/线程枚举": [
        "Process32First", "Process32FirstW", "Process32Next", "Process32NextW",
        "Thread32First", "Thread32Next", "NtQuerySystemInformation",
        "NtQueryInformationThread",
    ],
    "内存读取 (能读我们的代码)": [
        "ReadProcessMemory", "NtReadVirtualMemory", "NtQueryVirtualMemory",
        "VirtualQuery", "VirtualQueryEx",
    ],
    "内存写入 (能改我们)": [
        "WriteProcessMemory", "NtWriteVirtualMemory", "VirtualProtect",
        "VirtualProtectEx", "NtProtectVirtualMemory",
    ],
    "线程控制 (能挂起我们)": [
        "SuspendThread", "ResumeThread", "OpenThread", "NtSuspendThread",
        "CreateRemoteThread", "NtCreateThreadEx",
    ],
    "句柄操作": [
        "OpenProcess", "OpenThread", "DuplicateHandle", "NtOpenProcess",
    ],
    "栈回溯 (能定位调用者)": [
        "RtlVirtualUnwind", "StackWalk64", "CaptureStackBackTrace",
        "RtlCaptureStackBackTrace",
    ],
    "驱动/内核接口": [
        "DeviceIoControl", "NtLoadDriver", "NtQuerySystemInformation",
        "ZwQuerySystemInformation",
    ],
}


def main():
    for path in sys.argv[1:]:
        try:
            pe = pefile.PE(path, fast_load=False)
        except Exception as e:
            print("  %s : %s" % (path, e))
            continue

        imports = set()
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            for imp in entry.imports:
                if imp.name:
                    imports.add(imp.name.decode("latin-1", "replace"))

        print("=" * 78)
        print("### %s" % path.rsplit("\\", 1)[-1])
        print("=" * 78)
        any_hit = False
        for cat, apis in CATEGORIES.items():
            hit = sorted(a for a in apis if a in imports)
            if hit:
                any_hit = True
                print("  [%s]" % cat)
                for h in hit:
                    print("      %s" % h)
        if not any_hit:
            print("  (no detection-relevant imports)")
        print()


if __name__ == "__main__":
    main()
