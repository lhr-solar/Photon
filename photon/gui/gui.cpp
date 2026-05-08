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
#include "box_frag_spv.hpp"
#include "nucleus_frag_spv.hpp"
#include "bits_frag_spv.hpp"
#include "lens_frag_spv.hpp"
#include "custom_shader_vert_spv.hpp"
#include "config.hpp"

void GUI::init(GPU& gpu){
    this->gpu = &gpu;
    GuiSettings::regster(&settings);
    settings.setStyle();
    setTabs();
    testShader.dispatchInit(gpu, (uint32_t *)custom_shader_vert_spv, custom_shader_vert_spv_size, (uint32_t*)nucleus_frag_spv, nucleus_frag_spv_size);
    //testShader.dispatchInit(gpu, (uint32_t *)custom_shader_vert_spv, custom_shader_vert_spv_size, (uint32_t*)lens_frag_spv, lens_frag_spv_size);
}

void GUI::render(){
    if (!testShader.initialized.load() && testShader.partInitialized.load())
        testShader.finishInit(*gpu);
    if(testShader.showing) {testShader.render(*gpu, gpu->commandBuffers[gpu->frameIndex]); testShader.showing = false;}
};

void GUI::destroy(){
    if (sideBar.backgroundTexture) {
        ImGui::UnregisterUserTexture(sideBar.backgroundTexture);
        sideBar.backgroundTexture->SetStatus(ImTextureStatus_WantDestroy);
        sideBar.backgroundTexture = nullptr;
    }
    testShader.destroy();
};

void GUI::setFont(){
    bool incFlag = false;
    bool decFlag = false;
    auto incSize = [&]() -> void{
        settings.fontSize += 1.0f; ImGui::GetStyle().FontSizeBase = settings.fontSize;
        ImGui::MarkIniSettingsDirty();
    };
    auto decSize = [&]() -> void{
        settings.fontSize = settings.fontSize > 1.0f ?  settings.fontSize - 1.0f : 1.0f; 
        ImGui::GetStyle().FontSizeBase = settings.fontSize;
        ImGui::MarkIniSettingsDirty();
    };
    ifKey(ImGuiKey_Equal, incFlag, incSize);
    ifKey(ImGuiKey_Minus, decFlag, decSize);
};

void GUI::settingsUI(){
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    if(ImGui::BeginPopupModal("Settings", NULL, flags)){
        if(ImGui::Button("Exit")) ImGui::CloseCurrentPopup();
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

void GUI::exportUI(){
    if(ImGui::BeginPopupModal("Export", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure?");
        if(ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
};

void GUI::plotTest(ImGuiWindowFlags flags){
    std::vector<double> time = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> data = {2.0, 4.0, 3.0, 1.0, 2.0};
    ImPlotSpec spec{};
    spec = this->settings.plotLineSpec;
    if(ImGui::Begin("Page 1", NULL, flags)){
        ImGui::Text("some stuff on page 1");
        if(ImPlot::BeginPlot("some plot")){
            ImPlot::PlotLine("some data", time.data(), data.data(), data.size(), spec);
        } ImPlot::EndPlot();
    } ImGui::End();
};

VkExtent2D quantizeContentExtent(ImVec2 contentSize, VkExtent2D fallback) {
    if (contentSize.x <= 1.0f || contentSize.y <= 1.0f) return fallback;
    const uint32_t width = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.x)));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(contentSize.y)));
    return {width, height};
}

void GUI::shaderTest(ImGuiWindowFlags flags){
    testShader.showing = false;
    if(ImGui::Begin("shader test", nullptr, flags)){
        const bool ready = testShader.initialized.load()
        && !testShader.frames.empty()
        && testShader.frameIndex != nullptr;
        shaderFrame fallbackFrame{};
        shaderFrame& frame = ready ? testShader.frames[*testShader.frameIndex] : fallbackFrame;
        if(ready){
            const VkExtent2D nextExtent = quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
            if(nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
                frame.extent = nextExtent;
                testShader.dirty = true;
            }
            ImVec2 drawSize(frame.extent.width, frame.extent.height);
            drawSize.x = std::max(drawSize.x, 1.0f);
            drawSize.y = std::max(drawSize.y, 1.0f);
            if(ImGui::IsRectVisible(drawSize)){
                testShader.showing = true;
                ImGui::Image(frame.texture, drawSize); 
            } else {ImGui::Dummy(drawSize);};
        } else ImGui::Text("loading shader");
    } ImGui::End();
};

void GUI::testFunc(ImGuiWindowFlags flags){
    if(ImGui::Begin("test page", NULL, flags)){
        ImGui::Text("wasldfkjasdlfkj");
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
        ImGui::NewLine(); ImGui::NewLine();
    } ImGui::End();
};

void GUI::setTabs(){
    tabs.list.clear();
    //tabs.list.push_back(Tab{.function = page1, .name = "Page 1 Name"});
    //tabs.list.push_back(Tab{.function = page2, .name = "Page 2 Name"});
    tabs.list.push_back(Tab<GUI>{.function = &GUI::shaderTest, .name = "shader test"});
    tabs.list.push_back(Tab<GUI>{.function = &GUI::testFunc,   .name = "draw test"});
    tabs.list.push_back(Tab<GUI>{.function = &GUI::plotTest,   .name = "plot test"});
};

void GUI::buildUI(){
    /* Per-Frame state updates */
    settings.setStyle();
    setFont();
    setTabs();
    ImGui::NewFrame();
    iam_update_begin_frame();
    iam_clip_update(ImGui::GetIO().DeltaTime);

    /* Per-Frame UI building */
    titleBar.draw();
    sideBar.draw(*this);
    canvas.draw(*this, titleBar, sideBar, tabs);
    //shaderTest();

    /* stateful UI building */
    ifKey(ImGuiKey_F3, flags.showGPUInfo, gpuGUI::buildUI, *gpu); 
    ImGui::Render();
    render();
};
