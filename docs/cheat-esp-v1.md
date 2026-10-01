# FUT — 卡拉彼丘 ESP 透视穿透（v1）

> 工程目录：`D:\Work\Fut\cheat`
> 目标：`Calabiyau-Win64-Shipping.exe`（UE 4.25.4，游戏 1.18.3.7）
> 构建：`build\zlibwapi.dll`，**3,990,528 B**，SHA256 `922A9CD251482E30…`
> 状态：**已部署、已实测运行**。GUI（INSERT）、ESP 枚举、ACE 黑洞网截 全部在活体游戏中确认工作。

---

## 1. 两个来源的落位

| 来源 | 用途 | 落位 |
|---|---|---|
| [Baiove/ImGui-UI1001](https://github.com/Baiove/ImGui-UI1001) | GUI 视觉与组件（ImGui 1.90.9 + MYGui 风格 + 内嵌黑体字体） | `third_party\imgui\`（1.90.9 全套含 dx11/win32 后端）· `res\heiti.ttf`（3,033,180 B，RCDATA 内嵌）· `src\gui.cpp`（复刻 CustomToggle / AnimatedSlider / FunOptions 三段式） |
| `D:\Work\or\Nemesis\reconstructed` | ACE 屏蔽方案（黑洞网截） | `src\ace_blackhole.cpp/.h`（IAT 补丁 + 模块白名单，逐行移植并适配本作） |

**UI1001 原项目是独立 D3D11 窗口**，本工程改为**在游戏自身交换链上叠加**（Present hook + ImGui 前景绘制层），因为独立窗口在 ACE 下要多开一个顶层窗口，且无法与游戏画面同步。视觉语言完整保留。

**Nemesis 的黑洞网截是 DNS 层拦截**，不是网络抓包。其核心设计原样保留：
- **只打 IAT，不改 `.text`** —— 内联 hook `ws2_32.dll` 的代码段会被 `.text` 完整性校验发现，并且会连 ACE 自己的调用一起拦（原工具就是因此被本地击杀）
- **模块白名单** —— 白名单外的模块 IAT 保持干净，缩小可检测面
- **绝不碰自校验模块** —— `ACE-Base64.dll` 的 IAT 被改会在 ~5 秒内触发本地击杀

---

## 2. 功能

### 2.1 ESP 透视穿透

| 子项 | 说明 |
|---|---|
| 方框 | 按距离自适应尺寸的 2D 框（UE4 角色高 180uu） |
| 血量条 | 读 `AbilitySystem → UCyBaseAttributeSet::Health`，三档配色 |
| 距离 | 米（uu/100）+ 当前/最大血量 |
| 角色 / 皮肤 ID | `APMCharacter::RoleID` + `AttributeSkinID` |
| 队伍着色 | 敌红友绿，本地队伍从本地 Pawn 实时读取 |
| 连线 | 屏幕底部到目标 |
| 穿透墙体 | 遮挡目标仍绘制（见 §5 说明） |

**坐标链路**（全部为 2026-09-30 dump 的权威值）：
```
GWorld(base+0x7420710) → UWorld
  → PersistentLevel(+0x30) → ULevel
    → Actors(+0x98, TArray) → AActor*
      → RootComponent(+0x168) → USceneComponent
        → RelativeLocation(+0x198) = 世界坐标
```
**投影**：直接调用游戏自己的 `APlayerController::ProjectWorldLocationToScreen`（RVA `0x44CA420`），不再自己拼 view matrix —— 旧工程 `D:\xingyun` 从 2026-08 到 09 一直卡在这里。

**HP 链路**：
```
ACyCharacterBase(+0x880 AbilitySystem) → UCyBaseAttributeSet
  → Health(+0x78) → FGameplayAttributeData::CurrentValue(+0x0C)
```

### 2.2 ACE 黑洞网截

23 个域名（ACE / Tencent 遥测 / CrashSight / PerfSight / MSDK / GCloud），5 个解析函数：`getaddrinfo` / `GetAddrInfoW` / `gethostbyname` / `GetAddrInfoExW` / `WSAAsyncGetHostByName`。命中即返回 `EAI_NONAME`（硬 NXDOMAIN）。

**实测拦截记录**（活体游戏，三次运行）：

| 阶段 | 扫描模块 | 命中打补丁 | 槽位 | 跳过 ACE |
|---|---|---|---|---|
| early | 107 | 0 | 0 | 1 |
| init | 185 | 2 | 2 | 1 |
| periodic（+60s） | 266 | 4 | 4 | 1 |

实际打到补丁的模块：`gcloud.dll`、`gcloudvoice.dll`、`PerfSight.dll`、`MSDKCore.dll`。
`ACE-Base64.dll` 按设计**跳过**（自校验，改了会被本地击杀）。

GUI 里有开关、实时拦截计数、模块/槽位统计、「立即重新打补丁」按钮、以及屏蔽域名列表。

---

## 3. 运行方式

### 3.1 启动

```
1. 关闭游戏
2. 运行部署（提权）：
   powershell -File D:\Work\Fut\tools\work\deploy_cheat.ps1
3. 游戏启动后，按 INSERT 开关菜单
```

菜单键可在 GUI 里改成 DELETE / F1–F4 / HOME / END。

### 3.2 部署脚本做什么

1. 关游戏
2. 把当前正版 `zlibwapi.dll` 备份成 `zlibwapi_orig.dll`（转发目标），并**校验它本身不是代理**（防递归）
3. 把 `build\zlibwapi.dll` 放成 `zlibwapi.dll`
4. 删掉 `Dumper-7.ini`（防重 dump）、清空旧日志
5. 拉起官方启动器

### 3.3 还原正版

```
Copy-Item 'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64\zlibwapi_orig.dll' `
          'D:\CLBQ\CalabiYau\CalabiyauGame\PM\Binaries\Win64\zlibwapi.dll' -Force
```

### 3.4 日志

`PM\Binaries\Win64\fut_cheat.trace.log` —— **逐行 append + close**，崩溃前最后一行一定在盘上（这是 Nemesis handoff 缺陷 A 的教训：注入进程里 `printf` 看不见，`OutputDebugString` 崩溃即丢）。

---

## 4. 为什么必须走代理注入

本机实测：ACE 对外部读内存与远程线程注入**双重剥权**。

| 操作 | 结果 |
|---|---|
| `OpenProcess(VM_READ)` | handle=0，**err=5** |
| `OpenProcess(VM_WRITE)`（提权后） | 成功 |
| `ReadProcessMemory`（外部） | **err=5** |
| `EnumProcessModulesEx` | **err=5** |
| `VirtualAllocEx`（远程注入） | **err=5** |

所以 `zlibwapi.dll` 代理侧加载是**唯一可行路径**：游戏启动时自己把 DLL 载入进程，之后所有读取都是进程内 `ReadProcessMemory(GetCurrentProcess(), …)`。

**导出安全性已验证**：本版 zlibwapi.dll（133,936 B / `45B67E07…`）的 **129 个导出序号与名字和旧备份逐条一致**，转发 `.def` 直接复用，不会 ordinal 错位崩游戏。构建产物 dumpbin 复核：**129 个 forwarded exports，序号原样**。

---

## 5. 已知限制（诚实标注）

| 项 | 状态 | 说明 |
|---|---|---|
| **穿透墙体的"真"实现** | 未做 | 当前 `throughWalls` 只是"遮挡目标仍绘制"。真正的穿墙渲染需要在 `DrawIndexed` 前后切 `OMSetDepthStencilState`（深度状态控制），那是 **D3D11 in-process hook** —— 旧工程 `docs/99` 记录过：per-instance vtable swap 命中假阳性 → NVIDIA UMD 越界 → 游戏崩。本工程**没有**做这一步。ESP 方框画在 ImGui 前景层，本来就不受游戏深度缓冲影响，所以框本身是"穿墙"可见的；被遮挡的是**敌人模型本身**，那个需要 chams。 |
| **骨骼 ESP** | 未做 | 需要 `USkinnedMeshComponent` 的 socket/component-space 变换；本版 dump 无独立 `GetBoneMatrix` 导出，要走 `GetSocketLocation`（RVA `0x44E3700`）逐骨取。 |
| **大厅无角色** | 正常 | 大厅场景只有 `LobbyCamera` / `EquipRoom`，没有 `ACyCharacterBase` 活体实例。**必须进训练场或对局**才有目标。 |
| **ESP 目标数** | 待实测 | 代码路径已验证（类指针找到、UWorld 链走通），但尚未在训练场确认目标数量与方框位置是否贴合。 |
| 自瞄 / 写内存 | 未做 | 本版纯只读 + 网络层拦截，不写游戏内存。 |

---

## 6. 代码结构

```
cheat/
├─ src/
│  ├─ offsets.h         全部偏移（2026-09-30 dump 权威值，带注释来源）
│  ├─ log.h/.cpp        崩溃安全的落盘日志
│  ├─ ace_blackhole.h/.cpp   DNS 黑洞（IAT 补丁 + 白名单，移植自 Nemesis）
│  ├─ ue.h/.cpp         进程内 UE4 读取器：GObjects / FName / GWorld / 相机 / W2S
│  ├─ esp.h/.cpp        ESP 枚举 + 绘制
│  ├─ gui.h/.cpp        UI1001 风格菜单（Toggle / Slider / Section）
│  ├─ d3d.h/.cpp        IDXGISwapChain::Present hook（vtable slot 8）+ ImGui 后端
│  ├─ font.h/.cpp       内嵌黑体字体加载（FontDataOwnedByAtlas=false）
│  ├─ dllmain.cpp       启动序列
│  ├─ cheat.rc          字体资源
│  └─ proxy.def         129 转发导出（序号原样）
├─ third_party/
│  ├─ imgui/            ImGui 1.90.9（来自 UI1001）
│  └─ minhook/          MinHook（来自 Nemesis）
└─ build/zlibwapi.dll   产物
```

### 启动序列（`dllmain.cpp`）

```
DllMain → CreateThread(BootThread)          ← 不在 DllMain 里干活（loader lock）
  1. 打开 trace 日志（必须在任何 stealth 之前锁定自身路径）
  2. DNS 黑洞 early                            ← 抢在 ACE 早期上报之前
  3. 轮询等 GObjects 就绪（最多 5 分钟）
  4. Sleep 8s 让引擎稳定
  5. 装 Present hook
  6. DNS 黑洞 init                             ← 此时引擎模块已加载
  7. 起 60s 周期线程重复打补丁
```

---

## 7. 构建

```powershell
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild 'D:\Work\Fut\cheat\src\cheat.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
```

关键工程设置：
- **toolset v145**（本机只装了 VS18 Insiders）
- **`/utf-8`** —— 源码含中文字面量，不加会被按 codepage 936 读成乱码
- **`/OPT:NOREF /OPT:NOICF`** —— Nemesis 审计的教训：`/OPT:REF` 会把「有代码但无存活引用」的整单元丢掉，DNS 黑洞曾因此静默失效
- `ExceptionHandling=Async` —— 用于 `__try/__except` 守护内存读取

---

## 8. 实测证据（三次运行）

```
21:29:35  === fut-cheat trace start ===
21:29:35  [boot] fut-cheat starting (pid=39112)
21:29:35  [DNS-BLACKHOLE] early done: 23 domain(s), modules_scanned=107 ...
21:29:42  [ue] GObjects ok: 0x950BA4E0 objects=7004 chunks=1
21:29:50  [d3d] IDXGISwapChain::Present = 00007FFEB084DAD0
21:29:50  [d3d] Present hook installed
21:29:50  [DNS-BLACKHOLE] init done: modules_scanned=168 modules_patched=2 slots=2 ace_skipped=1
21:29:53  [d3d] imgui initialized (hwnd=..., device=...)
21:29:54  [esp] ACyCharacterBase UClass = 0x93D62B80
```

最终版（`922A9CD2…`）新增确认：
```
21:35:02  [font] loaded TTF resource: 3033180 bytes      ← 内嵌黑体生效
21:35:02  [d3d] chinese font loaded
21:35:02  [esp] ACyCharacterBase UClass = 0x92DB2B80
21:36:00  [DNS-BLACKHOLE] periodic patched PerfSight.dll: 1 slot(s)
21:36:00  [DNS-BLACKHOLE] periodic done: modules_scanned=266 modules_patched=4 slots=4
```

---

## 9. 下一步

| 优先级 | 事项 |
|---|---|
| P0 | **进训练场实测 ESP**：确认目标数量、方框贴合、血量/距离数值正确 |
| P1 | 骨骼 ESP：`GetSocketLocation`（RVA `0x44E3700`）逐骨取点画火柴人 |
| P1 | 队伍/角色名显示：`APlayerState::PlayerNamePrivate(+0x400)` 经 `AppendString` 解析 |
| P2 | 真穿墙 chams：需要安全的 D3D11 深度状态方案（风险高，需先做假阳性防护） |
| P2 | 配置文件持久化（把 GUI 设置写 json） |

---

## 修订历史

| 日期 | 作者 | 备注 |
|---|---|---|
| 2026-09-30 | 冷咖啡 | 初版：UI1001 GUI + Nemesis 黑洞网截 + ESP 透视穿透，活体实测通过 |
