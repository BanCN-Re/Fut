// D:\Work\Fut\cheat\src\stealth.h
// P4: module concealment (PEB unlink + VAD-aware design notes).
//
// ---------------------------------------------------------------------------
// WHAT THIS CAN AND CANNOT DO -- read before enabling.
// ---------------------------------------------------------------------------
//
// Our DLL is loaded by the game's own loader (it is the zlibwapi proxy), so it
// is necessarily visible in three places:
//
//   1. PEB->Ldr  (the loader's three module lists)
//   2. the VAD      (the memory manager's record of our mapped image)
//   3. the file system (zlibwapi.dll on disk)
//
// (3) cannot be changed -- the game must be able to load a real file.
// (2) can only be changed by unmapping, which we cannot do while executing.
// (1) CAN be unlinked, and that defeats the most common enumeration method.
//
// IMPORTANT: ACE-CSI64.dll imports NtQueryVirtualMemory + VirtualQueryEx
// (verified from its import table). That means the VAD route is available to
// ACE, and a PEB unlink alone will NOT hide us from a determined check.
//
// So this module is a PARTIAL measure with a real risk: touching the loader
// lists is a well-known cheat signature, and if ACE walks the PEB it will
// notice that the Ldr lists are inconsistent (our own import descriptors still
// point at us, our code still runs). A half-unlinked module is arguably MORE
// suspicious than an honest one.
//
// For that reason it is OFF by default and gated by a config switch:
//     SelfStealth = 0   (default) -- leave the loader lists intact
//     SelfStealth = 1             -- unlink from InLoadOrder + InMemoryOrder
//
// History note: the Nemesis project's earlier stealth attempt (own prior art in
// this workspace) additionally zeroed the PE headers and set the header page to
// PAGE_NOACCESS. That caused a deterministic crash 1 second after injection
// because the anti-cheat's own module enumerator read the inaccessible header
// page *inside its own code*. Do NOT re-add those two steps.
#pragma once

namespace stealth {

// Reads the SelfStealth switch (env var / config) and applies concealment when
// enabled. Safe to call once, early, from the boot thread.
void ApplyIfEnabled();

// Introspection for the GUI/trace.
bool WasApplied();
const char* LastResult();

} // namespace stealth
