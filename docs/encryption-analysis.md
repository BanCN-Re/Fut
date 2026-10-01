# 加密 / 混淆 实测结论（2026-09-30）

> 目标：`Calabiyau-Win64-Shipping.exe`（PID 34728 / 9076 / 18848，2026-09-22 版，1.18.3.7）
> 方法：**磁盘静态分析** + **进程内活体实测**（把探针作为 zlibwapi 代理注入，从进程内部读）
> 结论一句话：**磁盘上的代码是加密的，但内存里的代码和全部游戏数据都是明文。函数能直接调，坐标能直接读。**

---

## 1. 磁盘侧：代码确实被加密/保护

`tools\pe_scan.py` 扫 PE 节表：

| 节 | RVA | 虚拟大小 | 原始大小 | 熵 | 属性 |
|---|---|---|---|---|---|
| `.std` | `0x1000` | `0x51C5137`（81.8 MB） | `0x51C5200` | **7.9997** | R X |
| `.std` | `0x51C7000` | `0x1BE8856` | `0x1BE8A00` | 5.3125 | R |
| `.std` | `0x6DB0000` | `0x6E08E8` | `0x33E800` | 3.9913 | RW |
| `.std` | `0x7491000` | `0x4A259C` | `0x16E400` | **7.9999** | R |
| `.std` | `0x79E2000` | `0x2F5548` | `0x2FE00` | **7.9986** | R |
| `.std` | `0x847A000` | `0x418D6` | `0x41A00` | **7.9987** | R X |

- 节名全是 `.std`（不是标准 `.text`/`.rdata`）——段名被改写
- 主代码节熵 **7.9997**（理论最大 8.0）= 接近完全随机
- 123 MB 的 EXE 里，多个节熵 ≥7.99

`tools\prologue_scan.py` 统计 x64 函数序言（这是判别「明文代码」最硬的指标）：

| 序言模式 | 出现次数（81.8 MB 代码节） |
|---|---|
| `48 89 5C 24`（mov [rsp+x], rbx） | **0** |
| `48 89 4C 24` | **0** |
| `48 89 54 24` | **0** |
| `F3 0F 1E FA`（endbr64） | **0** |
| `CC CC CC CC`（INT3 填充） | **0** |
| `48 83 EC`（sub rsp, imm8） | **3** |
| `48 8D 05`（lea rax, [rip+x]） | **6** |
| 合计 | **67 次 / MB** |

**正常编译的 x64 代码每 MB 有数千个函数序言**，这里是 67 个。**磁盘上的 `.text` 是加密的**（自解密壳 / VM 保护），符合旧工程 `docs/19-vm-analysis` 的判断（`.tvm0` 自研 VM，不匹配公开 devirt 工具）。

---

## 2. 内存侧：全部明文，实测证据

把探针作为 zlibwapi 代理注入进程内部（外部 `ReadProcessMemory` 被 ACE 拒绝，`err=5`），读到的东西完全相反：

### 2.1 函数体在内存里是明文机器码

`tools\rva_probe.py` 对磁盘文件读同一批 RVA 得到的是随机字节；进程内读同一批 RVA 得到的是标准 x64 序言：

| 函数 | RVA | 内存中前 16 字节 | 熵 |
|---|---|---|---|
| `AActor::K2_GetActorLocation` | `0x43A1830` | `48 89 5C 24 18 55 48 83 EC 30 48 8B 42 20 45 33` | 4.56 |
| `AActor::K2_GetActorRotation` | `0x43A1910` | `48 89 6C 24 20 41 56 48 83 EC 40 48 8B 42 20 45` | 4.50 |
| `AActor::GetActorBounds` | `0x439EFA0` | `48 8B C4 48 89 58 08 55 56 57 41 56 41 57 48 83` | 4.14 |
| `AActor::GetTransform` | `0x43A0310` | `48 89 5C 24 10 48 89 6C 24 18 56 48 83 EC 20 48` | 4.39 |
| `AActor::GetVelocity` | `0x43A03C0` | `40 53 48 83 EC 30 48 8B 42 20 45 33 C9 48 85 C0` | 4.56 |
| `AActor::GetActorForwardVector` | `0x439F240` | `40 53 48 83 EC 30 48 8B 42 20 45 33 C9 48 85 C0` | 4.48 |
| `AActor::K2_GetRootComponent` | `0x23909A0` | `48 8B 42 20 45 33 C9 48 85 C0 41 0F 95 C1 4C 03` | 4.45 |
| `USceneComponent::K2_GetComponentLocation` | `0x44E4EA0` | `40 53 48 83 EC 30 48 8B 42 20 45 33 C9 48 85 C0` | 4.56 |
| `USceneComponent::GetSocketLocation` | `0x44E3700` | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 41` | 4.19 |
| `APlayerController::ProjectWorldLocationToScreen` | `0x44CA420` | `48 89 5C 24 08 48 89 6C 24 20 56 57 41 56 48 83` | 4.05 |
| `APlayerCameraManager::GetCameraLocation` | `0x44C0CA0` | `40 53 48 83 EC 30 48 8B 42 20 45 33 C9 48 85 C0` | 4.56 |
| `UGameplayStatics::GetPlayerCharacter` | `0x440A0E0` | `48 89 5C 24 08 48 89 74 24 20 57 48 83 EC 20 33` | 3.88 |

熵从磁盘的 7.99 降到内存的 **3.88–4.56**（正常代码范围），序言全部正常。

**顺带把函数语义读出来了**：`K2_GetActorLocation` 开头是 `48 8B 42 20` = `mov rax, [rdx+0x20]`，说明它走 `Actor → 某字段(+0x20) → RootComponent`，与 `AActor::RootComponent = 0x168` 的读取链一致。

### 2.2 GObjects 是明文

```
slot=0x1472BE4B0  Objects=0x94F9A4E0  NumElements=35825  NumChunks=1
```
- 直接可遍历，`FUObjectItem` stride `0x18`，**`DecryptPtr` 是恒等函数（指针未加密）**
- 抽样的对象 vtable / class / nameIdx 全部是合法指针与索引

### 2.3 FName 解析可用

调用 `AppendString`（RVA `0x0288FBB0`）解析索引 0：

```
index 0 -> "None"     （预期值，完全正确）
```
→ 类名/对象名能解出真字符串，不是加密的。

### 2.4 坐标是明文（关键证据）

`GWorld` 在进入地图后非空，走 `UWorld → PersistentLevel → Actors[]` 拿到 **1,097 个 actor**，读 `RootComponent(+0x168) + RelativeLocation(+0x198)` 得到**真实世界坐标**：

```
[  0] 'PMWorldSettings'                    raw=(0.00, 0.00, 0.00)
[  1] 'SkyLight2'                          raw=(-104.95, 1210.45, 831.66)
[  2] 'DirectionalLightStationary1_0'      raw=(56848.91, 59648.73, 256.05)
[  3] 'LobbyCamera'                        raw=(-380.00, 7460.00, 90.00)
[  4] 'Cube_4'                             raw=(520.00, 7460.00, 80.00)
[  5] 'CineCameraActor'                    raw=(960.00, -0.50, 86.00)
[  6] 'TextRenderActor_1'                  raw=(1459.81, -5432.70, 21.43)
[  8] 'EquipRoom_Particle_9'               raw=(-157.81, -84.99, 60.27)
[  9] 'EquipRoom_Particle_3'               raw=(-191.29, -1048.87, 212.89)
[ 12] 'BP_Sky_Sphere_new_2'                raw=(0.00, 0.00, 11613.49)
[ 13] 'Mesh_symbol_23_Ef1'                 raw=(-1267.76, 368.83, 211.67)
...
```

这些是**有物理意义的浮点数**（相机高度 90、天空球 z=11613、光照 56848），完全不是随机值。

---

## 3. 结论对照表

| 对象 | 磁盘 | 内存 | 可用性 |
|---|---|---|---|
| `.text` 代码字节 | **加密**（熵 7.9997，序言 0） | **明文**（熵 ~4.5，序言正常） | ✅ 可直接调用 |
| 函数 RVA | 有效（dump 出来的） | 明文代码 | ✅ 可直接 call |
| `GObjects` | — | **明文** | ✅ 可直接遍历 |
| `FUObjectItem` 指针 | — | **未加密**（恒等） | ✅ |
| `FName` / 类名 | — | **明文**，`AppendString` 可用 | ✅ 可解真名 |
| Actor 坐标 | — | **明文** FVector | ✅ 可直接读 |
| HP / 队伍 / 皮肤 | — | 明文字段（偏移已 dump） | ✅ |

**所以：**
- **磁盘上的静态分析（IDA/Ghidra 直接开 EXE）会看到一堆垃圾字节** —— 这就是旧工程 `docs/19` 里「AOB 签名全 miss、静态扫描 0 命中」的真正原因：**不是符号被藏了，是代码在磁盘上就是密文**。
- **内存里一切明文** —— 只要在进程内（或 DMA 硬件外读），函数能调、坐标能读、对象图能走。

---

## 4. 为什么旧工程会卡死（本次给出根因）

旧工程 `D:\xingyun` 的 `docs/99-postmortem` 判定「UE4 全局藏在 VMProtect `.tvm0`，传统 AOB + 内存反扫完全不适用」。本次实测把这句话拆成两半：

1. **磁盘静态扫描失败** —— 正确，因为 `.text` 加密，AOB 当然全 miss。
2. **内存反扫失败** —— 这一半**不成立**。内存里 `GObjects`/`GWorld`/坐标全是明文，本次一次就读到了。旧工程之所以读不到，是因为它在**外部**用 `ReadProcessMemory`（被 ACE 挡，`err=5`）或依赖**会话级堆锚点**（每次启动漂移），而不是在进程内直接读**模块 RVA**。

**关键差别**：`GWorld` / `GObjects` 是**模块内 RVA 偏移**（`base + 0x7420710`），不是堆地址。只要在进程内读模块基址 + RVA，就**永远稳定**，不受会话堆布局影响。旧工程一直在找「堆上的锚点」，方向错了。

---

## 5. 仍然存在的真实限制

| 限制 | 说明 |
|---|---|
| 外部读内存 | ACE 剥权，`OpenProcess(0x10)` → `err=5`，`EnumProcessModulesEx` → `err=5`。**必须进程内或 DMA** |
| 磁盘静态分析 | `.text` 加密，静态反汇编无效；只能靠**内存 dump** 后分析 |
| `GWorld` 时机 | 启动期/大厅早期为 `0`，进地图后才有值（本次 t=140s / t=275s 两次出现） |
| 大厅无角色 | 本次抓取时玩家在**大厅**（`LobbyCamera` / `EquipRoom`），没有 `ACyCharacterBase` 活体实例；**要进训练场/对局才有角色坐标** |
| `ProcessEvent` | 需要正确 `ProcessEventIdx=0x45` 与合法 UObject 上下文 |

---

## 6. 可直接用的产物

| 文件 | 说明 |
|---|---|
| `tools\pe_scan.py` | PE 节表 + 熵扫描（判断加壳/加密） |
| `tools\prologue_scan.py` | 函数序言计数（定量判断代码是否加密） |
| `tools\rva_probe.py` | 指定 RVA 的字节 + 局部熵 + 序言提示 |
| `dumper\verify\verify_probe.c` | 进程内验证器：函数体/ GObjects / FName / GWorld / 坐标 全链路实测 |
| `dumper\verify\x64\Release\zlibwapi.dll` | 编译好的验证代理（28,672 B，SHA `8625BFAB3CDF02FD`） |

---

## 7. 下一步

**当前游戏在大厅，看不到角色坐标。** 要拿到人物坐标，需要：

1. 进**训练场**或自定义房（有 bot）
2. 探针已在进程内，会自动重试并打印每个 `ACyCharacterBase` 实例的：
   - `LOC=(x, y, z)` —— 世界坐标
   - `team` / `skin` / `role` —— 队伍 / 皮肤 / 角色 ID
   - `hp / hpmax` —— 血量
3. 报告写在 `PM\Binaries\Win64\fut_verify_report.txt`

---

## 修订历史

| 日期 | 作者 | 备注 |
|---|---|---|
| 2026-09-30 | 冷咖啡 | 初版：磁盘加密 + 内存明文的双面实测，含 12 个坐标函数的序言/熵证据与 1097 个 actor 的真实坐标 |
