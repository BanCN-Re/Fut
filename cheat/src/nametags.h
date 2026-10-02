// D:\Work\Fut\cheat\src\nametags.h
#pragma once
//
// NameTags -- ported from the Remielle design (github.com/adkmc/Remielle,
// module/impl/render/Nametags.java) to this engine's UCanvas.
//
// WHAT IS PORTED, AND WHAT HAD TO CHANGE
//
// The reference is a Minecraft client mod, so the DATA is different (players with
// equipment slots, absorption health) while the DESIGN is what carries over:
//
//   * a rounded translucent panel behind the text, with a soft drop shadow
//   * name on the first line, centred, with a glow
//   * health in HP units and distance in metres on a second, smaller line
//   * a thin rounded health bar across the bottom of the panel, on a track
//   * health colour derived from the ratio via HSB
//
// The engine difference that matters: Minecraft draws through a 2D canvas with
// arbitrary rounded rectangles and a TrueType font. UCanvas offers only
// K2_DrawLine / K2_DrawBox / K2_DrawText / K2_DrawTexture. So:
//
//   rounded corner -> approximated by drawing the rectangle inset and adding a
//                     short line at each corner (see RoundRect below). At the
//                     1-4 px radii this UI uses, the difference is not visible.
//   drop shadow    -> a second, slightly larger panel drawn underneath at low
//                     alpha, which is what the reference's blur+shadow composited
//                     to anyway.
//   glow           -> a second text pass offset by 1 px at low alpha.
//   TrueType font  -> the engine's own UFont, so glyph coverage for CJK names is
//                     the game's problem rather than ours.
//
// WHY THIS IS SEPARATE FROM THE ESP BOX
//
// The box answers "where is the body". A nametag answers "who is it, how hurt, how
// far" and is placed ABOVE the head so it never overlaps the box. The reference
// treats them as separate modules for the same reason, and the user asked for the
// distance to be its own separate element.
#include <cstdint>
#include <vector>

namespace nametags {

struct Style {
    bool  enabled      = true;

    bool  showName     = true;
    bool  showHealth   = true;
    bool  showDistance = true;    // its own element, per the request
    bool  showBar      = true;    // the thin bar across the panel bottom

    bool  showPanel    = true;    // the rounded backdrop
    bool  showShadow   = true;    // drop shadow under the panel

    float panelAlpha   = 0.57f;   // 145/255 as in the reference
    float shadowAlpha  = 0.43f;   // 110/255

    float nameSize     = 1.0f;    // UCanvas scale multiplier
    float detailSize   = 0.85f;

    float offsetAbove  = 0.35f;   // body heights above the head (reference value)
    float padX         = 6.0f;    // horizontal padding inside the panel
    float padY         = 4.0f;    // top padding before the name

    // Reference geometry: 32 px panel, name at +4, details at +12, bar at the
    // bottom edge with a 2 px height.
    float panelHeight  = 32.0f;
    float barHeight    = 2.0f;
    float corner       = 4.0f;

    bool  showSelf     = false;
    bool  teamColorName = true;
    bool  glow         = true;
};

extern Style g_style;

// Build the per-frame list. Called from the ESP pass, which already knows every
// entity's screen position and metrics -- re-projecting here would double the
// projection cost and could disagree with the box.
struct Entry {
    float x, y;             // screen position of the anchor (above the head)
    const char* name;
    float hp, hpMax;
    float armor, armorMax;  // this title has armour; the reference has absorption
    float distance;         // metres
    bool  friendly;
};

void Begin();
void Add(const Entry& e);

// Emit everything queued. `font` is the engine UFont to draw text with; when it
// is null only the panel, bar and shadow are drawn (text silently skipped).
void Flush(void* canvasObj, void* font);

} // namespace nametags
