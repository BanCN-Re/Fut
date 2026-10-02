// D:\Work\Fut\cheat\src\offsets.h
// All offsets verified against the Dumper-7 dump of 2026-09-30
// (Calabiyau-Win64-Shipping.exe, UE 4.25.4, game 1.18.3.7).
// Image base = 0x140000000, so actual address = base + RVA.
#pragma once
#include <cstdint>

namespace off {

// ---- globals (module-relative RVA) -------------------------------------
//
// Cross-checked against Dumpspace\OffsetsInfo.json -- the machine-readable copy
// the dumper itself wrote, and therefore the authoritative source:
//
//     OFFSET_GOBJECTS     = 120317104 = 0x72BE4B0   matches
//     OFFSET_GWORLD       = 121767696 = 0x7420710   matches
//     OFFSET_GNAMES       = 120213376 = 0x72A4F80   matches
//     OFFSET_APPENDSTRING =  42531760 = 0x288FBB0   matches
//     OFFSET_PROCESSEVENT =  44553776 = 0x2A7D630   matches
//     INDEX_PROCESSEVENT  = 69        = 0x45        matches
//
// Do NOT hand-convert those decimals. 121767696 was briefly "corrected" to
// 0x7420AD0 by mental arithmetic and that was WRONG -- it is 0x7420710. The
// byte pattern for the bad value was confirmed present in the built DLL, i.e.
// the mistake would have shipped. Verify with a calculator, not in your head.
constexpr uint32_t GObjects      = 0x072BE4B0;
constexpr uint32_t GNames        = 0x072A4F80;
constexpr uint32_t GWorld        = 0x07420710;
constexpr uint32_t AppendString  = 0x0288FBB0;
constexpr uint32_t ProcessEvent  = 0x02A7D630;
constexpr uint32_t ProcessEventIdx = 0x45;

// ---- TUObjectArray / FUObjectItem --------------------------------------
constexpr uint32_t ElemsPerChunk = 0x10000;
constexpr uint32_t FUObjectItemSize = 0x18;

// ---- UObject -----------------------------------------------------------
constexpr uint32_t UObj_VTable = 0x00;
constexpr uint32_t UObj_Flags  = 0x08;
constexpr uint32_t UObj_Index  = 0x0C;
constexpr uint32_t UObj_Class  = 0x10;
constexpr uint32_t UObj_Name   = 0x18;
constexpr uint32_t UObj_Outer  = 0x20;

// ---- UWorld ------------------------------------------------------------
constexpr uint32_t UW_PersistentLevel    = 0x30;
constexpr uint32_t UW_Levels             = 0x148;
constexpr uint32_t UW_GameState          = 0x130;
constexpr uint32_t UW_OwningGameInstance = 0x188;

// ---- ULevel / TArray ---------------------------------------------------
constexpr uint32_t TArray_Data = 0x00;
constexpr uint32_t TArray_Num  = 0x08;
constexpr uint32_t TArray_Max  = 0x0C;
constexpr uint32_t UL_Actors   = 0x98;

// ---- UGameInstance / ULocalPlayer / UPlayer ----------------------------
constexpr uint32_t UGI_LocalPlayers  = 0x38;
constexpr uint32_t ULP_ViewportClient = 0x70;
constexpr uint32_t UPlayer_PlayerController = 0x30;

// ---- UStruct (UClass derives from it) ----------------------------------
// Used for UObject::IsA: walk SuperStruct until null.
constexpr uint32_t UStruct_SuperStruct = 0x40;

// ---- AActor / APawn / AController --------------------------------------
constexpr uint32_t AA_RootComponent = 0x168;
constexpr uint32_t AP_PlayerState   = 0x330;
constexpr uint32_t AP_Controller    = 0x348;
constexpr uint32_t AC_ControlRotation = 0x378;

// ---- APlayerController / Camera ----------------------------------------
constexpr uint32_t APC_AcknowledgedPawn    = 0x390;
constexpr uint32_t APC_PlayerCameraManager = 0x3A8;
constexpr uint32_t APCM_DefaultFOV         = 0x328;

// FCameraCacheEntry.POV, measured LIVE on this build.
//
// Dumper-7 reports CameraCache @0x3B0 (LastFrameCameraCache @0x9E0), but on this
// build BOTH read back as loc=(0,0,0) fov=90 -- those slots are not where the
// live POV is. A live scan of the camera manager found the real FMinimalViewInfo
// at pcm+0x1CD0:
//     loc=(-12032.9, 23448.9, 116.6)  rot=(13.2, -80.2, 0.3)  fov=55.8
// with a slightly older copy at pcm+0x2300 (fov 55.2).
// fov 55.8 is the game's real value, which is why the old default of 90 threw
// the projection off and made the boxes drift.
constexpr uint32_t APCM_CameraCache           = 0x1CC0;
constexpr uint32_t APCM_LastFrameCameraCache  = 0x22F0;

// FCameraCacheEntry: Timestamp@0x00, Pad[0xC], POV(FMinimalViewInfo)@0x10
constexpr uint32_t FCC_POV        = 0x10;
// FMinimalViewInfo: Location@0x00(12) Rotation@0x0C(12) FOV@0x18
constexpr uint32_t FMVI_Location  = 0x00;
constexpr uint32_t FMVI_Rotation  = 0x0C;
constexpr uint32_t FMVI_FOV       = 0x18;

// ---- UFunction ---------------------------------------------------------
// Native implementation pointer. Used to resolve UGameViewportClient::PostRender
// by matching this address against the object's vtable.
constexpr uint32_t UFunction_ExecFunction = 0x100;

// ---- UFunction ---------------------------------------------------------
// ---- ACharacter / USceneComponent --------------------------------------
// ---------------------------------------------------------------------------
// Character mesh -- TWO candidates, try both.
//
// ACharacter::Mesh (0x370) is the generic UE4 slot, but this game does not put
// the visible character there. AWFBaseCharacter declares:
//
//     UWFBaseSkeletalMeshComponent* MeshFor3PP     @ 0x0EC0   <- 3rd person model
//     UWFBaseSkeletalMeshComponent* PaperMan2DMesh @ 0x0ED8   <- 2D paper mode
//
// Measured consequence of using only 0x370: the mesh pointer resolved, but its
// CachedComponentSpaceTransforms had data=0x0 / num=0, so the bone probe failed
// on every character and the ESP fell back to a capsule-sized box.
//
// bones::Get tries MeshFor3PP first, then ACharacter::Mesh.
// ---------------------------------------------------------------------------
constexpr uint32_t ACh_Mesh            = 0x370;
constexpr uint32_t WF_MeshFor3PP       = 0x0EC0;
constexpr uint32_t WF_PaperMan2DMesh   = 0x0ED8;
constexpr uint32_t ACh_CapsuleComponent = 0x380;
constexpr uint32_t USC_RelativeLocation = 0x198;
constexpr uint32_t USC_RelativeRotation = 0x1A4;

// ComponentToWorld: an FTransform (0x30) that Dumper-7 cannot attribute to a
// UPROPERTY, so it shows up as the Pad_168[0x30] inside USceneComponent.
// Layout: FQuat@+0x00 (16B), FVector Translation@+0x10 (12B), Scale3D@+0x20.
constexpr uint32_t Comp_ComponentToWorld = 0x168;

// ---- USkeletalMeshComponent bone transforms ----------------------------
// TArray<FTransform> of COMPONENT-space bone transforms (0x30 bytes each).
constexpr uint32_t Skinned_CachedBoneSpaceTransforms      = 0x07F0;
constexpr uint32_t Skinned_CachedComponentSpaceTransforms = 0x0800;

// ---- UCapsuleComponent -------------------------------------------------
constexpr uint32_t Capsule_HalfHeight = 0x4C0;
constexpr uint32_t Capsule_Radius     = 0x4C4;

// ---- ACyCharacterBase (game-specific) ----------------------------------
constexpr uint32_t CY_InventoryComponent  = 0x7E8;
constexpr uint32_t CY_SkinComponent       = 0x868;
constexpr uint32_t CY_LifeComponent       = 0x870;
constexpr uint32_t CY_LifeStateComponent  = 0x878;
// ACyCharacterBase cached base health. These are the AUTHORITATIVE values for
// this build (PMGame_classes.hpp, ACyCharacterBase):
//     BaseHealthMax  0x0698
//     BaseHealth     0x069C
// Used as the HP fallback when the AbilitySystem attribute set is unreadable.
// NOTE: the previous code read 0x46C/0x470 instead -- offsets copied from another
// project's walker that do not exist here. They returned unrelated floats, which
// is why reported HP was wrong.
// ---------------------------------------------------------------------------
// HEALTH -- THE FULL PATH. Reading AbilitySystem(0x880) + Health(0x78) always
// returned 0.0, because 0x880 is the BASE UAbilitySystemComponent which does not
// carry this game's attribute set:
//
//   ACyCharacterBase::AbilitySystem       @ 0x0880  UAbilitySystemComponent*   (base)
//   ACyCharacterBase::ValidAbilitySystem  @ 0x0888  UCyAbilitySystemComponent* (game)
//   UAbilitySystemComponent::SpawnedAttributes @ 0x01C0  TArray<UAttributeSet*>
//
// Correct walk:
//   actor + 0x888 -> asc ;  asc + 0x1C0 -> TArray<UAttributeSet*>
//   set[i] + UCyBaseAttributeSet::Health (0x78) + FGameplayAttributeData::CurrentValue (0x0C)
// ---------------------------------------------------------------------------
constexpr uint32_t CY_ValidAbilitySystem  = 0x0888;
constexpr uint32_t ASC_SpawnedAttributes  = 0x01C0;

constexpr uint32_t CY_BaseHealthMax = 0x0698;
constexpr uint32_t CY_BaseHealth    = 0x069C;
// ACyCharacterBase::IsAlive(bool) -- the engine's own alive verdict.
constexpr uint32_t Fn_IsAlive       = 0x2378630;

// ---------------------------------------------------------------------------
// Game's own highlight (outline) and stealth systems.
//
// Both are used instead of drawing our own effect, because the game's version is
// produced by the same renderer as the real one: correct depth handling, correct
// material, and it updates inside the game frame.
//
//   UCyCachedWorldSubsystem::GetStealthActors(UWorld*, TArray<AActor*>*)
//       -> every actor currently in a stealth state. This is the correct
//          anti-invisibility: ask the system that decides it, rather than
//          guessing from opacity or a visibility flag that differs per ability.
//   USkinnedMeshComponent::SetRenderOutlineMaterialEnabled(bool)
//       -> the outline the game applies for its own "see enemies" abilities.
//   USkinnedMeshComponent::GetRenderOutlineMaterialEnabled()
//       -> read-back, used to verify the toggle took effect.
// ---------------------------------------------------------------------------
constexpr uint32_t Fn_GetStealthActors                   = 0x2356E60;
// UKismetSystemLibrary::LineTraceSingle -- line-of-sight test for aim filtering.
// Taken from FunctionsInfo.json; the parameter struct is in visibility.cpp.
constexpr uint32_t Fn_LineTraceSingle                    = 0x44794F0;
constexpr uint32_t Fn_SetRenderOutlineMaterialEnabled    = 0x25D67B0;
constexpr uint32_t Fn_GetRenderOutlineMaterialEnabled    = 0x44F46E0;

constexpr uint32_t CY_AbilitySystem       = 0x880;
constexpr uint32_t CY_AttributeTeamID     = 0x8B0;   // uint8
constexpr uint32_t CY_AttributeSkinID     = 0x8B4;   // uint32

// ---- UCyBaseAttributeSet (GAS) -----------------------------------------
// FGameplayAttributeData = 8B private head + float BaseValue + float CurrentValue
constexpr uint32_t ATTR_Health         = 0x78;
constexpr uint32_t ATTR_HealthMax      = 0x88;
constexpr uint32_t ATTR_Armor          = 0x98;
constexpr uint32_t ATTR_ArmorMax       = 0xA8;
constexpr uint32_t ATTR_ExtraArmor     = 0xB8;
constexpr uint32_t ATTR_ExtraArmorMax  = 0xC8;
constexpr uint32_t ATTR_CurrentValue   = 0x0C;   // inside FGameplayAttributeData

// ---- APMCharacter ------------------------------------------------------
constexpr uint32_t PM_RoleID = 0x17C4;

// ---- functions (RVA) ---------------------------------------------------
constexpr uint32_t Fn_ProjectWorldLocationToScreen = 0x44CA420;  // APlayerController
constexpr uint32_t Fn_K2_GetActorLocation          = 0x43A1830;  // AActor
constexpr uint32_t Fn_GetActorBounds               = 0x439EFA0;  // AActor
constexpr uint32_t Fn_GetPlayerController          = 0x440A1A0;  // UGameplayStatics

} // namespace off
