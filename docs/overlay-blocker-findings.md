# 调研结论：为什么 INSERT 打不开，以及为什么叠加层没出现

## 实测证据（不是猜测）

### 1. Present hook 确实装上了，但游戏不调用它

```
[d3d] legacy vt[8]  00007FFEB084DAD0  <- dxgi.dll
[d3d] legacy vt[22] 00007FFEB084DF70  <- dxgi.dll
[d3d] flip   vt[8]  00007FFEB084DAD0  <- dxgi.dll
[d3d] flip   vt[22] 00007FFEB084DF70  <- dxgi.dll
[d3d] hooks enabled (2 Present/Present1 hooks across both vtables)
[d3d] frame 1  menu=closed  obj=7006  tgt=0  path=none  legacy=1 flip=0
```

- 探针用**传统（DISCARD）**和**翻转模型（FLIP_DISCARD）**两种 dummy 交换链取样，
  **两种 vtable 的 slot 8 / slot 22 完全相同**，都在 `dxgi.dll` 里
- 两个槽位都 hook 成功
- 结果：`legacy=1 flip=0` —— 只被调用**一次**，之后再无调用

### 2. 游戏确实在渲染

```
CPU=497.5s → 503s（6 秒内 +5.5s，多核满载）
GPU pid_38544 engtype_3d = 39.8%
```

游戏窗口 `UnrealWindow` 2048×1280 可见、未最小化、已置前台。

### 3. 图形 API 探针给出决定性答案

```
[gfx]  d3d11.dll          base=0x7FFEB05D0000
[gfx]  d3d12.dll          base=0x7FFE40050000     ← 已加载
[gfx]  d3dcompiler_43.dll base=0x7FFE12550000
[gfx]  dxgi.dll           base=0x7FFEB0840000
[gfx] SUMMARY d3d11=LOADED d3d12=LOADED vulkan=absent
```

**`d3d12.dll` 已加载** —— 这是我前面所有推断里漏掉的关键事实。

## 根因

游戏走 **D3D12** 呈现。D3D12 的交换链是 **`IDXGISwapChain3/4`**，由 DXGI 的
**另一套实现类**支撑，它的 `Present`/`Present1` 是**不同的函数地址**，不在
`IDXGISwapChain`(v1) 的 vtable 上。

- 我 hook 的是 `IDXGISwapChain` v1 的 slot 8/22
- 游戏调用的是 D3D12 交换链对象的 Present
- 那一次 `legacy=1` 是 UE4 启动期做能力探测时对 v1 路径的一次调用（或 DXGI 内部转发）
- 之后游戏全部走 D3D12 路径，我的 hook 永远不再被触发

这也解释了为什么**旧工程 `D:\xingyun` 的 D3D11 hook 全盘失败并崩游戏** ——
它连正确的呈现路径都没找到；`docs/19` 里"view/proj 走 D3D11 常量缓冲进 GPU"
的结论在本版本上不成立。

## 另外确认的两件事

1. **INSERT 键不是问题**。三条独立通道全部就绪：
   ```
   [input] menu key vk=0x2D -- starting 3 detection paths
   [input] RegisterHotKey OK vk=0x2D
   ```
   `RegisterHotKey` 在即时窗口注册成功（这条路径完全绕开游戏的输入处理）。
   菜单打不开的真正原因是**叠加层根本没渲染出来**，不是按键没收到。

2. **字体、ESP 数据层、黑洞网截都正常**：
   ```
   [font] loaded TTF resource: 3033180 bytes
   [esp] ACyCharacterBase UClass = 0x92DE2B80
   [DNS-BLACKHOLE] periodic done: modules_scanned=267 modules_patched=4 slots=4
   ```

## 两条可行路线

### 路线 A（推荐）：独立透明叠加窗口

不复用游戏交换链，自己建一个 D3D11 设备 + 透明置顶窗口，每帧渲染 ImGui。
这是旧工程 `esp_v4` 已经验证可用的做法（`overlay_window.cpp`，独立进程/独立窗口）。

优点：
- 完全不依赖游戏的图形 API（D3D11/D3D12/Vulkan 都能用）
- 不需要 hook 任何图形函数 → 检测面最小
- 菜单和 ESP 都能画

代价：需要处理窗口透明（`WS_EX_LAYERED` + 透明色键）和置顶同步。

### 路线 B：D3D12 交换链 hook

用 D3D12 创建 dummy device + `IDXGIFactory2::CreateSwapChainForHwnd`（D3D12 队列），
取 `IDXGISwapChain3` vtable 再 hook。技术上可行但：
- 需要 hook `IDXGISwapChain3::Present`（不同的槽位映射）
- 部分游戏用 `Present` 的 flip 路径 + `SetFullscreenState`，容易黑屏
- 风险高于路线 A

**建议走路线 A。**

## 参考

- Nemesis 的独立窗口做法：`D:\Work\or\Nemesis\reconstructed\src\hooks\graphics_hooks.cpp`（158 KB，含全后端）
- 旧工程验证过的独立叠加窗：`D:\xingyun\esp\v4\overlay_window.cpp`（532 行，自建 D3D11 device + 透明色键，明确注释"未接触游戏进程渲染"）
