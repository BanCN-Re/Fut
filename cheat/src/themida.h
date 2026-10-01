// D:\Work\Fut\cheat\src\themida.h
//
// Oreans SecureEngine (Themida / Code Virtualizer) source-level markers.
//
// WHAT THIS BUYS
//   The macros expand to imports of SecureEngineSDK64.dll (VMStart / VMEnd /
//   MutateStart ...). When the DLL is processed by Themida64.exe /
//   Virtualizer.exe, the code between START/END is virtualised or mutated, so:
//     * a .text scan cannot recognise what the functions do
//     * the ESP/aim/net-filter logic stops being pattern-matchable
//   Unprotected builds still run fine: without the Themida pass the markers are
//   just no-op imported calls, so DEBUG builds stay debuggable.
//
//   This is exactly the property the project needs: ACE can read our memory
//   (verified from its import table), so the defence is to make the logic
//   unreadable, not to hide the module.
//
// USAGE
//   Wrap the hot, identifying routines -- not everything (virtualising the
//   render loop would cost frames). See aim.cpp / ue.cpp for placement.
//
// BUILD REQUIREMENT (release only)
//   Include: D:\obf\Themida\ThemidaSDK\Include\C
//   Link:    D:\obf\Themida\ThemidaSDK\Lib\COFF\SecureEngineSDK64.lib
//   Then process build\core\zw_core.dll with Themida64.exe.
//   The vcxproj wires these up under ZW_THEMIDA.
#pragma once

#ifdef ZW_THEMIDA
  #include "ThemidaSDK.h"
  #define ZW_VM_START()        VM_START
  #define ZW_VM_END()          VM_END
  #define ZW_MUTATE_START()    MUTATE_START
  #define ZW_MUTATE_END()      MUTATE_END
  #define ZW_STRENCRYPT_START() STR_ENCRYPT_START
  #define ZW_STRENCRYPT_END()   STR_ENCRYPT_END
#else
  // No-op stubs so debug/CI builds compile without the SDK.
  #define ZW_VM_START()        do {} while (0)
  #define ZW_VM_END()          do {} while (0)
  #define ZW_MUTATE_START()    do {} while (0)
  #define ZW_MUTATE_END()      do {} while (0)
  #define ZW_STRENCRYPT_START() do {} while (0)
  #define ZW_STRENCRYPT_END()   do {} while (0)
#endif
