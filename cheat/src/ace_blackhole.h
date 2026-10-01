// D:\Work\Fut\cheat\src\ace_blackhole.h
// DNS blackhole: per-module IAT patching with a module whitelist.
//
// Ported from D:\Work\or\Nemesis\reconstructed\src\hooks\ace_blackhole.cpp
// (the field-proven EndFieldHack approach). Key design points kept:
//   * IAT patch only -- never rewrite any .text of ws2_32.dll (an inline hook
//     there is visible to a .text integrity check and intercepts ACE's own
//     calls, which is what got the original tool killed locally).
//   * Module whitelist -- off-whitelist modules keep a clean IAT.
//   * Never patch the self-checking ACE module (ACE-Base64.dll here).
//   * Three phases: early / post-init / periodic, because ACE can load new
//     modules later whose clean IATs would re-open the upload path.
#pragma once

namespace ace {

// Runs one IAT-patching pass over the whitelisted modules.
void InstallDnsBlackholePhase(const char* phase);
void InstallDnsBlackholeEarly();
void InstallDnsBlackhole();
void InstallDnsBlackholePeriodic();

// Stats / introspection (used by the GUI panel).
unsigned BlockedCount();
bool     IsBlockedHost(const char* host);
int      BlocklistCount();
const char* BlocklistEntry(int i);
int      LastPatchedModules();
int      LastPatchedSlots();
int      LastModulesScanned();

} // namespace ace
