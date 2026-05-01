#include "im_anim.h"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include "imnodes.h"
#include "gui.hpp"
#include "gpuGui.hpp"
#include "nodes.hpp"

void GUI::init(GPU& gpu){
    Style::setStyle(colors);
    this->gpu = &gpu;
    //gui.init(&gpu, &network, &parse);
    //gui.pendingDBC = parse.activeDBC;
    //gui.bindNetworkResponses(network.getResponseReader());
    //gui.protocolConfig.kind = ProtocolKind::TCP;
    //gui.queueStartProtocol();
}

void GUI::render(){
    //gui.backgroundShader.render(gpu, gpu.commandBuffers[gpu.frameIndex]);
    //gui.sceneModel.render(gpu, gpu.commandBuffers[gpu.frameIndex]);
};

void GUI::destroy(){
    //gui.backgroundShader.destroy();
    //gui.sceneModel.destroy();
};

// renders a box with the given text at the position pos
// "start" starts the animation
// requires a pushID() and popID() for multiple boxes
void GUI::animTextBox(std::string_view text, bool start){
    static constexpr ImGuiID clip = 0xA11CE001;
    static constexpr ImGuiID ch = 0xA11CE002;
    if (!iam_clip_exists(clip)) iam_clip::begin(clip)
        .key_float(ch, 0.0f, 0.0f)
        .key_float(ch, 0.08f, 1.0f, iam_ease_out_cubic)
        .key_float(ch, 0.30f, 0.0f, iam_ease_in_cubic)
        .end();

    const ImGuiID id = ImGui::GetID("anim_text_box");
    if (start) iam_play(clip, id);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 textSize = ImGui::CalcTextSize(text.data());
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const ImVec2 boxSize(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f);
    const ImVec2 boxMin = pos;
    const ImVec2 boxMax(pos.x + boxSize.x, pos.y + boxSize.y);

    draw->AddRect(boxMin, boxMax, IM_COL32(255,255,255,255), 0.0f, 0, 2.0f);
    const ImVec2 textPos(
        boxMin.x + (boxMax.x - boxMin.x - textSize.x) * 0.5f,
        boxMin.y + (boxMax.y - boxMin.y - textSize.y) * 0.5f
    );
    draw->AddText(textPos, IM_COL32(255,255,255,255), text.data());

    float flash = 0.0f;
    iam_instance inst = iam_get_instance(id);
    if (inst.valid()) inst.get_float(ch, &flash);
    if (flash > 0.0f) {
        const float glow = 8.0f * flash;
        draw->AddRectFilled(
            ImVec2(boxMin.x - glow, boxMin.y - glow),
            ImVec2(boxMax.x + glow, boxMax.y + glow),
            IM_COL32(255, 255, 255, static_cast<int>(255.0f * flash)),
            6.0f + glow * 0.2f
        );
    }
    ImGui::Dummy(boxSize);
};

void GUI::animLine(ImVec2 begin, ImVec2 end, bool start){
    static constexpr ImGuiID clip = 0xA11CE011;
    static constexpr ImGuiID ch = 0xA11CE012;
    if (!iam_clip_exists(clip)) iam_clip::begin(clip)
        .key_float(ch, 0.0f, 0.0f)
        .key_float(ch, 0.6f, 1.0f, iam_ease_in_out_cubic)
        .end();

    const ImGuiID id = ImGui::GetID("anim_line");
    if (start) iam_play(clip, id);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(begin, end, IM_COL32(255, 255, 255, 255), 1.0f);
    const ImVec2 delta(end.x - begin.x, end.y - begin.y);
    const float span = sqrtf(delta.x * delta.x + delta.y * delta.y);
    const ImVec2 lineMin(std::min(begin.x, end.x), std::min(begin.y, end.y));
    const ImVec2 lineMax(std::max(begin.x, end.x), std::max(begin.y, end.y));
    if (span <= 0.0f) {
        ImGui::SetCursorScreenPos(lineMin);
        ImGui::Dummy(ImVec2(lineMax.x - lineMin.x, lineMax.y - lineMin.y));
        return;
    }
    const ImVec2 dir(delta.x / span, delta.y / span);
    const float len = span * 0.2f;
    float t = 0.0f;
    iam_instance inst = iam_get_instance(id);
    if (inst.valid() && inst.is_playing() && inst.get_float(ch, &t)) {
        const float offset = (span - len) * t;
        const ImVec2 stubBegin(begin.x + dir.x * offset, begin.y + dir.y * offset);
        draw->AddLine(stubBegin, ImVec2(stubBegin.x + dir.x * len, stubBegin.y + dir.y * len), IM_COL32(255, 255, 255, 255), 4.0f);
    }
    ImGui::SetCursorScreenPos(lineMin);
    ImGui::Dummy(ImVec2(lineMax.x - lineMin.x, lineMax.y - lineMin.y));
};

void GUI::drawTest(){
    if(ImGui::Begin("boxes & lines")){
        bool val1 = ImGui::Button("button1");
        bool val2 = ImGui::Button("button2");
        ImGui::PushID(0); animTextBox("some text here", val1);  ImGui::PopID();
        ImGui::PushID(1); animTextBox("new text!", false);      ImGui::PopID();
        ImGui::NewLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(2); animLine(p, {p.x + 240, p.y}, val1);  ImGui::PopID();
        ImGui::Text("something random");
        p = ImGui::GetCursorScreenPos();
        ImGui::PushID(3); animLine(p, {p.x + 240, p.y}, val2);  ImGui::PopID();
        ImGui::PushID(4); animLine(p, {p.x + 240, p.y + 240}, val2);  ImGui::PopID();
        ImGui::PushID(5); animLine(p, {p.x, p.y + 240}, val2);  ImGui::PopID();
        ImGui::Text("let us see...");
    }ImGui::End();
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

void GUI::buildUI(){
    setFont();
    ImGui::NewFrame();
    iam_update_begin_frame();
    iam_clip_update(ImGui::GetIO().DeltaTime);
    ifKey(ImGuiKey_F3, flags.showGPUInfo, gpuGUI::buildUI, *gpu); 
    Style::showColors(colors);
    drawTest();
    ImAnimDemoWindow();
    ImGui::ShowDemoWindow();

    ImGui::Render();
};
