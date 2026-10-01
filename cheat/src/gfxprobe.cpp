// D:\Work\Fut\cheat\src\gfxprobe.cpp
// Identifies the graphics API the game presents through.
//
// Why this exists: we hooked IDXGISwapChain::Present (slot 8) and Present1
// (slot 22) in dxgi.dll, confirmed the addresses are inside dxgi.dll, and the
// game still rendered (GPU ~40%) without ever calling them. Both a legacy
// (DISCARD) and a flip-model (FLIP_DISCARD) dummy swap chain reported the SAME
// vtable slots, so the hook target was right for D3D11. The remaining
// explanation is that the game does not present through a D3D11 swap chain --
// most likely D3D12, which has its own swap-chain implementation class in DXGI.
//
// This walks the PEB module list (no psapi dependency) and logs everything
// graphics-related, which is enough to tell D3D11 from D3D12/Vulkan.
#include "gfxprobe.h"
#include "log.h"

#include <windows.h>
#include <cstring>

namespace gfxprobe {

namespace {

struct LdrUnicodeString { USHORT Length; USHORT MaximumLength; wchar_t* Buffer; };

struct LdrEntry {
    LIST_ENTRY        InLoadOrderLinks;
    LIST_ENTRY        InMemoryOrderLinks;
    LIST_ENTRY        InInitializationOrderLinks;
    void*             DllBase;
    void*             EntryPoint;
    ULONG             SizeOfImage;
    LdrUnicodeString  FullDllName;
    LdrUnicodeString  BaseDllName;
};

struct PebLdrData {
    ULONG      Length;
    BOOLEAN    Initialized;
    HANDLE     SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
};

bool Narrow(const wchar_t* s, int chars, char* out, size_t cap) {
    if (!s || chars <= 0 || (size_t)chars + 1 > cap) return false;
    for (int i = 0; i < chars; ++i) out[i] = (char)(s[i] & 0x7F);
    out[chars] = 0;
    return true;
}

bool Interesting(const char* n) {
    static const char* pats[] = {
        "d3d11", "d3d12", "dxgi", "d3dcompiler", "dxvk", "vulkan", "nvapi",
        "nvwgf2um", "amdxc", "atio6", "opengl", "libgl", "amdvlk", "igd",
        "gdi32", "win32u", "dwmapi", "dcomp", "presentmon",
    };
    for (const char* p : pats)
        if (strstr(n, p)) return true;
    return false;
}

} // namespace

void DumpLoadedGraphicsModules() {
    BYTE* peb = reinterpret_cast<BYTE*>(__readgsqword(0x60));
    BYTE* ldr = *reinterpret_cast<BYTE**>(peb + 0x18);
    if (!ldr) { LOG("[gfx] PEB/Ldr unavailable"); return; }

    auto* l = reinterpret_cast<PebLdrData*>(ldr);
    LIST_ENTRY* head = &l->InLoadOrderModuleList;

    int total = 0, shown = 0;
    LOG("[gfx] --- graphics-related loaded modules ---");
    for (LIST_ENTRY* it = head->Flink; it != head; it = it->Flink) {
        auto* e = reinterpret_cast<LdrEntry*>(it);
        char name[MAX_PATH]{};
        if (!Narrow(e->BaseDllName.Buffer, e->BaseDllName.Length / sizeof(wchar_t),
                    name, sizeof(name)))
            continue;
        ++total;
        if (!Interesting(name)) continue;

        // lower-case copy for matching
        char low[MAX_PATH]{};
        for (size_t i = 0; i < strlen(name) && i < sizeof(low) - 1; ++i)
            low[i] = (char)tolower((unsigned char)name[i]);

        LOG("[gfx]  %-34s base=0x%llX size=0x%X", name,
            (unsigned long long)(uintptr_t)e->DllBase, e->SizeOfImage);
        ++shown;
    }
    LOG("[gfx] scanned %d modules, %d graphics-related", total, shown);

    // decisive summary
    HMODULE h11 = GetModuleHandleA("d3d11.dll");
    HMODULE h12 = GetModuleHandleA("d3d12.dll");
    HMODULE hvk = GetModuleHandleA("vulkan-1.dll");
    LOG("[gfx] SUMMARY d3d11=%s d3d12=%s vulkan=%s",
        h11 ? "LOADED" : "absent", h12 ? "LOADED" : "absent", hvk ? "LOADED" : "absent");
    LOG("[gfx] dxgi.dll own base=0x%llX",
        (unsigned long long)(uintptr_t)GetModuleHandleA("dxgi.dll"));
}

} // namespace gfxprobe
