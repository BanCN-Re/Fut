# D:\Work\Fut\docs\BUGFIX-ROUND2.md
# 玩家报告的 7 个 bug · 逐条根因与修法

来源：玩家实测反馈（2026-10-02 00:2x）
> 骨骼位置不对 / 骨骼数量不对 / 小地图有bug / 框会乱跳 /
> 有bot不显示框也就是甚至没被锁上 / 血量显示错误 / 人物死亡后框还会存留

---

## B1 骨骼位置不对

**根因**：`features::DrawSkeletonFor` 用**自定位扫描**（走 char+0x0..0x2000 找骨骼数组最长的组件）
选中了**网格组件**，再用 `ComponentToWorld` 把 component 空间变换到世界。
但骨架的 `CachedComponentSpaceTransforms` 在不同组件上的参考系不同 ——
武器挂件组件的 `ComponentToWorld` 与身体组件不同，导致整体偏移。

**修法**：不再扫描，改用**权威字段** `AWFBaseCharacter::MeshFor3PP (0x0EC0)`；
该字段在 dump 中明确属于角色本体网格。

---

## B2 骨骼数量不对

**根因**：扫描"骨骼数最多"的组件，选中的是 **246 根**（含武器/挂件的合并骨架），
而人体本体只有约 **85-112 根**。246 根里大量是武器骨骼，画出来关节错位。

**修法**：同上，改用 `MeshFor3PP`（本体），并在 `skeleton::Resolve` 里打印关节命中数以便核对。

---

## B3 小地图有 bug

**根因**：`features::DrawRadar` 用 **world 绝对坐标差**画点：
```cpp
const float dx = a.loc.X - gWorld.camLoc.X;
const float dy = a.loc.Y - gWorld.camLoc.Y;
const float sx = cx + dx * scale;
const float sy = cy - dy * scale;
```
`camLoc` 是**相机**位置（第三人称时在角色后上方），而朝向箭头用的是
**相机 Yaw**。两者不同源、雷达中心也不是本地角色位置，导致：
- 点相对箭头**偏移**
- 旋转雷达时点会**绕错误中心转**

**修法**：中心改用**本地角色**位置；朝向用本地角色的 `ControlRotation.Yaw`；
并对 `dx/dy` 做**极坐标旋转**（先转到角色朝向系，再画），而不是直接线性映射。

---

## B4 框会乱跳

**根因**：`s_boxes` 平滑（`kBoxHoldMs=250` + 0.55 lerp）与
`DrawOneInGame` 每帧重新投影**同时存在**，两套位置互相打架：
- 平滑后的框还在追旧位置
- 新投影已经跳到新位置
- 结果视觉上"乱跳"

**修法**：删掉屏幕空间平滑，改为**只对世界坐标做平滑**（跟在实体位置后），
屏幕框由平滑后的世界坐标投影得出 —— 单一数据源，不会打架。

---

## B5 bot 不显示框（甚至没被锁上）

**根因**：`ScanCharacters::consider` 里：
```cpp
if (!HasController(a) && !HasPlayerState(a)) { ++s_filter.noController; return; }
```
bot 的 `AIController` 与 `PlayerState` 在出生后**有延迟**才赋值，
该帧两者都为空 → 被永久跳过。而 `s_addrCache` 250ms 才刷新一次，
一旦漏掉，要等下一轮才可能补上，体感就是"bot 不出框"。

**修法**：
1. 放宽为**三者任一**即可（`Controller` / `PlayerState` / **`IsAlive` 可调用**）
2. 新增 `[filter]` 诊断日志，每 5s 打印各类过滤计数，
   让"到底被哪一条过滤掉"可见

---

## B6 血量显示错误

**根因**：`RefreshSlow` 用**双路径猜测**：
```
A) AbilitySystem(0x880) + ATTR_Health(0x78) + CurrentValue(0x0C)
B) pawn 直读 0x46C / 0x470
```
B 路径的 `0x46C/0x470` 是**从别的项目猜来的偏移**，本项目 dump 里
`ACyCharacterBase::BaseHealth=0x69C` / `BaseHealthMax=0x698` 才是真实字段。
猜的偏移读到的是无关浮点，所以血量数字荒谬。

**修法**：删掉猜测路径，改为：
1. 主路径：`AbilitySystem(0x880)` → `UCyBaseAttributeSet::Health(0x78)`（**已核对 dump**）
2. 兜底：`ACyCharacterBase::BaseHealth(0x69C)` / `BaseHealthMax(0x698)`（**已核对 dump**）
3. 装甲：`Armor(0x98)` / `ArmorMax(0xA8)` 一并读，血条按
   `(Health+Armor)/(HealthMax+ArmorMax)` 显示，与游戏 HUD 一致

---

## B7 人物死亡后框还会存留

**根因**：致死判定写成：
```cpp
if (e.hpMax > 0.0f && e.hp <= 0.0f) { ++s_filter.dead; return false; }
```
`hpMax > 0` 是**前置条件**。当血量读取失败（`hpMax == 0`，正是 B6 的后果），
条件整体为假 → **尸体不清除**，框一直留着。

**修法**：改用权威存活判定 `ACyCharacterBase::IsAlive`（RVA `0x2378630`）：
```cpp
bool alive = IsAlive(actor);          // 引擎自己的判定
if (!alive) { ++s_filter.dead; return false; }
```
这不再依赖任何血量读数，读失败也不会误判为"活着"。

---

## 修正后的字段对照（全部来自本项目 dump，非猜测）

| 字段 | 偏移 | 来源 |
|---|---|---|
| `ACyCharacterBase::BaseHealth` | `0x069C` | PMGame_classes.hpp:17388 |
| `ACyCharacterBase::BaseHealthMax` | `0x0698` | 同上 |
| `ACyCharacterBase::LifeComponent` | `0x0870` | 同上 |
| `ACyCharacterBase::LifeStateComponent` | `0x0878` | 同上 |
| `ACyCharacterBase::AbilitySystem` | `0x0880` | 同上 |
| `ACyCharacterBase::AttributeTeamID` | `0x08B0` | 同上 |
| `ACyCharacterBase::IsAlive` | RVA `0x2378630` | FunctionsInfo.json |
| `AWFBaseCharacter::MeshFor3PP` | `0x0EC0` | PMGame_classes.hpp |
| `UCyBaseAttributeSet::Health` | `0x0078` | PMGame_classes.hpp:7185 |
| `UCyBaseAttributeSet::HealthMax` | `0x0088` | 同上 |
| `UCyBaseAttributeSet::Armor` | `0x0098` | 同上 |
| `UCyBaseAttributeSet::ArmorMax` | `0x00A8` | 同上 |
| `FGameplayAttributeData::CurrentValue` | `+0x0C` | CoreUObject |
