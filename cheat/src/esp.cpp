// D:\Work\Fut\cheat\src\esp.cpp
// ESP: enumerate ACyCharacterBase instances, project their world position to
// screen with the game's own ProjectWorldLocationToScreen, and draw boxes /
// health / distance through ImGui's foreground draw list.
#include "esp.h"
#include "ue.h"
#include "bones.h"
#include "nametags.h"
#include "canvas.h"
#include "sdk.h"
#include "features.h"
#include "gui.h"
#include "offsets.h"
#include "log.h"
#include "obf.h"

#include "imgui.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "game_systems.h"

namespace esp {

Settings g_settings;

static int s_lastTargets = 0;
static int s_lastActors = 0;
static char s_lastError[256] = {0};

int LastTargetCount() { return s_lastTargets; }
int LastActorCount()   { return s_lastActors; }
const char* LastError(){ return s_lastError; }

// ---------------------------------------------------------------------------
// held local-pawn identity -- the single source for every self filter.
// GetLocalPawn() flickers null during possess/respawn; holding the last good
// address for 2 s stops our own box from flashing on screen in those windows
// (and the aim from snapping to it).
// ---------------------------------------------------------------------------
static uintptr_t s_selfPawnHeld = 0;
static ULONGLONG s_selfPawnAt   = 0;
static uintptr_t LocalPawnHeld() {
    const uintptr_t p = ue::GetLocalPawn();
    if (ue::PtrOk(p)) { s_selfPawnHeld = p; s_selfPawnAt = GetTickCount64(); return p; }
    if (s_selfPawnHeld && GetTickCount64() - s_selfPawnAt <= 2000) return s_selfPawnHeld;
    return 0;
}

// ---------------------------------------------------------------------------
// entity model
// ---------------------------------------------------------------------------
struct Entity {
    uintptr_t addr = 0;
    float     x = 0, y = 0, z = 0;
    float     hp = 0, hpMax = 0;
    float     armor = 0, armorMax = 0;
    bool      alive = true;
    uint8_t   team = 0;
    uint32_t  role = 0;
    uint32_t  skin = 0;
    float     distance = 0;
    char      name[32] = {0};      // PlayerState nickname (best effort)

    // bone-derived geometry (world space); used for an accurate box
    bool  bonesOk = false;
    float headZ = 0, feetZ = 0;
    float chestX = 0, chestY = 0, chestZ = 0;
    float pelvisX = 0, pelvisY = 0, pelvisZ = 0;
};

// last frame's entity list, published for the aim module (see SnapshotForAim).
// Declared after Entity because it depends on the complete type.
static std::vector<Entity> s_lastEnts;

// ---------------------------------------------------------------------------
// box smoothing (anti-flicker)
//
// A character that is momentarily unreadable (animation update in flight, the
// level streaming, whatever) used to make its box vanish for a frame or two,
// which is the "闪一下" the user reported. We now keep a short-lived record of
// each box's last screen rectangle and reuse it for up to kBoxHoldMs when the
// underlying read fails. The box position is also interpolated, so a slow read
// no longer looks like a teleport.
// ---------------------------------------------------------------------------
struct BoxRec {
    uintptr_t addr;
    float x0, y0, x1, y1;
    ULONGLONG t;
};
static std::vector<BoxRec> s_boxes;
static const ULONGLONG kBoxHoldMs = 250;

static BoxRec* FindHeld(uintptr_t addr) {
    for (BoxRec& b : s_boxes)
        if (b.addr == addr) return &b;
    return nullptr;
}

// Draw the surviving held boxes when a frame cannot be projected at all
// (no camera, level transition, ...). This keeps the ESP from blinking off.
static void DrawHeldBoxes() {
    if (!ImGui::GetCurrentContext()) return;
    const ULONGLONG now2 = GetTickCount64();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (const BoxRec& b : s_boxes) {
        if (now2 - b.t >= kBoxHoldMs) continue;
        dl->AddRect(ImVec2(b.x0, b.y0), ImVec2(b.x1, b.y1),
                    IM_COL32(200, 200, 200, 120), 0.f, 0, g_settings.thickness);
    }
}

static uintptr_t s_cyClass = 0;

// ---------------------------------------------------------------------------
// filter bookkeeping
//
// Declared early because ReadEntity (the HP-dead check) also counts into it.
// ---------------------------------------------------------------------------
struct FilterStats {
    int selfByAddr = 0;
    int selfByDist = 0;
    int noController = 0;
    int lobbyClass = 0;
    int dead = 0;
};
static FilterStats s_filter;

// Forward declaration: the definition lives with the other filter helpers
// further down, but ReadEntity (which runs first) needs it.
static bool IsAliveSafe(uintptr_t actor);

// read one character's fields
//
// SLOW LANE / FAST LANE. Position is read EVERY frame (boxes must track).
// Everything expensive that barely changes -- the GAS attribute walk, the
// PlayerState FString, team/skin/role -- is refreshed at most every 500 ms
// from a per-address side cache. Before this, ReadEntity re-walked all of it
// for every entity on every frame at the game's full render rate, which is
// a large slice of the "ESP on = game lags" report.
struct SlowRec {
    uintptr_t addr = 0;
    ULONGLONG t = 0;
    float hp = 0, hpMax = 0;
    float armor = 0, armorMax = 0;   // shown in the bar together with health
    bool  alive = false;             // engine verdict (IsAlive), not a guess
    uint8_t team = 0;
    uint32_t skin = 0, role = 0;
    char name[32] = {0};
};
static SlowRec s_slow[96];
static int s_slowN = 0;

static void RefreshSlow(uintptr_t actor, Entity& e) {
    const ULONGLONG now = GetTickCount64();
    SlowRec* r = nullptr;
    for (int i = 0; i < s_slowN; ++i)
        if (s_slow[i].addr == actor) { r = &s_slow[i]; break; }
    if (r && now - r->t < 500) {
        e.hp = r->hp; e.hpMax = r->hpMax;
        e.armor = r->armor; e.armorMax = r->armorMax; e.alive = r->alive;
        e.team = r->team;
        e.skin = r->skin; e.role = r->role;
        memcpy(e.name, r->name, sizeof(e.name));
        return;
    }
    if (!r) {
        if (s_slowN < 96) r = &s_slow[s_slowN++];
        else { r = &s_slow[0]; r->addr = 0; }   // recycle
        r->addr = actor;
    }
    r->t = now;

    // ------------------------------------------------------------------
    // HP / ARMOUR -- AUTHORITATIVE OFFSETS ONLY, NO GUESSING.
    //
    // The previous version had a second "path B" reading pawn+0x46C / +0x470,
    // copied from another project's walker. Those offsets do not exist in THIS
    // build -- the real fields are BaseHealth/BaseHealthMax at 0x69C/0x698 -- so
    // path B returned whatever unrelated floats live there and the reported HP
    // was nonsense.
    //
    // Both paths below are verified against this project's own dump
    // (PMGame_classes.hpp, UCyBaseAttributeSet starts at line 7177):
    //
    //   AbilitySystem           ACyCharacterBase +0x0880
    //   Health                  UCyBaseAttributeSet +0x0078
    //   HealthMax               UCyBaseAttributeSet +0x0088
    //   Armor                   UCyBaseAttributeSet +0x0098
    //   ArmorMax                UCyBaseAttributeSet +0x00A8
    //   CurrentValue            FGameplayAttributeData +0x0C  (base value at +0x00)
    //   BaseHealth              ACyCharacterBase +0x069C
    //   BaseHealthMax           ACyCharacterBase +0x0698
    // ------------------------------------------------------------------
    r->hp = r->hpMax = 0.0f;
    r->armor = r->armorMax = 0.0f;
    r->alive = false;

    // ------------------------------------------------------------------
    // HP / ARMOUR -- FULL PATH, EVERY OFFSET VERIFIED AGAINST THIS DUMP.
    //
    // The previous version read `AbilitySystem (0x880) + Health (0x78) + 0x0C`
    // and always got 0.0. Root cause:
    //
    //   0x880 is UAbilitySystemComponent, the BASE class, which does not carry
    //   this game's attribute set. The game's own subclass sits next to it:
    //
    //       ACyCharacterBase::AbilitySystem       @ 0x0880  UAbilitySystemComponent*
    //       ACyCharacterBase::ValidAbilitySystem  @ 0x0888  UCyAbilitySystemComponent*
    //
    //   and the attribute sets are not at a fixed offset inside the component --
    //   they live in a TArray:
    //
    //       UAbilitySystemComponent::SpawnedAttributes @ 0x01C0  TArray<UAttributeSet*>
    //
    // So the correct walk is:
    //       actor + 0x888                     -> UCyAbilitySystemComponent
    //       asc   + 0x1C0 (Data)              -> UAttributeSet* array
    //       set[0] + 0x78 + 0x0C              -> Health.CurrentValue
    //
    // Every element is checked before use: a half-valid set would otherwise
    // produce a plausible-looking wrong number, which is worse than zero.
    // ------------------------------------------------------------------
    auto readSet = [&](uintptr_t set) -> bool {
        if (!ue::PtrOk(set)) return false;
        const float cur = ue::RF32(set + off::ATTR_Health    + off::ATTR_CurrentValue);
        const float mx  = ue::RF32(set + off::ATTR_HealthMax + off::ATTR_CurrentValue);
        if (!(mx > 0.0f) || mx > 100000.0f) return false;
        if (!(cur >= -1.0f) || cur > mx + 1.0f) return false;
        r->hp = cur; r->hpMax = mx;

        const float ac = ue::RF32(set + off::ATTR_Armor    + off::ATTR_CurrentValue);
        const float am = ue::RF32(set + off::ATTR_ArmorMax + off::ATTR_CurrentValue);
        if (am > 0.0f && am < 100000.0f && ac >= -1.0f && ac <= am + 1.0f) {
            r->armor = ac; r->armorMax = am;
        }
        return true;
    };

    // primary: the game's own ability system component
    bool got = false;
    {
        const uintptr_t asc = ue::R64(actor + off::CY_ValidAbilitySystem);
        if (ue::PtrOk(asc)) {
            const uintptr_t data = ue::R64(asc + off::ASC_SpawnedAttributes + off::TArray_Data);
            const int32_t   num  = ue::RI32(asc + off::ASC_SpawnedAttributes + off::TArray_Num);
            if (ue::PtrOk(data) && num > 0 && num <= 32) {
                for (int i = 0; i < num && !got; ++i) {
                    const uintptr_t set = ue::R64(data + (uintptr_t)i * 8);
                    got = readSet(set);
                }
            }
        }
    }

    // secondary: the base component, in case the subclass pointer is not set yet
    if (!got) {
        const uintptr_t asc = ue::R64(actor + off::CY_AbilitySystem);
        if (ue::PtrOk(asc)) {
            const uintptr_t data = ue::R64(asc + off::ASC_SpawnedAttributes + off::TArray_Data);
            const int32_t   num  = ue::RI32(asc + off::ASC_SpawnedAttributes + off::TArray_Num);
            if (ue::PtrOk(data) && num > 0 && num <= 32) {
                for (int i = 0; i < num && !got; ++i) {
                    const uintptr_t set = ue::R64(data + (uintptr_t)i * 8);
                    got = readSet(set);
                }
            }
        }
    }

    // tertiary: the character's own cached base values (verified offsets)
    if (!(r->hpMax > 0.0f)) {
        const float bh = ue::RF32(actor + off::CY_BaseHealth);
        const float bm = ue::RF32(actor + off::CY_BaseHealthMax);
        if (bm > 0.0f && bm < 100000.0f && bh >= -1.0f && bh <= bm + 1.0f) {
            r->hp = bh; r->hpMax = bm;
        }
    }

    // ------------------------------------------------------------------
    // ALIVE -- THE ENGINE'S OWN VERDICT, NOT A DERIVED GUESS.
    //
    // The old death check was:
    //     if (e.hpMax > 0.0f && e.hp <= 0.0f) -> treat as dead
    // `hpMax > 0` was a PRECONDITION, so whenever the HP read failed (hpMax == 0)
    // the whole condition was false and DEAD PLAYERS KEPT THEIR BOX. That is the
    // reported "dead bodies still boxed".
    //
    // ACyCharacterBase::IsAlive(bool bIsInjuredAlive) is the game's own answer
    // (RVA 0x2378630, from the current dump). It does not depend on any float we
    // read, so a failed HP read can no longer resurrect a corpse.
    //
    // Uses the single shared IsAliveSafe() wrapper declared below rather than its
    // own local typedef: two copies of the same guarded call had drifted, and only
    // one of them was kept in sync when the offset moved.
    // ------------------------------------------------------------------
    r->alive = IsAliveSafe(actor);
    if (!r->alive && r->hpMax > 0.0f && r->hp > 0.0f) {
        // The engine says dead but the attribute set says alive. Trust the
        // attribute set only when it has real numbers -- this covers the window
        // right after a respawn where IsAlive has not caught up yet.
        r->alive = true;
    }

    // ------------------------------------------------------------------
    // NAME -- the player nickname lives on the PlayerState
    // (APawn::PlayerState 0x330 -> APlayerState::PlayerNamePrivate 0x400,
    // an FString; UTF-16, truncated to ASCII here).
    // ------------------------------------------------------------------
    r->name[0] = 0;
    {
        uintptr_t ps = ue::R64(actor + off::AP_PlayerState);
        if (ue::PtrOk(ps)) {
            uintptr_t strp = ue::R64(ps + 0x400);
            int32_t   strn = ue::RI32(ps + 0x408);
            if (ue::PtrOk(strp) && strn > 0 && strn < 64) {
                uint8_t raw[128] = {0};
                if (ue::Read(strp, raw, (size_t)strn * 2)) {
                    int w = 0;
                    for (int i = 0; i + 1 < strn * 2 && w < 30; i += 2) {
                        const wchar_t c = (wchar_t)(raw[i] | (raw[i + 1] << 8));
                        if (!c) break;
                        r->name[w++] = (c < 128) ? (char)c : '?';
                    }
                    r->name[w] = 0;
                }
            }
        }
    }

    r->team = ue::R8(actor + off::CY_AttributeTeamID);
    r->skin = ue::R32(actor + off::CY_AttributeSkinID);
    r->role = ue::R32(actor + off::PM_RoleID);

    e.hp = r->hp; e.hpMax = r->hpMax;
    e.armor = r->armor; e.armorMax = r->armorMax; e.alive = r->alive;
    e.team = r->team;
    e.skin = r->skin; e.role = r->role;
    memcpy(e.name, r->name, sizeof(e.name));

    // ------------------------------------------------------------------
    // DISTANCE -- was declared on Entity and never written.
    //
    // The nametag needs it, and so does the box's own max-distance cull, but no
    // code path ever assigned it: the field existed and stayed 0, which is why a
    // distance readout would have shown "0.0 m" for every target. Computed here
    // from the local pawn, in uu; the display layer converts to metres.
    //
    // The pawn is resolved once per frame inside LocalPawnHeld(), so this is a
    // cached read rather than a fresh chain walk per entity.
    // ------------------------------------------------------------------
    {
        ue::Vec3 self{};
        const uintptr_t lp = LocalPawnHeld();
        if (ue::PtrOk(lp) && ue::GetActorLocation(lp, self)) {
            const float dx = e.x - self.x, dy = e.y - self.y, dz = e.z - self.z;
            e.distance = sqrtf(dx * dx + dy * dy + dz * dz);
        }
    }

    // ------------------------------------------------------------------
    // HP PROBE -- reports the NEW path so the fix can be verified from the log
    // without guessing. A few lines per 5 s window.
    //
    // The old probe reported AbilitySystem(0x880) and two invented offsets
    // (+0x46C/+0x470 copied from another project). Both are gone; what matters now
    // is whether the SpawnedAttributes walk produced a sane hp/max pair.
    // ------------------------------------------------------------------
    {
        static ULONGLONG s_probeNext = 0;
        static int       s_probeLeft = 0;
        if (now >= s_probeNext) { s_probeNext = now + 5000; s_probeLeft = 3; }
        if (s_probeLeft > 0 && actor != ue::GetLocalPawn()) {
            --s_probeLeft;
            const uintptr_t vasc = ue::R64(actor + off::CY_ValidAbilitySystem);
            const uintptr_t base = ue::R64(actor + off::CY_AbilitySystem);
            const int32_t nV = ue::PtrOk(vasc)
                ? ue::RI32(vasc + off::ASC_SpawnedAttributes + off::TArray_Num) : -1;
            const int32_t nB = ue::PtrOk(base)
                ? ue::RI32(base + off::ASC_SpawnedAttributes + off::TArray_Num) : -1;
            LOG("[hp] 0x%llX name='%s' vasc=0x%llX sets=%d base=0x%llX sets=%d -> hp=%.1f/%.1f armor=%.1f/%.1f alive=%d",
                (unsigned long long)actor, r->name[0] ? r->name : "-",
                (unsigned long long)vasc, nV, (unsigned long long)base, nB,
                r->hp, r->hpMax, r->armor, r->armorMax, (int)r->alive);
        }
    }
}

static bool ReadEntity(uintptr_t actor, Entity& e) {
    ue::Vec3 loc;
    if (!ue::GetActorLocation(actor, loc)) return false;

    e.addr = actor;
    e.x = loc.x; e.y = loc.y; e.z = loc.z;

    RefreshSlow(actor, e);

    // ------------------------------------------------------------------
    // DEAD CHECK -- USES THE ENGINE VERDICT.
    //
    // The old form was `if (e.hpMax > 0.0f && e.hp <= 0.0f)`, where hpMax > 0 was
    // a PRECONDITION: a failed HP read (hpMax == 0) made the condition false, so
    // corpses kept their box forever. That is the reported "dead players still
    // boxed" bug.
    //
    // e.alive comes from ACyCharacterBase::IsAlive, so it is correct regardless of
    // whether any float we read succeeded.
    // ------------------------------------------------------------------
    if (!e.alive) { ++s_filter.dead; return false; }

    // bone-accurate geometry: head / feet give a correct box height, and it does
    // not depend on the capsule or on guessing the actor's centre.
    bones::BoneSet bs;
    if (bones::Get(actor, bs) && bs.valid) {
        e.bonesOk = true;
        e.headZ   = bs.headZ;
        e.feetZ   = bs.feetZ;
        e.chestX  = bs.chestX;  e.chestY = bs.chestY;  e.chestZ = bs.chestZ;
        e.pelvisX = bs.pelvisX; e.pelvisY = bs.pelvisY; e.pelvisZ = bs.pelvisZ;
    }
    return true;
}

// Is this actor a character we want to draw?
// Instances carry their most-derived class (APMCharacter / AWFBaseCharacter),
// so an exact pointer compare against ACyCharacterBase never matches -- we need
// the SuperStruct walk.
static bool IsCharacter(uintptr_t actor) {
    return ue::IsAOfClassNamed(actor, SBOX("ACyCharacterBase", 0x77));
}

// ---------------------------------------------------------------------------
// SELF / TEAM filtering
//
// Where "drawing myself" came from: the GObjects fallback walks EVERY object,
// and the local pawn is of course a character, so it got an enemy-coloured box.
// Address comparison alone is not enough either -- the local pawn pointer can be
// re-read before AcknowledgedPawn settles, and the mesh/root components are
// separate objects that also pass the class test.
//
// So we use four independent checks, cheapest first:
//   1. address == local pawn                      (exact self)
//   2. class chain contains a Lobby/Viewer prefix (spectator / camera proxies)
//   3. the actor has no Controller and no PlayerState (not a real participant)
//   4. distance to the camera < kSelfRadius       (this is our own body)
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// SELF-EXCLUSION RADIUS -- AND WHY 60uu WAS WRONG.
//
// The address comparison above is the authoritative test for "this is me". This
// radius is only a BACKSTOP for the case where AcknowledgedPawn has not settled
// yet and the address compare cannot run.
//
// It must not be used as the primary filter, and 60uu was a bad value for this
// game: it is THIRD PERSON, so the camera sits roughly 150-250uu behind and above
// the player. The local character is therefore always FARTHER than 60uu from the
// camera, the backstop never fired, and the player's own body got a box -- which
// the aim module then locked onto (the "aim locks onto myself" report).
//
// The radius below is deliberately generous: it only has to cover the gap between
// the camera and the player's own capsule, and being slightly too large is safe
// because the address check already removed the exact self case.
// ---------------------------------------------------------------------------
static const float kSelfRadius = 400.0f;   // uu; third-person camera offset

// A participant has a Controller (AIController for bots, APlayerController for
// humans). Spectator proxies, lobby previews and detached meshes do not.
// ---------------------------------------------------------------------------
// IsAliveSafe -- the game's own alive verdict, used both as the death filter and
// as a fallback "is this a real participant" test for freshly spawned bots whose
// Controller/PlayerState have not been assigned yet.
//
// ACyCharacterBase::IsAlive(bool bIsInjuredAlive), RVA 0x2378630.
// ---------------------------------------------------------------------------
typedef bool(__fastcall* tIsAliveFn)(void* self, bool bIsInjuredAlive);

static bool IsAliveSafe(uintptr_t actor) {
    if (!ue::PtrOk(actor)) return false;
    static tIsAliveFn s_fn = nullptr;
    if (!s_fn) s_fn = reinterpret_cast<tIsAliveFn>(ue::ImageBase() + off::Fn_IsAlive);
    __try {
        return s_fn(reinterpret_cast<void*>(actor), true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static bool HasController(uintptr_t actor) {
    return ue::PtrOk(ue::R64(actor + off::AP_Controller));
}
static bool HasPlayerState(uintptr_t actor) {
    return ue::PtrOk(ue::R64(actor + off::AP_PlayerState));
}

// ---------------------------------------------------------------------------
// Lobby / viewing-room actors are characters too, but they are not in the match.
// Their CLASS names carry these markers.
//
// COST NOTE -- this used to walk the whole SuperStruct chain with ClassChain(),
// which calls NameOf() (and therefore the engine's AppendString) up to eight
// times PER ACTOR, on every 250 ms rescan. With ~10 characters that is ~80 engine
// string calls per scan for a filter that almost always answers "no".
//
// Only the actor's OWN class name is needed for these markers, so one NameOf()
// call replaces the chain walk. The result is cached per class pointer: a level
// has a handful of distinct classes and thousands of actors.
// ---------------------------------------------------------------------------
static bool IsLobbyActor(uintptr_t actor) {
    const uintptr_t cls = ue::R64(actor + off::UObj_Class);
    if (!ue::PtrOk(cls)) return false;

    struct Entry { uintptr_t cls; bool lobby; };
    static Entry s_cache[64];
    static int   s_cacheN = 0;

    for (int i = 0; i < s_cacheN; ++i)
        if (s_cache[i].cls == cls) return s_cache[i].lobby;

    bool lobby = false;
    {
        std::string nm;
        if (ue::NameOf(cls, nm)) {
            static const char* kBad[] = { "Lobby", "Viewer", "Preview", "Display", "Show" };
            for (const char* b : kBad)
                if (nm.find(b) != std::string::npos) { lobby = true; break; }
        }
    }

    if (s_cacheN < 64) s_cache[s_cacheN++] = Entry{ cls, lobby };
    return lobby;
}

// ---------------------------------------------------------------------------
// collect all live characters
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// entity address cache
//
// Walking UWorld->Actors[] and running the IsA class-chain test on every slot is
// expensive; doing it every frame was part of the frame-time problem. The SET of
// characters changes rarely, so we cache the addresses and refresh them on a
// timer, while positions/HP are still read fresh every frame.
// ---------------------------------------------------------------------------
// Per-frame position refresh is what matters for "跟手"; the ADDRESS list is
// what changes slowly. So:
//   * addresses are re-scanned every kCacheMs (the set of characters rarely
//     changes and the IsA walk is the expensive part)
//   * positions / HP / bones are re-read EVERY frame for every cached address
//     (ReadEntity in the Collect loop below)
// 500 ms was too slow for the address list during a match: a newly spawned or
// re-entering enemy could take half a second to appear. 250 ms keeps the walk
// cheap while making spawns feel immediate.
static std::vector<uintptr_t> s_addrCache;
static ULONGLONG              s_cacheTime = 0;
static const ULONGLONG        kCacheMs = 250;

static int ScanCharacters(std::vector<uintptr_t>& out) {
    out.clear();
    s_filter = FilterStats{};

    // SELF IDENTITY HOLD -- see LocalPawnHeld() above.
    uintptr_t myPawn = LocalPawnHeld();
    uintptr_t level = ue::GetPersistentLevel();
    int actorCount = ue::GetActorCount(level);
    s_lastActors = actorCount;

    auto consider = [&](uintptr_t a) {
        if (!ue::PtrOk(a)) return;
        if (!IsCharacter(a)) return;

        // 1. exact self
        if (ue::PtrOk(myPawn) && a == myPawn) { ++s_filter.selfByAddr; return; }

        // 2. lobby / spectator proxies
        if (IsLobbyActor(a)) { ++s_filter.lobbyClass; return; }

        // ------------------------------------------------------------------
        // 3. MUST LOOK LIKE A REAL PARTICIPANT -- BUT DO NOT DROP BOTS.
        //
        // This used to require Controller OR PlayerState, both of which are
        // assigned a FRAME OR TWO AFTER an AI pawn spawns. A bot caught in that
        // window failed the test, was skipped, and -- because the address list is
        // only rebuilt every 250 ms -- could stay missing for a noticeable time.
        // That is the reported "a bot has no box and never gets locked".
        //
        // A third, always-available signal is used now: the game own
        // IsAlive() call. If it answers, the actor is a live character pawn.
        // ------------------------------------------------------------------
        if (!HasController(a) && !HasPlayerState(a) && !IsAliveSafe(a)) {
            ++s_filter.noController;
            return;
        }

        out.push_back(a);
    };

    if (actorCount > 0) {
        for (int i = 0; i < actorCount; ++i)
            consider(ue::GetActor(level, i));
        if (!out.empty()) return (int)out.size();
    }

    // fallback: GObjects scan (e.g. lobby, or when PersistentLevel has no actors).
    // The same `consider` filter is applied -- without it this path drew the
    // local pawn and every lobby preview as an enemy.
    int n = ue::NumObjects();
    for (int i = 0; i < n && out.size() < 256; ++i)
        consider(ue::ObjectAt(i));
    return (int)out.size();
}

void Collect(std::vector<Entity>& out) {
    out.clear();

    if (!s_cyClass) {
        s_cyClass = ue::FindClassByName(SBOX("CyCharacterBase", 0x89));
        if (!s_cyClass) s_cyClass = ue::FindClassByName(SBOX("ACyCharacterBase", 0x78));
        if (s_cyClass)
            LOG(SBOX("[esp] class resolved = 0x%llX", 0x87), (unsigned long long)s_cyClass);
    }

    const ULONGLONG now = GetTickCount64();
    if (s_addrCache.empty() || now - s_cacheTime > kCacheMs) {
        std::vector<uintptr_t> fresh;
        const int found = ScanCharacters(fresh);

        // Only adopt the new list when it actually found something.
        //
        // The UWorld chain goes briefly null during level transitions and the
        // GObjects fallback can also come back empty; adopting those empty
        // results made the cache oscillate (10 -> 0 -> 10) and the ESP flicker.
        // Keeping the previous list lets ReadEntity fail naturally per entry
        // instead, so boxes simply stop drawing for stale slots.
        if (found > 0) {
            s_addrCache.swap(fresh);
            s_cacheTime = now;

            static int s_lastFound = -1;
            static bool s_bonesOk = false;   // set once any character yields a usable skeleton
            if (found != s_lastFound) {
                s_lastFound = found;
                std::string chain;
                ue::ClassChain(s_addrCache[0], chain);
                {
                    // Do NOT log the class chain: it contains the live class
                    // names and would put them into the trace file verbatim.
                    // A short hash is enough to correlate runs.
                    unsigned h = 2166136261u;
                    for (char c : chain) { h ^= (unsigned char)c; h *= 16777619u; }
                    char hh[16]; _snprintf_s(hh, sizeof(hh), _TRUNCATE, "%08X", h);
                    LOG(SBOX("[esp] %d character(s) cached; chain#%s", 0x88),
                        found, hh);
                }
            }
            // ---- bone pipeline verification ----
            //
            // Try EVERY cached character, not just the first. The first version
            // probed s_addrCache[0] once and gave up; if that actor was not yet
            // animated (or is a mesh-less variant) the probe reported FAILED
            // forever even though the pipeline worked for the others.
            if (!s_bonesOk && !s_addrCache.empty()) {
                for (uintptr_t a : s_addrCache) {
                    bones::BoneSet bs;
                    if (bones::Get(a, bs)) {
                        s_bonesOk = true;
                        LOG("[esp] bone probe: OK on 0x%llX headZ=%.1f feetZ=%.1f chestZ=%.1f pelvisZ=%.1f",
                            (unsigned long long)a, bs.headZ, bs.feetZ, bs.chestZ, bs.pelvisZ);
                        break;
                    }
                }
                if (!s_bonesOk) {
                    static int s_probeTries = 0;
                    if (s_probeTries++ < 8) {
                        LOG("[esp] bone probe: no character has bones yet (tried %d)",
                            (int)s_addrCache.size());
                        bones::DumpBones(s_addrCache[0]);
                    }
                }
            }
        } else if (s_addrCache.empty()) {
            // nothing cached yet: retry sooner than the normal interval
            s_cacheTime = now - (kCacheMs - 100);
        } else {
            // keep the old list; try again next interval
            s_cacheTime = now;
        }
    }

    // Read live fields for each cached address, and PRUNE the ones that fail.
    //
    // This is the fix for the "gets slower and slower" symptom: when a character
    // is destroyed its address stays in the cache, and every later read throws an
    // SEH exception (an expensive kernel round-trip). Those stale slots piled up
    // over time. Dead addresses are now dropped immediately, and the whole cache
    // is rebuilt when too many have gone bad.
    int live = 0, dead = 0;
    for (uintptr_t a : s_addrCache) {
        if (out.size() >= 256) break;
        Entity e;
        if (ReadEntity(a, e)) {
            out.push_back(e);
            ++live;
        } else {
            ++dead;
        }
    }

    if (dead > 0) {
        // drop dead entries so they are never read again
        std::vector<uintptr_t> keep;
        keep.reserve(out.size());
        for (const Entity& e : out) keep.push_back(e.addr);
        s_addrCache.swap(keep);

        // too many dead in one pass -> the level likely changed; rescan now
        if (dead > live) {
            s_cacheTime = 0;
            s_addrCache.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// colours
// ---------------------------------------------------------------------------
static ImU32 TeamColor(uint8_t team, uint8_t localTeam, float alpha) {
    if (team == localTeam) return IM_COL32(60, 220, 90, (int)(255 * alpha));   // friend: green
    return IM_COL32(240, 60, 60, (int)(255 * alpha));                          // enemy: red
}

static uint8_t LocalTeam() {
    uintptr_t pawn = ue::GetLocalPawn();
    if (!ue::PtrOk(pawn)) return 0xFF;
    return ue::R8(pawn + off::CY_AttributeTeamID);
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
void Render() {
    if (!g_settings.enabled) return;
    if (!ImGui::GetCurrentContext()) return;

    if (!ue::GObjectsReady()) {
        if (!ue::Init()) {
            strcpy_s(s_lastError, "GObjects not ready");
            return;
        }
    }

    // tell the projector our viewport and snapshot the camera ONCE for this
    // frame (WorldToScreen reads the snapshot, so it is cheap per entity)
    {
        const ImGuiIO& tio = ImGui::GetIO();
        ue::SetScreenSize((int)tio.DisplaySize.x, (int)tio.DisplaySize.y);
    }
    ue::BeginFrame();

    std::vector<Entity> ents;
    Collect(ents);
    s_lastTargets = 0;

    // -----------------------------------------------------------------------
    // ENTITY HOLD -- THIS IS THE "ESP SUDDENLY DISAPPEARS" FIX.
    //
    // `if (ents.empty()) return;` cleared every box for a frame whenever Collect()
    // came back empty. That happens routinely and briefly: during a weapon swap,
    // a level stream, or a frame where every ReadEntity hit a torn pointer. The
    // result was the ESP blinking off and back on, which reads as instability.
    //
    // Holding the previous list for a short window removes the blink without
    // hiding a genuinely empty scene: if the list stays empty past the window the
    // boxes do disappear, as they should.
    //
    // WHY THE WINDOW IS 120 ms AND NOT 400 ms
    //
    // The held entities carry POSITIONS that are frozen at the moment of the copy,
    // while the camera keeps moving. Projecting stale world positions through a
    // live camera is exactly what makes a box "jump": for the duration of the hold
    // the box slides against a scene it no longer matches, then snaps to the new
    // value when a real read succeeds.
    //
    // The hold only has to cover a dropped frame or two (16-33 ms at 60 Hz). 120 ms
    // is generous for that and short enough that the drift stays sub-pixel at
    // normal movement speeds.
    // -----------------------------------------------------------------------
    static std::vector<Entity> s_holdEnts;
    static ULONGLONG           s_holdUntil = 0;
    constexpr ULONGLONG        kEntityHoldMs = 120;

    if (ents.empty()) {
        if (!s_holdEnts.empty() && GetTickCount64() < s_holdUntil) {
            ents = s_holdEnts;          // keep drawing last frame's set
        } else {
            s_holdEnts.clear();
            return;
        }
    } else {
        s_holdEnts  = ents;
        s_holdUntil = GetTickCount64() + kEntityHoldMs;
    }

    ue::Camera cam;
    ue::GetCamera(cam);

    // If we have characters but no camera, we cannot project anything. Rather
    // than dropping the frame (the old flicker), hold the previous boxes.
    if (!cam.valid) {
        static int s_chainDumps = 0;
        if (!ents.empty() && s_chainDumps < 2) {
            ++s_chainDumps;
            LOG("[esp] have %d character(s) but NO camera -- dumping chain",
                (int)ents.size());
            ue::DumpPlayerChain();
        }
        DrawHeldBoxes();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const uint8_t localTeam = LocalTeam();
    const uintptr_t myPawn = LocalPawnHeld();
    const ULONGLONG now = GetTickCount64();

    // ---- expire the held boxes ----
    {
        std::vector<BoxRec> keep;
        for (const BoxRec& b : s_boxes)
            if (now - b.t < kBoxHoldMs) keep.push_back(b);
        s_boxes.swap(keep);
    }

    int drawn = 0;
    for (const Entity& e : ents) {
        // ---- self exclusion ----
        //
        // The GObjects fallback walks EVERY object, including our own pawn, and
        // our pawn is of course a character -- that is the "画了我自己并标成敌人"
        // symptom. Compare against the local pawn address first, and treat
        // anything at ~0 distance from the camera as us as well.
        if (ue::PtrOk(myPawn) && e.addr == myPawn) continue;

        float dx = e.x - cam.locX, dy = e.y - cam.locY, dz = e.z - cam.locZ;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > g_settings.maxDistance) continue;
        // NOTE: no distance-based self filter here. The address check above is
        // exact, and a distance threshold in third person either misses the player
        // (too small) or eats close-range enemies (too large).

        // ---- project the bone-derived extremes when available ----
        //
        // Boxes built from a fixed 180uu guess were wrong: the projection is
        // perspective, so a constant world height does not map to a constant
        // pixel height once the target is off-centre or partly below the camera.
        // Using the real head and feet gives a box that actually tracks the body.
        float x0, y0, x1, y1;
        bool projected = false;

        if (e.bonesOk) {
            float hx, hy, fx, fy;
            const bool headOk = ue::WorldToScreen(e.chestX, e.chestY, e.headZ, hx, hy);
            const bool feetOk = ue::WorldToScreen(e.chestX, e.chestY, e.feetZ, fx, fy);
            if (headOk && feetOk) {
                // width from the pelvis->chest horizontal spread, with a floor so
                // a head-on target still gets a usable box
                float px, py;
                float wpx = 0.0f;
                if (ue::WorldToScreen(e.pelvisX, e.pelvisY, e.pelvisZ, px, py))
                    wpx = fabsf(px - hx);

                float boxH2 = fabsf(fy - hy);
                if (boxH2 < 3.0f) boxH2 = 3.0f;
                float boxW2 = wpx > 3.0f ? wpx * 1.6f : boxH2 * 0.45f;
                if (boxW2 < 3.0f) boxW2 = 3.0f;

                const float midX = (hx + fx) * 0.5f;
                const float midY = (hy + fy) * 0.5f;
                x0 = midX - boxW2 * 0.5f;
                x1 = midX + boxW2 * 0.5f;
                y0 = midY - boxH2 * 0.5f;
                y1 = midY + boxH2 * 0.5f;
                projected = true;
            }
        }
        if (!projected) {
            // fallback: project the actor origin, approximate the body
            float sx, sy;
            if (!ue::WorldToScreen(e.x, e.y, e.z, sx, sy)) {
                // nothing worked this frame: reuse the last known rect
                if (const BoxRec* held = FindHeld(e.addr)) {
                    x0 = held->x0; y0 = held->y0; x1 = held->x1; y1 = held->y1;
                    projected = true;   // reuse, do not refresh the timestamp
                } else {
                    continue;
                }
            } else {
                float scale = 1.0f;
                if (cam.fov > 1.0f)
                    scale = (H * 0.5f) / tanf(cam.fov * 0.5f * 3.14159265f / 180.0f);
                float boxH2 = (180.0f * scale) / (dist > 1.0f ? dist : 1.0f);
                float boxW2 = boxH2 * 0.5f;
                if (boxH2 < 4.0f) boxH2 = 4.0f;
                if (boxH2 > H)   boxH2 = H;
                if (boxW2 < 3.0f) boxW2 = 3.0f;
                x0 = sx - boxW2 * 0.5f;
                x1 = sx + boxW2 * 0.5f;
                y0 = sy - boxH2;          // feet at the actor origin
                y1 = sy;
                projected = true;
            }
        }

        if (x1 < -W || x0 > W * 2 || y1 < -H || y0 > H * 2) continue;

        // ---- record / update the held rect for anti-flicker ----
        if (projected) {
            BoxRec* held = FindHeld(e.addr);
            if (held) {
                // smooth toward the new rect: kills the 1-frame teleport feel
                const float a = 0.55f;
                held->x0 += (x0 - held->x0) * a;
                held->y0 += (y0 - held->y0) * a;
                held->x1 += (x1 - held->x1) * a;
                held->y1 += (y1 - held->y1) * a;
                held->t = now;
            } else {
                s_boxes.push_back({ e.addr, x0, y0, x1, y1, now });
            }
        }

        // ---- draw the SMOOTHED rect (anti-flicker) ----
        const BoxRec* hb = FindHeld(e.addr);
        const float dx0 = hb ? hb->x0 : x0;
        const float dy0 = hb ? hb->y0 : y0;
        const float dx1 = hb ? hb->x1 : x1;
        const float dy1 = hb ? hb->y1 : y1;

        const ImU32 col = g_settings.teamColor ? TeamColor(e.team, localTeam, g_settings.boxAlpha)
                                               : IM_COL32(255, 255, 255, (int)(255 * g_settings.boxAlpha));

        // ---- box ----
        if (g_settings.box) {
            dl->AddRect(ImVec2(dx0, dy0), ImVec2(dx1, dy1), IM_COL32(0, 0, 0, 160), 0.f, 0, g_settings.thickness + 2);
            dl->AddRect(ImVec2(dx0, dy0), ImVec2(dx1, dy1), col, 0.f, 0, g_settings.thickness);
        }

        // ---- snapline: from the TOP of the screen down to the target ----
        // (previously drawn from the bottom edge; the requested direction is a
        //  ray coming down from above)
        if (g_settings.snapline) {
            dl->AddLine(ImVec2(W * 0.5f, 0.0f), ImVec2((dx0 + dx1) * 0.5f, dy0), col,
                        (float)g_settings.thickness);
        }

        // ---- health bar (left of the box) ----
        if (g_settings.health && e.hpMax > 0.0f) {
            float frac = e.hp / e.hpMax;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            float bx0 = x0 - 6.0f, bx1 = x0 - 2.0f;
            dl->AddRectFilled(ImVec2(bx0, y0), ImVec2(bx1, y1), IM_COL32(0, 0, 0, 180));
            float fh = (dy1 - dy0) * frac;
            ImU32 hc = frac > 0.5f ? IM_COL32(70, 220, 90, 230)
                     : frac > 0.25f ? IM_COL32(240, 200, 60, 230)
                                    : IM_COL32(240, 60, 60, 230);
            dl->AddRectFilled(ImVec2(bx0, dy1 - fh), ImVec2(bx1, dy1), hc);
        }

        // ---- labels ----
        char label[160] = {0};
        if (g_settings.name) {
            _snprintf_s(label, sizeof(label), _TRUNCATE, "role %u  skin %u",
                        e.role, e.skin);
            dl->AddText(ImVec2(dx0, dy0 - 16.0f), col, label);
        }
        if (g_settings.distance) {
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%.0fm  hp %.0f/%.0f",
                        dist / 100.0f, e.hp, e.hpMax);
            dl->AddText(ImVec2(dx0, dy1 + 2.0f), IM_COL32(230, 230, 230, 220), label);
        }

        ++drawn;
    }
    s_lastTargets = drawn;
    s_lastEnts = ents;      // publish for the aim module
}

// ---------------------------------------------------------------------------
// snapshot accessor for the aim module
//
// ESP already walks the entity list each frame; the aim module needs the same
// data. Rather than walk twice, aim reads this copy of the last ESP pass.
// ---------------------------------------------------------------------------
int SnapshotForAim(EspEnt* out, int maxOut) {
    if (!out || maxOut <= 0) return 0;
    int n = 0;
    for (const Entity& e : s_lastEnts) {
        if (n >= maxOut) break;
        EspEnt& d = out[n++];
        d.addr = e.addr;
        d.x = e.x; d.y = e.y; d.z = e.z;
        d.hp = e.hp; d.hpMax = e.hpMax;
        d.team = e.team;
        d.role = e.role; d.skin = e.skin;
        d.bonesOk = e.bonesOk;
        d.headZ = e.headZ; d.feetZ = e.feetZ;
        d.chestX = e.chestX; d.chestY = e.chestY; d.chestZ = e.chestZ;
        d.pelvisX = e.pelvisX; d.pelvisY = e.pelvisY; d.pelvisZ = e.pelvisZ;
    }
    return n;
}

// ===========================================================================
// IN-GAME RENDER (UCanvas)
//
// This is the path the user asked for: draw inside the game's own frame instead
// of in a separate overlay window.
//
// WHY IT REPLACES THE OVERLAY
//   The overlay window is composited by DWM after the game's frame, so it can
//   never be in step with it -- at 239 fps that is several frames of lag. Drawing
//   through UCanvas puts the primitives into the same render pass as the game's
//   own HUD, so there is no added latency at all.
//
// WHEN IT RUNS
//   canvas::Hook_PostRender fires once per rendered frame, on the game's render
//   thread, with a valid UCanvas. We project and emit primitives there so the
//   data is sampled in the same frame that is being drawn -- no cross-thread
//   copy, no one-frame-old snapshot.
//
// BONES
//   When bone data is available we draw the actual skeleton (head, chest,
//   pelvis, feet plus the connecting chain) instead of a plain rectangle, which
//   is what the reference screenshot shows. bones::Get supplies the world points.
// ===========================================================================
namespace {

// Draw one character the way the reference does: skeleton, box, name, distance,
// health bar -- coloured by visibility and team.
void DrawOneInGame(void* canvasObj, const Entity& e, const ue::Camera& cam,
                   uint8_t localTeam, float screenW, float screenH) {
    (void)canvasObj;

    // -----------------------------------------------------------------------
    // ALIVE GATE AT THE DRAW ENTRY.
    //
    // This is the fix for "dead players keep their box".
    //
    // The alive test already ran in ReadEntity, but two paths could still put a
    // dead actor in front of this function:
    //
    //   1. the 400 ms entity hold (s_holdEnts) restores last frame's snapshot
    //      verbatim when a scan comes back empty, so a player who dies during
    //      that window is drawn from the stale copy;
    //   2. the address cache keeps an actor for up to 250 ms after it fails
    //      ReadEntity, and ReadEntity is where the alive test lives.
    //
    // Gating here means the decision is made at the moment of drawing, against
    // the entity that is actually about to be drawn -- the only place where it
    // cannot be bypassed by a cache.
    // -----------------------------------------------------------------------
    if (!e.alive) return;

    const float camDx = e.x - cam.locX, camDy = e.y - cam.locY, camDz = e.z - cam.locZ;
    const float worldDist = sqrtf(camDx * camDx + camDy * camDy + camDz * camDz);
    // A very small floor only rejects the degenerate case of a point sitting on
    // the camera itself; it must not be large enough to hide nearby enemies.
    if (worldDist > g_settings.maxDistance || worldDist < 10.0f) return;

    // ---- project the extremes ----
    // headZ is the skull CENTRE anchor (capsule 0.80); +16 uu restores the
    // box top to just above the head without floating over it.
    float topZ = e.bonesOk ? (e.headZ + 16.0f) : (e.z + 90.0f);
    float botZ = e.bonesOk ? e.feetZ : (e.z - 90.0f);
    float midX = e.bonesOk ? e.chestX : e.x;
    float midY = e.bonesOk ? e.chestY : e.y;

    float hx, hy, fx, fy;
    if (!canvas::Project(canvasObj, midX, midY, topZ, hx, hy)) return;
    if (!canvas::Project(canvasObj, midX, midY, botZ, fx, fy)) return;

    float boxH = fabsf(fy - hy);
    if (boxH < 3.0f) boxH = 3.0f;
    float boxW = boxH * 0.42f;
    if (boxW < 3.0f) boxW = 3.0f;

    const float cx = (hx + fx) * 0.5f;
    const float cy = (hy + fy) * 0.5f;
    const float x0 = cx - boxW * 0.5f;
    const float y0 = cy - boxH * 0.5f;
    const float x1 = cx + boxW * 0.5f;
    const float y1 = cy + boxH * 0.5f;

    if (x1 < 0 || x0 > screenW || y1 < 0 || y0 > screenH) return;

    // ---- colour: team first, then visibility ----
    const bool friend_ = g_settings.teamColor && (e.team == localTeam);
    float r = friend_ ? 0.25f : 0.95f;
    float g = friend_ ? 0.95f : 0.25f;
    float b = 0.25f;
    const float alpha = g_settings.boxAlpha;

    // ---- box ----
    if (g_settings.box)
        canvas::AddRect(x0, y0, boxW, boxH, (float)g_settings.thickness, r, g, b, alpha);

    // ---- skeleton ----
    //
    // Reference-style: connect head -> chest -> pelvis -> feet. Those four points
    // come straight from the bone array, so the lines follow the actual pose
    // instead of being decorative.
    if (e.bonesOk) {
        float hX, hY, cX, cY, pX, pY, fX, fY;
        const bool okH = canvas::Project(canvasObj, e.chestX, e.chestY, e.headZ,   hX, hY);
        const bool okC = canvas::Project(canvasObj, e.chestX, e.chestY, e.chestZ,  cX, cY);
        const bool okP = canvas::Project(canvasObj, e.pelvisX, e.pelvisY, e.pelvisZ, pX, pY);
        const bool okF = canvas::Project(canvasObj, e.pelvisX, e.pelvisY, e.feetZ, fX, fY);

        const float sr = friend_ ? 0.4f : 1.0f;
        const float sg = friend_ ? 1.0f : 0.6f;
        const float sb = 0.3f;

        if (okH && okC) canvas::AddLine(hX, hY, cX, cY, 1.5f, sr, sg, sb, 1.0f);
        if (okC && okP) canvas::AddLine(cX, cY, pX, pY, 1.5f, sr, sg, sb, 1.0f);
        if (okP && okF) canvas::AddLine(pX, pY, fX, fY, 1.5f, sr, sg, sb, 1.0f);

        // shoulder / hip cross-bars give the skeleton a readable silhouette
        if (okC) canvas::AddLine(cX - boxW * 0.35f, cY, cX + boxW * 0.35f, cY, 1.5f, sr, sg, sb, 1.0f);
        if (okP) canvas::AddLine(pX - boxW * 0.25f, pY, pX + boxW * 0.25f, pY, 1.5f, sr, sg, sb, 1.0f);
    }

    // ---- snapline from the top of the screen ----
    if (g_settings.snapline)
        canvas::AddLine(screenW * 0.5f, 0.0f, cx, y0, (float)g_settings.thickness, r, g, b, alpha);

    // ---- health bar ----
    if (g_settings.health && e.hpMax > 0.0f) {
        float frac = e.hp / e.hpMax;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        const float bx = x0 - 5.0f;
        canvas::AddFillRect(bx, y0, 3.0f, boxH, 0.0f, 0.0f, 0.0f, 0.65f);
        const float fh = boxH * frac;
        const float hr = frac > 0.5f ? 0.27f : (frac > 0.25f ? 0.94f : 0.94f);
        const float hg = frac > 0.5f ? 0.86f : (frac > 0.25f ? 0.78f : 0.23f);
        const float hb = frac > 0.5f ? 0.35f : (frac > 0.25f ? 0.23f : 0.23f);
        canvas::AddFillRect(bx, y1 - fh, 3.0f, fh, hr, hg, hb, 0.9f);

        // -------------------------------------------------------------------
        // ARMOUR BAR -- RIGHT SIDE, MIRRORING THE HEALTH BAR.
        //
        // THIS IS THE "WHITE BAR UNDER THE CHARACTER" THE USER ASKED ABOUT.
        //
        // It was not armour. The old code drew a white tick under the box whose
        // WIDTH encoded distance:
        //
        //     AddFillRect(x0, y1 + 3, fminf(60, 8 + dist/200), 2, 0.9,0.9,0.9, 0.6)
        //
        // An unlabelled white bar that changes length with range reads as a stat
        // bar, so it was reported as "the armour bar is wrong". It has been removed
        // in favour of a labelled distance in the nametag (see nametags.cpp), and
        // real armour now gets its own bar here:
        //
        //   * straight from UCyBaseAttributeSet::Armor / ArmorMax (0x98 / 0xA8)
        //   * on the RIGHT of the box so it cannot be confused with health
        //   * blue-white, distinct from the green/amber/red health ramp
        //   * only drawn when the target actually has armour
        //
        // This title's armour is a real regenerating resource, so an enemy at full
        // health behind full armour is a different proposition from one at full
        // health with none -- worth its own bar rather than a text suffix.
        // -------------------------------------------------------------------
        if (e.armorMax > 0.0f) {
            float af = e.armor / e.armorMax;
            if (af < 0.0f) af = 0.0f;
            if (af > 1.0f) af = 1.0f;
            const float ax = x1 + 2.0f;
            canvas::AddFillRect(ax, y0, 3.0f, boxH, 0.0f, 0.0f, 0.0f, 0.65f);
            const float ah = boxH * af;
            if (af > 0.0f)
                canvas::AddFillRect(ax, y1 - ah, 3.0f, ah, 0.78f, 0.86f, 0.95f, 0.9f);
        }
    }

    // -----------------------------------------------------------------------
    // NAME / DISTANCE / HP TEXT ARE NOW DRAWN BY THE NAMETAGS MODULE.
    //
    // They used to be emitted here as three unrelated pieces: a white tick under
    // the box for distance, a bare name above it, and "hp / hpmax" below. The user
    // asked for the reference client's presentation instead -- a single rounded
    // panel carrying name, health, distance and a bar -- and for distance to be
    // its own labelled element rather than an unlabelled tick.
    //
    // Keeping BOTH would double-draw the same information, so the old primitives
    // are removed rather than left behind a switch. See nametags.cpp.
    // -----------------------------------------------------------------------
}

} // namespace

// Called by canvas::Hook_PostRender on the game's render thread, inside the
// frame. Everything below runs synchronously with the frame being drawn.
void RenderInGame(void* canvasObj) {
    // -----------------------------------------------------------------------
    // RENDER PATH -- RESTORED TO THE ONE THAT WAS MEASURED CORRECT.
    //
    // A detour handed the whole ESP over to the ported `features` module. That
    // module re-derives the box from GetActorBounds and reprojects through a
    // second code path, and the result was WORSE: the box drifted upward and the
    // aim module stopped landing hits.
    //
    // The path below is the one with screenshot evidence behind it:
    //   Collect()            -> builds the entity list
    //   canvas::Project      -> the ENGINE's own K2_Project (cannot be misaligned)
    //   head/feet bone Z     -> box extent
    //
    // The ported features are now strictly ADDITIVE. They get the settings they
    // need, but they never own the geometry.
    // -----------------------------------------------------------------------
    canvas::SetCanvasSize(canvasObj);
    gui::SyncFeatureFlags();          // menu -> features, every frame

    // sdk layer: only needed by the additive features (skeleton). A failure here
    // must not stop the ESP, so it is not allowed to return.
    {
        static bool s_sdkTried = false;
        if (!s_sdkTried) {
            s_sdkTried = true;
            if (!sdk::Init())
                LOG(SBOX("[esp/ingame] sdk layer unavailable; skeletal overlay off", 0xE2));
        }
    }

    // ---- entry diagnostic ----
    // Placed FIRST, on purpose. Every earlier attempt at this logged at the end
    // of the function, so any early return (no camera, no entities, GObjects not
    // ready) produced total silence -- which is indistinguishable from "the hook
    // never fired". This line proves the hook runs, once every 2 s.
    static ULONGLONG s_nextLog = 0;
    {
        const ULONGLONG n = GetTickCount64();
        if (n >= s_nextLog) {
            s_nextLog = n + 2000;
            LOG(SBOX("[esp/ingame] ENTER cv=0x%llX enabled=%d objectsReady=%d", 0xAB),
                (unsigned long long)(uintptr_t)canvasObj,
                (int)g_settings.enabled, (int)ue::GObjectsReady());
        }
    }

    // ---- CAPTURE PROTECTION GATE ----
    //
    // When the overlay carries WDA_EXCLUDEFROMCAPTURE the ESP must be drawn THERE,
    // not here. Anything drawn through UCanvas becomes part of the game's own
    // window and is captured by OBS/screenshots regardless of what we do to our
    // window -- WDA is a per-window property and cannot protect the game's frame.
    //
    // So this path stands down while protection is on. The two paths are mutually
    // exclusive by design; see overlay.cpp for the other half.
    if (::g_captureProtect) return;

    if (!canvasObj) return;
    if (!g_settings.enabled) return;
    if (!ue::GObjectsReady() && !ue::Init()) return;

    // -----------------------------------------------------------------------
    // NO IMGUI HERE.
    //
    // The first version called ImGui::GetIO() for the display size. That crashed
    // every frame: this hook is installed during boot, BEFORE the overlay thread
    // creates its ImGui context, so GetCurrentContext() was null and GetIO()
    // dereferenced it. The SEH guard swallowed the fault, which is why the only
    // symptom was "draw raised; swallowed" 26 ms after install and nothing drawn.
    //
    // The canvas knows its own size -- read it straight from UCanvas.
    //   +0x30 ClipX  +0x34 ClipY  (the drawable area)
    //   +0x40 SizeX  +0x44 SizeY
    // -----------------------------------------------------------------------
    float W = (float)ue::RI32((uintptr_t)canvasObj + 0x40);
    float H = (float)ue::RI32((uintptr_t)canvasObj + 0x44);
    if (W < 16.0f || H < 16.0f) {
        W = (float)ue::RI32((uintptr_t)canvasObj + 0x30);   // ClipX
        H = (float)ue::RI32((uintptr_t)canvasObj + 0x34);   // ClipY
    }
    if (W < 16.0f || H < 16.0f) { W = 1920.0f; H = 1080.0f; }
    ue::SetScreenSize((int)W, (int)H);
    ue::BeginFrame();              // camera sampled once, this frame

    std::vector<Entity> ents;
    Collect(ents);
    if (ents.empty()) return;
    s_lastEnts = ents;             // publish for the aim module

    // ------------------------------------------------------------------
    // CAMERA HOLD -- the POV read flickers for a frame or two during
    // spectate cuts / respawn transitions. Dropping the whole frame then
    // was the "boxes flash off for an instant" (NO_CAMERA) bug. Keep the
    // last good camera for up to 500 ms and draw through the gap.
    // ------------------------------------------------------------------
    ue::Camera cam;
    static ue::Camera s_camHold{};
    static ULONGLONG  s_camHoldAt = 0;
    if (ue::GetCamera(cam) && cam.valid) {
        s_camHold = cam; s_camHoldAt = GetTickCount64();
    } else if (s_camHold.valid && GetTickCount64() - s_camHoldAt <= 500) {
        cam = s_camHold;
    } else {
        static ULONG64 s_next = 0;
        const ULONG64 n2 = GetTickCount64();
        if (n2 >= s_next) {
            s_next = n2 + 2000;
            // Walk the camera chain by hand so the failing LINK is named instead
            // of guessed at. Logging only "NO_CAMERA" cannot distinguish
            // "no world" from "no controller" from "no camera manager" from
            // "the cached POV slot went stale" -- all four are possible here.
            const uintptr_t w  = ue::GetWorld();
            const uintptr_t pc = ue::GetLocalPlayerController();
            const uintptr_t pw = ue::GetLocalPawn();
            uintptr_t pcm = 0;
            if (ue::PtrOk(pc)) pcm = ue::R64(pc + off::APC_PlayerCameraManager);
            LOG(SBOX("[esp/ingame] NO_CAMERA ents=%d world=0x%llX pc=0x%llX pcm=0x%llX pawn=0x%llX", 0xB0),
                (int)ents.size(),
                (unsigned long long)w, (unsigned long long)pc,
                (unsigned long long)pcm, (unsigned long long)pw);
        }
        return;
    }

    const uint8_t localTeam = LocalTeam();
    const uintptr_t myPawn = LocalPawnHeld();

    canvas::BeginFrame();
    int drawn = 0;
    int projected = 0;

    // -----------------------------------------------------------------------
    // NAMETAGS -- collected here, drawn after the boxes.
    //
    // The ESP already knows every entity's screen position, health, armour and
    // distance, so the tag is built from that data rather than re-projecting.
    // Re-projecting would double the cost and, worse, could disagree with the box
    // about where the body is -- the exact class of bug that made the skeleton and
    // the box drift apart.
    //
    // Drawn AFTER the boxes so a panel never covers a box outline.
    // -----------------------------------------------------------------------
    nametags::Begin();

    for (const Entity& e : ents) {
        if (ue::PtrOk(myPawn) && e.addr == myPawn) continue;   // never our own body
        DrawOneInGame(canvasObj, e, cam, localTeam, W, H);
        ++drawn;

        // ---- queue the nametag -------------------------------------------
        if (nametags::g_style.enabled && e.alive && e.name[0]) {
            // Anchor above the head, at the reference's 0.35 body-height offset.
            // headZ already carries the +16 uu the box uses, so the tag sits just
            // clear of the box top.
            float topZ = e.bonesOk ? (e.headZ + 16.0f) : (e.z + 90.0f);
            const float bodyH = e.bonesOk ? (topZ - e.feetZ) : 180.0f;
            const float tagZ  = topZ + bodyH * nametags::g_style.offsetAbove;

            const float ax = e.bonesOk ? e.chestX : e.x;
            const float ay = e.bonesOk ? e.chestY : e.y;

            float sx, sy;
            if (canvas::Project(canvasObj, ax, ay, tagZ, sx, sy)) {
                nametags::Entry ne{};
                ne.x = sx;
                ne.y = sy;
                ne.name = e.name;
                ne.hp = e.hp;
                ne.hpMax = e.hpMax;
                ne.armor = e.armor;
                ne.armorMax = e.armorMax;
                ne.distance = e.distance / 100.0f;   // uu -> metres
                ne.friendly = (e.team == localTeam);
                nametags::Add(ne);
            }
        }
    }
    s_lastTargets = drawn;

    // Nametags flush after the boxes, using the engine's own font.
    nametags::Flush(canvasObj, features::gWorld.font);

    // =======================================================================
    // ADDITIVE FEATURES
    //
    // Everything below draws ON TOP of the verified ESP and never feeds back into
    // it. Box geometry, projection and the aim source of truth all come from the
    // code above; these only add the skeleton, radar and FOV ring.
    //
    // Keeping them strictly additive is the point: the previous attempt let this
    // code OWN the box and the result was a box that drifted upward. A feature
    // here must be switchable without the ESP changing at all.
    // =======================================================================
    // -----------------------------------------------------------------------
    // GAME-SYSTEM FEATURES -- use the game's own renderer, not our own drawing.
    // -----------------------------------------------------------------------
    {
        const uintptr_t w = ue::GetWorld();

        // ---- anti-stealth ----
        // Ask the game which actors IT considers stealthed. Those stay visible to
        // us even while the game renders them invisible. Strictly better than
        // probing opacity or a visibility bool: those differ per ability, whereas
        // this is the system that owns the decision.
        if (features::g.antiStealth) {
            game_systems::RefreshStealth(w);
            static ULONGLONG s_censusNext = 0;
            const ULONGLONG now = GetTickCount64();
            if (now >= s_censusNext) {
                s_censusNext = now + 15000;
                game_systems::LogStealthCensus(w);   // periodic, for verification
            }
        }

        // ---- outline / wall view ----
        // Toggle the game's own skeletal outline on each valid target. Applied on a
        // 500 ms lane because each call walks the component's material state --
        // doing it every frame for every actor would cost real frame time.
        if (features::g.outline) {
            static ULONGLONG s_outlineNext = 0;
            const ULONGLONG now = GetTickCount64();
            if (now >= s_outlineNext) {
                s_outlineNext = now + 500;
                for (const Entity& e : ents) {
                    if (ue::PtrOk(myPawn) && e.addr == myPawn) continue;
                    if (!e.alive) continue;
                    if (e.team >= 0 && localTeam != 0xFF && e.team == localTeam) continue;
                    game_systems::SetActorOutline(e.addr, true);
                }
            }
        }
    }

    features::DrawFovCircle(canvasObj);      // centred on ClipX/ClipY
    features::DrawRadar(canvasObj);

    // -------------------------------------------------------------------
    // STATUS STRIP -- lives IN-GAME now. The overlay stops presenting while
    // the menu is closed (third dxgi crash), so its old ImGui strip would
    // either freeze on screen or vanish; this is its replacement.
    // -------------------------------------------------------------------
    if (void* font = features::gWorld.font) {
        char strip[96];
        _snprintf_s(strip, sizeof(strip), _TRUNCATE,
                    "FUT  obj %d | tgt %d | INSERT menu",
                    ue::NumObjects(), s_lastTargets);
        features::draw::Text(canvasObj, font, strip, 10.0f, 8.0f,
                             sdk::LinearColor(0.55f, 0.75f, 1.0f, 0.9f), false);
    }

    if (features::g.espBones) {
        for (const Entity& e : ents) {
            if (ue::PtrOk(myPawn) && e.addr == myPawn) continue;
            if (!e.bonesOk) continue;
            features::DrawSkeletonFor(canvasObj, e.addr);
        }
    }

    static ULONG64 s_next3 = 0;
    const ULONG64 n3 = GetTickCount64();
    if (n3 >= s_next3) {
        s_next3 = n3 + 2000;
        int proj = 0;
        for (const Entity& e : ents) { float sx, sy; if (canvas::Project(canvasObj, e.x, e.y, e.z + 60.0f, sx, sy)) ++proj; }
        LOG(SBOX("[esp/ingame] stage=DRAW ents=%d drawn=%d proj=%d cmds=%d ok=%ld bad=%ld W=%.0f H=%.0f", 0xAD),
            (int)ents.size(), drawn, proj, canvas::CmdCount(), canvas::DrawOkCalls(), canvas::DrawBadCalls(), W, H);
    }
    // canvas::Flush runs in the hook right after this returns.
}

} // namespace esp
