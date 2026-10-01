#include <cstdint>
#include <cstddef>
namespace obf {
inline constexpr uint8_t KS(uint8_t key, size_t i) { return (uint8_t)(key + (uint8_t)(i*31)); }
template <size_t N> struct Blob { uint8_t d[N]; };
template <size_t N> inline void Dec(const uint8_t (&s)[N], uint8_t k, char* o) {
    for (size_t i=0;i<N;++i) o[i]=(char)(s[i]^KS(k,i));
}
}
#define SBOX_BYTES(str, key) []() constexpr { obf::Blob<sizeof(str)> b{}; \
    for (size_t i=0;i<sizeof(str);++i) b.d[i]=(uint8_t)((uint8_t)(str[i])^obf::KS((uint8_t)(key),i)); return b; }()

extern "C" void useA(char* o) { obf::Dec(SBOX_BYTES("[boot] net filter (early) installing", 0x1D).d, 0x1D, o); }
extern "C" void useB(char* o) { static constexpr auto k = SBOX_BYTES("[boot] net filter (init) done", 0x57); obf::Dec(k.d, 0x57, o); }