#pragma once

#include "imgui.h"

namespace ui {

/**
 * Theme constants matching the dark mode design from globals.css
 * oklch colors converted to sRGB for ImGui
 */
namespace Colors {
    // Core background/foreground - Pure black cosmic theme
    inline ImVec4 Background()         { return ImVec4(0.02f, 0.02f, 0.03f, 1.0f); }  // Nearly pure black
    inline ImVec4 Foreground()         { return ImVec4(0.95f, 0.95f, 0.97f, 1.0f); }  // Bright white text
    
    // Card/Panel colors - Very dark with subtle visibility
    inline ImVec4 Card()               { return ImVec4(0.06f, 0.06f, 0.08f, 1.0f); }  // Very dark gray
    inline ImVec4 CardForeground()     { return ImVec4(0.95f, 0.95f, 0.97f, 1.0f); }
    
    // Primary (vibrant cyan/plasma blue)
    inline ImVec4 Primary()            { return ImVec4(0.00f, 0.85f, 0.95f, 1.0f); }  // Bright cyan
    inline ImVec4 PrimaryDark()        { return ImVec4(0.00f, 0.65f, 0.75f, 1.0f); }
    inline ImVec4 PrimaryForeground()  { return ImVec4(0.02f, 0.02f, 0.03f, 1.0f); }
    
    // Secondary (dark gray)
    inline ImVec4 Secondary()          { return ImVec4(0.12f, 0.12f, 0.14f, 1.0f); }
    inline ImVec4 SecondaryForeground(){ return ImVec4(0.85f, 0.85f, 0.88f, 1.0f); }
    
    // Muted - Darker
    inline ImVec4 Muted()              { return ImVec4(0.08f, 0.08f, 0.10f, 1.0f); }
    inline ImVec4 MutedForeground()    { return ImVec4(0.50f, 0.50f, 0.55f, 1.0f); }
    
    // Accent (vibrant orange/plasma)
    inline ImVec4 Accent()             { return ImVec4(1.00f, 0.45f, 0.15f, 1.0f); }  // Bright orange
    inline ImVec4 AccentForeground()   { return ImVec4(0.02f, 0.02f, 0.03f, 1.0f); }
    
    // Status colors - Vibrant neon
    inline ImVec4 Success()            { return ImVec4(0.20f, 0.95f, 0.55f, 1.0f); }  // Neon green
    inline ImVec4 Warning()            { return ImVec4(1.00f, 0.80f, 0.00f, 1.0f); }  // Bright yellow
    inline ImVec4 Destructive()        { return ImVec4(1.00f, 0.25f, 0.30f, 1.0f); }  // Bright red
    inline ImVec4 Info()               { return ImVec4(0.40f, 0.60f, 1.00f, 1.0f); }  // Blue
    
    // Border - Subtle glow
    inline ImVec4 Border()             { return ImVec4(0.15f, 0.15f, 0.18f, 1.0f); }
    
    // Input
    inline ImVec4 Input()              { return ImVec4(0.08f, 0.08f, 0.10f, 1.0f); }
    
    // Transparent variants
    inline ImVec4 SuccessBg()          { return ImVec4(0.20f, 0.95f, 0.55f, 0.25f); }
    inline ImVec4 WarningBg()          { return ImVec4(1.00f, 0.80f, 0.00f, 0.25f); }
    inline ImVec4 DestructiveBg()      { return ImVec4(1.00f, 0.25f, 0.30f, 0.25f); }
    inline ImVec4 PrimaryBg()          { return ImVec4(0.00f, 0.85f, 0.95f, 0.20f); }
}

/**
 * Spacing and sizing constants
 */
namespace Spacing {
    constexpr float WindowPadding = 16.0f;
    constexpr float FramePadding = 8.0f;
    constexpr float ItemSpacing = 8.0f;
    constexpr float ItemInnerSpacing = 6.0f;
    constexpr float CardPadding = 16.0f;
    constexpr float SmallPadding = 8.0f;
}

/**
 * Rounding constants for modern UI look
 */
namespace Rounding {
    constexpr float Window = 10.0f;
    constexpr float Frame = 6.0f;
    constexpr float Popup = 8.0f;
    constexpr float ScrollBar = 6.0f;
    constexpr float Tab = 8.0f;
    constexpr float Card = 8.0f;
    constexpr float Button = 6.0f;
    constexpr float Badge = 4.0f;
    constexpr float ProgressBar = 4.0f;
}

/**
 * Font sizes (in pixels, assuming 1.0 scale)
 */
namespace FontSize {
    constexpr float Small = 11.0f;
    constexpr float Normal = 13.0f;
    constexpr float Medium = 15.0f;
    constexpr float Large = 18.0f;
    constexpr float XLarge = 24.0f;
    constexpr float Huge = 48.0f;
}

/**
 * Apply the dark theme to ImGui style
 * Call this once after ImGui context creation
 */
void ApplyTheme();

/**
 * Load fonts for the UI
 * Call this once after ImGui context creation, before rendering
 * 
 * @param fontPath Optional path to custom font file (nullptr for default)
 * @param monoFontPath Optional path to monospace font (nullptr for default)
 * @param iconFontPath Optional path to icon font to merge (nullptr to skip)
 * @return true if fonts loaded successfully
 * 
 * TODO: If fonts are not present in assets/fonts, falls back to ImGui default
 */
bool LoadFonts(const char* fontPath = nullptr, 
               const char* monoFontPath = nullptr,
               const char* iconFontPath = nullptr);

/**
 * Helper to convert ImVec4 color to ImU32 (for ImDrawList)
 */
inline ImU32 ColorToU32(const ImVec4& col) {
    return IM_COL32(
        static_cast<int>(col.x * 255.0f),
        static_cast<int>(col.y * 255.0f),
        static_cast<int>(col.z * 255.0f),
        static_cast<int>(col.w * 255.0f)
    );
}

/**
 * Create a color with modified alpha
 */
inline ImVec4 ColorWithAlpha(const ImVec4& col, float alpha) {
    return ImVec4(col.x, col.y, col.z, alpha);
}

} // namespace ui
