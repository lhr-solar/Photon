#include "style.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot3d.h"
#include "imnodes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

float mixf(float min, float max, float t) {
    return min + (max - min) * t;
}

float randomUnit() {
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}

std::array<float, 3> oklabToLinearSrgb(float L, float a, float b) {
    const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;

    return {
        +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
    };
}

std::array<float, 3> oklchToOklab(float L, float c, float h) {
    return {L, c * std::cos(h), c * std::sin(h)};
}

std::array<float, 3> hslToRgb(float h, float s, float l) {
    h = std::fmod(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    if (s == 0.0f) return {l, l, l};

    const auto hue2rgb = [](float p, float q, float t) {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 0.5f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };

    const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;
    return {
        hue2rgb(p, q, h + 1.0f / 3.0f),
        hue2rgb(p, q, h),
        hue2rgb(p, q, h - 1.0f / 3.0f),
    };
}

ImVec4 tintTowardWhite(ImVec4 color, float amount) {
    return ImVec4(
        color.x + (1.0f - color.x) * amount,
        color.y + (1.0f - color.y) * amount,
        color.z + (1.0f - color.z) * amount,
        1.0f
    );
}

float seedToHueBase(ImVec4 seed) {
    const float r = std::clamp(seed.x, 0.0f, 1.0f);
    const float g = std::clamp(seed.y, 0.0f, 1.0f);
    const float b = std::clamp(seed.z, 0.0f, 1.0f);
    const float maxv = std::max({r, g, b});
    const float minv = std::min({r, g, b});
    const float delta = maxv - minv;
    if (delta <= 0.0f) return 0.0f;

    float hue = 0.0f;
    if (maxv == r) hue = std::fmod((g - b) / delta, 6.0f);
    else if (maxv == g) hue = (b - r) / delta + 2.0f;
    else hue = (r - g) / delta + 4.0f;

    hue /= 6.0f;
    if (hue < 0.0f) hue += 1.0f;
    return hue;
}

std::array<float, 3> seedToHsv(ImVec4 seed) {
    const float r = std::clamp(seed.x, 0.0f, 1.0f);
    const float g = std::clamp(seed.y, 0.0f, 1.0f);
    const float b = std::clamp(seed.z, 0.0f, 1.0f);
    const float maxv = std::max({r, g, b});
    const float minv = std::min({r, g, b});
    const float delta = maxv - minv;
    const float saturation = maxv <= 0.0f ? 0.0f : delta / maxv;
    return {seedToHueBase(seed), saturation, maxv};
}

void Style::setStyle(){
    ImGuiStyle &style = ImGui::GetStyle();
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.55f;
    style.TabRounding = 0.0f;
    style.WindowRounding = 00.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowPadding = ImVec2{18.0f, 16.0f};
    style.FramePadding = ImVec2{12.0f, 9.0f};
    style.CellPadding = ImVec2{14.0f, 8.0f};
    style.ItemSpacing = ImVec2{10.0f, 8.0f};
    style.ItemInnerSpacing = ImVec2{8.0f, 6.0f};
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    auto color0 = colorScheme.color0;
    auto color1 = colorScheme.color1;
    auto color2 = colorScheme.color2;
    auto color3 = colorScheme.color3;
    auto color4 = colorScheme.color4;
    auto color5 = colorScheme.color5;
    auto color6 = colorScheme.color6;
    auto color7 = colorScheme.color7;
    ImVec4 red = {1.0f, 0.0, 0.0f, 1.0};

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = color0;
    colors[ImGuiCol_TextDisabled] = color3;
    colors[ImGuiCol_WindowBg] = {color7.x, color7.y, color7.z, 1.0};
    colors[ImGuiCol_ChildBg] = color7;
    colors[ImGuiCol_PopupBg] = color7;
    colors[ImGuiCol_Border] = color5;
    colors[ImGuiCol_BorderShadow] = color7;
    colors[ImGuiCol_FrameBg] = color4;
    colors[ImGuiCol_FrameBgHovered] = color3;
    colors[ImGuiCol_FrameBgActive] = color4;
    colors[ImGuiCol_TitleBg] = color6;
    colors[ImGuiCol_TitleBgActive] = color5;
    colors[ImGuiCol_TitleBgCollapsed] = color7;
    colors[ImGuiCol_MenuBarBg] = color5;
    colors[ImGuiCol_ScrollbarBg] = color2;
    colors[ImGuiCol_ScrollbarGrab] = color3;
    colors[ImGuiCol_ScrollbarGrabHovered] = color1;
    colors[ImGuiCol_ScrollbarGrabActive] = color0;
    colors[ImGuiCol_CheckMark] = color1;
    colors[ImGuiCol_SliderGrab] = color2;
    colors[ImGuiCol_SliderGrabActive] = color1;
    colors[ImGuiCol_Button] = color4;
    colors[ImGuiCol_ButtonHovered] = color3;
    colors[ImGuiCol_ButtonActive] = color7;
    colors[ImGuiCol_Header] = color4;
    colors[ImGuiCol_HeaderHovered] = color3;
    colors[ImGuiCol_HeaderActive] = color2;
    colors[ImGuiCol_Separator] = color3;
    colors[ImGuiCol_SeparatorHovered] = color2;
    colors[ImGuiCol_SeparatorActive] = color1;
    colors[ImGuiCol_ResizeGrip] = color2;
    colors[ImGuiCol_ResizeGripHovered] = color1;
    colors[ImGuiCol_ResizeGripActive] = color0;
    colors[ImGuiCol_Tab] = color7;
    colors[ImGuiCol_TabHovered] = color3;
    colors[ImGuiCol_TabActive] = color4;
    colors[ImGuiCol_TabUnfocused] = color7;
    colors[ImGuiCol_TabUnfocusedActive] = color4;
    colors[ImGuiCol_TabSelectedOverline] = color4;
    colors[ImGuiCol_DockingPreview] = color1;
    colors[ImGuiCol_DockingEmptyBg] = color1;
    colors[ImGuiCol_PlotLines] = color1;
    colors[ImGuiCol_PlotLinesHovered] = color2;
    colors[ImGuiCol_PlotHistogram] = color1;
    colors[ImGuiCol_PlotHistogramHovered] = color2;
    colors[ImGuiCol_TableHeaderBg] = color3;
    colors[ImGuiCol_TableBorderStrong] = color4;
    colors[ImGuiCol_TableBorderLight] = color4;
    colors[ImGuiCol_TableRowBg] = color5;
    colors[ImGuiCol_TableRowBgAlt] = color6;
    colors[ImGuiCol_TextSelectedBg] = color3;
    colors[ImGuiCol_DragDropTarget] = color3;
    colors[ImGuiCol_NavHighlight] = color1;
    colors[ImGuiCol_NavWindowingHighlight] = color1;
    colors[ImGuiCol_NavWindowingDimBg] = {color1.x, color1.y, color1.z, 0.2f};
    colors[ImGuiCol_ModalWindowDimBg] = {color0.x, color0.y, color0.z, 0.0f};

    ImPlotStyle &plotStyle = ImPlot::GetStyle();
    colors = plotStyle.Colors;
    colors[ImPlotCol_FrameBg] = color5;
    colors[ImPlotCol_PlotBg] = color4;
    colors[ImPlotCol_PlotBorder] = color5;
    colors[ImPlotCol_LegendBg] = color4;
    colors[ImPlotCol_LegendBorder] = color5;
    colors[ImPlotCol_LegendText] = color0;
    colors[ImPlotCol_TitleText] = color0;
    colors[ImPlotCol_InlayText] = color1;
    colors[ImPlotCol_AxisText] = color1;
    colors[ImPlotCol_AxisGrid] = color1;
    colors[ImPlotCol_AxisTick] = color1;
    colors[ImPlotCol_AxisBg] = color5;
    colors[ImPlotCol_AxisBgHovered] = color4;
    colors[ImPlotCol_AxisBgActive] = color3;
    colors[ImPlotCol_Selection] = color1;
    colors[ImPlotCol_Crosshairs] = color1;

    ImPlot3DStyle &tplotStyle = ImPlot3D::GetStyle();
    colors = tplotStyle.Colors;
    colors[ImPlot3DCol_TitleText] = color0;
    colors[ImPlot3DCol_InlayText] = color1;
    colors[ImPlot3DCol_FrameBg] = color5;
    colors[ImPlot3DCol_PlotBg] = color4;
    colors[ImPlot3DCol_PlotBorder] = color5;
    colors[ImPlot3DCol_LegendBg] = color4;
    colors[ImPlot3DCol_LegendBorder] = color5;
    colors[ImPlot3DCol_LegendText] = color0;
    colors[ImPlot3DCol_AxisText] = color1;
    colors[ImPlot3DCol_AxisGrid] = color1;
    colors[ImPlot3DCol_AxisTick] = color1;

    ImNodesStyle &nodeStyle = ImNodes::GetStyle();
    const auto packNodeColor = [](const ImVec4 &color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    };

    nodeStyle.Colors[ImNodesCol_NodeBackground] = packNodeColor(color5);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundHovered] = packNodeColor(color4);
    nodeStyle.Colors[ImNodesCol_NodeBackgroundSelected] = packNodeColor(color3);
    nodeStyle.Colors[ImNodesCol_NodeOutline] = packNodeColor(color5);
    nodeStyle.Colors[ImNodesCol_TitleBar] = packNodeColor(color5);
    nodeStyle.Colors[ImNodesCol_TitleBarHovered] = packNodeColor(color4);
    nodeStyle.Colors[ImNodesCol_TitleBarSelected] = packNodeColor(color3);
    nodeStyle.Colors[ImNodesCol_Link] = packNodeColor(color2);
    nodeStyle.Colors[ImNodesCol_LinkHovered] = packNodeColor(color1);
    nodeStyle.Colors[ImNodesCol_LinkSelected] = packNodeColor(color0);
    nodeStyle.Colors[ImNodesCol_Pin] = packNodeColor(color2);
    nodeStyle.Colors[ImNodesCol_PinHovered] = packNodeColor(color0);
    // so you can see what you are selecting
    nodeStyle.Colors[ImNodesCol_BoxSelector] = packNodeColor({color0.x, color0.y, color0.z, 0.2});
    nodeStyle.Colors[ImNodesCol_BoxSelectorOutline] = packNodeColor(color1);
    nodeStyle.Colors[ImNodesCol_GridBackground] = packNodeColor(color7);
    nodeStyle.Colors[ImNodesCol_GridLine] = packNodeColor(color4);
    nodeStyle.Colors[ImNodesCol_GridLinePrimary] = packNodeColor(color3);

    nodeStyle.Colors[ImNodesCol_MiniMapBackground] = packNodeColor(color4);
    nodeStyle.Colors[ImNodesCol_MiniMapBackgroundHovered] = packNodeColor(color6);
    nodeStyle.Colors[ImNodesCol_MiniMapOutline] = packNodeColor(color4);
    nodeStyle.Colors[ImNodesCol_MiniMapOutlineHovered] = packNodeColor(color1);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackground] = packNodeColor(color3);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundHovered] = packNodeColor(color2);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeBackgroundSelected] = packNodeColor(color1);
    nodeStyle.Colors[ImNodesCol_MiniMapNodeOutline] = packNodeColor(color4);
    nodeStyle.Colors[ImNodesCol_MiniMapLink] = packNodeColor(color1);
    nodeStyle.Colors[ImNodesCol_MiniMapLinkSelected] = packNodeColor(color0);
    // otherwise minimap not visible
    nodeStyle.Colors[ImNodesCol_MiniMapCanvas] = packNodeColor({0.0, 0.0, 0.0, 0.0});
    nodeStyle.Colors[ImNodesCol_MiniMapCanvasOutline] = packNodeColor(color3);
};

//returns a random "colorScheme"
//as specified by acerola color generator
ColorScheme Style::genColors(ImVec4 seed){
    struct Settings {
        float hueBase;
        float hueContrast;
        float saturationBase;
        float saturationContrast;
        float luminanceBase;
        float luminanceContrast;
        float fixed;
        bool saturationConstant;
        int colorCount;
    };

    const auto hsv = seedToHsv(seed);

    Settings settings{
        .hueBase = hsv[0],
        .hueContrast = 0.8,//randomUnit(),
        .saturationBase = randomUnit(),
        .saturationContrast = randomUnit(),
        .luminanceBase = hsv[2],
        .luminanceContrast = 0.8,//randomUnit(),
        .fixed = 0.1,//hsv[1],
        .saturationConstant = true,
        .colorCount = 8,
    };

    constexpr float kPi = 3.14159265358979323846f;
    const float hueBase = settings.hueBase * 2.0f * kPi;
    const float hueContrast = mixf(0.33f, 1.0f, settings.hueContrast);
    const float chromaBase = mixf(0.01f, 0.1f, settings.saturationBase);
    const float chromaContrast = mixf(0.075f, 0.125f - chromaBase, settings.saturationContrast);
    const float chromaFixed = mixf(0.01f, 0.125f, settings.fixed);
    const float lightnessBase = mixf(0.3f, 0.6f, settings.luminanceBase);
    const float lightnessContrast = mixf(0.3f, 1.0f - lightnessBase, settings.luminanceContrast);
    const float lightnessFixed = mixf(0.6f, 0.9f, settings.fixed);

    ColorScheme scheme{};
    ImVec4* colors[] = {
        &scheme.color7, &scheme.color6, &scheme.color5, &scheme.color4,
        &scheme.color3, &scheme.color2, &scheme.color1, &scheme.color0
    };

    for (int i = 0; i < settings.colorCount; ++i) {
        const float linearIterator = static_cast<float>(i) / static_cast<float>(settings.colorCount - 1);
        float hueOffset = linearIterator * hueContrast * 2.0f * kPi + (kPi / 4.0f);
        hueOffset *= 0.33f;
        hueOffset += (randomUnit() * 2.0f - 1.0f) * 0.01f;

        float chroma = chromaBase + linearIterator * chromaContrast;
        float lightness = lightnessBase + linearIterator * lightnessContrast;
        if (settings.saturationConstant) chroma = chromaFixed;
        else lightness = lightnessFixed;

        const auto lab = oklchToOklab(lightness, chroma, hueBase + hueOffset);
        auto rgb = oklabToLinearSrgb(lab[0], lab[1], lab[2]);
        rgb[0] = std::clamp(rgb[0], 0.0f, 1.0f);
        rgb[1] = std::clamp(rgb[1], 0.0f, 1.0f);
        rgb[2] = std::clamp(rgb[2], 0.0f, 1.0f);
        *colors[i] = ImVec4(rgb[0], rgb[1], rgb[2], 1.0f);
    }

    scheme.color0 = tintTowardWhite(scheme.color0, 0.9f);

    return scheme;
};

void Style::colorUI() {
    auto color0 = colorScheme.color0;
    auto color1 = colorScheme.color1;
    auto color2 = colorScheme.color2;
    auto color3 = colorScheme.color3;
    auto color4 = colorScheme.color4;
    auto color5 = colorScheme.color5;
    auto color6 = colorScheme.color6;
    auto color7 = colorScheme.color7;
    const ImVec4 cv[] = { color0, color1, color2, color3, color4, color5, color6, color7 };

    if (ImGui::BeginPopupModal("Theme")) {
        constexpr float hueMax = 0.999f;
        constexpr float valueMin = 0.001f;
        constexpr float gap = 12.0f;
        constexpr float barWidth = 18.0f;
        constexpr float paletteSize = 72.0f;
        constexpr float rounding = 6.0f;
        ImGui::SeparatorText("UI");
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        const float buttonWidth = std::max({
            ImGui::CalcTextSize("Default").x,
            ImGui::CalcTextSize("Light").x,
            ImGui::CalcTextSize("Custom").x
        }) + pad.x * 2.0f;
        const float buttonHeight = ImGui::GetFrameHeight();
        if(ImGui::Button("Default", {buttonWidth, buttonHeight})) selectedColor = dark; ImGui::SameLine();
        if(ImGui::Button("Light", {buttonWidth, buttonHeight})) selectedColor = light; ImGui::SameLine();
        if(ImGui::Button("Custom", {buttonWidth, buttonHeight})) selectedColor = custom;
        if(selectedColor == dark) colorScheme = baseColors;
        if(selectedColor == light) colorScheme = lightMode;
        if(selectedColor == custom) colorScheme = genColors(colorSeed);
        float hue = 0.0f;
        float saturation = 0.0f;
        float value = 0.0f;
        ImGui::ColorConvertRGBtoHSV(colorSeed.x, colorSeed.y, colorSeed.z, hue, saturation, value);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float size = 180.0f;
        ImGui::InvisibleButton("##HueValueField", {size, size});
        if(ImGui::IsItemActive() || ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            ImVec2 m = ImGui::GetIO().MousePos;
            hue = std::clamp((m.x - p.x) / size, 0.0f, hueMax);
            value = std::clamp(1.0f - (m.y - p.y) / size, valueMin, 1.0f);
        }
        const ImU32 hues[] = {
            IM_COL32(255,   0,   0, 255), IM_COL32(255, 255,   0, 255),
            IM_COL32(  0, 255,   0, 255), IM_COL32(  0, 255, 255, 255),
            IM_COL32(  0,   0, 255, 255), IM_COL32(255,   0, 255, 255),
            IM_COL32(255,   0,   0, 255)
        };
        for(int i = 0; i < 6; ++i){
            float x0 = p.x + size * (static_cast<float>(i) / 6.0f);
            float x1 = p.x + size * (static_cast<float>(i + 1) / 6.0f);
            draw->AddRectFilledMultiColor(
                {x0, p.y}, {x1, p.y + size},
                hues[i], hues[i + 1], hues[i + 1], hues[i]
            );
        }
        draw->AddRectFilledMultiColor(
            p, {p.x + size, p.y + size},
            0, 0, IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255)
        );
        draw->AddRect(p, {p.x + size, p.y + size}, IM_COL32(255, 255, 255, 64));
        draw->AddCircle({p.x + hue * size, p.y + (1.0f - value) * size}, 5.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);

        ImGui::SameLine(0.0f, gap);
        ImVec2 huePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##HueBar", {barWidth, size});
        if(ImGui::IsItemActive() || ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            ImVec2 m = ImGui::GetIO().MousePos;
            hue = std::clamp((m.y - huePos.y) / size, 0.0f, hueMax);
        }
        for(int i = 0; i < 6; ++i){
            float y0 = huePos.y + size * (static_cast<float>(i) / 6.0f);
            float y1 = huePos.y + size * (static_cast<float>(i + 1) / 6.0f);
            draw->AddRectFilledMultiColor(
                {huePos.x, y0}, {huePos.x + barWidth, y1},
                hues[i], hues[i], hues[i + 1], hues[i + 1]
            );
        }
        draw->AddRect(huePos, {huePos.x + barWidth, huePos.y + size}, IM_COL32(255, 255, 255, 64));
        draw->AddLine(
            {huePos.x, huePos.y + hue * size}, {huePos.x + barWidth, huePos.y + hue * size},
            IM_COL32(255, 255, 255, 255), 2.0f
        );

        ImGui::SameLine(0.0f, gap);
        ImVec2 valuePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##ValueBar", {barWidth, size});
        if(ImGui::IsItemActive() || ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            ImVec2 m = ImGui::GetIO().MousePos;
            value = std::clamp(1.0f - (m.y - valuePos.y) / size, valueMin, 1.0f);
        }
        float vr = 0.0f;
        float vg = 0.0f;
        float vb = 0.0f;
        ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, vr, vg, vb);
        draw->AddRectFilledMultiColor(
            valuePos, {valuePos.x + barWidth, valuePos.y + size},
            IM_COL32(static_cast<int>(vr * 255.0f), static_cast<int>(vg * 255.0f), static_cast<int>(vb * 255.0f), 255),
            IM_COL32(static_cast<int>(vr * 255.0f), static_cast<int>(vg * 255.0f), static_cast<int>(vb * 255.0f), 255),
            IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255)
        );
        draw->AddRect(valuePos, {valuePos.x + barWidth, valuePos.y + size}, IM_COL32(255, 255, 255, 64));
        draw->AddLine(
            {valuePos.x, valuePos.y + (1.0f - value) * size}, {valuePos.x + barWidth, valuePos.y + (1.0f - value) * size},
            IM_COL32(255, 255, 255, 255), 2.0f
        );

        ImGui::ColorConvertHSVtoRGB(hue, 1.0f, value, colorSeed.x, colorSeed.y, colorSeed.z);
        ImGui::SameLine(0.0f, gap);
        ImVec2 palettePos = ImGui::GetCursorScreenPos();
        for (int i = 0; i < 8; ++i) {
            ImVec2 min = {
                palettePos.x + (i % 4) * (paletteSize + gap),
                palettePos.y + (i / 4) * (paletteSize + gap)
            };
            ImVec2 max = {min.x + paletteSize, min.y + paletteSize};
            draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(cv[i]), rounding);
            draw->AddRect(min, max, IM_COL32(255, 255, 255, 32), rounding);
        }
        ImGui::Dummy({4.0f * paletteSize + 3.0f * gap, 2.0f * paletteSize + gap});
        ImGui::SeparatorText("Plots");
        ImPlot::ShowCustomColormapSelector("##ImPlotColormap");
        std::vector<double> time = {0, 5};
        std::vector<double> points = {0, 2};
        plotLineSpec.LineWeight = 4.0f;
        if(ImPlot::BeginPlot("##colorPalletePlot")){
            for(int i{0uz}; i < 10; i++){
                char buf[10];
                snprintf(buf, sizeof(buf), "##Color %i", i);
                ImPlot::PlotLine((const char*)buf, time.data(), points.data(), points.size(), plotLineSpec);
                points[1] += 2;
            }
            ImPlot::EndPlot();
        }
        if(ImGui::Button("Exit")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
