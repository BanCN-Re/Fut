# Dumper-7 SDK 抓取报告

- SDK 根目录: `D:\Work\Fut\sdk\4.25.4-0+++UE4+Release-4.25-PM`
- 生成时间: 2026-09-30 20:36:41

## 1. 全局偏移（Dumpspace/OffsetsInfo.json）

来源: `Dumpspace\OffsetsInfo.json`

| 项 | 新值 | 旧值(D:\xingyun) | 变化 |
|---|---|---|---|
| GObjects | 0x72BE4B0 | 0x723A4B0 | **+540672 (0x84000)** |
| GNames | 0x72A4F80 | 0x7220F80 | **+540672 (0x84000)** |
| GWorld | 0x7420710 | 0x6E99018 | **+5797624 (0x5876F8)** |
| AppendString | 0x288FBB0 | 0x282FE60 | **+392528 (0x5FD50)** |
| ProcessEvent | 0x2A7D630 | 0x2A1D8F0 | **+392512 (0x5FD40)** |
| ProcessEventIdx | 0x45 | 0x45 | 相同 |

原始 data: `{"Dumper": 7, "OFFSET_GOBJECTS": 120317104, "OFFSET_APPENDSTRING": 42531760, "OFFSET_GNAMES": 120213376, "OFFSET_GWORLD": 121767696, "OFFSET_PROCESSEVENT": 44553776, "INDEX_PROCESSEVENT": 69}`

## 2. Basic.hpp 交叉核对

| 项 | Basic.hpp | OffsetsInfo | 一致 |
|---|---|---|---|
| GObjects | 0x72BE4B0 | 0x72BE4B0 | OK |
| GNames | 0x72A4F80 | 0x72A4F80 | OK |
| GWorld | 0x7420710 | 0x7420710 | OK |
| AppendString | 0x288FBB0 | 0x288FBB0 | OK |
| ProcessEvent | 0x2A7D630 | 0x2A7D630 | OK |
| ProcessEventIdx | 0x45 | 0x45 | OK |

## 3. 关键类成员偏移

扫描 7678 个 `*_classes.hpp`。

### ACyCharacterBase

| 成员 | 偏移 | 大小 |
|---|---|---|
| InventoryComponent | 0x7E8 | 0x8 |
| SkinComponent | 0x868 | 0x8 |
| LifeComponent | 0x870 | 0x8 |
| LifeStateComponent | 0x878 | 0x8 |
| AbilitySystem | 0x880 | 0x8 |
| AttributeTeamID | 0x8B0 | 0x1 |
| AttributeSkinID | 0x8B4 | 0x4 |

### UCyBaseAttributeSet

| 成员 | 偏移 | 大小 |
|---|---|---|
| Health | 0x78 | 0x10 |
| HealthMax | 0x88 | 0x10 |
| Armor | 0x98 | 0x10 |
| ArmorMax | 0xA8 | 0x10 |
| ExtraArmor | 0xB8 | 0x10 |
| ExtraArmorMax | 0xC8 | 0x10 |
| TotalHealthMax | 0xE8 | 0x10 |

### APMPlayerState

| 成员 | 偏移 | 大小 |
|---|---|---|
| InitGameTeamId | 0x1171 | 0x1 |
| TeamIndex | 0x13D0 | 0x1 |
| Rank | 0x1780 | 0x4 |
| PlayerPing | 0x1B00 | 0x4 |

### ACyPlayerState

| 成员 | 偏移 | 大小 |
|---|---|---|
| ShopComponent | 0x740 | 0x8 |
| SuitRoleSkinID | 0xCE8 | 0x4 |
| RoleSkinId | 0xD08 | 0x4 |

### ACyWeaponBase

| 成员 | 偏移 | 大小 |
|---|---|---|
| WeaponSkinId | 0x4E8 | 0x4 |
| MeshComponent | 0x8C8 | 0x8 |
| AttackComponent | 0x8D8 | 0x8 |

### UCyWeaponManagerBaseComponent

| 成员 | 偏移 | 大小 |
|---|---|---|
| CurrentWeapon | 0x1610 | 0x8 |
| Inventory | 0x1628 | 0x10 |

### APMCharacter

| 成员 | 偏移 | 大小 |
|---|---|---|
| FPPCameraRotateOffset | 0x17B8 | 0xC |
| RoleID | 0x17C4 | 0x4 |
| Rescuer | 0x1C50 | 0x8 |

### AActor

| 成员 | 偏移 | 大小 |
|---|---|---|
| RootComponent | 0x168 | 0x8 |

### ACharacter

| 成员 | 偏移 | 大小 |
|---|---|---|
| Mesh | 0x370 | 0x8 |
| CapsuleComponent | 0x380 | 0x8 |

### APawn

| 成员 | 偏移 | 大小 |
|---|---|---|
| PlayerState | 0x330 | 0x8 |
| Controller | 0x348 | 0x8 |

### AController

| 成员 | 偏移 | 大小 |
|---|---|---|
| Character | 0x350 | 0x8 |
| ControlRotation | 0x378 | 0xC |

### APlayerController

| 成员 | 偏移 | 大小 |
|---|---|---|
| AcknowledgedPawn | 0x390 | 0x8 |
| PlayerCameraManager | 0x3A8 | 0x8 |

### APlayerCameraManager

| 成员 | 偏移 | 大小 |
|---|---|---|
| DefaultFOV | 0x328 | 0x4 |
| DefaultAspectRatio | 0x364 | 0x4 |
| CameraCache | 0x3B0 | 0x630 |
| LastFrameCameraCache | 0x9E0 | 0x630 |

### APlayerState

| 成员 | 偏移 | 大小 |
|---|---|---|
| Score | 0x310 | 0x4 |
| PlayerNamePrivate | 0x400 | 0x10 |

### USceneComponent

| 成员 | 偏移 | 大小 |
|---|---|---|
| RelativeLocation | 0x198 | 0xC |
| RelativeRotation | 0x1A4 | 0xC |
| RelativeScale3D | 0x1B0 | 0xC |

### UWorld

| 成员 | 偏移 | 大小 |
|---|---|---|
| PersistentLevel | 0x30 | 0x8 |
| Levels | 0x148 | 0x10 |
| OwningGameInstance | 0x188 | 0x8 |

### ULevel

| 成员 | 偏移 | 大小 |
|---|---|---|
| Actors | 0x98 | 0x10 |

### UGameInstance

| 成员 | 偏移 | 大小 |
|---|---|---|
| LocalPlayers | 0x38 | 0x10 |

### UCharacterMovementComponent

| 成员 | 偏移 | 大小 |
|---|---|---|
| JumpZVelocity | 0x1D0 | 0x4 |
| MaxWalkSpeed | 0x20C | 0x4 |
| MaxWalkSpeedCrouched | 0x210 | 0x4 |
| MaxAcceleration | 0x220 | 0x4 |

### UCyFireComponent

| 成员 | 偏移 | 大小 |
|---|---|---|
| RecoilProcessor | 0x7E0 | 0x478 |
| RecoilProcessorMap | 0xC58 | 0x50 |

## 4. 产物清单

- `./` — 2 项
- `CppSDK/` — 6 项
- `Dumpspace/` — 5 项
- `IDAMappings/` — 1 项
- `Mappings/` — 1 项

