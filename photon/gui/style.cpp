#include "style.hpp"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "imnodes.h"

void Style::setStyle(colorScheme& colorScheme){
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

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = color0;
    colors[ImGuiCol_TextDisabled] = color3;
    colors[ImGuiCol_WindowBg] = color7;
    colors[ImGuiCol_ChildBg] = color7;
    colors[ImGuiCol_PopupBg] = color4;
    colors[ImGuiCol_Border] = color5;
    colors[ImGuiCol_BorderShadow] = color5;
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
    colors[ImGuiCol_NavWindowingDimBg] = {color1.x, color1.y, color1.z, 0.2};
    colors[ImGuiCol_ModalWindowDimBg] = color7;

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

    ImVec4 red = {1.0f, 0.0, 0.0f, 1.0};
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

void Style::showColors(colorScheme& colors) {
    auto color0 = colors.color0;
    auto color1 = colors.color1;
    auto color2 = colors.color2;
    auto color3 = colors.color3;
    auto color4 = colors.color4;
    auto color5 = colors.color5;
    auto color6 = colors.color6;
    auto color7 = colors.color7;
    const ImVec4 cv[] = { color0, color1, color2, color3, color4, color5, color6, color7 };

    if (ImGui::Begin("colors")) {
      ImDrawList* draw = ImGui::GetWindowDrawList();
      const float size = 72.0f;
      const float gap = 12.0f;
      const float rounding = 6.0f;

      for (int i = 0; i < 8; i++) {
          ImVec2 p = ImGui::GetCursorScreenPos();
          ImVec2 min = p;
          ImVec2 max = ImVec2(p.x + size, p.y + size);

          draw->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(cv[i]), rounding);
          draw->AddRect(min, max, IM_COL32(255, 255, 255, 32), rounding);

          ImGui::Dummy(ImVec2(size, size));
          ImGui::SameLine();
      }
    }
    ImGui::End();
}
