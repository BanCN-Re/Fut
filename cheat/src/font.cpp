// D:\Work\Fut\cheat\src\font.cpp
// Loads the 黑体 font embedded as an RCDATA resource (same font UI1001 ships).
// Also supports a fallback: if the resource is absent, try a system font file.
#include "font.h"
#include "log.h"
#include "resource.h"
#include "font_ranges.h"

#include "imgui.h"
#include <windows.h>
#include <cstdio>

namespace font {

// The atlas must own its pixels: we hand ImGui a pointer into a resource that
// lives in .rdata, so FontDataOwnedByAtlas must be false or ImGui will call
// free() on a static pointer (this exact bug is documented in the Nemesis
// handoff, defect B).
static void* s_fontData = nullptr;

bool LoadChineseFont(ImGuiIO& io, float size) {
    // The resource lives in OUR module, not the game's executable. Passing
    // nullptr would search the host process image and fail with err 1813.
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&LoadChineseFont), &self);

    HRSRC hRes = FindResourceA(self, MAKEINTRESOURCEA(IDR_TTF1), RT_RCDATA);
    if (!hRes) {
        // legacy layouts may have used a named type
        hRes = FindResourceA(self, MAKEINTRESOURCEA(IDR_TTF1), "TTF");
    }
    if (hRes) {
        HGLOBAL hData = LoadResource(self, hRes);
        if (hData) {
            DWORD sz = SizeofResource(self, hRes);
            void* p = LockResource(hData);
            if (p && sz > 0) {
                // ---- glyph ranges ----
                //
                // GetGlyphRangesChineseFull() bakes ~2500 common characters into
                // the atlas. The default atlas is 4096x4096 and a 20 px CJK font
                // can overflow it, at which point ImGui SILENTLY drops whatever
                // did not fit -- which is why some labels rendered fine ('启用')
                // while others became '???' ('触发键').
                //
                // We instead feed the exact codepoint set the UI draws, generated
                // into font_ranges.h from the u8"" literals in src/*.cpp. Every
                // glyph the UI needs is guaranteed a slot, and the atlas stays
                // small.
                //
                // The atlas is also enlarged and built explicitly BEFORE the font
                // is added, so there is headroom even if the list grows.
                ImFontConfig cfg;
                cfg.FontDataOwnedByAtlas = false;   // resource memory is static
                cfg.OversampleH = 1;                // fewer pixels per glyph -> more fit
                cfg.OversampleV = 1;
                cfg.PixelSnapH  = true;

                // Build the atlas ourselves at a wider size than the 4096
                // default. With the exact glyph set the default is ample, but
                // this headroom means a future UI string with new characters
                // cannot silently overflow the atlas and lose glyphs again.
                io.Fonts->TexDesiredWidth = 4096;

                ImFont* f = io.Fonts->AddFontFromMemoryTTF(
                    p, (int)sz, size, &cfg, fontranges::kUi);
                if (f) {
                    s_fontData = p;
                    LOG("[font] loaded TTF resource: %u bytes (exact glyph set)", sz);
                    return true;
                }
                LOG("[font] AddFontFromMemoryTTF returned null");
            }
        }
    } else {
        LOG("[font] TTF resource not found (err=%lu)", GetLastError());
    }

    // fallback: a system CJK font, so the menu is still readable
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };
    for (const char* path : candidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = true;   // ImGui loads and owns this one
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, size, &cfg,
                                                 io.Fonts->GetGlyphRangesChineseFull());
        if (f) {
            LOG("[font] fallback font: %s", path);
            return true;
        }
    }
    return false;
}

} // namespace font
