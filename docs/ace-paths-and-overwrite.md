# ACE 安装路径与"被顶掉"风险排查（2026-10-01）

> 你的问题：**ACE 路径在哪？先前下载其他游戏的 ACE 会不会把这个游戏的弱 ACE 顶掉？**
> 答案：**不会"顶掉"，但确实存在系统级共用与版本覆盖 —— 而且我在你机器上找到了实证。**

---

## 一、ACE 有两个层面，这是理解一切的关键

ACE 不是"一个游戏一份"，而是**系统级安装 + 游戏自带副本**的双层结构：

| 层 | 路径 | 作用 |
|---|---|---|
| **系统级（共用）** | `C:\Program Files\AntiCheatExpert\` | 服务、引导驱动、共享组件 |
| **系统驱动目录** | `C:\Windows\System32\drivers\ACE-*.sys` | 内核驱动（按需加载） |
| **程序数据** | `C:\ProgramData\AntiCheatExpert\` | 运行时数据、崩溃转储 |
| **游戏自带副本** | `<游戏>\...\AntiCheatExpert\` | 该游戏**专属**的组件（DLL/驱动） |

**系统级是共用的**（所以有覆盖风险），**游戏自带副本是隔离的**（所以核心逻辑不会被顶掉）。

---

## 二、你机器上的实测情况

### 2.1 系统级 ACE（共用区）

```
C:\Program Files\AntiCheatExpert\
  2026-09-30 23:27   4,029 KB  ACE-BASE.sys          ← 与卡拉彼丘版本哈希相同
  2026-09-28 11:33   1,969 KB  ACE-BOOT.sys          ← Start=1 开机启动！最高权限
  2026-09-30 16:34   3,826 KB  ACE-CORE106010071.sys
  2026-09-29 21:09   3,788 KB  ACE-CORE106095076.sys
  2026-09-30 23:27   3,196 KB  ACE-CORE206010014.sys
  2026-09-30 16:34   3,065 KB  ACE-CORE206010071.sys
  2026-09-29 21:09   3,166 KB  ACE-CORE206095076.sys
  2026-09-28 11:33   4,274 KB  ACE-CORE302706.sys
  2026-09-28 15:21   4,274 KB  ACE-CORE30542.sys
  2026-09-15 05:20   3,175 KB  ACE-Service64.exe      ← 与卡拉彼丘版本哈希相同
  2026-09-28 11:33   7,496 KB  ACE-Tray.exe

C:\Windows\System32\drivers\
  2026-09-30 23:27   4,029 KB  ACE-BASE.sys
  2026-08-15 13:22   4,131 KB  ACE-BASE.sys.disabled   ← ★ 见下
  2026-09-30 23:27   1,141 KB  ACE-ADVT.sys
  2026-09-28 15:21   1,946 KB  ACE-GAME.sys

C:\ProgramData\AntiCheatExpert\
  2026-09-30 23:28  13,697 KB  ace-drc.dat
  2026-09-30 23:40      12 KB  sldh.dat
```

### 2.2 ★ 决定性证据：`ACE-BASE.sys.disabled` 就是终结地的版本

```
C:\Windows\System32\drivers\ACE-BASE.sys.disabled
    4,230,360 B   2026-08-15 13:22   sha256=3D9BCE1EE10462BAFB36CC742C0E7C4C

D:\Arknights Endfield\AntiCheatExpert\ACE-BASE.sys
    4,230,360 B   2025-11-20 11:38   sha256=3D9BCE1EE10462BAFB36CC742C0E7C4C
                                        ↑ 完全相同
```

**这就是被顶掉的证据。** 终结地的 `ACE-BASE.sys`（4,131 KB，2025-11-20）曾经装进系统驱动目录，后来被重命名为 `.disabled`，替换成了卡拉彼丘的版本（4,029 KB，2026-09-15）。

**说明**：**后启动的游戏会把自己的 ACE-BASE.sys 写进系统驱动目录**，覆盖前一个。你机器上最后写入的是**卡拉彼丘的版本**（系统里那份哈希与游戏目录那份**完全相同**）：

```
系统      ACE-BASE.sys  sha256=5A88DBE617BC8F1A8B87D9B9F1221CAF
卡拉彼丘  ACE-BASE.sys  sha256=5A88DBE617BC8F1A8B87D9B9F1221CAF   ← 相同
```

---

## 三、回答你的问题：会不会被"顶掉"？

### 3.1 核心结论：**ACE-Base64.dll 不会被顶掉**

这是最重要的好消息。`ACE-Base64.dll` 是**进程内注入**的（游戏启动时从自己的目录加载），**每个游戏加载自己那份**：

| 游戏 | ACE-Base64.dll | 大小 | 日期 |
|---|---|---|---|
| 终结地 | `D:\Arknights Endfield\AntiCheatExpert\` | 61,877,648 B | 2026-08-20 |
| **卡拉彼丘** | `D:\CLBQ\...\AntiCheatExpert\` | **70,453,136 B** | **2026-09-15** |

**两者完全不同**（差 8.6 MB）。因为它是**游戏目录内的 DLL**，不走系统路径，所以**不会被另一个游戏覆盖**。

**→ 我们分析的 `ACE-Base64.dll`（70,453,136 B / `18190E2C…`）就是你游戏实际加载的那份，分析有效。**

### 3.2 会被共用/覆盖的是**驱动层**

```
ACE-BASE.sys     ← 系统共用，已被卡拉彼丘版本覆盖终结地版本
ACE-CORE*.sys    ← 版本化命名，可共存（你机器上有 9 个不同版本）
ACE-BOOT.sys     ← Start=1 开机启动，系统级共用
ACE-ADVT.sys     ← 系统级
ACE-GAME.sys     ← 系统级
```

**注意 `ACE-CORE` 是版本化命名**（`ACE-CORE106010071` / `ACE-CORE206095076` …），设计上就允许**多版本共存**，这是 ACE 刻意的隔离机制——不同游戏用不同版本的核心驱动。

### 3.3 所以"弱 ACE 被强 ACE 顶掉"这个担心

**部分成立，但方向可能相反**：

- 系统**驱动**层：后安装/后启动的游戏版本会覆盖 → **可能被升级**
- 进程内 **DLL** 层：完全隔离 → **不会被顶掉**

而且从证据看，**你机器上现在的系统驱动就是卡拉彼丘的版本**（哈希一致），所以**当前状态是"卡拉彼丘的 ACE 在系统层生效"**，没有被终结地顶掉。

---

## 四、对我们工作的实际影响

| 项 | 影响 | 结论 |
|---|---|---|
| 我们分析的 `ACE-Base64.dll` | 游戏目录内，隔离 | ✅ **分析有效，未被顶掉** |
| 我们 hook 的模块（`webio`/`winhttp`/`ws2_32`） | 系统 DLL，与 ACE 版本无关 | ✅ 不受影响 |
| ACE 驱动能力 | 系统共用，当前=卡拉彼丘版本 | ✅ 与我们的分析一致 |
| `ACE-CORE*` 多版本 | 版本化共存 | ⚠️ 分析某个版本时需确认游戏实际加载哪个 |

### 需要留意的一点

我们 hook 的是**用户态网络层**（`ws2_32`/`webio`/`winhttp`），**不碰 ACE 自己的模块**。所以：
- ACE 版本被顶掉/升级 → **对我们的拦截逻辑没有影响**
- 只要 ACE 还是通过**系统解析器**上报（已实证），黑洞就有效

**唯一会受影响的情况**：如果 ACE 升级后改用 DoH 或硬编码 IP。目前实测**都不是**（DoH 未配置、无硬编码 IP）。

---

## 五、顺带发现的问题（不在你问题范围内，但值得知道）

```
HtAntiCheatDriver    Start=3
  D:\Neverness To Everness\NTELauncher\driver\gamedriverX64.sys
```

你机器上还有**另一个游戏的反作弊驱动**（Neverness To Everness / 无限大）。它和 ACE 是**不同厂商**，共存但互不干扰。

另外 `spaceport.sys` / `spaceparser.sys` / `filetrace.sys` / `pacer.sys` 这几个**不是 ACE 的** —— 前三个是 Windows 存储空间（Storage Spaces）组件，`pacer.sys` 是 QoS 数据包调度器。别被名字误导。

---

## 六、结论

| 你的问题 | 答案 |
|---|---|
| ACE 路径在哪 | **系统级** `C:\Program Files\AntiCheatExpert\` + `System32\drivers\` + `ProgramData\`；**游戏自带** `D:\CLBQ\...\AntiCheatExpert\` |
| 会被其他游戏的 ACE 顶掉吗 | **驱动层会被覆盖**（实证：终结地版本变成 `.disabled`）；**进程内 DLL 不会**（各游戏加载自己那份） |
| 我们的分析还有效吗 | ✅ **有效**。`ACE-Base64.dll` 在游戏目录内，70,453,136 B，未被顶掉 |
| 当前系统层是谁的 | **卡拉彼丘的**（系统 `ACE-BASE.sys` 哈希与游戏目录一致） |
| 还剩一个真风险 | **切换游戏会覆盖系统驱动** → 若你之后启动终结地，系统驱动会换回旧版。但只要不影响用户态网络层，黑洞依然有效 |

---

## 修订历史

| 日期 | 作者 | 备注 |
|---|---|---|
| 2026-10-01 | 冷咖啡 | ACE 双层结构梳理 + `.disabled` 实证覆盖 + 确认游戏内 DLL 未被顶掉 |
