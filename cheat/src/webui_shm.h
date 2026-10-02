// D:\Work\Fut\cheat\src\webui_shm.h
#pragma once
//
// Shared-memory bridge for an out-of-process parameter UI.
//
// ===========================================================================
// WHY SHARED MEMORY AND NOT A LOCAL HTTP SERVER
//
// The obvious design -- open a socket inside zw_core.dll and serve a web page --
// is the one that must not be used, for three separate reasons:
//
//   1. THIS PROCESS IS SCANNED BY ACE. A listening socket appearing inside a
//      module that claims to be a compression proxy is about the loudest signal
//      available. Socket-table inspection is standard for a kernel-assisted
//      anti-cheat.
//
//   2. WE HOOK THE NETWORK OURSELVES. The ACE blackhole intercepts ws2_32 /
//      winhttp / dnsapi in this very process. An HTTP server here would have its
//      own traffic routed through our own filters -- we would be fighting
//      ourselves, and any mistake in the filter becomes a UI bug too.
//
//   3. FAILURE COUPLING. A crash in the UI server would take the game down with
//      it. Out of process, the UI can die and the game never notices.
//
// So the game side does ONE thing: it mirrors its settings into a named shared
// memory block and reads back whatever the UI wrote there. No sockets, no pipes,
// no threads, no I/O of any kind -- a memcpy into a mapping this process already
// owns. There is nothing for a network scan to find.
//
// The reader side (tools\webui\serve.ps1 or the static HTML exporter) is a
// SEPARATE, UNPRIVILEGED process. It owns the network traffic and, if it is ever
// flagged, the game is not involved.
//
// ===========================================================================
// SAFETY OF THE SHARED BLOCK ITSELF
//
//   * Fixed-size, POD-only layout. No pointers cross the boundary, so a writer
//     that dies mid-update cannot leave a dangling reference for the reader.
//   * A sequence counter (seqlock) makes torn reads detectable: the reader
//     discards a snapshot whose sequence changed or is odd. Settings are read and
//     written by the game every frame, so a dropped snapshot costs nothing.
//   * The name carries no marker words. It is a random-looking token derived from
//     the build, not "FUT_CHEAT_SETTINGS" -- a scan for suspicious mapping names
//     finds an opaque string.
//   * No Global\ prefix: the mapping is session-local, so another user on the
//     machine cannot open it.
#include <cstdint>

namespace webui_shm {

// ---------------------------------------------------------------------------
// The mirrored settings. POD, fixed size, no pointers, no std:: types.
// Every field the UI can edit lives here; the game copies this whole struct.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// PACK(1) IS REQUIRED, NOT COSMETIC.
//
// The reader is a PowerShell script that addresses this struct by BYTE OFFSET.
// With any padding the offsets depend on the compiler alignment rules and on
// whatever #pragma pack an including translation unit happened to leave set --
// which is exactly what went wrong: the floats landed at 13/17/21 instead of the
// 16/20/24 the reader assumed, so every value would have been read from the wrong
// address with no error reported anywhere.
//
// pack(1) removes the ambiguity: each offset is simply the sum of all preceding
// field sizes, which the reader can verify. Field order must never be rearranged
// without updating the $FIELDS table in tools\webui\serve.ps1 to match.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct Params {
    // ---- ESP ----
    uint8_t  espEnabled;
    uint8_t  espBox;
    uint8_t  espCornerBox;
    uint8_t  espBones;
    uint8_t  espName;
    uint8_t  espDistance;
    uint8_t  espHealth;
    uint8_t  espSnapline;
    uint8_t  espTeamColor;
    uint8_t  radarEnable;
    uint8_t  antiStealth;
    uint8_t  outline;
    uint8_t  _pad0[4];

    float    espMaxDist;
    float    boxAlpha;
    float    radarRange;
    float    radarSize;
    float    radarX;
    float    radarY;

    // ---- aim ----
    uint8_t  aimEnabled;
    uint8_t  softEnable;
    uint8_t  hardEnable;
    uint8_t  softHold;
    uint8_t  hardHold;
    uint8_t  teamCheck;
    uint8_t  boneHead;
    uint8_t  preferChestWhenMoving;
    uint8_t  jitter;
    uint8_t  _pad1[3];

    int32_t  softKey;
    int32_t  hardKey;
    float    aimFov;
    float    aimSmooth;
    float    hardHoldMs;
    float    movingThreshold;
    float    jitterDeg;
    float    jitterHz;

    // ---- read-only status, written BY the game ----
    // The UI displays these but must not write them; the game overwrites the
    // whole block every frame, so a UI write here is simply lost.
    uint8_t  cameraOk;
    uint8_t  hasTarget;
    uint8_t  hardLocked;
    uint8_t  menuOpen;
    uint8_t  _pad2[4];

    int32_t  objects;
    int32_t  targets;
    int32_t  activeMode;      // 0 off, 1 soft, 2 hard
    // -----------------------------------------------------------------------
    // ONE DECLARATION PER LINE. DO NOT COMBINE.
    //
    // `float camX, camY, camZ;` is legal C++ but invisible to the field-table
    // generator, which matches single declarations. The three fields were
    // therefore missing from the generated offsets, every field after them was
    // read 12 bytes early, and the symptom was camFov reporting 2005.2 -- a value
    // that cannot exist. A reader that silently reads the wrong address is the
    // exact failure this layout is designed to make impossible, so the invariant
    // is: ONE FIELD PER LINE, always.
    // -----------------------------------------------------------------------
    float    camX;
    float    camY;
    float    camZ;
    float    camFov;
    char     lastError[64];

    // ---- control bytes, written BY the UI ----
    // `generation` is bumped by the UI on every change so the game can tell a
    // real edit from a stale block, and `heartbeat` lets the UI detect that the
    // game has gone away.
    uint32_t uiGeneration;
    uint32_t gameHeartbeat;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// API (game side)
// ---------------------------------------------------------------------------

// Create/open the mapping. Returns false when it cannot be created, which is
// NOT fatal: the feature is optional and the game runs without it.
bool Init();

// True once Init() succeeded and the block is usable.
bool Ready();

// Copy the game's current settings into the block and pick up any edit the UI
// made. Call once per frame; the cost is two memcpys.
//
// `get`/`set` are function pointers so this file needs no dependency on the
// settings modules -- the caller supplies the marshalling.
struct Bridge {
    void (*readFromGame)(Params& out);
    void (*writeToGame)(const Params& in);
    void (*readStatus)(Params& out);
};
void Tick(const Bridge& bridge);

// The seq lock, exposed for the reader side to mirror.
uint32_t Sequence();

// The mapping name (for the tools side to log/verify). Not secret, just opaque.
const char* MappingName();

} // namespace webui_shm
