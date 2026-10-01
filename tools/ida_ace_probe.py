# D:\Work\Fut\tools\ida_ace_probe.py
# Headless IDAPython: analyse ACE-Base64.dll's reporting path.
#
#   * decompile sub_4CE440 (the thread entry the user identified)
#   * list its callers / callees
#   * surface every string it touzches (to catch an inlined/decrypted name)
#   * separately hunt for report_bk-style obfuscated strings
#
# Run:  idat.exe -A -S"ida_ace_probe.py" -L"ida_ace.log" ACE-Base64.dll
import ida_auto
import ida_bytes
import ida_funcs
import ida_hexrays
import ida_lines
import ida_name
import ida_nalt
import ida_segment
import ida_ua
import ida_xref
import idautils
import idc

TARGET = 0x1804CE440          # sub_4CE440 (imagebase 0x180000000 + 0x4CE440)
OUT = r"D:\Work\Fut\evidence\ace_probe.txt"

lines = []


def w(s=""):
    lines.append(str(s))


def flush():
    with open(OUT, "w", encoding="utf-8", errors="replace") as f:
        f.write("\n".join(lines))


def main():
    w("=== ACE-Base64.dll analysis ===")
    w("input: %s" % ida_nalt.get_input_file_path())
    w("imagebase: 0x%X" % ida_nalt.get_imagebase())
    w()

    w("waiting for auto-analysis...")
    flush()
    ida_auto.auto_wait()
    w("auto-analysis done")
    w()

    # ---------------------------------------------------------------- target
    fn = ida_funcs.get_func(TARGET)
    if not fn:
        w("[!] no function at 0x%X -- forcing creation" % TARGET)
        idc.create_insn(TARGET)
        idc.add_func(TARGET)
        fn = ida_funcs.get_func(TARGET)

    if fn:
        w("=== sub_4CE440 ===")
        w("start   : 0x%X" % fn.start_ea)
        w("end     : 0x%X" % fn.end_ea)
        w("size    : 0x%X (%d bytes)" % (fn.end_ea - fn.start_ea, fn.end_ea - fn.start_ea))
        w("name    : %s" % ida_funcs.get_func_name(fn.start_ea))
        w()

        # ---- callers ----
        w("--- callers ---")
        n = 0
        for xref in idautils.XrefsTo(fn.start_ea, 0):
            pf = ida_funcs.get_func(xref.frm)
            nm = ida_funcs.get_func_name(pf.start_ea) if pf else "?"
            w("  from 0x%X  in %s" % (xref.frm, nm))
            n += 1
        if n == 0:
            w("  (none -- likely a thread entry passed by address)")
        w()

        # ---- callees + strings ----
        w("--- callees / referenced strings ---")
        seen = set()
        for ea in idautils.Heads(fn.start_ea, fn.end_ea):
            if not ida_ua.can_decode(ea):
                continue
            for xref in idautils.XrefsFrom(ea, 0):
                tgt = xref.to
                if tgt in seen:
                    continue
                seen.add(tgt)
                pf = ida_funcs.get_func(tgt)
                if pf and pf.start_ea == tgt:
                    w("  call 0x%X  %s" % (tgt, ida_funcs.get_func_name(tgt)))
                else:
                    s = ida_bytes.get_strlit_contents(tgt, -1, ida_nalt.STRTYPE_C)
                    if not s:
                        s = ida_bytes.get_strlit_contents(tgt, -1, ida_nalt.STRTYPE_C_16)
                    if s:
                        try:
                            txt = s.decode("utf-8", "replace")
                        except Exception:
                            txt = repr(s)
                        w("  str  0x%X  %r" % (tgt, txt[:160]))
        w()

        # ---- decompile ----
        w("--- Hex-Rays decompilation ---")
        try:
            if ida_hexrays.init_hexrays_plugin():
                cf = ida_hexrays.decompile(fn.start_ea)
                if cf:
                    for ln in cf.get_pseudocode():
                        w("  " + ida_lines.tag_remove(ln.line))
                else:
                    w("  (decompile returned None)")
            else:
                w("  (Hex-Rays unavailable -- falling back to disassembly)")
        except Exception as e:
            w("  decompile failed: %s" % e)

        if not ida_hexrays.init_hexrays_plugin():
            w()
            w("--- disassembly (first 200 insns) ---")
            c = 0
            for ea in idautils.Heads(fn.start_ea, fn.end_ea):
                w("  0x%X  %s" % (ea, idc.generate_disasm_line(ea, 0)))
                c += 1
                if c > 200:
                    break
    else:
        w("[!] could not create a function at 0x%X" % TARGET)
    w()
    flush()

    # ------------------------------------------------- obfuscated string hunt
    w("=== obfuscated 'report_bk' hunt ===")
    w("plaintext scan already failed; looking for the dword-immediate form")
    w("(MSVC builds stack strings as mov dword ptr [rsp+x], imm32)")
    w()

    # 'repo' 'rt_b' 'k'  little-endian immediates
    pats = {
        "repo": b"\x72\x65\x70\x6F",
        "rt_b": b"\x72\x74\x5F\x62",
        "t_bk": b"\x74\x5F\x62\x6B",
        "port": b"\x70\x6F\x72\x74",
        "_bk":  b"\x5F\x62\x6B\x00",
    }
    text_start = 0x180001000
    text_end = 0x180001000 + 0x188ABB3
    for name, pat in pats.items():
        hits = []
        ea = text_start
        while ea < text_end and len(hits) < 40:
            found = ida_bytes.find_bytes(pat, ea, text_end)
            if found == idc.BADADDR:
                break
            hits.append(found)
            ea = found + 1
        w("pattern %-6s (%s): %d hit(s)" % (name, pat.hex(), len(hits)))
        for h in hits[:20]:
            pf = ida_funcs.get_func(h)
            nm = ida_funcs.get_func_name(pf.start_ea) if pf else "?"
            w("    0x%X  in %s" % (h, nm))
    w()
    flush()

    w("=== done ===")
    flush()
    idc.qexit(0)


main()
