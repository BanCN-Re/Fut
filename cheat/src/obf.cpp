// D:\Work\Fut\cheat\src\obf.cpp
// Runtime half of the string obfuscation. See obf.h for the full rationale.
//
// This lives in its own translation unit ON PURPOSE. With the loop in the header
// MSVC folded the entire decrypt at /O2 and emitted plaintext into .rdata,
// defeating the obfuscation entirely (measured, not theorised). A separate TU
// makes that impossible without link-time code generation, which is off here.
#include "obf.h"

namespace obf {

void DecryptStr(const uint8_t* src, size_t n, uint8_t key, char* out) {
    if (!src || !out || n == 0) {
        if (out && n) out[0] = 0;
        return;
    }
    for (size_t i = 0; i < n; ++i)
        out[i] = (char)(src[i] ^ KS(key, i));
    out[n - 1] = 0;                 // the source literal always includes the NUL
}

const char* DecryptTls(const uint8_t* src, size_t n, uint8_t key) {
    // 256 bytes is far beyond any literal we ship (longest hostname ~29 chars);
    // guard anyway so a future long string cannot silently overflow.
    static thread_local char buf[256];
    if (n > sizeof(buf)) {
        buf[0] = 0;
        return buf;
    }
    DecryptStr(src, n, key, buf);
    return buf;
}

} // namespace obf
