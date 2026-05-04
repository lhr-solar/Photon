#include "imgui.h"
#include "im_anim.h"
#include "widget.hpp"

// renders a box with the given text at the position pos
// "start" starts the animation
// requires a pushID() and popID() for multiple boxes
void Widget::animTextBox(std::string_view text, bool start){
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
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImU32 drawColor = ImGui::GetColorU32(ImGuiCol_Text);

    draw->AddRect(boxMin, boxMax, drawColor, 0.0f, 0, 2.0f);
    const ImVec2 textPos(
        boxMin.x + (boxMax.x - boxMin.x - textSize.x) * 0.5f,
        boxMin.y + (boxMax.y - boxMin.y - textSize.y) * 0.5f
    );
    draw->AddText(textPos, drawColor, text.data());

    float flash = 0.0f;
    iam_instance inst = iam_get_instance(id);
    if (inst.valid()) inst.get_float(ch, &flash);
    if (flash > 0.0f) {
        const float glow = 8.0f * flash;
        draw->AddRectFilled(
            ImVec2(boxMin.x - glow, boxMin.y - glow),
            ImVec2(boxMax.x + glow, boxMax.y + glow),
            ImGui::ColorConvertFloat4ToU32({textColor.x, textColor.y, textColor.z, flash}),
            6.0f + glow * 0.2f
        );
    }
    ImGui::Dummy(boxSize);
};

void Widget::animLine(ImVec2 begin, ImVec2 end, bool start){
    static constexpr ImGuiID clip = 0xA11CE011;
    static constexpr ImGuiID ch = 0xA11CE012;
    if (!iam_clip_exists(clip)) iam_clip::begin(clip)
        .key_float(ch, 0.0f, 0.0f)
        .key_float(ch, 0.6f, 1.0f, iam_ease_in_out_cubic)
        .end();

    const ImGuiID id = ImGui::GetID("anim_line");
    if (start) iam_play(clip, id);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 drawColor = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddLine(begin, end, drawColor, 1.0f);
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
        draw->AddLine(stubBegin, ImVec2(stubBegin.x + dir.x * len, stubBegin.y + dir.y * len), drawColor, 4.0f);
    }
    ImGui::SetCursorScreenPos(lineMin);
    ImGui::Dummy(ImVec2(lineMax.x - lineMin.x, lineMax.y - lineMin.y));
};
