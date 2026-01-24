#include "theme.h"

namespace ui {

void ApplyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Spacing
    style.WindowPadding = ImVec2(Spacing::WindowPadding, Spacing::WindowPadding);
    style.FramePadding = ImVec2(Spacing::FramePadding, Spacing::FramePadding);
    style.ItemSpacing = ImVec2(Spacing::ItemSpacing, Spacing::ItemSpacing);
    style.ItemInnerSpacing = ImVec2(Spacing::ItemInnerSpacing, Spacing::ItemInnerSpacing);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    
    // Rounding
    style.WindowRounding = Rounding::Window;
    style.ChildRounding = Rounding::Card;
    style.FrameRounding = Rounding::Frame;
    style.PopupRounding = Rounding::Popup;
    style.ScrollbarRounding = Rounding::ScrollBar;
    style.GrabRounding = Rounding::Frame;
    style.TabRounding = Rounding::Tab;
    
    // Borders - subtle
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    
    // Button text alignment
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    
    // Anti-aliasing
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    
    // Colors
    ImVec4* colors = style.Colors;
    
    colors[ImGuiCol_Text]                   = Colors::Foreground();
    colors[ImGuiCol_TextDisabled]           = Colors::MutedForeground();
    colors[ImGuiCol_WindowBg]               = Colors::Background();
    colors[ImGuiCol_ChildBg]                = Colors::Card();
    colors[ImGuiCol_PopupBg]                = Colors::Card();
    colors[ImGuiCol_Border]                 = Colors::Border();
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]                = Colors::Muted();
    colors[ImGuiCol_FrameBgHovered]         = Colors::Secondary();
    colors[ImGuiCol_FrameBgActive]          = Colors::Secondary();
    colors[ImGuiCol_TitleBg]                = Colors::Card();
    colors[ImGuiCol_TitleBgActive]          = Colors::Card();
    colors[ImGuiCol_TitleBgCollapsed]       = Colors::Card();
    colors[ImGuiCol_MenuBarBg]              = Colors::Card();
    colors[ImGuiCol_ScrollbarBg]            = Colors::Muted();
    colors[ImGuiCol_ScrollbarGrab]          = Colors::Secondary();
    colors[ImGuiCol_ScrollbarGrabHovered]   = Colors::MutedForeground();
    colors[ImGuiCol_ScrollbarGrabActive]    = Colors::Primary();
    colors[ImGuiCol_CheckMark]              = Colors::Primary();
    colors[ImGuiCol_SliderGrab]             = Colors::Primary();
    colors[ImGuiCol_SliderGrabActive]       = Colors::PrimaryDark();
    colors[ImGuiCol_Button]                 = Colors::Muted();
    colors[ImGuiCol_ButtonHovered]          = Colors::Secondary();
    colors[ImGuiCol_ButtonActive]           = Colors::Primary();
    colors[ImGuiCol_Header]                 = Colors::Muted();
    colors[ImGuiCol_HeaderHovered]          = Colors::Secondary();
    colors[ImGuiCol_HeaderActive]           = Colors::Primary();
    colors[ImGuiCol_Separator]              = Colors::Border();
    colors[ImGuiCol_SeparatorHovered]       = Colors::MutedForeground();
    colors[ImGuiCol_SeparatorActive]        = Colors::Primary();
    colors[ImGuiCol_ResizeGrip]             = Colors::Muted();
    colors[ImGuiCol_ResizeGripHovered]      = Colors::Secondary();
    colors[ImGuiCol_ResizeGripActive]       = Colors::Primary();
    colors[ImGuiCol_Tab]                    = Colors::Card();
    colors[ImGuiCol_TabHovered]             = Colors::Primary();
    colors[ImGuiCol_TabActive]              = Colors::Primary();
    colors[ImGuiCol_TabUnfocused]           = Colors::Card();
    colors[ImGuiCol_TabUnfocusedActive]     = Colors::Secondary();
    colors[ImGuiCol_PlotLines]              = Colors::Primary();
    colors[ImGuiCol_PlotLinesHovered]       = Colors::Accent();
    colors[ImGuiCol_PlotHistogram]          = Colors::Primary();
    colors[ImGuiCol_PlotHistogramHovered]   = Colors::Accent();
    colors[ImGuiCol_TableHeaderBg]          = Colors::Muted();
    colors[ImGuiCol_TableBorderStrong]      = Colors::Border();
    colors[ImGuiCol_TableBorderLight]       = ColorWithAlpha(Colors::Border(), 0.5f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]          = ColorWithAlpha(Colors::Muted(), 0.3f);
    colors[ImGuiCol_TextSelectedBg]         = ColorWithAlpha(Colors::Primary(), 0.35f);
    colors[ImGuiCol_DragDropTarget]         = Colors::Accent();
    colors[ImGuiCol_NavHighlight]           = Colors::Primary();
    colors[ImGuiCol_NavWindowingHighlight]  = Colors::Primary();
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);
}

bool LoadFonts(const char* fontPath, const char* monoFontPath, const char* iconFontPath) {
    ImGuiIO& io = ImGui::GetIO();
    
    ImFontConfig config;
    config.OversampleH = 3;  // Higher quality
    config.OversampleV = 3;
    config.PixelSnapH = true;
    
    // Try to load Satoshi font
    const char* satoshiPaths[] = {
        "fonts/Satoshi-Medium.ttf",
        "../fonts/Satoshi-Medium.ttf",
        "../../fonts/Satoshi-Medium.ttf",
        fontPath
    };
    
    bool fontLoaded = false;
    
    for (const char* path : satoshiPaths) {
        if (path == nullptr) continue;
        
        ImFont* font = io.Fonts->AddFontFromFileTTF(path, 16.0f, &config);
        if (font != nullptr) {
            fontLoaded = true;
            
            // Also load larger sizes for headings
            ImFontConfig largeConfig = config;
            io.Fonts->AddFontFromFileTTF(path, 24.0f, &largeConfig);
            io.Fonts->AddFontFromFileTTF(path, 32.0f, &largeConfig);
            break;
        }
    }
    
    // Fallback to default ImGui font if Satoshi not found
    if (!fontLoaded) {
        io.Fonts->AddFontDefault(&config);
    }
    
    return fontLoaded;
}

} // namespace ui
