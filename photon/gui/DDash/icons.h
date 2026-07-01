#pragma once
/**
 * Vector icon drawing functions for the dashboard.
 * Each icon is drawn via ImDrawList path commands, derived from SVG sources.
 * All functions take a center position, size (icon fits in size×size box),
 * and color. They render resolution-independently.
 */

#include "imgui.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ui { namespace icons {

// Heart icon (from heart.svg)
// SVG path in 0–16 × 0–15 viewbox, filled heart shape.
inline void DrawHeart(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    float s = size * 0.5f;
    // Scale from SVG coords (0-16, 0-15) to [-s, +s]
    float sx = s / 8.0f;   // half SVG width = 8
    float sy = s / 7.5f;   // half SVG height = 7.5
    float ox = center.x;
    float oy = center.y + s * 0.1f; // shift down slightly (SVG center is ~7)

    // Convert SVG coord (0-16, 0-15) to screen
    auto P = [&](float x, float y) -> ImVec2 {
        return ImVec2(ox + (x - 8.0f) * sx, oy + (y - 7.5f) * sy);
    };

    dl->PathClear();
    // Start at bottom point
    dl->PathLineTo(P(8.0f, 15.0f));
    // Left side up
    dl->PathLineTo(P(1.24f, 8.24f));
    // Left lobe (cubic bezier approximation)
    dl->PathBezierCubicCurveTo(P(0.45f, 7.45f), P(0.0f, 6.37f), P(0.0f, 5.24f));
    dl->PathBezierCubicCurveTo(P(0.0f, 2.81f), P(1.81f, 1.0f), P(4.05f, 1.0f));
    dl->PathBezierCubicCurveTo(P(5.28f, 1.0f), P(6.45f, 1.56f), P(7.22f, 2.52f));
    dl->PathLineTo(P(8.0f, 3.5f));
    // Right lobe
    dl->PathLineTo(P(8.78f, 2.52f));
    dl->PathBezierCubicCurveTo(P(9.55f, 1.56f), P(10.72f, 1.0f), P(11.95f, 1.0f));
    dl->PathBezierCubicCurveTo(P(14.19f, 1.0f), P(16.0f, 2.81f), P(16.0f, 5.05f));
    dl->PathBezierCubicCurveTo(P(16.0f, 6.37f), P(15.55f, 7.45f), P(14.76f, 8.24f));
    // Close back to bottom
    dl->PathLineTo(P(8.0f, 15.0f));
    dl->PathFillConvex(color);
}

// Left arrow (from left-arrow.svg)
// SVG: filled triangle pointing left in 0–24 × 0–24 viewbox
inline void DrawLeftArrow(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    float s = size * 0.5f;
    // Triangle vertices from SVG: (19,3)→(19,21)→(5,12)
    float sx = s / 12.0f;
    float sy = s / 12.0f;
    float ox = center.x;
    float oy = center.y;

    ImVec2 p1(ox + (19.0f - 12.0f) * sx, oy + (3.0f - 12.0f) * sy);   // top-right
    ImVec2 p2(ox + (19.0f - 12.0f) * sx, oy + (21.0f - 12.0f) * sy);  // bottom-right
    ImVec2 p3(ox + (5.0f - 12.0f) * sx,  oy + (12.0f - 12.0f) * sy);  // left tip

    dl->AddTriangleFilled(p1, p2, p3, color);
}

// Right arrow (from right-arrow.svg)
// SVG: filled triangle pointing right in 0–24 × 0–24 viewbox
inline void DrawRightArrow(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    float s = size * 0.5f;
    float sx = s / 12.0f;
    float sy = s / 12.0f;
    float ox = center.x;
    float oy = center.y;

    ImVec2 p1(ox + (5.0f - 12.0f) * sx,  oy + (3.0f - 12.0f) * sy);   // top-left
    ImVec2 p2(ox + (5.0f - 12.0f) * sx,  oy + (21.0f - 12.0f) * sy);  // bottom-left
    ImVec2 p3(ox + (19.0f - 12.0f) * sx, oy + (12.0f - 12.0f) * sy);  // right tip

    dl->AddTriangleFilled(p1, p2, p3, color);
}

// Brake icon (from break.svg)
// SVG: filled circle centered at (12,12) radius 10 in 0–24 viewbox
inline void DrawBrake(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    float r = size * 0.42f;  // 10/24 ≈ 0.42
    dl->AddCircleFilled(center, r, color, 32);
}

// Cruise Control (from CruiseControl.svg)
// Speedometer gauge with needle. Simplified from the complex SVG.
inline void DrawCruiseControl(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    float r = size * 0.42f;
    float thick = size * 0.06f;
    if (thick < 1.0f) thick = 1.0f;

    // Outer gauge ring (270° arc, open at bottom)
    float startA = (float)M_PI * 0.75f;   // 135°
    float endA   = (float)M_PI * 2.25f;   // 405°
    dl->PathArcTo(center, r, startA, endA, 48);
    dl->PathStroke(color, 0, thick * 1.5f);

    // Tick marks around the arc (8 ticks)
    for (int i = 0; i <= 8; i++) {
        float a = startA + (endA - startA) * (float)i / 8.0f;
        float innerR = r * 0.82f;
        float outerR = r * 0.95f;
        dl->AddLine(
            ImVec2(center.x + cosf(a) * innerR, center.y + sinf(a) * innerR),
            ImVec2(center.x + cosf(a) * outerR, center.y + sinf(a) * outerR),
            color, thick);
    }

    // Needle (pointing to ~10 o'clock position)
    float needleAngle = (float)M_PI * 1.1f;  // ~200°
    float needleLen = r * 0.65f;
    ImVec2 needleTip(center.x + cosf(needleAngle) * needleLen,
                     center.y + sinf(needleAngle) * needleLen);
    dl->AddLine(center, needleTip, color, thick * 2.0f);

    // Center dot
    dl->AddCircleFilled(center, thick * 2.0f, color, 12);
}

// Regen icon (from Regen.svg)
// Recycling triangle with lightning bolt. Simplified from the complex SVG.
inline void DrawRegen(ImDrawList* dl, ImVec2 center, float size, ImU32 color) {
    float r = size * 0.40f;
    float thick = size * 0.06f;
    if (thick < 1.0f) thick = 1.0f;

    // Three curved recycling arrows (120° apart)
    for (int i = 0; i < 3; i++) {
        float baseAngle = (float)M_PI * 2.0f * (float)i / 3.0f - (float)M_PI * 0.5f;
        float a1 = baseAngle - 0.35f;
        float a2 = baseAngle + 0.35f;

        // Arc segment
        dl->PathArcTo(center, r, a1, a2, 16);
        dl->PathStroke(color, 0, thick * 1.5f);

        // Arrowhead at end of arc
        float tipAngle = a2;
        float tipX = center.x + cosf(tipAngle) * r;
        float tipY = center.y + sinf(tipAngle) * r;
        float headSize = size * 0.1f;
        float perpAngle = tipAngle + (float)M_PI * 0.5f;
        ImVec2 tip(tipX + cosf(tipAngle) * headSize,
                   tipY + sinf(tipAngle) * headSize);
        ImVec2 left(tipX + cosf(perpAngle) * headSize * 0.5f,
                    tipY + sinf(perpAngle) * headSize * 0.5f);
        ImVec2 right(tipX - cosf(perpAngle) * headSize * 0.5f,
                     tipY - sinf(perpAngle) * headSize * 0.5f);
        dl->AddTriangleFilled(tip, left, right, color);
    }

    // Lightning bolt in center
    float bw = size * 0.08f;
    float bh = size * 0.22f;
    dl->PathClear();
    dl->PathLineTo(ImVec2(center.x + bw * 0.3f, center.y - bh));
    dl->PathLineTo(ImVec2(center.x - bw * 0.8f, center.y + bh * 0.1f));
    dl->PathLineTo(ImVec2(center.x + bw * 0.1f, center.y + bh * 0.1f));
    dl->PathLineTo(ImVec2(center.x - bw * 0.3f, center.y + bh));
    dl->PathLineTo(ImVec2(center.x + bw * 0.8f, center.y - bh * 0.1f));
    dl->PathLineTo(ImVec2(center.x - bw * 0.1f, center.y - bh * 0.1f));
    dl->PathLineTo(ImVec2(center.x + bw * 0.3f, center.y - bh));
    dl->PathFillConvex(color);
}

}}  // namespace ui::icons
