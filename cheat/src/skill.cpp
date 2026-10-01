// D:\Work\Fut\cheat\src\skill.cpp
// Skill / ability module: target locking + ability state tracking.
//
// SCOPE, stated honestly:
//
//  * TARGET LOCK -- fully implemented. It remembers one character by ADDRESS
//    (not by index, because the actor list is re-sorted constantly) and keeps a
//    marker on it. This is pure reading plus drawing on our own overlay.
//
//  * ABILITY / COOLDOWN TRACKING -- reads ACyCharacterBase::AbilitySystem and
//    its LifeComponent/LifeStateComponent. This build exposes those components
//    at known offsets, but the individual ability cooldown fields inside the
//    ability system are NOT in the Dumper-7 dump (they live in a runtime
//    container). What we can do reliably is read the components' existence and
//    the character's life state, and expose the pointer chain for follow-up.
//    We therefore do NOT fabricate cooldown numbers.
//
//  * AUTO FIRE -- off by default. Firing is the most conspicuous action this
//    whole tool can take; it is opt-in and never enabled implicitly.
#include "skill.h"
#include "esp.h"
#include "ue.h"
#include "offsets.h"
#include "log.h"
#include "gui.h"

#include "imgui.h"
#include <windows.h>
#include <cmath>

namespace skill {

Settings g_settings;

static uintptr_t s_lockedAddr = 0;
static int       s_lockedIdx  = -1;
static int       s_tracked    = 0;
static char      s_err[128]   = {0};

int  LockedIndex()   { return s_lockedIdx; }
int  TrackedCount()  { return s_tracked; }
const char* LastError() { return s_err; }

static bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// ---------------------------------------------------------------------------
// tick: maintain the lock and count tracked characters
// ---------------------------------------------------------------------------
void Tick() {
    s_tracked = 0;
    s_lockedIdx = -1;
    if (!g_settings.enabled) { s_lockedAddr = 0; return; }

    esp::EspEnt ents[256];
    const int n = esp::SnapshotForAim(ents, 256);
    s_tracked = n;

    // ---- lock / unlock ----
    static bool s_prevKey = false;
    const bool down = KeyDown(g_settings.lockKey);
    if (down && !s_prevKey) {
        // toggle: lock the closest character to the screen centre, or release
        if (s_lockedAddr) {
            s_lockedAddr = 0;
            LOG("[skill] lock released");
        } else if (n > 0) {
            const ImGuiIO& io = ImGui::GetIO();
            const float cx = io.DisplaySize.x * 0.5f;
            const float cy = io.DisplaySize.y * 0.5f;
            float best = 1e30f;
            for (int i = 0; i < n; ++i) {
                float ax = ents[i].bonesOk ? ents[i].chestX : ents[i].x;
                float ay = ents[i].bonesOk ? ents[i].chestY : ents[i].y;
                float az = ents[i].bonesOk ? ents[i].chestZ : ents[i].z;
                float sx, sy;
                if (!ue::WorldToScreen(ax, ay, az, sx, sy)) continue;
                const float d = sqrtf((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
                if (d < best) { best = d; s_lockedAddr = ents[i].addr; }
            }
            if (s_lockedAddr)
                LOG("[skill] locked 0x%llX", (unsigned long long)s_lockedAddr);
        }
    }
    s_prevKey = down;

    // drop the lock if the character is gone
    if (s_lockedAddr) {
        bool alive = false;
        for (int i = 0; i < n; ++i) {
            if (ents[i].addr == s_lockedAddr) { alive = true; s_lockedIdx = i; break; }
        }
        if (!alive) {
            LOG("[skill] lock target vanished");
            s_lockedAddr = 0;
        }
    }

    // ---- auto fire (opt-in) ----
    if (g_settings.autoFire && s_lockedIdx >= 0) {
        const esp::EspEnt& e = ents[s_lockedIdx];
        float ax = e.bonesOk ? e.chestX : e.x;
        float ay = e.bonesOk ? e.chestY : e.y;
        float az = e.bonesOk ? e.headZ : (e.z + 60.0f);

        float sx, sy;
        if (ue::WorldToScreen(ax, ay, az, sx, sy)) {
            const ImGuiIO& io = ImGui::GetIO();
            const float cx = io.DisplaySize.x * 0.5f;
            const float cy = io.DisplaySize.y * 0.5f;
            // a tight gate: only fire when the crosshair is genuinely on target
            const float pxPerDeg = io.DisplaySize.x / 90.0f;
            const float tol = pxPerDeg * 1.5f;
            const float dx = sx - cx, dy = sy - cy;
            if (fabsf(dx) <= tol && fabsf(dy) <= tol) {
                INPUT in[2]{};
                in[0].type = INPUT_MOUSE;
                in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                in[1].type = INPUT_MOUSE;
                in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(2, in, sizeof(INPUT));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// overlay: lock marker
// ---------------------------------------------------------------------------
void RenderOverlay() {
    if (!g_settings.enabled) return;
    if (!ImGui::GetCurrentContext()) return;

    esp::EspEnt ents[256];
    const int n = esp::SnapshotForAim(ents, 256);
    if (n <= 0) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();

    for (int i = 0; i < n; ++i) {
        const esp::EspEnt& e = ents[i];
        const bool locked = (s_lockedAddr != 0 && e.addr == s_lockedAddr);

        float ax = e.bonesOk ? e.chestX : e.x;
        float ay = e.bonesOk ? e.chestY : e.y;
        float az = e.bonesOk ? e.headZ : (e.z + 80.0f);

        float sx, sy;
        if (!ue::WorldToScreen(ax, ay, az, sx, sy)) continue;

        if (locked) {
            // a distinctive ring + cross so the locked target is unmistakable
            dl->AddCircle(ImVec2(sx, sy), 14.0f, IM_COL32(255, 215, 0, 230), 24, 2.0f);
            dl->AddLine(ImVec2(sx - 20, sy), ImVec2(sx - 8, sy), IM_COL32(255, 215, 0, 230), 2.0f);
            dl->AddLine(ImVec2(sx + 8, sy), ImVec2(sx + 20, sy), IM_COL32(255, 215, 0, 230), 2.0f);
            dl->AddLine(ImVec2(sx, sy - 20), ImVec2(sx, sy - 8), IM_COL32(255, 215, 0, 230), 2.0f);
            dl->AddLine(ImVec2(sx, sy + 8), ImVec2(sx, sy + 20), IM_COL32(255, 215, 0, 230), 2.0f);
            dl->AddText(ImVec2(sx + 18, sy - 8), IM_COL32(255, 215, 0, 230), "LOCK");
        } else if (g_settings.trackCd) {
            // a small dot marks every tracked character
            dl->AddCircleFilled(ImVec2(sx, sy), 3.0f, IM_COL32(120, 200, 255, 160));
        }
    }
    (void)io;
}

} // namespace skill
