// D:\Work\Fut\cheat\src\gui.cpp
// Menu, styled after ImGui-UI1001 (its colour palette + rounded dark theme).
//
// Deliberate change vs UI1001: UI1001 draws its toggles/sliders with custom
// InvisibleButton + manual draw calls. Those are NOT keyboard navigable, and in
// a game the mouse cursor is often captured/hidden, which would leave the menu
// unusable. So we use standard ImGui widgets (Checkbox / SliderFloat / Combo /
// CollapsingHeader / Button) styled to the same palette -- identical look, but
// they work with arrow keys + Enter as well as the mouse.
#include "gui.h"
#include "capture_guard.h"
#include "overlay.h"
#include "features.h"
#include "keybind.h"
#include "canvas.h"
#include "esp.h"
#include "aim.h"
#include "skill.h"
#include "ue.h"
#include "ace_blackhole.h"
#include "obf.h"
#include "input.h"
#include "log.h"

#include "imgui.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

namespace gui {

bool g_open = false;
int  g_menuKey = VK_INSERT;
bool g_debugStrip = true;
bool g_opaqueMenu = true;   // menu open -> solid black backdrop

static bool s_cursorFreed = false;
static bool s_cursorWasVisible = true;

void ToggleMenu() { g_open = !g_open; }
void SetMenuOpen(bool open) { g_open = open; }
bool WantsMouse() { return g_open; }

// ---------------------------------------------------------------------------
// cursor management: free the OS cursor while the menu is up, restore after.
// A game that captures the cursor would otherwise make the menu unclickable.
// ---------------------------------------------------------------------------
static void UpdateCursor() {
    if (g_open && !s_cursorFreed) {
        CURSORINFO ci{ sizeof(CURSORINFO) };
        s_cursorWasVisible = GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING);
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) { /* keep incrementing until visible */ }
        s_cursorFreed = true;
    } else if (!g_open && s_cursorFreed) {
        ClipCursor(nullptr);
        if (!s_cursorWasVisible) {
            while (ShowCursor(FALSE) >= 0) { /* keep decrementing until hidden */ }
        }
        s_cursorFreed = false;
    }
}

// ---------------------------------------------------------------------------
// menu
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Push the menu's master switches into the ported features module.
//
// Called once per frame from Render(). The ESP/Aim panels and the features
// module hold separate settings because they grew from different sources: the
// panels drive the original renderer, `features::g` drives the ported one. Rather
// than let them drift, the important switches are mirrored here so a single click
// affects both renderers.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// BIND BUTTON -- "click, then press the key you want"
//
// Behaviour:
//   * shows the current binding, or 无 when unbound
//   * click -> enters capture: the label changes to 请按键...
//   * the next key or mouse button pressed (including the X1/X2 side buttons,
//     which keybind routes through the WndProc because Windows gives them no VK
//     code) becomes the binding
//   * shows 绑定成功 for a moment, then returns to showing the key
//   * ESC cancels instead of binding, and a second click on the button also
//     cancels -- a capture you cannot escape is a trap
//
// The capture itself lives in keybind so it also works while the game window has
// focus: the WndProc detour feeds it, not ImGui.
// ---------------------------------------------------------------------------
static void BindButton(const char* label, int& vk) {
    ImGui::PushID(&vk);

    // A key may have arrived since the last frame.
    int got = keybind::kNone;
    if (keybind::ConsumeCaptured(got) && got != keybind::kNone) {
        vk = got;
        LOG(SBOX("[bind] '%s' bound to 0x%X", 0x1F1), label, vk);
    }

    const bool capturing = keybind::Capturing();

    // Button text: label on the left, current binding on the right.
    char text[192];
    if (capturing)
        _snprintf_s(text, sizeof(text), _TRUNCATE, u8"%s: 请按键...（ESC 取消）", label);
    else
        _snprintf_s(text, sizeof(text), _TRUNCATE, u8"%s: %s", label, keybind::Name(vk));

    const float w = ImGui::GetContentRegionAvail().x;
    if (capturing)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.35f, 0.15f, 1.0f));

    if (ImGui::Button(text, ImVec2(w, 0))) {
        if (capturing) keybind::CancelCapture();
        else           keybind::BeginCapture();
    }

    if (capturing) ImGui::PopStyleColor();

    // Right-click clears the binding, so 无 is reachable without a keyboard trick.
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        vk = keybind::kNone;
        keybind::CancelCapture();
        LOG(SBOX("[bind] '%s' cleared", 0x1F2), label);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(u8"左键点击后按下要绑定的按键\n（支持键盘 / 鼠标左右中键 / 侧键 X1 X2）\n右键清除绑定");

    ImGui::PopID();
}

void SyncFeatureFlags() {
    features::g.espEnable    = esp::g_settings.enabled;
    features::g.espBox       = esp::g_settings.box;
    features::g.espSnapline  = esp::g_settings.snapline;
    features::g.espName      = esp::g_settings.name;
    features::g.espDistance  = esp::g_settings.distance;
    features::g.espHealth    = esp::g_settings.health;
    features::g.espTeamColor = esp::g_settings.teamColor;
    if (features::g.espMaxDist == 500.0f)          // still at default
        features::g.espMaxDist = esp::g_settings.maxDistance / 100.0f;

    features::g.aimEnable     = aim::g_settings.enabled;
    features::g.aimDrawFov    = aim::g_settings.drawFov;
    features::g.aimIgnoreTeam = aim::g_settings.teamCheck;
    features::g.aimBoneHead   = aim::g_settings.boneHead;
    features::g.aimSmooth     = aim::g_settings.smooth;

    // -----------------------------------------------------------------------
    // FOV UNITS -- THE REASON AIM "HAD NO EFFECT".
    //
    // The menu slider is in DEGREES (aim::g_settings.fov, default 60). The ported
    // features module works in PIXELS at the crosshair, because that is what the
    // overseas Aimbot.h and its FOV circle use (default 150).
    //
    // Those were copied straight across, so a 60-degree slider produced a 60-PIXEL
    // selection radius while the drawn circle used 150 px -- the two disagreed, no
    // target was ever inside the tiny real cone, and aim appeared dead.
    //
    // Convert properly: pixels = degrees * (screenWidth / horizontalFovDegrees).
    // The camera FOV is not available here, so use the common 90-degree baseline;
    // the result is a sane radius either way and the circle and the gate now match.
    // -----------------------------------------------------------------------
    {
        float width = canvas::ClipX();
        if (width < 16.0f) width = 1920.0f;
        const float degToPx = width / 90.0f;
        features::g.aimFov = aim::g_settings.fov * degToPx;
    }
    features::g.aimJitter     = aim::g_settings.jitter;
    features::g.aimJitterPct  = aim::g_settings.jitterPct;
    features::g.aimJitterHz   = aim::g_settings.jitterHz;
}

void Render() {
    SyncFeatureFlags();
    UpdateCursor();
    if (!g_open) return;
    if (!ImGui::GetCurrentContext()) return;

    const ImGuiIO& io = ImGui::GetIO();

    if (g_opaqueMenu) {
        // Full-screen sheet on the opaque black backdrop. No title bar, no
        // resize, no move: it behaves like a real menu screen.
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.06f, 1.0f));
        ImGui::Begin("##futfull", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    } else {
        // -------------------------------------------------------------------
        // MENU WIDTH -- THIS IS THE "RIGHT SIDE TEXT IS CUT OFF" FIX.
        //
        // 440 px was too narrow for the CJK labels plus their value text, so
        // ImGui clipped the right-hand side (slider values, status strings).
        // CJK glyphs are full-width, so a Chinese label costs roughly twice the
        // pixels of the equivalent English one.
        //
        // 520 px fits the longest row ("最远距离 (uu)" + a 5-digit value) with room
        // to spare, and the height is raised so the new 录屏隐身 / 骨骼 / 雷达
        // sections do not force a scrollbar.
        // -------------------------------------------------------------------
        ImGui::SetNextWindowSize(ImVec2(520, 720), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(80, 90), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::Begin("Calabiyau  |  FUT", &g_open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    }

    // ---- header ----
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "FUT   |   UE 4.25.4   |   build 2026-09-30");
    ImGui::SameLine();
    ImGui::TextDisabled("    INSERT 开关    ESC 关闭");
    ImGui::Separator();
    ImGui::Spacing();

    // ---- status ----
    ue::Camera cam;
    bool camOk = ue::GetCamera(cam);
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "UE 4.25.4   build 2026-09-30");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ue::GObjectsReady() ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                           : ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                       ue::GObjectsReady() ? "GObjects" : "no GObjects");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(camOk ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                       camOk ? "camera" : "no camera");

    ImGui::TextDisabled("objects %d   actors %d   targets %d",
                        ue::NumObjects(), esp::LastActorCount(), esp::LastTargetCount());
    if (camOk)
        ImGui::TextDisabled("cam (%.0f, %.0f, %.0f)  fov %.1f",
                            cam.locX, cam.locY, cam.locZ, cam.fov);

    ImGui::Separator();

    // ---- Capture protection ----
    //
    // Placed BEFORE the ESP section: it changes WHERE the ESP is drawn, so the
    // trade-off belongs next to the ESP toggles.
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(u8"录屏隐身（反截图 / 反录屏）", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool prot = ::g_captureProtect;
        if (ImGui::Checkbox(u8"启用录屏隐身", &prot)) {
            ::g_captureProtect = prot;
            overlay::ApplyCaptureProtection(prot);
        }
        const capture_guard::Mode m = capture_guard::Current();
        ImGui::TextDisabled(u8"状态: %s", capture_guard::ModeName(m));
        if (m == capture_guard::Mode::Blocked)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), u8"降级为黑块（需 Win10 2004+）");
        if (m == capture_guard::Mode::Failed)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"保护未生效");
        ImGui::TextDisabled(u8"开启后 ESP 改画在本窗口才能隐身，代价约 1 帧延迟");
    }

    // ---- ESP ----
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(u8"ESP 透视穿透", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox(u8"启用 ESP", &esp::g_settings.enabled);
        ImGui::Checkbox(u8"方框", &esp::g_settings.box);
        ImGui::Checkbox(u8"连线", &esp::g_settings.snapline);
        ImGui::Checkbox(u8"血量条", &esp::g_settings.health);
        ImGui::Checkbox(u8"距离 / 血量", &esp::g_settings.distance);
        ImGui::Checkbox(u8"角色 / 皮肤 ID", &esp::g_settings.name);
        ImGui::Checkbox(u8"队伍着色（敌红友绿）", &esp::g_settings.teamColor);
        ImGui::Checkbox(u8"穿透墙体（遮挡也画）", &esp::g_settings.throughWalls);
        ImGui::Checkbox(u8"仅屏幕内", &esp::g_settings.visibleOnly);
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"最远距离 (uu)", &esp::g_settings.maxDistance, 500.f, 50000.f, "%.0f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"不透明度", &esp::g_settings.boxAlpha, 0.1f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt(u8"线宽", &esp::g_settings.thickness, 1, 6);

        ImGui::Separator();
        ImGui::TextDisabled(u8"骨骼 / 雷达（国际版复刻）");
        // These drive the ported features module. Keeping the ESP toggles above
        // and these in sync matters: two independent renderers with two sets of
        // flags is how the box and the skeleton end up disagreeing.
        if (ImGui::Checkbox(u8"骨骼骨架", &features::g.espBones))
            esp::g_settings.drawBones = features::g.espBones;
        if (ImGui::Checkbox(u8"角框（Corner Box）", &features::g.espCornerBox))
            esp::g_settings.cornerBox = features::g.espCornerBox;
        ImGui::Checkbox(u8"雷达 / 地图", &features::g.radarEnable);
        ImGui::Separator();
        ImGui::TextDisabled(u8"游戏原生系统（效果与游戏一致）");
        ImGui::Checkbox(u8"反隐身（识别隐身单位）", &features::g.antiStealth);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"调用游戏自己的 GetStealthActors，\n隐身敌人照常显示");
        ImGui::Checkbox(u8"描边透视（游戏原生高亮）", &features::g.outline);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"开启游戏自身的骨骼描边材质，\n与游戏技能透视同源，正确穿墙渲染");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"雷达范围 (uu)", &features::g.radarRange, 300.0f, 5000.0f, u8"%.0f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"雷达边长 (px)", &features::g.radarSize, 100.0f, 400.0f, u8"%.0f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"雷达 X（负=靠右）", &features::g.radarX, -600.0f, 600.0f, u8"%.0f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"雷达 Y（负=靠下）", &features::g.radarY, -600.0f, 600.0f, u8"%.0f");
        ImGui::TextDisabled(u8"地图默认靠右，避免挡住游戏左上角提示");
    }

    // ---- Aim ----
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(u8"Aim 自瞄（硬锁 / 软锁）", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox(u8"启用 Aim 总开关", &aim::g_settings.enabled);

        ImGui::Separator();
        ImGui::TextDisabled(u8"软锁 Soft —— 平滑移动准心（跟枪手感）");
        ImGui::Checkbox(u8"启用软锁", &aim::g_settings.softEnable);
        BindButton(u8"软锁触发键", aim::g_settings.softKey);
        ImGui::Checkbox(u8"软锁：按住生效（关=切换）", &aim::g_settings.softHold);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"软锁平滑（越大越慢）", &aim::g_settings.smooth, 1.0f, 30.0f, "%.1f");

        ImGui::Separator();
        ImGui::TextDisabled(u8"硬锁 Hard —— 直接锁死目标（必中手感）");
        ImGui::Checkbox(u8"启用硬锁", &aim::g_settings.hardEnable);
        BindButton(u8"硬锁触发键", aim::g_settings.hardKey);
        ImGui::Checkbox(u8"硬锁：按住生效（关=切换）", &aim::g_settings.hardHold);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"硬锁松开后保持 (ms)", &aim::g_settings.hardHoldMs, 0.0f, 500.0f, u8"%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"松开触发键后继续锁定的时间，\n用于甩枪后立刻松开仍保持命中");

        ImGui::Separator();
        ImGui::TextDisabled(u8"目标选择（两种锁共用）");
        ImGui::Checkbox(u8"队友过滤", &aim::g_settings.teamCheck);
        ImGui::Checkbox(u8"瞄准头部（否则瞄胸部）", &aim::g_settings.boneHead);
        ImGui::Checkbox(u8"快速移动时自动改瞄胸部", &aim::g_settings.preferChestWhenMoving);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"头骨是全身最抖的关节，飘飞/冲刺时\n每帧位移远大于胸腔，改瞄胸部更稳");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"判定为快速的阈值 (uu/s)", &aim::g_settings.movingThreshold,
                           50.0f, 1500.0f, u8"%.0f");
        ImGui::Checkbox(u8"FOV 限制", &aim::g_settings.fovLimit);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"FOV 角度", &aim::g_settings.fov, 5.0f, 180.0f, "%.0f");

        ImGui::Separator();
        ImGui::TextDisabled(u8"拟人抖动");
        ImGui::Checkbox(u8"启用随机抖动", &aim::g_settings.jitter);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"抖动幅度 (度)", &aim::g_settings.jitterPct, 0.05f, 3.0f, u8"%.2f");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"抖动频率", &aim::g_settings.jitterHz, 1.0f, 30.0f, "%.0f Hz");

        ImGui::Separator();
        const char* modeName = aim::ActiveMode() == aim::Mode::Hard ? u8"硬锁"
                             : aim::ActiveMode() == aim::Mode::Soft ? u8"软锁" : u8"空闲";
        ImGui::Text(u8"状态: %s   目标数: %d   有目标: %s",
                    modeName, aim::TargetCount(), aim::HasTarget() ? u8"是" : u8"否");
        if (aim::HardLocked()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), u8"硬锁锁定中");
        if (aim::LastError()[0]) ImGui::TextDisabled(u8"状态: %s", aim::LastError());
    }

    // ---- Skill / lock ----
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(u8"技能锁定")) {
        ImGui::Checkbox(u8"启用技能模块", &skill::g_settings.enabled);
        ImGui::Checkbox(u8"标记全部目标", &skill::g_settings.trackCd);
        ImGui::TextDisabled(u8"锁定键: 鼠标侧键4 (VK 0x05)，按下切换锁定/解锁");
        ImGui::Checkbox(u8"自动开火（准心命中才开，风险高）", &skill::g_settings.autoFire);
        ImGui::Separator();
        ImGui::Text(u8"追踪目标: %d   已锁定: %s",
                    skill::TrackedCount(), skill::LockedIndex() >= 0 ? u8"是" : u8"否");
        ImGui::TextDisabled(u8"说明: 目标锁定=读取+绘制; 技能CD字段未在 dump 中，不伪造数值");
    }

    // ---- ACE blackhole ----
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(u8"ACE 黑洞网截", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(u8"模块白名单 IAT 补丁（不改 .text）");
        ImGui::Text(u8"已拦截: %u", ace::BlockedCount());
        ImGui::Text(u8"上次补丁: 扫描 %d / 命中 %d / 槽位 %d",
                    ace::LastModulesScanned(), ace::LastPatchedModules(), ace::LastPatchedSlots());
        if (ImGui::Button(u8"立即重新打补丁"))
            ace::InstallDnsBlackholePhase("manual");
        ImGui::SameLine();
        if (ImGui::Button(u8"测试拦截"))
            LOG(SBOX("[gui] manual test example.invalid -> %s", 0x37),
                // P1: the probe hostname is obfuscated too; no anti-cheat domain
                // literal may exist anywhere in the image.
                ace::IsBlockedHost(SBOX("dump.example.invalid", 0x63))
                    ? "BLOCKED" : "allowed");
        if (ImGui::TreeNode(u8"屏蔽域名列表")) {
            for (int i = 0; i < ace::BlocklistCount(); ++i)
                ImGui::BulletText("%s", ace::BlocklistEntry(i));
            ImGui::TreePop();
        }
    }

    // ---- input diagnostics ----
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(u8"输入 / 诊断", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(u8"菜单键 VK 0x%X   生效通道: %s", g_menuKey, input::ActivePath());
        ImGui::TextDisabled(u8"WndProc %u   HotKey %u   Poll %u",
                            input::CountWndProc(), input::CountHotkey(), input::CountPoll());
        ImGui::Checkbox(u8"显示常驻状态条", &g_debugStrip);
        ImGui::Checkbox(u8"菜单不透明背景（遮盖游戏画面）", &g_opaqueMenu);
        ImGui::TextDisabled(u8"键盘: 方向键 / Tab 选择, Enter 或 Space 切换");
        static int s_sel = 0;
        const char* keys[] = { "INSERT", "DELETE", "F1", "F2", "F3", "F4", "HOME", "END", "F8" };
        const int map[]    = { VK_INSERT, VK_DELETE, VK_F1, VK_F2, VK_F3, VK_F4, VK_HOME, VK_END, VK_F8 };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo(u8"菜单键", &s_sel, keys, IM_ARRAYSIZE(keys))) {
            g_menuKey = map[s_sel];
            LOG("[gui] menu key -> 0x%X (restart game for RegisterHotKey to rebind)", g_menuKey);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled(u8"INSERT 开关菜单   ·   菜单打开时鼠标已释放");

    ImGui::End();

    if (g_opaqueMenu) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

// ---------------------------------------------------------------------------
// ESC closes the menu without touching the game binding semantics beyond that
// ---------------------------------------------------------------------------
void PollEscape() {
    static bool s_down = false;
    const bool down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (down && !s_down && g_open) {
        g_open = false;
        LOG("[gui] menu closed by ESC");
    }
    s_down = down;
}

// ---------------------------------------------------------------------------
// always-on strip: proves the render path works even if the key never arrives,
// and gives a mouse-clickable way in.
// ---------------------------------------------------------------------------
void RenderDebugStrip() {
    if (!g_debugStrip) return;
    if (g_open && g_opaqueMenu) return;    // full-screen menu already shows status
    if (!ImGui::GetCurrentContext()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##futstrip", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "FUT");
    ImGui::SameLine();
    ImGui::TextDisabled("obj %d | tgt %d | key 0x%X [%s]",
                        ue::NumObjects(), esp::LastTargetCount(),
                        g_menuKey, input::ActivePath());
    ImGui::SameLine();
    if (ImGui::SmallButton(g_open ? u8"关闭菜单" : u8"打开菜单")) {
        ToggleMenu();
        LOG("[gui] toggle via strip button");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| mouse %.0f,%.0f", io.MousePos.x, io.MousePos.y);

    ImGui::End();
}

} // namespace gui
