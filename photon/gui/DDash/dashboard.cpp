#include "dashboard.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>
#include <cmath>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ui {

// Helper to format timestamp to HH:MM (thread-safe, cross-platform)
static void FormatTime(int64_t timestamp, char* buf, size_t bufSize) {
    time_t t = static_cast<time_t>(timestamp / 1000);
    struct tm tm_storage;
    struct tm* tm_info = nullptr;
    
#if defined(_WIN32) || defined(_WIN64)
    // Windows: localtime_s has reversed parameter order and returns errno_t
    if (localtime_s(&tm_storage, &t) == 0) {
        tm_info = &tm_storage;
    }
#else
    // POSIX (Linux, macOS, etc.): localtime_r is thread-safe
    tm_info = localtime_r(&t, &tm_storage);
#endif
    
    if (tm_info) {
        snprintf(buf, bufSize, "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
    } else {
        snprintf(buf, bufSize, "--:--");
    }
}

// Get color based on battery SOC level
static ImVec4 GetBatteryColor(float soc) {
    if (soc < 20.0f) return Colors::Destructive();
    if (soc < 40.0f) return Colors::Warning();
    return Colors::Primary();
}

// Get color based on speed
static ImVec4 GetSpeedColor(int speed) {
    if (speed > 160) return Colors::Destructive();
    if (speed > 100) return Colors::Warning();
    return Colors::Primary();
}

//=============================================================================
// MAIN DASHBOARD
//=============================================================================
void RenderDashboard(AppState& state) {
    ImGuiIO& io = ImGui::GetIO();
    
    ImGui::SetNextWindowSize(ImVec2(1024, 700), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background());
    
    if (!ImGui::Begin("LHR Photon Dashboard", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }
    
    // Header with branding, heartbeat, and gear selector
    RenderHeader(state);
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Main content - 3 column layout
    float availWidth = ImGui::GetContentRegionAvail().x;
    float availHeight = ImGui::GetContentRegionAvail().y - 50.0f; // Reserve for fault ticker
    
    float leftWidth = availWidth * 0.22f;
    float rightWidth = availWidth * 0.30f;
    float centerWidth = availWidth - leftWidth - rightWidth - 16.0f;
    
    // LEFT COLUMN: Speed Gauge
    ImGui::BeginChild("##LeftCol", ImVec2(leftWidth, availHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    RenderSpeedGauge(state);
    ImGui::EndChild();
    
    ImGui::SameLine(0, 8);
    
    // CENTER COLUMN: Camera Feeds
    ImGui::BeginChild("##CenterCol", ImVec2(centerWidth, availHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    RenderCameraFeeds(state);
    ImGui::EndChild();
    
    ImGui::SameLine(0, 8);
    
    // RIGHT COLUMN: Battery + System Status
    ImGui::BeginChild("##RightCol", ImVec2(rightWidth, availHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    RenderBatteryPanel(state);
    widgets::Space(8);
    RenderSystemStatus(state);
    ImGui::EndChild();
    
    // FOOTER: Fault Ticker
    ImGui::Spacing();
    RenderFaultTicker(state);
    
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

//=============================================================================
// HEADER: Branding + Heartbeat + Gear Selector
//=============================================================================
void RenderHeader(AppState& state) {
    float headerHeight = 50.0f;
    
    // Add background to header
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Card());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    
    ImGui::BeginChild("##Header", ImVec2(0, headerHeight), ImGuiChildFlags_Borders);
    
    float contentWidth = ImGui::GetContentRegionAvail().x;
    
    // Left: LHR Photon branding
    ImGui::SetCursorPosY((headerHeight - ImGui::GetTextLineHeight()) * 0.5f - 4);
    ImGui::SetCursorPosX(12);
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary());
    ImGui::Text("LHR");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 4);
    ImGui::Text("Photon");
    
    // Center: Heartbeat indicator with 1-second pulse
    float centerX = contentWidth * 0.5f - 40;
    ImGui::SameLine(centerX);
    ImGui::SetCursorPosY((headerHeight - 20) * 0.5f - 4);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 hbPos = ImGui::GetCursorScreenPos();
    
    // Use real time for smooth 1-second pulse (fade in/out)
    float time = static_cast<float>(ImGui::GetTime());
    float pulse = 0.4f + 0.6f * (0.5f + 0.5f * sinf(time * 2.0f * static_cast<float>(M_PI)));
    ImVec4 hbColor = ColorWithAlpha(Colors::Primary(), pulse);
    drawList->AddCircleFilled(ImVec2(hbPos.x + 8, hbPos.y + 10), 6, ColorToU32(hbColor));
    
    ImGui::SetCursorPosX(centerX + 20);
    char hbText[16];
    snprintf(hbText, sizeof(hbText), "HB %03d", state.heartbeat);
    ImGui::Text("%s", hbText);
    
    // Right: Gear selector pills - use absolute positioning
    float gearY = (headerHeight - 32) * 0.5f - 4;
    float buttonWidth = 36.0f;
    float buttonSpacing = 4.0f;
    float totalGearWidth = 4 * buttonWidth + 3 * buttonSpacing;  // 4 buttons + 3 gaps
    float gearStartX = contentWidth - totalGearWidth - 8;  // 8px padding from right
    
    const char* gears[] = {"P", "R", "N", "D"};
    const Gear gearVals[] = {Gear::Park, Gear::Reverse, Gear::Neutral, Gear::Drive};
    bool disabled = state.speed >= 5;
    
    for (int i = 0; i < 4; i++) {
        bool selected = state.gear == gearVals[i];
        bool isDisabled = disabled && !selected;
        
        ImVec4 btnColor = selected ? Colors::Primary() : Colors::Muted();
        ImVec4 textColor = selected ? Colors::PrimaryForeground() : Colors::MutedForeground();
        
        if (isDisabled) {
            btnColor = ColorWithAlpha(Colors::Muted(), 0.5f);
            textColor = ColorWithAlpha(Colors::MutedForeground(), 0.5f);
        }
        
        // Set explicit position for each button
        float btnX = gearStartX + i * (buttonWidth + buttonSpacing);
        ImGui::SetCursorPos(ImVec2(btnX, gearY));
        
        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? Colors::PrimaryDark() : Colors::Secondary());
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
        
        char btnId[16];
        snprintf(btnId, sizeof(btnId), "%s##Gear%d", gears[i], i);
        
        if (ImGui::Button(btnId, ImVec2(buttonWidth, 32)) && !isDisabled) {
            state.gear = gearVals[i];
        }
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

//=============================================================================
// SPEED GAUGE: Large circular gauge with gear badge
//=============================================================================
void RenderSpeedGauge(const AppState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Card());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    
    ImGui::BeginChild("##SpeedCard", ImVec2(0, 0), ImGuiChildFlags_Borders);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    
    // Gauge parameters
    float gaugeSize = std::min(size.x, size.y - 60) * 0.85f;
    float radius = gaugeSize * 0.5f;
    float thickness = 14.0f;
    ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + radius + 20);
    
    // Background arc (270 degrees)
    float startAngle = static_cast<float>(M_PI) * 0.75f;
    float maxAngle = static_cast<float>(M_PI) * 1.5f;
    
    drawList->PathArcTo(center, radius, startAngle, startAngle + maxAngle, 64);
    drawList->PathStroke(ColorToU32(Colors::Muted()), 0, thickness);
    
    // Progress arc
    float maxSpeed = 200.0f;
    float pct = std::max(0.0f, std::min(1.0f, static_cast<float>(state.speed) / maxSpeed));
    
    if (pct > 0.001f) {
        ImVec4 speedColor = GetSpeedColor(state.speed);
        drawList->PathArcTo(center, radius, startAngle, startAngle + maxAngle * pct, 64);
        drawList->PathStroke(ColorToU32(speedColor), 0, thickness);
    }
    
    // Speed value - large (use 72px font from atlas index 3)
    char speedText[8];
    snprintf(speedText, sizeof(speedText), "%d", state.speed);
    
    ImGuiIO& io = ImGui::GetIO();
    ImFont* hugeFont = (io.Fonts->Fonts.Size > 3) ? io.Fonts->Fonts[3] : nullptr;
    if (hugeFont) ImGui::PushFont(hugeFont);
    ImVec2 textSize = ImGui::CalcTextSize(speedText);
    drawList->AddText(hugeFont, hugeFont ? hugeFont->FontSize : 72.0f,
                     ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                     ColorToU32(Colors::Foreground()), speedText);
    if (hugeFont) ImGui::PopFont();
    
    // Unit label
    const char* unit = "km/h";
    ImVec2 unitSize = ImGui::CalcTextSize(unit);
    drawList->AddText(ImVec2(center.x - unitSize.x * 0.5f, center.y + 35),
                     ColorToU32(Colors::MutedForeground()), unit);
    
    // Gear badge below gauge
    float badgeY = center.y + radius + 15;
    float badgeWidth = 100.0f;
    float badgeHeight = 28.0f;
    
    ImVec2 badgeMin(center.x - badgeWidth * 0.5f, badgeY);
    ImVec2 badgeMax(center.x + badgeWidth * 0.5f, badgeY + badgeHeight);
    
    drawList->AddRectFilled(badgeMin, badgeMax, ColorToU32(Colors::Primary()), 6.0f);
    
    const char* gearNames[] = {"PARK", "REVERSE", "NEUTRAL", "DRIVE"};
    const char* gearLetters[] = {"P", "R", "N", "D"};
    int gearIdx = static_cast<int>(state.gear);
    
    char gearLabel[32];
    snprintf(gearLabel, sizeof(gearLabel), "%s - %s", gearLetters[gearIdx], gearNames[gearIdx]);
    
    ImVec2 gearSize = ImGui::CalcTextSize(gearLabel);
    drawList->AddText(ImVec2(center.x - gearSize.x * 0.5f, badgeY + (badgeHeight - gearSize.y) * 0.5f),
                     ColorToU32(Colors::PrimaryForeground()), gearLabel);
    
    ImGui::Dummy(size);
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

//=============================================================================
// CAMERA FEEDS: Stacked rear view and side camera
//=============================================================================
void RenderCameraFeeds(AppState& state) {
    float spacing = 8.0f;
    float halfHeight = (ImGui::GetContentRegionAvail().y - spacing) * 0.5f;
    
    // Rear View Camera
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Card());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    
    ImGui::BeginChild("##RearCam", ImVec2(0, halfHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    {
        // Header
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
        ImGui::Text("REAR VIEW");
        ImGui::PopStyleColor();
        
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 45);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 livePos = ImGui::GetCursorScreenPos();
        drawList->AddCircleFilled(ImVec2(livePos.x + 5, livePos.y + 7), 4, ColorToU32(Colors::Success()));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::Success());
        ImGui::Text("LIVE");
        ImGui::PopStyleColor();
        
        // Camera feed area
        ImVec2 feedSize = ImGui::GetContentRegionAvail();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Muted());
        ImGui::BeginChild("##RearFeed", feedSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        
        if (state.rearCameraTexture) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(state.rearCameraTexture)), feedSize);
        } else {
            // Placeholder with camera grid
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec4 gridColor = ColorWithAlpha(Colors::Primary(), 0.3f);
            
            // Grid lines
            for (int i = 1; i < 4; i++) {
                float x = p.x + feedSize.x * i / 4.0f;
                float y = p.y + feedSize.y * i / 4.0f;
                dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + feedSize.y), ColorToU32(gridColor), 1.0f);
                dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + feedSize.x, y), ColorToU32(gridColor), 1.0f);
            }
            
            // Guideline arc
            ImVec2 arcCenter(p.x + feedSize.x * 0.5f, p.y + feedSize.y);
            dl->PathArcTo(arcCenter, feedSize.x * 0.2f, -3.14f, 0, 32);
            dl->PathStroke(ColorToU32(Colors::Warning()), 0, 2.0f);
        }
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    
    widgets::Space(spacing);
    
    // Side Camera
    const char* sideLabel = "SIDE CAMERA";
    bool sideActive = false;
    
    if (state.turnSignal == TurnSignal::Left) {
        sideLabel = "LEFT TURN SIGNAL";
        sideActive = true;
    } else if (state.turnSignal == TurnSignal::Right) {
        sideLabel = "RIGHT TURN SIGNAL";
        sideActive = true;
    }
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Card());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    
    ImGui::BeginChild("##SideCam", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::PushStyleColor(ImGuiCol_Text, sideActive ? Colors::Warning() : Colors::MutedForeground());
        ImGui::Text("%s", sideLabel);
        ImGui::PopStyleColor();
        
        if (sideActive) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 45);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 livePos = ImGui::GetCursorScreenPos();
            drawList->AddCircleFilled(ImVec2(livePos.x + 5, livePos.y + 7), 4, ColorToU32(Colors::Warning()));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Warning());
            ImGui::Text("LIVE");
            ImGui::PopStyleColor();
        }
        
        ImVec2 feedSize = ImGui::GetContentRegionAvail();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Muted());
        ImGui::BeginChild("##SideFeed", feedSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        
        if (state.sideCameraTexture) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(state.sideCameraTexture)), feedSize);
        } else {
            // Placeholder
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec4 gridColor = ColorWithAlpha(Colors::Primary(), 0.2f);
            
            for (int i = 1; i < 4; i++) {
                float x = p.x + feedSize.x * i / 4.0f;
                float y = p.y + feedSize.y * i / 4.0f;
                dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + feedSize.y), ColorToU32(gridColor), 1.0f);
                dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + feedSize.x, y), ColorToU32(gridColor), 1.0f);
            }
        }
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

//=============================================================================
// BATTERY PANEL: Main + 12V Aux
//=============================================================================
void RenderBatteryPanel(const AppState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Card());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    
    // Main Battery Card
    ImGui::BeginChild("##MainBatCard", ImVec2(0, 180), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Text("MAIN BATTERY");
        widgets::Space(4);
        
        // Large SOC display (use 48px font from atlas index 2)
        ImGui::PushStyleColor(ImGuiCol_Text, GetBatteryColor(state.mainBattery.soc));
        ImGuiIO& batIO = ImGui::GetIO();
        ImFont* largeFont = (batIO.Fonts->Fonts.Size > 2) ? batIO.Fonts->Fonts[2] : nullptr;
        if (largeFont) ImGui::PushFont(largeFont);
        ImGui::Text("%.0f%%", state.mainBattery.soc);
        if (largeFont) ImGui::PopFont();
        ImGui::PopStyleColor();
        
        // Progress bar
        widgets::ProgressBar(state.mainBattery.soc / 100.0f, ImVec2(-1, 8), GetBatteryColor(state.mainBattery.soc));
        
        widgets::Space(8);
        
        // Voltage and Current row
        float halfW = ImGui::GetContentRegionAvail().x * 0.5f - 4;
        
        ImGui::BeginChild("##Volt", ImVec2(halfW, 45), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
        ImGui::Text("VOLTAGE");
        ImGui::PopStyleColor();
        ImGui::Text("%.1f V", state.mainBattery.voltage);
        ImGui::EndChild();
        
        ImGui::SameLine(0, 8);
        
        ImGui::BeginChild("##Curr", ImVec2(halfW, 45), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
        ImGui::Text("CURRENT");
        ImGui::PopStyleColor();
        ImVec4 currColor = state.mainBattery.current < 0 ? Colors::Accent() : Colors::Success();
        ImGui::PushStyleColor(ImGuiCol_Text, currColor);
        ImGui::Text("%.1f A", state.mainBattery.current);
        ImGui::PopStyleColor();
        ImGui::EndChild();
    }
    ImGui::EndChild();
    
    widgets::Space(8);
    
    // 12V Aux Card
    ImGui::BeginChild("##AuxBatCard", ImVec2(0, 100), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Text("12V AUX");
        
        float halfW = ImGui::GetContentRegionAvail().x * 0.5f;
        
        ImGui::SameLine(halfW);
        ImVec4 auxColor = state.suppBattery.soc < 30 ? Colors::Destructive() : Colors::Foreground();
        ImGui::PushStyleColor(ImGuiCol_Text, auxColor);
        ImGui::Text("%.0f%%", state.suppBattery.soc);
        ImGui::PopStyleColor();
        
        widgets::Space(4);
        widgets::ProgressBar(state.suppBattery.soc / 100.0f, ImVec2(-1, 6), 
                            state.suppBattery.soc < 30 ? Colors::Destructive() : Colors::MutedForeground());
        
        widgets::Space(4);
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
        ImGui::Text("%.1f V", state.suppBattery.voltage);
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

//=============================================================================
// SYSTEM STATUS: Contactors, Brake, Cruise
//=============================================================================
void RenderSystemStatus(AppState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Card());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    
    ImGui::BeginChild("##SystemCard", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Text("SYSTEM");
        widgets::Space(8);
        
        // Contactors row
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
        ImGui::Text("CONTACTORS");
        ImGui::PopStyleColor();
        widgets::Space(4);
        
        float btnW = (ImGui::GetContentRegionAvail().x - 16) / 3.0f;
        
        // Main contactor
        ImVec4 mainColor = state.contactorStates.main ? Colors::Success() : Colors::Muted();
        ImGui::PushStyleColor(ImGuiCol_Button, mainColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("MAIN##Cont", ImVec2(btnW, 28))) {
            state.contactorStates.main = !state.contactorStates.main;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        
        ImGui::SameLine(0, 8);
        
        // Precharge
        ImVec4 preColor = state.contactorStates.precharge ? Colors::Success() : Colors::Muted();
        ImGui::PushStyleColor(ImGuiCol_Button, preColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("PRE##Cont", ImVec2(btnW, 28))) {
            state.contactorStates.precharge = !state.contactorStates.precharge;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        
        ImGui::SameLine(0, 8);
        
        // HVIL (read-only indicator)
        ImVec4 hvilColor = state.contactorStates.hvil ? Colors::Success() : Colors::Destructive();
        ImGui::PushStyleColor(ImGuiCol_Button, hvilColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::Button("HVIL##Ind", ImVec2(btnW, 28));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        
        widgets::Space(12);
        
        // Brake Status
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
        ImGui::Text("BRAKE");
        ImGui::PopStyleColor();
        
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
        
        if (state.brakeEngaged) {
            widgets::Badge("ENGAGED", Colors::DestructiveBg(), Colors::Destructive());
        } else {
            widgets::Badge("RELEASED", Colors::Muted(), Colors::MutedForeground());
        }
        
        // Make brake clickable
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 22);
        if (ImGui::InvisibleButton("##BrakeToggle", ImVec2(ImGui::GetContentRegionAvail().x, 22))) {
            state.brakeEngaged = !state.brakeEngaged;
        }
        
        widgets::Space(8);
        
        // Cruise Control
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
        ImGui::Text("CRUISE CONTROL");
        ImGui::PopStyleColor();
        widgets::Space(4);
        
        // Toggle button
        ImVec4 ccColor = state.cruise.enabled ? Colors::Primary() : Colors::Muted();
        ImVec4 ccText = state.cruise.enabled ? Colors::PrimaryForeground() : Colors::MutedForeground();
        
        ImGui::PushStyleColor(ImGuiCol_Button, ccColor);
        ImGui::PushStyleColor(ImGuiCol_Text, ccText);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
        
        if (ImGui::Button(state.cruise.enabled ? "ON##CC" : "OFF##CC", ImVec2(50, 28))) {
            state.cruise.enabled = !state.cruise.enabled;
            if (state.cruise.enabled) {
                state.cruise.setSpeed = state.speed;
            }
        }
        
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        
        if (state.cruise.enabled) {
            ImGui::SameLine(0, 12);
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("-##CC", ImVec2(24, 24))) {
                state.cruise.setSpeed = std::max(0, state.cruise.setSpeed - 5);
            }
            ImGui::PopStyleVar();
            
            ImGui::SameLine(0, 4);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Primary());
            ImGui::Text("%d", state.cruise.setSpeed);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 2);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("km/h");
            ImGui::PopStyleColor();
            
            ImGui::SameLine(0, 4);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("+##CC", ImVec2(24, 24))) {
                state.cruise.setSpeed = std::min(200, state.cruise.setSpeed + 5);
            }
            ImGui::PopStyleVar();
        }
        
        // Turn signal controls
        widgets::Space(12);
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
        ImGui::Text("TURN SIGNALS");
        ImGui::PopStyleColor();
        widgets::Space(4);
        
        float sigW = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
        
        // Left signal
        bool leftOn = state.turnSignal == TurnSignal::Left;
        ImVec4 leftColor = leftOn ? Colors::Warning() : Colors::Muted();
        ImGui::PushStyleColor(ImGuiCol_Button, leftColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("<##TurnL", ImVec2(sigW, 28))) {
            state.turnSignal = leftOn ? TurnSignal::None : TurnSignal::Left;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        
        ImGui::SameLine(0, 8);
        
        // Right signal
        bool rightOn = state.turnSignal == TurnSignal::Right;
        ImVec4 rightColor = rightOn ? Colors::Warning() : Colors::Muted();
        ImGui::PushStyleColor(ImGuiCol_Button, rightColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button(">##TurnR", ImVec2(sigW, 28))) {
            state.turnSignal = rightOn ? TurnSignal::None : TurnSignal::Right;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

//=============================================================================
// FAULT TICKER: Horizontal scrolling fault display
//=============================================================================
void RenderFaultTicker(AppState& state) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::Card());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    
    ImGui::BeginChild("##FaultTicker", ImVec2(0, 40), ImGuiChildFlags_Borders);
    {
        ImGui::SetCursorPosY(10);
        
        if (state.faults.empty()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(pos.x + 8, pos.y + 6), 5, ColorToU32(Colors::Success()));
            ImGui::SetCursorPosX(20);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Success());
            ImGui::Text("No active faults");
            ImGui::PopStyleColor();
            
            // Add fault button (for testing)
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
            ImGui::SetCursorPosY(6);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("+##AddF", ImVec2(24, 24))) {
                static const struct { const char* code; const char* msg; FaultSeverity sev; } templates[] = {
                    { "E001", "Battery temp high", FaultSeverity::Warning },
                    { "E002", "Motor overheat", FaultSeverity::Critical },
                    { "E003", "CAN timeout", FaultSeverity::Warning },
                };
                int idx = state.heartbeat % 3;
                Fault f;
                f.code = templates[idx].code;
                f.message = templates[idx].msg;
                f.severity = templates[idx].sev;
                f.timestamp = static_cast<int64_t>(time(nullptr)) * 1000;
                if (state.faults.size() >= 5) state.faults.erase(state.faults.begin());
                state.faults.push_back(f);
            }
            ImGui::PopStyleVar();
        } else {
            // Display faults horizontally
            for (size_t i = 0; i < state.faults.size(); i++) {
                const Fault& f = state.faults[i];
                
                ImVec4 bgColor = f.severity == FaultSeverity::Critical ? Colors::DestructiveBg() : 
                                 f.severity == FaultSeverity::Warning ? Colors::WarningBg() : Colors::Muted();
                ImVec4 textColor = f.severity == FaultSeverity::Critical ? Colors::Destructive() :
                                   f.severity == FaultSeverity::Warning ? Colors::Warning() : Colors::Foreground();
                
                char faultText[64];
                snprintf(faultText, sizeof(faultText), "%s: %s", f.code.c_str(), f.message.c_str());
                
                widgets::Badge(faultText, bgColor, textColor);
                
                if (i < state.faults.size() - 1) {
                    ImGui::SameLine(0, 8);
                }
            }
            
            // Clear button
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30);
            ImGui::SetCursorPosY(6);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("X##ClrF", ImVec2(24, 24))) {
                state.faults.clear();
            }
            ImGui::PopStyleVar();
        }
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// Legacy functions kept for compatibility but simplified
void RenderGearIndicator(AppState& state) {
    // Gear is now in header - this is a no-op
}

void RenderCruiseControl(AppState& state) {
    // Cruise is now in system panel - this is a no-op
}

void RenderFaultPanel(AppState& state) {
    // Faults are now in ticker - this is a no-op
}

void RenderCameraFeed(const char* label, const char* type, bool isActive, void* texture) {
    // Cameras now use RenderCameraFeeds - this is a no-op
}

bool RenderTurnIndicator(bool isLeft, bool active) {
    // Turn signals now in system panel
    return false;
}

} // namespace ui
