# 卡拉彼丘 UE4 全新逆向 · Dumper-7 完整抓取报告

> 工程目录：`D:\Work\Fut`
> 目标进程：`Calabiyau-Win64-Shipping.exe`（PID 34728 → dump 时 9076）
> 游戏版本：`1.18.3.7`，`Calabiyau-Win64-Shipping.exe` 2026-09-22 20:38 更新
> 引擎版本：**UE 4.25.4**（`4.25.4-0+++UE4+Release-4.25-PM`）
> 抓取时间：2026-09-30 20:32（dump 用时约 90 秒）
> 结论：**全新 SDK 已生成，198,592 个对象，全局偏移与函数 RVA 全部拿到并落盘。旧 `D:\xingyun` 数据仅作对照。**

---

## 1. 交付物清单

| 产物 | 路径 | 大小 | SHA256(前16) |
|---|---|---|---|
| **完整 CppSDK**（8,428 文件） | `sdk\4.25.4-0+++UE4+Release-4.25-PM\CppSDK\` | — | — |
| **全局偏移（机器可读）** | `sdk\...\Dumpspace\OffsetsInfo.json` | 335 B | `AF3BFBBCD8691F9F` |
| **类信息** | `sdk\...\Dumpspace\ClassesInfo.json` | 4,783,808 B | `6742E0CA70141C24` |
| **函数表（14,517 个）** | `sdk\...\Dumpspace\FunctionsInfo.json` | 2,342,970 B | — |
| **结构体表** | `sdk\...\Dumpspace\StructsInfo.json` | — | — |
| **枚举表** | `sdk\...\Dumpspace\EnumsInfo.json` | — | — |
| **GObjects 全量对象表** | `sdk\...\GObjects-Dump.txt` | 23,511,655 B | `D07DDF3B662F1928` |
| **GObjects + 属性** | `sdk\...\GObjects-Dump-WithProperties.txt` | 28,433,552 B | `C3A48FBD06148B4B` |
| **IDA 映射** | `sdk\...\IDAMappings\*.idmap` | 6,297,955 B | `3E0BD0526C5DAAF8` |
| **UE 映射（CUE4Parse）** | `sdk\...\Mappings\*.usmap` | 672,259 B | `F674EB82A05B0EC7` |
| SDK 总占用 | `sdk\` | **136.1 MB** | — |

- 对象总数：**198,592**（旧 `D:\xingyun` dump 为 5,291，本次是其 **37.5 倍**）
- 类头文件：**7,678** 个 `*_classes.hpp`；`PMGame_classes.hpp` 单文件 1,280 个类
- 分析报告：[sdk-report.md](sdk-report.md) · [funcs.md](funcs.md)

---

## 2. 全局偏移（新旧对照）—— 全部已变

来源 `Dumpspace\OffsetsInfo.json`，与 `CppSDK\SDK\Basic.hpp` 交叉核对**完全一致**。

| 项 | **新值（2026-09-30）** | 旧值（D:\xingyun） | 位移 |
|---|---|---|---|
| **GObjects** | **`0x072BE4B0`** | `0x0723A4B0` | +0x84000 |
| **GNames** | **`0x072A4F80`** | `0x07220F80` | +0x84000 |
| **GWorld** | **`0x07420710`** | `0x06E99018` | +0x5876F8 |
| **AppendString** | **`0x0288FBB0`** | `0x0282FE60` | +0x5FD50 |
| **ProcessEvent** | **`0x02A7D630`** | `0x02A1D8F0` | +0x5FD40 |
| **ProcessEventIdx** | `0x45` | `0x45` | 相同 |

> `AppendString` 与 `ProcessEvent` 位移几乎相同（差 0x10），`GObjects` 与 `GNames` 位移完全一致（+0x84000）——说明这两组符号在模块里相邻排布，可作为下次更新后的快速校验依据。

---

## 3. UObject / 容器布局（本次实测确认）

`TUObjectArray`（`CppSDK\SDK\Basic.hpp`）：

```cpp
class TUObjectArray final {
    static constexpr auto DecryptPtr = [](void* ObjPtr) -> uint8* {
        return reinterpret_cast<uint8*>(ObjPtr);      // 恒等，无 XOR
    };
    static constexpr int32 ElementsPerChunk = 0x10000;   // 每块 65536 个
    struct FUObjectItem** Objects;   // 0x0000
    uint8                 Pad_8[0x8];// 0x0008
    int32                 MaxElements;// 0x0010
    int32                 NumElements;// 0x0014
    int32                 MaxChunks;  // 0x0018
    int32                 NumChunks;  // 0x001C
};
```

`FUObjectItem` 大小 = **0x18（24 字节）**，仅 `Object` 指针 + 0x10 填充。

> **这解决了旧工程长期悬而未决的问题**：`D:\xingyun\docs\10/14/18` 反复怀疑「stride 是 16B 还是 24B」，答案确定是 **0x18 = 24B**，且 **DecryptPtr 是恒等函数**（指针未加密），chunk 容量 `0x10000`。

`UObject` 布局（`CoreUObject_classes.hpp`，与旧工程假设一致）：

```cpp
void*         VTable;   // 0x00
EObjectFlags  Flags;    // 0x08
int32         Index;    // 0x0C
class UClass* Class;    // 0x10
class FName   Name;     // 0x18  (int32 ComparisonIndex + uint32 Number)
class UObject* Outer;   // 0x20
```

---

## 4. 坐标 / 变换 / 投影 —— 函数 RVA 速查表

**实际地址 = 镜像基址（`0x140000000`）+ RVA。** 完整分组表（含组件级、骨骼、相机、数学共 8 组）见 [funcs.md](funcs.md)。

### 4.1 Actor 级位置 / 旋转

| 函数 | 类 | RVA | 实际地址 |
|---|---|---|---|
| `K2_GetActorLocation` | `AActor` | `0x43A1830` | `0x1443A1830` |
| `K2_GetActorRotation` | `AActor` | `0x43A1910` | `0x1443A1910` |
| `GetActorBounds` | `AActor` | `0x439EFA0` | `0x1439EFA0` |
| `GetTransform` | `AActor` | `0x43A0310` | `0x1443A0310` |
| `GetVelocity` | `AActor` | `0x43A03C0` | `0x1443A03C0` |
| `GetActorForwardVector` | `AActor` | `0x439F240` | `0x1439F240` |
| `K2_GetRootComponent` | `AActor` | `0x23909A0` | `0x1423909A0` |
| `GetActorEyesViewPoint` | `AActor` | `0x439F550` | `0x1439F550` |

### 4.2 组件级位置 / 变换

| 函数 | 类 | RVA |
|---|---|---|
| `K2_GetComponentLocation` | `USceneComponent` | `0x44E4EA0` |
| `GetSocketLocation` | `USceneComponent` | `0x44E3700` |
| `GetComponentToWorld` 系列 | `USceneComponent` | 见 funcs.md |

### 4.3 世界坐标 → 屏幕投影

| 函数 | 类 | RVA |
|---|---|---|
| **`ProjectWorldLocationToScreen`** | `APlayerController` | **`0x44CA420`** |
| `ProjectWorldToScreen` | `UGameplayStatics` | `0x440CDC0` |

### 4.4 相机 / 视角

| 函数 | 类 | RVA |
|---|---|---|
| `GetCameraLocation` | `APlayerCameraManager` | `0x44C0CA0` |
| `GetCameraRotation` | `APlayerCameraManager` | `0x44C0CE0` |
| `GetPlayerCameraManager` | `UGameplayStatics` | `0x440A020` |

### 4.5 玩家链

| 函数 | 类 | RVA |
|---|---|---|
| `GetPlayerCharacter` | `UGameplayStatics` | `0x440A0E0` |
| `GetPlayerController` | `UGameplayStatics` | `0x440A1A0` |
| `GetActorFeetLocation` | `UFlyingNavFunctionLibrary` | `0x11DA5A0` |

### 4.6 骨骼坐标

| 函数 | 类 | RVA |
|---|---|---|
| `GetBoneLocationByName` | `UPoseableMeshComponent` | `0x44D04B0` |

> `GetBoneMatrix` 在本版函数表中未直接出现，骨骼矩阵需走 `USkinnedMeshComponent::GetSocketMatrix` / `GetComponentSpaceTransforms`（见 funcs.md「骨骼 / 网格」组）或直接读骨骼缓存。

---

## 5. 关键类成员偏移（新值）

### 5.1 全局偏移链

| 类 | 成员 | 偏移 | 大小 |
|---|---|---|---|
| `UWorld` | PersistentLevel | `0x30` | 0x8 |
| | Levels | `0x148` | 0x10 |
| | OwningGameInstance | `0x188` | 0x8 |
| `ULevel` | Actors | `0x98` | 0x10 |
| `UGameInstance` | LocalPlayers | `0x38` | 0x10 |
| `AActor` | RootComponent | `0x168` | 0x8 |
| `APawn` | PlayerState | `0x330` | 0x8 |
| | Controller | `0x348` | 0x8 |
| `AController` | Character | `0x350` | 0x8 |
| | ControlRotation | `0x378` | 0xC |
| `APlayerController` | AcknowledgedPawn | `0x390` | 0x8 |
| | PlayerCameraManager | `0x3A8` | 0x8 |
| `APlayerCameraManager` | CameraCache | `0x3B0` | 0x630 |
| | LastFrameCameraCache | `0x9E0` | 0x630 |
| | DefaultFOV | `0x328` | 0x4 |
| `ACharacter` | Mesh | `0x370` | 0x8 |
| | CapsuleComponent | `0x380` | 0x8 |
| `USceneComponent` | **RelativeLocation** | **`0x198`** | 0xC |
| | RelativeRotation | `0x1A4` | 0xC |
| | RelativeScale3D | `0x1B0` | 0xC |
| `APlayerState` | PlayerNamePrivate | `0x400` | 0x10 |
| | Score | `0x310` | 0x4 |

> ⚠️ **`USceneComponent::RelativeLocation` 是 `0x198`，不是旧工程一直用的 `0xA0`。** 旧 `0xA0` 是 4.27 stock 猜测值，本次 dump 权威值 = `0x198`（旧 `docs/31` 海外 SDK 也写 `0x198`，与本次一致）。

### 5.2 游戏专有类（Cy 前缀）

| 类 | 成员 | 偏移 | 大小 |
|---|---|---|---|
| `ACyCharacterBase` | InventoryComponent | `0x7E8` | 0x8 |
| | **SkinComponent** | `0x868` | 0x8 |
| | LifeComponent | `0x870` | 0x8 |
| | LifeStateComponent | `0x878` | 0x8 |
| | **AbilitySystem** | **`0x880`** | 0x8 |
| | **AttributeTeamID** | **`0x8B0`** | 0x1 |
| | **AttributeSkinID** | **`0x8B4`** | 0x4 |
| `UCyBaseAttributeSet` | **Health** | **`0x78`** | 0x10 |
| | HealthMax | `0x88` | 0x10 |
| | Armor | `0x98` | 0x10 |
| | ArmorMax | `0xA8` | 0x10 |
| | ExtraArmor | `0xB8` | 0x10 |
| | TotalHealthMax | `0xE8` | 0x10 |
| `ACyPlayerState` | ShopComponent | `0x740` | 0x8 |
| | SuitRoleSkinID | `0xCE8` | 0x4 |
| | **RoleSkinId** | **`0xD08`** | 0x4 |
| `ACyWeaponBase` | **WeaponSkinId** | **`0x4E8`** | 0x4 |
| | MeshComponent | `0x8C8` | 0x8 |
| | **AttackComponent** | **`0x8D8`** | 0x8 |
| `UCyWeaponManagerBaseComponent` | CurrentWeapon | `0x1610` | 0x8 |
| | Inventory | `0x1628` | 0x10 |
| `APMCharacter` | FPPCameraRotateOffset | `0x17B8` | 0xC |
| | **RoleID** | **`0x17C4`** | 0x4 |
| | Rescuer | `0x1C50` | 0x8 |
| `APMPlayerState` | InitGameTeamId | `0x1171` | 0x1 |
| | TeamIndex | `0x13D0` | 0x1 |
| | Rank | `0x1780` | 0x4 |
| | PlayerPing | `0x1B00` | 0x4 |
| `UCyFireComponent` | RecoilProcessor | `0x7E0` | 0x478 |
| | RecoilProcessorMap | `0xC58` | 0x50 |
| `UCharacterMovementComponent` | JumpZVelocity | `0x1D0` | 0x4 |
| | MaxWalkSpeed | `0x20C` | 0x4 |
| | MaxWalkSpeedCrouched | `0x210` | 0x4 |
| | MaxAcceleration | `0x220` | 0x4 |

**HP 读取链（新偏移）**：
```
ACyCharacterBase (0x880 AbilitySystem) → UCyBaseAttributeSet (0x78 Health)
  → FGameplayAttributeData { +0x08 BaseValue, +0x0C CurrentValue }
当前血量 = *(float*)(AttributeSet + 0x78 + 0x0C)
最大血量 = *(float*)(AttributeSet + 0x88 + 0x0C)
```

**坐标读取链（新偏移）**：
```
AActor (0x168 RootComponent) → USceneComponent (0x198 RelativeLocation)
世界坐标 = *(FVector*)(RootComponent + 0x198)
```

### 5.3 与旧工程的关键差异

| 项 | 旧（D:\xingyun） | **新（本次 dump）** | 说明 |
|---|---|---|---|
| `USceneComponent::RelativeLocation` | `0xA0` | **`0x198`** | 旧值错误，本次为权威值 |
| `ACyCharacterBase::AbilitySystem` | `0x860` | **`0x880`** | +0x20 |
| `ACyCharacterBase::AttributeTeamID` | `0x890` | **`0x8B0`** | +0x20 |
| `ACyCharacterBase::AttributeSkinID` | `0x894` | **`0x8B4`** | +0x20 |
| `UCyBaseAttributeSet::Health` | `0x70` | **`0x78`** | +0x8 |
| `ACyWeaponBase::WeaponSkinId` | `0x4B0`/`0x4E0` | **`0x4E8`** | 旧文档本身冲突，本次权威 |
| `ACyWeaponBase::AttackComponent` | `0x898` | **`0x8D8`** | +0x40 |
| `ACyPlayerState::RoleSkinId` | `0xCD8` | **`0xD08`** | +0x30 |
| `UCharacterMovementComponent::MaxWalkSpeed` | `0x2CC` | **`0x20C`** | 差 0xC0 |
| `APMCharacter::RoleID` | `0x1784` | **`0x17C4`** | +0x40 |
| `APlayerCameraManager::CameraCache` | `0x1F0`(stock 猜测) | **`0x3B0`** | 本次权威 |
| `FUObjectItem` stride | 疑 16B / 24B | **`0x18` = 24B** | 悬案结案 |

---

## 6. 复现步骤

### 6.1 环境

| 项 | 值 |
|---|---|
| 编译器 | MSVC `14.51.36231`（VS 18 Insiders，toolset **v145**） |
| MSBuild | `C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe` |
| Windows SDK | `10.0.26100.0` / `10.0.28000.0` |
| Dumper-7 | `dumper\src\Dumper-7-main\`（toolset 已从 v142 改为 **v145**） |
| 输出目录 | 已写入 `Settings.h` 的 `SDKGenerationPath = "D:/Work/Fut/sdk"` |

### 6.2 构建代理

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild 'D:\Work\Fut\dumper\src\Dumper-7-main\Dumper\Dumper.vcxproj' `
    /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
# 产物: dumper\src\Dumper-7-main\Dumper\x64\Release\zlibwapi.dll  (1,477,632 B)
```

### 6.3 部署（提权通道）

Dumper-7 是**进程内 DLL**，而本机 ACE 拦截外部 `ReadProcessMemory`/`EnumProcessModules`（实测 `err=5`），
所以只能走 **zlibwapi 代理侧加载**——游戏启动时自己把 DLL 载入进程。

```powershell
# 1) 关游戏 → 2) 把当前正版 zlibwapi.dll 备份成 zlibwapi_orig.dll（转发目标）
# 3) 把代理放成 zlibwapi.dll  4) 在游戏目录写 Dumper-7.ini  5) 重启游戏
powershell -File D:\Work\Fut\tools\work\deploy_and_launch.ps1
```

**导出安全性已验证**：本版 `zlibwapi.dll`（2026-09-22，133,936 B）与旧备份的 **129 个导出序号 + 名字逐条完全一致**，因此代理 .def 可直接复用，不会出现 ordinal 错位导致游戏崩溃。

游戏目录 `Dumper-7.ini`（**优先于** `C:\Dumper-7\Dumper-7.ini`）：

```ini
[Settings]
SDKGenerationPath=D:/Work/Fut/sdk
SleepTimeout=180
SDKNamespaceName=SDK
```

`SleepTimeout=180` 让 Dumper-7 等 180 秒再开始生成，避开启动期 ACE 扫描与地图加载。

### 6.4 还原

```powershell
powershell -File D:\Work\Fut\tools\work\restore.ps1
# 关游戏 → 用 zlibwapi_orig.dll 覆盖 zlibwapi.dll → 校验哈希 → 删 Dumper-7.ini
```

### 6.5 生成报告

```powershell
python D:\Work\Fut\tools\sdk_report.py  'D:\Work\Fut\sdk\4.25.4-0+++UE4+Release-4.25-PM' --out D:\Work\Fut\docs\sdk-report.md
python D:\Work\Fut\tools\func_report.py 'D:\Work\Fut\sdk\4.25.4-0+++UE4+Release-4.25-PM' --out D:\Work\Fut\docs\funcs.md
```

---

## 7. 提权通道说明

本机当前 shell **非管理员**，且 ACE 对目标进程剥权（普通权限 `OpenProcess(VM_READ)` → `err=5`）。
可用的提权通道是既有计划任务 **`ClbqAdmin`**（`RunLevel=Highest`，`LogonType=Interactive`）：

```powershell
# 写待执行脚本路径 → 删 flag → 触发
Set-Content 'D:\xingyun\tools\work\clbq_work.txt' 'D:\Work\Fut\tools\work\<脚本>.ps1' -NoNewline
Remove-Item 'D:\xingyun\tools\work\clbq_work_done.flag' -Force -ErrorAction SilentlyContinue
Remove-Item 'D:\xingyun\tools\work\clbq_runner.lock' -Force -ErrorAction SilentlyContinue
Start-ScheduledTask -TaskName 'ClbqAdmin'
# 完成后出现 clbq_work_done.flag，日志在 D:\xingyun\tools\work\clbq_work.log
```

> 该 runner 的 `clbq_work.txt` 此前被 `D:\Work\Skybuff` 占用，已备份为 `clbq_work.txt.skybuff.bak`。

---

## 8. 关键结论

1. **游戏 2026-09-22 更新后，旧 `D:\xingyun` 的全部 RVA 已作废**——`GObjects` 移了 `0x84000`，`GWorld` 移了 `0x5876F8`，`ProcessEvent` 移了 `0x5FD40`。旧文档里的所有 vtable RVA、堆锚点、函数地址一律不可复用。
2. **本次拿到的是权威值**，来源是 Dumper-7 在进程内实测（`OffsetsInfo.json` 与 `Basic.hpp` 双份一致），不是签名猜测。
3. **旧工程的两个悬案结案**：`FUObjectItem` stride = `0x18`（24B）；指针**未加密**（`DecryptPtr` 恒等）。
4. **`USceneComponent::RelativeLocation` = `0x198`**（旧工程长期用错的 `0xA0` 可以彻底废弃）。
5. **坐标链路已完整**：`AActor::RootComponent(0x168)` → `USceneComponent::RelativeLocation(0x198)`；投影走 `APlayerController::ProjectWorldLocationToScreen(0x44CA420)`；相机走 `APlayerCameraManager::CameraCache(0x3B0)` / `GetCameraLocation(0x44C0CA0)`。
6. **HP 链路已完整**：`ACyCharacterBase::AbilitySystem(0x880)` → `UCyBaseAttributeSet::Health(0x78)+0x0C`。
7. **外部读内存被 ACE 挡死**（`OpenProcess(0x10)` → `err=5`，`EnumProcessModulesEx` → `err=5`）。任何活体取值都必须**进程内**（代理 DLL）或走 DMA 硬件。

---

## 9. 下一步可选

| 优先级 | 事项 |
|---|---|
| P0 | 用新偏移把 `esp` 的 walker 重定向（`GWorld`→`PersistentLevel`→`Actors`），替换旧 vtable 扫描方案 |
| P0 | 用 `ProjectWorldLocationToScreen` 打通 3D ESP（旧工程从未解决 view matrix，现在有函数可直接调） |
| P1 | 写进程内取值器：遍历 Actors → 读坐标/HP/队伍/皮肤，输出实时表 |
| P1 | 骨骼坐标：走 `USkinnedMeshComponent` 的 socket/component-space 变换（`GetBoneMatrix` 本版无独立导出） |
| P2 | 用 `Mappings\*.usmap` 配合 CUE4Parse 做资源层解析 |

---

## 10. 修订历史

| 日期 | 作者 | 备注 |
|---|---|---|
| 2026-09-30 | 冷咖啡 | 初版：Dumper-7 全新 dump（198,592 对象）+ 偏移/函数/类成员三层提取 + 新旧差异对照 |
