// D:\Work\Fut\cheat\src\webui_shm.cpp
// See webui_shm.h for why this is shared memory rather than a local HTTP server.
#include "webui_shm.h"
#include "log.h"
#include "obf.h"

#include <windows.h>
#include <cstring>

namespace webui_shm {

// ---------------------------------------------------------------------------
// Mapping name
//
// A fixed, obvious name is a fingerprint: a scanner enumerating section objects
// for words like CHEAT / FUT / HACK finds it immediately. This is an opaque
// token instead -- 16 hex characters that mean nothing without the reader.
//
// It is still DETERMINISTIC (same build -> same name) so the reader can find it
// without discovery, but it is not a word.
//
// Derived from a compile-time constant so it is stable across runs; do not make
// it random per-run or the reader cannot locate it without a side channel.
// ---------------------------------------------------------------------------
static const char kName[] = "L6c1f0a9d84be3275";

// ---------------------------------------------------------------------------
// layout of the mapping
//
//   uint32 seq      -- seqlock: odd = a write is in progress
//   uint32 pad
//   Params params   -- the payload
//
// The seq counter is what makes a torn read detectable. The game bumps it to odd
// before writing and to even after; a reader that sees an odd value, or a
// different value before and after its copy, throws the snapshot away.
// ---------------------------------------------------------------------------
#pragma pack(push, 8)
struct Block {
    volatile uint32_t seq;
    uint32_t          pad;
    Params            params;
};
#pragma pack(pop)

static HANDLE  s_map = nullptr;
static Block*  s_blk = nullptr;
static bool    s_ready = false;
static uint32_t s_lastUiGen = 0;

bool Ready() { return s_ready; }
const char* MappingName() { return kName; }
uint32_t Sequence() { return s_blk ? s_blk->seq : 0; }

bool Init() {
    if (s_ready) return true;

    const SIZE_T size = sizeof(Block);

    // No Global\ prefix: the mapping is per-session, so another user logged into
    // the same machine cannot open it.
    //
    // CreateFileMappingA (not OpenFileMapping) so the game owns the object; if a
    // previous run left one behind, its contents are reset below.
    s_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                               0, (DWORD)size, kName);
    if (!s_map) {
        LOG(SBOX("[webui] CreateFileMapping failed err=%lu (feature disabled)", 0x200),
            GetLastError());
        return false;
    }

    void* p = MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!p) {
        LOG(SBOX("[webui] MapViewOfFile failed err=%lu", 0x201), GetLastError());
        CloseHandle(s_map);
        s_map = nullptr;
        return false;
    }

    s_blk = reinterpret_cast<Block*>(p);
    std::memset(s_blk, 0, size);
    s_blk->seq = 0;
    s_ready = true;

    LOG(SBOX("[webui] shared block ready (%u bytes, name=%s)", 0x202),
        (unsigned)size, kName);
    return true;
}

// ---------------------------------------------------------------------------
// per-frame update
// ---------------------------------------------------------------------------
void Tick(const Bridge& bridge) {
    if (!s_ready || !s_blk) return;
    if (!bridge.readFromGame || !bridge.writeToGame) return;

    // ---- 1. publish the game's state -------------------------------------
    //
    // Seqlock: mark a write in progress (odd), write, then mark complete (even).
    // A reader that samples the counter before and after its copy and gets the
    // same EVEN value has a consistent snapshot.
    const uint32_t s0 = s_blk->seq;
    s_blk->seq = s0 + 1;                       // odd

    bridge.readFromGame(s_blk->params);
    if (bridge.readStatus) bridge.readStatus(s_blk->params);

    // Keep the game's own heartbeat and the UI's generation, which readFromGame
    // does not know about.
    s_blk->params.gameHeartbeat = GetTickCount();
    s_blk->params.uiGeneration  = s_lastUiGen;

    s_blk->seq = s0 + 2;                       // even

    // ---- 2. pick up a UI edit --------------------------------------------
    //
    // The UI bumps uiGeneration when it changes anything. Comparing against the
    // last value we saw means an unchanged block costs one integer compare, and a
    // change is applied exactly once rather than every frame (which would fight
    // any live editing in the ImGui panel).
    __try {
        const uint32_t gen = s_blk->params.uiGeneration;
        if (gen != s_lastUiGen) {
            Params incoming = s_blk->params;
            s_lastUiGen = gen;
            bridge.writeToGame(incoming);
            LOG(SBOX("[webui] settings applied from UI (gen %u)", 0x203), gen);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // The mapping is gone (the reader unmapped it, or teardown raced). Stop
        // touching it rather than faulting every frame.
        s_ready = false;
        s_blk = nullptr;
    }
}

} // namespace webui_shm
