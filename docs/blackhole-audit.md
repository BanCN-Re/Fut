# 黑洞完整性审计（2026-09-30）

> 目标：验证"黑洞网截是否完整、会不会泄露"
> 结论：**不完整。找到 4 类泄露路径，其中 1 类是致命的（异步解析完全绕过）。**

---

## 0. 结论先行

| # | 泄露点 | 严重度 | 现状 |
|---|---|---|---|
| **L1** | **异步 `GetAddrInfoExW` 完全绕过** | 🔴 **致命** | 现有判断 `overlapped==nullptr && completion==nullptr` 会放行 |
| **L2** | `WinHttpConnect` 可传 **IP 字面量** | 🔴 高 | 黑洞只看域名，不看 IP |
| **L3** | `getnameinfo`（反向解析）未拦 | 🟡 中 | 不影响上报，但可用于旁路探测 |
| **L4** | 动态解析的 `WSALookupService*` 未覆盖 | 🟡 中 | ACE 字符串里有，未见调用点 |
| **L5** | `SGuard64.exe` 独立进程 | 🔴 高 | 进程内黑洞**原理上**覆盖不到 |

**L1 是必须马上修的**——它不是理论风险，是实测存在的调用路径。

---

## 1. L1（致命）：异步 GetAddrInfoExW 绕过

### 证据链

**① ACE 动态解析了异步专用 API**

`0x180355723` 处 `lea rcx, [rip+0x1552cb6]` → `0x1818A83E0`，该地址是 UTF-16 字符串：

```
0x1818A83E0  "ws2_32.dll"
0x1818A83F8  "GetAddrInfoExOverlappedResult"     ← 异步专用
0x1818A8418  "GetAddrInfoExCancel"               ← 异步专用
0x1818A8448  "GetAddrInfoExW"
```

`GetAddrInfoExOverlappedResult` 和 `GetAddrInfoExCancel` **只对异步（overlapped）调用有意义**。它们出现在同一张解析表里，说明 ACE **确实在用异步路径**。

**② 调用点确实传了非 NULL 的 overlapped**

`0x1803554F0`（GetAddrInfoExW 调用者）的实际参数准备：

```asm
0x1803555CA  cmp  qword ptr [rbp-8], 7
0x1803555CF  cmova rcx, qword ptr [rbp-0x20]    ; node（域名指针）
0x1803555D4  mov  qword ptr [rsp+0x48], rdi     ; completion = NULL
0x1803555D9  mov  qword ptr [rsp+0x40], rdi     ; (arg8)
0x1803555DE  mov  qword ptr [rsp+0x38], rdi     ; (arg7)
0x1803555E3  mov  qword ptr [rsp+0x30], rdi     ; (arg6)
0x1803555E8  lea  rax, [rsp+0x50]
0x1803555ED  mov  qword ptr [rsp+0x28], rax     ; (arg5)
0x1803555F2  lea  rax, [rsp+0x70]
0x1803555F7  mov  qword ptr [rsp+0x20], rax     ; ★ overlapped = &stack（非 NULL！）
0x1803555FC  xor  r9d, r9d                      ; nspId = NULL
0x1803555FF  xor  edx, edx                      ; service = NULL
0x180355601  mov  r8d, 0xc                      ; ns = NS_ALL(12)
0x180355607  call qword ptr [rip+0x154bf6b]     ; GetAddrInfoExW
```

win64 调用约定下 `GetAddrInfoExW` 的第 8 个参数 `lpOverlapped` 在 `[rsp+0x20]`：
**它被设成了 `&[rsp+0x70]`，非 NULL。**

**③ 而黑洞只拦同步**

我照抄 Nemesis 的判断：

```cpp
if (node && IsBlockedHostT(node) && overlapped == nullptr && completion == nullptr)
```

`overlapped != nullptr` → **条件不成立 → 直接转发 → 域名照常解析 → 上报照常发出。**

### 影响

ACE 的 `cschannel*` 上报**只要走 `0x1803554F0` 这条异步路径，黑洞就是完全无效的**。而这条路径恰恰是 ACE 精心实现的那条（配了 OverlappedResult + Cancel 的完整异步生命周期）。

### 修法

判断里**去掉 overlapped/completion 的限制**，改成按域名无条件拦：

```cpp
if (node && IsBlockedHostT(node)) {
    // 同步与异步都拦。异步调用返回 EAI_NONAME 并由调用方走失败分支，
    // 这正是我们想要的（上报发不出去）。
    LogBlock("GetAddrInfoExW", narrow);
    if (nameHandle) *nameHandle = nullptr;
    WSASetLastError(WSAHOST_NOT_FOUND);
    return EAI_NONAME;
}
```

> 注意：不能只 return，还要处理 `lpNameHandle`（异步调用会用它取消/查询）。置 NULL 并返回错误码是标准失败路径。

---

## 2. L2（高）：WinHttpConnect 接受 IP 字面量

WinHTTP 允许 `WinHttpConnect(hSession, L"1.2.3.4", port, 0)` 直接用 IP，**完全不过 DNS**。

虽然静态扫描显示 ACE 里**只有 1 个 IPv4 字面量**（在 `.rsrc`，是公司信息），但这不代表运行时不会拿到 IP：
- 服务端可以在前一次响应里下发 IP
- 配置/缓存文件里可能有

**修法**：`WinHttpConnect` / `InternetConnectW` 也要拦，判断第 2 参是否为 IP 字面量或黑名单域名。

---

## 3. L3/L4（中）

| 项 | 说明 | 修法 |
|---|---|---|
| `getnameinfo` | 反向解析（IP→域名）。**不影响上报**，但可被用来探测网络环境 | 低优先，可加 |
| `WSALookupServiceBegin/Next` | ACE 字符串里有（`0x181927790` 附近），静态未见调用点。这是**另一条解析路径**（NS 查询） | 中优先，加上更稳 |

---

## 4. L5（高，原理性）：SGuard64.exe

实测连接：

```
PID 32692 SGuard64  → 198.18.0.162:10012  Established
```

`SGuard64.exe` 是**独立进程**，也导入 `getaddrinfo`，**也连到同一个上报端点**。

我的 DLL 在**游戏进程内**，`SGuard64.exe` 的 IAT **碰不到**。这是进程内黑洞的**原理性盲区**，不是实现问题。

**可选方向**（都超出"进程内黑洞"范畴）：
- 系统级 DNS（改 hosts / 自建 DNS 服务器）→ 触及系统，风险与副作用大
- 对 `SGuard64.exe` 单独做一次代理/注入 → 它是 ACE 服务进程，改动它 = 直接撞完整性检查
- **接受这个盲区**，只保证游戏进程内不泄露

---

## 5. 修复后的完整覆盖矩阵

| 解析路径 | 现状 | 修复后 |
|---|---|---|
| `getaddrinfo`（同步） | ✅ 已拦 | ✅ |
| `GetAddrInfoW`（同步） | ✅ 已拦 | ✅ |
| `gethostbyname` | ✅ 已拦 | ✅ |
| **`GetAddrInfoExW`（异步）** | ❌ **放行** | ✅ **改为无条件拦** |
| `WSAAsyncGetHostByName` | ✅ 已拦 | ✅ |
| **`WinHttpConnect`（IP 直连）** | ❌ 未拦 | ✅ 加 IP 检测 |
| `getnameinfo` | ❌ 未拦 | ✅ 加（低风险） |
| `WSALookupService*` | ❌ 未拦 | ✅ 加 |
| `dnsapi!DnsQuery_*` | ✅ 间接覆盖 | ✅ |
| `SGuard64.exe` | ❌ 盲区 | ⚠️ 仍盲区（原理性） |

**补丁目标模块**（修复后）：

```cpp
kWhitelistExact[] = { "winhttp.dll", "wininet.dll", "ws2_32.dll", "dnsapi.dll" };
```

为什么补 `winhttp.dll` 的 IAT 有效：`winhttp.dll` **自己导入** `GetAddrInfoW`/`GetAddrInfoExW`（已用 dumpbin 确认）。所以即使 ACE 用 `GetProcAddress` 动态拿 WinHTTP（实测确实如此，`WinHttpConnect` 在 ACE 里零 xref），补 `winhttp.dll` 依然拦得到解析。

---

## 6. 行动项

| # | 事项 | 优先级 |
|---|---|---|
| 1 | **修异步 GetAddrInfoExW 绕过**（去掉 overlapped 限制） | 🔴 P0 |
| 2 | 加 `WinHttpConnect`/`InternetConnectW` + IP 字面量检测 | 🔴 P0 |
| 3 | 加 `getnameinfo` | 🟡 P1 |
| 4 | 加 `WSALookupServiceBeginW/NextW` | 🟡 P1 |
| 5 | 补丁目标确认含 `winhttp.dll`/`wininet.dll`/`ws2_32.dll`/`dnsapi.dll` | ✅ 已写 |
| 6 | 部署后**动态验证**：日志出现 `blocked` 才算真拦到 | 🔴 P0 |

---

## 修订历史

| 日期 | 作者 | 备注 |
|---|---|---|
| 2026-09-30 | 冷咖啡 | 黑洞完整性审计：发现异步绕过等 5 类泄露，给出覆盖矩阵与修法 |
