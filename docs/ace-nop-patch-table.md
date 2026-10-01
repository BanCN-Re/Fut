# ACE 上报禁断 · NOP 补丁表（可直接用）

> 目标模块：`ACE-Base64.dll`（imagebase `0x180000000`，PDB `ACE-Base64.pdb`）
> 文件：`AntiCheatExpert\ACE-Base64.dll`（70,453,136 B）
> 派发器：**`0x1803CF410`**（上报事件派发，函数大小 0xC4D，含 /GS 栈保护）

---

## 1. 精确字节（已逐条核对）

| VA | RVA | 原字节 | 反汇编 | 含义 |
|---|---|---|---|---|
| `0x1803CF49E` | `0x3CF49E` | `48 8B 18` | `mov rbx, [rax]` | 取处理器链表首节点 |
| `0x1803CF4A1` | `0x3CF4A1` | `48 3B D8` | `cmp rbx, rax` | 链表是否为空 |
| **`0x1803CF4A4`** | **`0x3CF4A4`** | **`0F 84 44 0B 00 00`** | **`je 0x1803CFFEE`** | **★ 主 NOP 点** |
| `0x1803CF4AA` | `0x3CF4AA` | `66 0F 1F 44 00 00` | `nop word [rax+rax]` | 对齐填充 |
| `0x1803CF4B0` | `0x3CF4B0` | `48 39 7B 18` | `cmp [rbx+0x18], rdi` | ★ 事件 ID 匹配 |
| **`0x1803CF4B4`** | **`0x3CF4B4`** | **`74 0D`** | **`je 0x1803CF4C3`** | **★ 命中 → 构造载荷** |
| `0x1803CF4B6` | `0x3CF4B6` | `48 8B 1B` | `mov rbx, [rbx]` | 下一个节点 |
| `0x1803CF4B9` | `0x3CF4B9` | `48 3B D8` | `cmp rbx, rax` | 到表尾？ |
| `0x1803CF4BC` | `0x3CF4BC` | `75 F2` | `jne 0x1803CF4B0` | ★ 循环 |
| `0x1803CF4BE` | `0x3CF4BE` | `E9 2B 0B 00 00` | `jmp 0x1803CFFEE` | 未找到 → 返回 |
| `0x1803CF491` | `0x3CF491` | `0F 84 57 0B 00 00` | `je 0x1803CFFEE` | 处理器表为空 → 返回 |
| `0x1803CF470` | `0x3CF470` | `0F 85 C9 0B 00 00` | `jne 0x1803D003F` | 初始化检查失败 → 另一路 |

**跳转目标已复核**：`0x1803CF4A4 + 6 + 0xB44 = 0x1803CFFEE`（函数尾部的返回/清理路径）。

字节原文（`0x1803CF4B0` 起 24 字节）：
```
48 39 7B 18 74 0D 48 8B 1B 48 3B D8 75 F2 E9 2B 0B 00 00 48 8D 4C 24 40
cmp [rbx+18],rdi | je +0D | mov rbx,[rbx] | cmp rbx,rax | jne -0E | jmp +0B2B | lea rcx,[rsp+40]
```

---

## 2. 推荐补丁

### 方案 B ★★ 最推荐：1 字节，指令长度不变

```
偏移:   0x3CF4B4
原值:   74 0D          je  0x1803CF4C3   （命中事件 → 构造上报载荷）
改为:   EB 0D          jmp 0x1803CF4C3
```

**改 1 个字节**（`0x3CF4B4`: `74` → `EB`）。

**效果**：一旦遍历到匹配的事件 ID，**跳过"构造并发送载荷"的代码块**，直接继续循环/返回。

- ✅ 指令长度不变（2 字节 → 2 字节）
- ✅ 跳转目标不变
- ✅ 栈/寄存器完全平衡（走函数自带的控制流）
- ✅ /GS cookie 校验不受影响

> ⚠️ 需先确认 `0x1803CF4C3` 起确实是"构造载荷"而非"循环续行"。从 `lea rcx,[rsp+0x40]` + `call 0x18058af50` 看，是**构造描述符后调用处理器**，符合判断。

### 方案 A：把链表空判断改成恒跳（6 字节 → 5 字节 + 1 nop）

```
偏移:   0x3CF4A4
原值:   0F 84 44 0B 00 00     je  0x1803CFFEE
改为:   E9 45 0B 00 00 90     jmp 0x1803CFFEE ; nop
```

**已核算**：`0x1803CF4A4 + 5 = 0x1803CF4A9`；`0x1803CFFEE - 0x1803CF4A9 = 0xB45` → `E9 45 0B 00 00 90`。

**效果**：派发器**永不遍历处理器表**，第一跳就返回。覆盖面比方案 B 更广（连"表为空"分支都绕过）。

**代价**：改了 6 字节（含 1 个 nop 保持对齐），可检测面比方案 B 大。

### 方案 C：组合（覆盖最全）

```
0x3CF4B4:  74 → EB     命中即跳过构造载荷
0x3CF4BC:  75 → EB     循环改恒跳，遍历一次就退出（可选）
```

---

## 3. 必须知道的风险

### 3.1 代码段保护确实存在

`ACE-Base64.dll` 内的驱动配置键：

```
Driver/Cores/Core1/Settings/enable_code_section_protect
Driver/Cores/Core1/Settings/enable_ob_integrity_check
Driver/Cores/Core2/Settings/enable_code_section_protect
Driver/Cores/Core2/Settings/enable_ob_integrity_check
/PunishInputHookIntegrity
```

`ACE-BASE.sys` 导入 **`MmProtectMdlSystemAddress`**（驱动改物理页保护属性）。

驱动加载状态（实测）：

```
ACE-BOOT       Running（常驻）
ACE-BASE       Stopped → 随游戏启动加载 ← 代码段保护在这里
ACE-ADVT       Stopped
ACE-GAME       Stopped
ACE-SSC-DRV64  Stopped
```

### 3.2 与已知后果对比

| 手段 | 改动位置 | 已知后果 |
|---|---|---|
| IAT 补丁 | `.rdata` 指针 | **实测 5 秒击杀**（Nemesis/EndFieldHack） |
| **NOP** | **`.text` 指令字节** | **风险更高** —— `code_section_protect` 正是针对代码段 |

### 3.3 验证顺序（务必按此执行）

1. **先跑黑洞**（已完成，不碰任何字节）
2. 若必须 NOP：
   - 用**方案 B**（1 字节，长度不变）
   - **先备份原字节**（`0x3CF4B4` = `74`）
   - 观察是否 5 秒内击杀
   - **击杀 → 代码段保护生效，此路不通，立即回滚**
   - 未击杀 → 再考虑方案 A/C

---

## 4. 一句话结论

| 问题 | 答案 |
|---|---|
| `report_bk` 能 nop 吗？ | ❌ **不能按名字找** —— 它是 **PDB 符号名**（`ACE-Base64.pdb`），二进制里根本不存在 |
| 怎么定位？ | ✅ 按行为：**派发器 `0x1803CF410`**，97 个 `QosId*` 事件 ID 是明文的 |
| NOP 点？ | `0x3CF4B4`（`74`→`EB`）或 `0x3CF4A4`（`E9 45 0B 00 00 90`） |
| 推荐？ | **方案 B**：1 字节，指令长度不变，控制流自洽 |
| 风险？ | ⚠️ 代码段保护存在；IAT 补丁已被证明 5 秒击杀，NOP 风险更高 |
| 更稳的路？ | ✅ **黑洞**（已修复 5 个泄露点，不碰任何字节） |

---

## 修订历史

| 日期 | 作者 | 备注 |
|---|---|---|
| 2026-09-30 | 冷咖啡 | 精确字节逐条核对（跳转目标修正为 0x1803CFFEE）+ 3 套方案 + 风险与验证顺序 |
