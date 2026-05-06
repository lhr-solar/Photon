#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot3d.h"
#include "imnodes.h"
#include "gui.hpp"
#include "gpuGui.hpp"
#include "nodes.hpp"
#include "widget.hpp"
#include "../gpu/shader.hpp"
#include <vector>


void GUI::init(GPU& gpu){
    style.setStyle();
    this->gpu = &gpu;
    setTabs();
}

void GUI::render(){
};

void GUI::destroy(){
};

void GUI::setFont(){
    bool incFlag = false;
    bool decFlag = false;
    float size = ImGui::GetStyle().FontSizeBase;
    auto incSize = [&]() -> void{ ImGui::GetStyle().FontSizeBase = size + 1.0f; };
    auto decSize = [&]() -> void{ ImGui::GetStyle().FontSizeBase = size > 1.0f ? size - 1.0f : 1.0f; };
    ifKey(ImGuiKey_Equal, incFlag, incSize);
    ifKey(ImGuiKey_Minus, decFlag, decSize);
};

void GUI::settingsUI(){
    if(ImGui::BeginPopupModal("Settings")){
        if(ImGui::Button("Exit")) ImGui::CloseCurrentPopup();
        bool val1 = ImGui::Button("button1");
        bool val2 = ImGui::Button("button2");
        ImGui::PushID(0); Widget::animTextBox("some text here", val1);  ImGui::PopID();
        ImGui::PushID(1); Widget::animTextBox("new text!", false);      ImGui::PopID();
        ImGui::NewLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(2); Widget::animLine(p, {p.x + 240, p.y}, val1);  ImGui::PopID();
        ImGui::Text("something random");
        p = ImGui::GetCursorScreenPos();
        ImGui::PushID(3); Widget::animLine(p, {p.x + 240, p.y}, val2);  ImGui::PopID();
        ImGui::PushID(4); Widget::animLine(p, {p.x + 240, p.y + 240}, val2);  ImGui::PopID();
        ImGui::PushID(5); Widget::animLine(p, {p.x, p.y + 240}, val2);  ImGui::PopID();
        ImGui::Text("let us see...");
        auto draw = ImGui::GetWindowDrawList();

        float s = 50;
        p = ImGui::GetCursorScreenPos();
        draw->AddRectFilledMultiColor(p, {p.x + s, p.y + s}, 
                IM_COL32(255,   0,   0, 255), IM_COL32(  0, 255,   0, 255), 
                IM_COL32(  0,   0, 255, 255), IM_COL32(255, 255, 255, 255)
        );
        ImGui::EndPopup();
    }
};

void GUI::updateUI(){
    if(ImGui::BeginPopupModal("Update", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure?");
        if(ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
  }
};

void page1(ImGuiWindowFlags flags){
    std::vector<double> time = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> data = {2.0, 4.0, 3.0, 1.0, 2.0};
    ImPlotSpec spec{};
    spec.LineWeight = 4.0f;
    if(ImGui::Begin("Page 1", NULL, flags)){
        ImGui::Text("some stuff on page 1");
        if(ImPlot::BeginPlot("some plot")){
            ImPlot::PlotLine("some data", time.data(), data.data(), data.size(), spec);
        } ImPlot::EndPlot();
    } ImGui::End();
};

void page2(ImGuiWindowFlags flags){
    if(ImGui::Begin("Page 2", NULL, flags)){
        ImGui::Text("some stuff on page 2");
    } ImGui::End();
}

void testFunc(ImGuiWindowFlags flags){
    if(ImGui::Begin("test page", NULL, flags)){
        ImGui::Text("wasldfkjasdlfkj");

    } ImGui::End();
};
void GUI::setTabs(){
    tabs.list.clear();
    tabs.list.push_back(Tab{.function = page1, .name = "Page 1 Name"});
    tabs.list.push_back(Tab{.function = page2, .name = "Page 2 Name"});
    tabs.list.push_back(Tab{.function = testFunc, .name = "test func page"});
};

void GUI::buildUI(){
    /* Per-Frame state updates */
    style.setStyle();
    setFont();
    setTabs();
    ImGui::NewFrame();
    iam_update_begin_frame();
    iam_clip_update(ImGui::GetIO().DeltaTime);

    /* Per-Frame UI building */
    titleBar.draw();
    sideBar.draw(*this);
    canvas.draw(titleBar, sideBar, tabs);

    /* stateful UI building */
    ifKey(ImGuiKey_F3, flags.showGPUInfo, gpuGUI::buildUI, *gpu); 
    ImGui::Render();
};
