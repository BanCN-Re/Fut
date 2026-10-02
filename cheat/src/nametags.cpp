// D:\Work\Fut\cheat\src\nametags.cpp
//
// NameTags panel, ported from the Remielle design.
// See nametags.h for what was ported and what had to change for UCanvas.
#include "nametags.h"
#include "canvas.h"
#include "features.h"
#include "sdk.h"
#include "log.h"
#include "obf.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace nametags {

Style g_style;

// ---------------------------------------------------------------------------
// queue
//
// Fixed capacity: the ESP caps entities at 256, and a nametag per entity is the
// ceiling. A static array avoids a per-frame allocation on the render thread.
// ---------------------------------------------------------------------------
static const int kMaxEntries = 256;
static Entry     s_entries[kMaxEntries];
static std::string s_names[kMaxEntries];    // owned copies; Entry::name is a view
static int       s_count = 0;

void Begin() { s_count = 0; }

void Add(const Entry& e) {
    if (s_count >= kMaxEntries) return;
    if (!e.name || !e.name[0]) return;
    s_entries[s_count] = e;
    s_names[s_count] = e.name;              // copy: the caller's buffer is reused
    s_entries[s_count].name = s_names[s_count].c_str();
    ++s_count;
}

// ---------------------------------------------------------------------------
// colour helpers
//
// The reference derives the health colour with Color.getHSBColor(ratio/3, 0.65, 1)
// -- hue sweeping red -> yellow -> green as the ratio rises. Ported directly so
// the palette matches.
// ---------------------------------------------------------------------------
static void HsbToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = h - floorf(h);                       // wrap to [0,1)
    const float i = floorf(h * 6.0f);
    const float f = h * 6.0f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    switch ((int)i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default:r = v; g = p; b = q; break;
    }
}

static void HealthColor(float ratio, float& r, float& g, float& b) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    HsbToRgb(ratio / 3.0f, 0.65f, 1.0f, r, g, b);
}

// ---------------------------------------------------------------------------
// primitives on top of UCanvas
//
// UCanvas has no rounded rectangle, so the radius is approximated: a filled
// rectangle inset by the radius, plus the corner squares removed by drawing the
// rectangle in three bands. At the 4 px radius used here the difference from a
// true rounded corner is sub-pixel at 1080p, and it costs no extra draw calls
// compared with a stencil.
//
// If the radius is 0 (or tiny) this collapses to a single fill.
// ---------------------------------------------------------------------------
static void FillRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (w <= 0.0f || h <= 0.0f || a <= 0.0f) return;
    canvas::AddFillRect(x, y, w, h, r, g, b, a);
}

// Panel with a soft shadow and approximated rounded corners.
static void Panel(float x, float y, float w, float h, float radius,
                  float r, float g, float b, float a) {
    if (w <= 0.0f || h <= 0.0f) return;

    // ---- shadow -----------------------------------------------------------
    // Drawn as a slightly larger rectangle at low alpha. The reference composites
    // a gaussian blur; a single expanded rect at ~1/3 the alpha reads the same at
    // this size and costs one draw instead of a blur pass.
    if (g_style.showShadow) {
        const float e = 2.0f;
        FillRect(x - e, y - e, w + e * 2.0f, h + e * 2.0f, 0.0f, 0.0f, 0.0f,
                 g_style.shadowAlpha * 0.55f);
    }

    // ---- body -------------------------------------------------------------
    if (radius <= 1.0f) {
        FillRect(x, y, w, h, r, g, b, a);
        return;
    }

    const float c = (radius < h * 0.5f) ? radius : h * 0.5f;
    // middle band: full width, inset vertically by the radius
    FillRect(x, y + c, w, h - c * 2.0f, r, g, b, a);
    // top and bottom bands: inset horizontally by the radius
    FillRect(x + c, y, w - c * 2.0f, c, r, g, b, a);
    FillRect(x + c, y + h - c, w - c * 2.0f, c, r, g, b, a);
}

// ---------------------------------------------------------------------------
// text
// ---------------------------------------------------------------------------
static void TextAt(void* cv, void* font, const std::string& s, float x, float y,
                   float r, float g, float b, float a, float scale) {
    if (!font || s.empty()) return;

    // Glow: the reference draws a 2 px blurred halo, which is what makes light
    // text readable over a bright scene. UCanvas has no blur, so the halo is a
    // 1 px offset copy at low alpha -- the same trick the reference's own
    // non-blur path falls back to.
    if (g_style.glow) {
        const float ga = a * 0.35f;
        features::draw::Text(cv, font, s, x + 1.0f, y + 1.0f, sdk::LinearColor(0, 0, 0, ga), true);
    }
    features::draw::Text(cv, font, s, x, y, sdk::LinearColor(r, g, b, a), true);
}

// UCanvas draws text with a font object at its own scale; measuring it exactly
// requires the engine's text size function, which is not worth a call per frame.
// A character-width estimate is used for panel sizing instead, which is what the
// panel actually needs -- the text itself is still centred by the engine.
static float EstimateWidth(const std::string& s, float scale) {
    // CJK glyphs are full-width, latin roughly half. Counting bytes above 0x7F
    // as full-width is accurate enough for a UTF-8 string in this font.
    float units = 0.0f;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((unsigned char)s[i] >= 0x80) {
            units += 1.0f;
            // skip the continuation bytes of this sequence
            while (i + 1 < s.size() && ((unsigned char)s[i + 1] & 0xC0) == 0x80) ++i;
        } else {
            units += 0.55f;
        }
    }
    return units * 9.0f * scale;      // 9 px per unit at scale 1
}

// ---------------------------------------------------------------------------
// flush
// ---------------------------------------------------------------------------
void Flush(void* canvasObj, void* font) {
    if (!g_style.enabled || !canvasObj) return;

    const float W = canvas::ClipX();
    const float H = canvas::ClipY();

    for (int i = 0; i < s_count; ++i) {
        const Entry& e = s_entries[i];

        float ratio = 1.0f;
        if (e.hpMax > 0.0f) ratio = e.hp / e.hpMax;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;

        float hr, hg, hb;
        HealthColor(ratio, hr, hg, hb);

        // ---- content strings -------------------------------------------------
        char hpText[48] = {0};
        char distText[32] = {0};
        if (g_style.showHealth && e.hpMax > 0.0f)
            _snprintf_s(hpText, sizeof(hpText), _TRUNCATE, "%.1f HP", e.hp);
        if (g_style.showDistance)
            _snprintf_s(distText, sizeof(distText), _TRUNCATE, "%.1f m", e.distance);

        const std::string nameStr = g_style.showName ? std::string(e.name) : std::string();

        // ---- measure ---------------------------------------------------------
        const float nameW   = EstimateWidth(nameStr, g_style.nameSize);
        const float hpW     = EstimateWidth(hpText, g_style.detailSize);
        const float distW   = EstimateWidth(distText, g_style.detailSize);

        float detailW = 0.0f;
        if (hpW > 0.0f)   detailW += hpW;
        if (distW > 0.0f) detailW += distW + 12.0f;     // the reference's gap

        float contentW = nameW;
        if (detailW > contentW) contentW = detailW;

        const float panelW = contentW + g_style.padX * 2.0f;
        const float panelH = g_style.panelHeight;
        const float px = e.x - panelW * 0.5f;
        const float py = e.y - panelH;

        // Off-screen cull: a panel partly outside the canvas still draws (the
        // engine clips it), but one fully outside is skipped.
        if (px > W || px + panelW < 0.0f || py > H || py + panelH < 0.0f) continue;

        // ---- panel -----------------------------------------------------------
        if (g_style.showPanel) {
            // Reference: rgb(7,12,18) at alpha 145.
            Panel(px, py, panelW, panelH, g_style.corner,
                  7.0f / 255.0f, 12.0f / 255.0f, 18.0f / 255.0f,
                  g_style.panelAlpha);
        }

        // ---- name ------------------------------------------------------------
        if (!nameStr.empty()) {
            float nr = 1.0f, ng = 1.0f, nb = 1.0f;      // reference uses white
            if (g_style.teamColorName && e.friendly) {
                nr = 0x55 / 255.0f; ng = 0xFF / 255.0f; nb = 0x77 / 255.0f;
            }
            TextAt(canvasObj, font, nameStr,
                   e.x, py + g_style.padY, nr, ng, nb, 1.0f, g_style.nameSize);
        }

        // ---- detail line: health then distance -------------------------------
        {
            const float detailY = py + g_style.padY + 12.0f;
            float cx = e.x - detailW * 0.5f;

            if (hpW > 0.0f) {
                TextAt(canvasObj, font, hpText, cx + hpW * 0.5f, detailY,
                       hr, hg, hb, 1.0f, g_style.detailSize);
                cx += hpW;
            }
            if (distW > 0.0f) {
                // Reference MUTED = rgb(185,195,207)
                TextAt(canvasObj, font, distText, cx + 12.0f + distW * 0.5f, detailY,
                       185.0f / 255.0f, 195.0f / 255.0f, 207.0f / 255.0f,
                       1.0f, g_style.detailSize);
            }
        }

        // ---- health bar ------------------------------------------------------
        if (g_style.showBar && e.hpMax > 0.0f) {
            const float barW = panelW - g_style.padX * 2.0f;
            const float barX = px + g_style.padX;
            const float barY = py + panelH - 5.0f;      // reference: height - 5

            // track: white at alpha 35
            Panel(barX, barY, barW, g_style.barHeight, 1.0f, 1.0f, 1.0f, 1.0f,
                  35.0f / 255.0f);
            if (ratio > 0.0f) {
                Panel(barX, barY, barW * ratio, g_style.barHeight, 1.0f,
                      hr, hg, hb, 1.0f);
            }
        }

        // ---- armour, as a second thinner bar ---------------------------------
        //
        // The reference shows absorption as text ("+X.X"); this title's equivalent
        // is armour, and armour is what the user asked to see. Drawn as a second
        // track directly under the health bar so the panel does not grow.
        if (g_style.showBar && e.armorMax > 0.0f && e.armor > 0.0f) {
            float ar = e.armor / e.armorMax;
            if (ar < 0.0f) ar = 0.0f;
            if (ar > 1.0f) ar = 1.0f;
            const float barW = panelW - g_style.padX * 2.0f;
            const float barX = px + g_style.padX;
            const float barY = py + panelH - 2.0f;
            Panel(barX, barY, barW, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 30.0f / 255.0f);
            // armour blue-white, distinct from the health ramp
            Panel(barX, barY, barW * ar, 1.5f, 1.0f, 0.78f, 0.86f, 0.95f, 0.9f);
        }
    }
}

} // namespace nametags
