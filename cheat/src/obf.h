// D:\Work\Fut\cheat\src\obf.h
// Compile-time string obfuscation (P1).
//
// WHY: a memory scan of our DLL found plaintext markers that are individually
// innocuous but damning in aggregate inside a "zlib" module:
//
//     fut-cheat            module name -> the word "cheat"
//     DNS-BLACKHOLE        describes the technique
//     anticheatexpert.com  <- the worst one: an anti-cheat domain literal
//                             inside a general-purpose compression library
//
// ACE-CSI64.dll imports ReadProcessMemory + NtQueryVirtualMemory + RtlVirtualUnwind
// (verified from its import table), so it CAN scan our image and walk our stack.
// String encryption does not defeat a stack walk, but it removes the cheapest and
// most common detector: a byte-pattern scan over loaded module images.
//
// HOW: SBOX_BYTES() XORs the literal at compile time (constexpr), so only
// ciphertext reaches .rdata. SBOX() decrypts it at run time.
//
// CRITICAL IMPLEMENTATION NOTE -- why decryption lives in obf.cpp:
//   If the decrypt loop is inlined at the call site, MSVC constant-folds the
//   whole expression at /O2 and silently emits the PLAINTEXT into .rdata,
//   undoing the obfuscation. A `volatile` key read was NOT sufficient (measured:
//   "ZLW_SELF_STEALTH" and a log string stayed readable). Keeping the loop in a
//   separate translation unit makes folding impossible without LTCG, which this
//   project does not enable. Do not "optimise" this back into the header.
#pragma once
#include <cstdint>
#include <cstddef>

namespace obf {

// Key schedule: byte i is XOR'd with (key + i*31) truncated to 8 bits. Simple on
// purpose -- this defeats byte-pattern scans, not a cryptanalyst.
inline constexpr uint8_t KS(uint8_t key, size_t i) {
    return (uint8_t)(key + (uint8_t)(i * 31));
}

// Storage for the ciphertext. C++ cannot return a bare array by value, so the
// compile-time lambda returns this wrapper and callers read `.d`.
template <size_t N>
struct Blob {
    uint8_t d[N];
    static constexpr size_t size = N;
};

// Decrypt `n` bytes of `src` into `out`, NUL-terminating at out[n-1].
// Defined in obf.cpp ON PURPOSE -- see the note at the top of this header.
// `out` must hold at least `n` bytes.
void DecryptStr(const uint8_t* src, size_t n, uint8_t key, char* out);

// Convenience: decrypt into a thread-local buffer and return it. The buffer is
// valid until the next call on the same thread, which is enough for log lines
// and comparisons. Use the explicit form when you need two live strings.
const char* DecryptTls(const uint8_t* src, size_t n, uint8_t key);

// Opaque view of a ciphertext array.
//
// WHY THIS EXISTS: MSVC /O2 constant-folds `DecryptTls(SBOX_BYTES("x",k).d, ...)`
// when the blob is a temporary, and emits the PLAINTEXT into .rdata -- silently
// undoing the obfuscation. Measured twice: after the first fix, 13 inline call
// sites still leaked ("net filter", "blackhole" reappeared in the built DLL).
//
// Routing the bytes through a volatile pointer makes them runtime data as far as
// the optimiser is concerned, so the fold cannot happen. The object file still
// contains ciphertext only.
template <size_t N>
inline const uint8_t* ForceOpaque(const uint8_t (&src)[N]) {
    const volatile uint8_t* v = src;
    return const_cast<const uint8_t*>(v);
}

} // namespace obf

// Compile-time encryption. Yields an obf::Blob<N> whose `.d` is the ciphertext.
//
//   static constexpr auto k = SBOX_BYTES("host.com", 0x5B);
//   // use k.d and decltype(k)::size
//
// IMPORTANT -- always bind the result to a namespace-scope `static constexpr`
// variable (or a function-local `static constexpr`) before decrypting, e.g.
//
//     static constexpr auto kFoo = SBOX_BYTES("foo", 0x11);
//     const char* p = obf::DecryptTls(kFoo.d, sizeof(kFoo.d), 0x11);
//
// Passing an SBOX_BYTES(...) *temporary* straight into DecryptTls lets MSVC /O2
// materialise the plaintext as a real string constant in .rdata (observed: the
// symbol `??_C@_0BB@...@ZLW_SELF_STEALTH@`). Binding to a named constexpr object
// forces the ciphertext array to be the emitted entity instead.
#define SBOX_BYTES(str, key)                                                   \
    []() constexpr {                                                           \
        obf::Blob<sizeof(str)> b{};                                            \
        for (size_t i = 0; i < sizeof(str); ++i)                               \
            b.d[i] = (uint8_t)((uint8_t)(str[i]) ^ obf::KS((uint8_t)(key), i));\
        return b;                                                              \
    }()

// Runtime decryption into a thread-local buffer.
//
// WHY THIS IS A LAMBDA AND NOT A DIRECT CALL -- read before "simplifying" it.
//
// Writing the obvious form,
//     #define SBOX(s,k) obf::DecryptTls(SBOX_BYTES(s,k).d, sizeof(s), (k))
// does NOT work: MSVC /O2 sees the whole expression as a compile-time constant,
// evaluates the XOR, and emits the PLAINTEXT into .rdata. Measured repeatedly --
// "[boot] net filter ..." and "blackhole" kept reappearing in the built DLL even
// though the source only contained ciphertext. A volatile pointer to the bytes
// was tried and was NOT sufficient.
//
// A function-local `static constexpr` object IS sufficient: the array has a real
// address and is materialised as ciphertext, exactly like the namespace-scope
// blobs that demonstrably work (the 22-entry blocklist and stealth.cpp both
// verified clean). Each expansion of this macro creates a distinct lambda type,
// hence its own static, so literals stay independent.
//
// The buffer returned by DecryptTls is thread-local and valid until the next call
// on the same thread: fine for a single log line. When two obfuscated strings must
// be live at once, use obf::DecryptStr into an explicit local buffer instead.
#define SBOX(str, key)                                                         \
    []() -> const char* {                                                      \
        static constexpr auto _k = SBOX_BYTES(str, key);                       \
        return obf::DecryptTls(_k.d, sizeof(_k.d), (uint8_t)(key));            \
    }()
