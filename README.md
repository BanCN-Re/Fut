# Fut

**卡拉彼丘 / Strinova（Calabiyau）UE 4.25 逆向工程 · 完整实现档案**

目标：`Calabiyau-Win64-Shipping.exe` · UE **4.25.4** · 游戏 **1.18.3.7**（2026-09-22 更新）
目标进程内运行（`zlibwapi.dll` 代理侧载），反作弊为腾讯 **ACE**。

本仓库收录**源码、偏移、工具与全过程文档**；不收录 IDA 数据库、编译中间产物与构建产物（见 `.gitignore`）。

---

## 结果一览

| 项 | 状态 |
|---|---|
| SDK Dump | **198,592 objects** / 8,437 文件 / 136 MB |
| 注入方式 | `zlibwapi.dll`（**21 KB** 薄载体，转发 129 导出）+ `zw_core.dll`（真实载荷） |
| ESP 绘制位置 | **游戏内 `UCanvas`**（`PostRender` vtable slot 100），非外部 overlay |
| 骨骼 | **246 根**，按**骨骼名**解析 17 关节，画真骨架 |
| 血量 | `ValidAbilitySystem(0x888) → SpawnedAttributes(0x1C0) → Health(0x78)+0x0C` |
| 反隐身 | 调游戏自身 `GetStealthActors` |
| 透视/描边 | 游戏自身高亮系统（`FCyHighlightInfo`） |
| 录屏隐身 | `WDA_EXCLUDEFROMCAPTURE`（Win10 2004+，本机 build 26200 已验证） |
| ACE 网截 | 6 层 API · 22 域名 · 含 delay-import 与 ordinal-only 导入 |
| 接口混淆 | 字符串编译期加密 + 明文审计全绿 + Themida 集成 |
| 帧率 | 60 Hz 封锁（解除会导致 `dxgi+0xdad0` 崩溃） |

---

## 目录

```
cheat/
  src/            核心源码（ESP / 自瞄 / 骨骼 / 雷达 / 技能 / 网截 / 混淆 / GUI）
  loader/         21 KB 薄载体（zlibwapi 代理，129 导出转发）
  build/          构建输出（不入库）
tools/
  build_protected.ps1   构建 → 明文审计 → Themida 工程
  work/deploy_core.ps1  部署（校验哈希 + 转发目标 + 清日志 + 启动）
  *.py                  静态分析（PE / 导入 / 延迟导入 / 字符串 / 调用图）
docs/
  IMPLEMENTATION-LOG.md 12 阶段全过程档案（含每次失败与根因）
  REQUIREMENTS.md       需求台账 + 铁律
  funcs.md              坐标/变换类函数 RVA（8 组）
  *.md                  ACE 分析 / 加密分析 / 崩溃复盘
dumper/           Dumper-7 使用痕迹与校验脚本
sdk/              Dumper-7 产出（CppSDK + Dumpspace）
inject/           注入器
```

---

## 关键偏移

```
GObjects        0x072BE4B0     GNames        0x072A4F80
GWorld          0x07420710     AppendString  0x0288FBB0
ProcessEvent    0x02A7D630     ProcessEventIdx 0x45

UObject:  VTable 0x00  Flags 0x08  Index 0x0C  Class 0x10  Name 0x18  Outer 0x20
UFunction_ExecFunction 0x100    FunctionFlags 0xD8  (FUNC_Native = 0x400)
UStruct_SuperStruct 0x40

UWorld   PersistentLevel 0x30   OwningGameInstance 0x188   Levels 0x148
APCM     CameraCache 0x1CC0 (POV 0x1CD0)   LastFrameCameraCache 0x22F0
APC      ControlRotation 0x378  AcknowledgedPawn 0x390  PlayerCameraManager 0x3A8
Character Mesh 0x370  Capsule 0x380  WF_MeshFor3PP 0x0EC0  WF_PaperMan2DMesh 0x0ED8
USkinnedMesh  CachedBoneSpaceTransforms 0x07F0   CachedComponentSpaceTransforms 0x0800
CyCharacterBase  AbilitySystem 0x880  ValidAbilitySystem 0x888
                 AttributeTeamID 0x8B0  AttributeSkinID 0x8B4  LifeState 0x878
UCyAbilitySystem SpawnedAttributes 0x1C0
UCyBaseAttributeSet  Health 0x78  HealthMax 0x88  Armor 0x98  ArmorMax 0xA8
FGameplayAttributeData::CurrentValue 0x0C
```

**函数 RVA**
```
UCanvas::K2_Project              0x43DE820    UCanvas::K2_DrawLine   0x43DDA10
UCanvas::K2_DrawBox              0x43DD8C0    UCanvas::K2_DrawText   0x43DE070
UCanvas::K2_DrawTexture          0x43DE430    UCanvas::K2_StrLen     0x43DE8D0
ACyCharacterBase::IsAlive        0x2378630    ACyCharacterBase::GetInventoryComponent 0x23782F0
USkinnedMeshComponent::GetBoneName 0x44F42A0  GetNumBones            0x44F44B0
USceneComponent::GetSocketTransform 0x44E3900
UKismetSystemLibrary::LineTraceSingle 0x44794F0
APlayerController::ProjectWorldLocationToScreen 0x44CA420
AController::SetControlRotation  0x43EB250
```

---

## 构建与部署

```powershell
# 1. 构建（发布构建不得带 ZW_THEMIDA，否则引入 SecureEngineSDK64.dll 依赖 → 加载失败）
MSBuild.exe cheat\src\cheat.vcxproj /p:Configuration=Release /p:Platform=x64 /m
MSBuild.exe cheat\loader\loader.vcxproj /p:Configuration=Release /p:Platform=x64 /m

# 2. 部署（提权；会先关游戏）
powershell -File tools\work\deploy_core.ps1
```

部署产物：
- `zlibwapi.dll` → 游戏目录（**必须**保持薄载体）
- `zw_core.dll` → 游戏目录
- `zlibwapi_orig.dll`（133,936 B，正版）→ **转发目标，绝不可覆盖**

---

## 铁律

违反任意一条都会立刻回退到崩溃 / 黑屏 / 点不动。完整版见 `docs/REQUIREMENTS.md`。

1. `kFrameMs` **必须 16（60 Hz）** —— 2 ms 会导致 `dxgi+0xdad0` 崩溃（复现两次）
2. **不得 hook 游戏 dxgi 交换链**，不得对其调 `ResizeBuffers`
3. 交换链**必须 `DXGI_SWAP_EFFECT_DISCARD`**，不得 FLIP + ALLOW_TEARING
4. 穿透**只能靠 `LWA_COLORKEY`**：样式含 `WS_EX_LAYERED`、清除洋红、不用 `LWA_ALPHA`、不删 `WS_EX_LAYERED`、不依赖 `WM_NCHITTEST`
5. **不得隐藏 overlay 窗口**（菜单 + 常驻条在里面）
6. `input::Tick()` **必须在任何 `continue` 之前**
7. **不得把成员函数当自由函数调**（RCX 传 `this`）→ 崩 `0x08000008`
8. 偏移换算**必须用工具**，不能心算
9. 验证**必须用真实指标**（穿透要真点，不能用 `WindowFromPoint`）

---

## 已确认的坑（详见 `docs/IMPLEMENTATION-LOG.md`）

- 磁盘 `.text` **加密**（熵 7.9997），内存明文 → 只能活体 dump
- `PostRender` **不是 UFUNCTION** → GObjects 找不到，只能走 vtable
- `GObjects` 计数会在加载期被**冻结**在 7,004 → 必须每帧重读
- native UFUNCTION 经 `ProcessEvent` 必须临时置位 `FunctionFlags |= 0x400`
- 骨骼数组 `Num` 恒为 **0**，真实数量在 `Max`
- `ComponentToWorld` 四元数可能是 `(0,0,0,0)` → 按单位四元数处理
- 同一 mesh **246 根骨骼**含武器/挂件 → 必须选骨骼数最多的组件
- dump 里的 `CameraCache 0x3B0` 在此版本**已死**（读回 fov=90）
- `AppendString` 用调用方栈缓冲可避免 `FString` 堆分配泄漏

---

## 已知未完成

- [ ] 后坐力消除（`FCyRecoilProcessor` 字段偏移不在 dump 中）
- [ ] 墙后/墙前分色显示
- [ ] 技能冷却监控（`docs/skill-functions.md` 已提取 74 条函数）
- [ ] 骨骼命名表按本游戏实际骨架校正（现用 Biped/Mannequin 两套模式）

---

## 免责声明

本项目仅用于**逆向工程学习**与**反作弊机制研究**。请勿用于任何违反游戏服务条款或法律法规的用途。
使用者需自行承担全部后果。
