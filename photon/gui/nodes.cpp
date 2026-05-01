#include "nodes.hpp"
#include "imgui.h"
#include "imnodes.h"
void Nodes::demo(){
    if(ImGui::Begin("Node Demo")){
        ImNodes::BeginNodeEditor();

        ImNodes::BeginNode(1);
        ImNodes::BeginNodeTitleBar(); ImGui::TextUnformatted("A"); ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(11); ImGui::Text("In 1"); ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(12); ImGui::Text("In 2"); ImNodes::EndInputAttribute();
        ImNodes::BeginOutputAttribute(13); ImGui::Indent(40); ImGui::Text("Out 1"); ImNodes::EndOutputAttribute();
        ImNodes::BeginOutputAttribute(14); ImGui::Indent(40); ImGui::Text("Out 2"); ImNodes::EndOutputAttribute();
        ImNodes::EndNode();

        ImNodes::BeginNode(2);
        ImNodes::BeginNodeTitleBar(); ImGui::TextUnformatted("B"); ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(21); ImGui::Text("In 1"); ImNodes::EndInputAttribute();
        ImNodes::BeginInputAttribute(22); ImGui::Text("In 2"); ImNodes::EndInputAttribute();
        ImNodes::BeginOutputAttribute(23); ImGui::Indent(40); ImGui::Text("Out 1"); ImNodes::EndOutputAttribute();
        ImNodes::BeginOutputAttribute(24); ImGui::Indent(40); ImGui::Text("Out 2"); ImNodes::EndOutputAttribute();
        ImNodes::EndNode();

        ImNodes::Link(100, 13, 21);
        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
    } ImGui::End();
};
