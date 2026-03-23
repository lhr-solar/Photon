#include "dashboard.h"
#include "widgets.h"
#include "theme.h"
#include "icons.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <ctime>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ui {

// Color helpers

static ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        1.0f
    );
}

static ImVec4 FaultAwareCardBg(const AppState& state) {
    ImVec4 bg = Colors::Card();
    if (state.canFault) {
        return BlendColor(bg, Colors::Destructive(), 0.18f);
    }
    if (state.canFaultRecoverable) {
        return BlendColor(bg, Colors::Accent(), 0.18f);
    }
    return bg;
}

static ImVec4 FaultAwareScreenBg(const AppState& state) {
    ImVec4 bg = Colors::Background();
    if (state.canFault) {
        return BlendColor(bg, Colors::Destructive(), 0.18f);
    }
    if (state.canFaultRecoverable) {
        return BlendColor(bg, Colors::Accent(), 0.18f);
    }
    return bg;
}

static ImVec4 GetBatteryColor(float soc) {
    if (soc < 20.0f) return Colors::Destructive();
    if (soc < 40.0f) return Colors::Warning();
    return Colors::Primary();
}

static ImVec4 GetSpeedColor(int speed) {
    if (speed > 80) return Colors::Destructive();
    if (speed > 30) return Colors::Warning();
    return Colors::Primary();
}

static const char* BpsFaultName(uint8_t code);

// Forward declarations

static void RenderBatteryPanel(const AppState& state, const ImVec2& size);
static void RenderButtonGrid(AppState& state, const ImVec2& size);
static void RenderSpeedGauge(AppState& state, const ImVec2& size);


//Camera placeholder (used for LEFT / RIGHT / REAR views)

static void RenderCameraView(const AppState& state, const char* label, void* texture, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, FaultAwareCardBg(state));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

    ImGui::BeginChild(label, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (texture) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)), avail);
        } else {
            // Center the label text, split at space for two-line display
            const char* space = strchr(label, ' ');
            if (space) {
                char line1[32], line2[32];
                size_t len1 = static_cast<size_t>(space - label);
                if (len1 >= sizeof(line1)) len1 = sizeof(line1) - 1;
                memcpy(line1, label, len1);
                line1[len1] = '\0';
                snprintf(line2, sizeof(line2), "%s", space + 1);

                ImVec2 s1 = ImGui::CalcTextSize(line1);
                ImVec2 s2 = ImGui::CalcTextSize(line2);
                float totalH = s1.y + s2.y + 4.0f;
                float startY = (avail.y - totalH) * 0.5f;

                ImGui::SetCursorPos(ImVec2((avail.x - s1.x) * 0.5f, startY));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
                ImGui::TextUnformatted(line1);
                ImGui::SetCursorPosX((avail.x - s2.x) * 0.5f);
                ImGui::TextUnformatted(line2);
                ImGui::PopStyleColor();
            } else {
                ImVec2 textSize = ImGui::CalcTextSize(label);
                ImGui::SetCursorPos(ImVec2((avail.x - textSize.x) * 0.5f,
                                           (avail.y - textSize.y) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// Battery 

static void RenderBatteryPanel(const AppState& state, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, FaultAwareCardBg(state));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));

    ImGui::BeginChild("##BatteryPanel", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    {
        float avW = ImGui::GetContentRegionAvail().x;
        float avH = ImGui::GetContentRegionAvail().y;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        {
            char currentLabel[16];
            snprintf(currentLabel, sizeof(currentLabel), "%.0fA",
                 std::abs(state.mainBattery.current));
            ImGuiIO& io = ImGui::GetIO();
            ImFont* medFont = (io.Fonts->Fonts.Size > 2)
                      ? io.Fonts->Fonts[2] : nullptr;
            float fs = 28.0f;
            if (medFont) {
            ImVec2 sz = medFont->CalcTextSizeA(fs, FLT_MAX, 0, currentLabel);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float centeredX = pos.x + (avW - sz.x) * 0.5f;
            dl->AddText(medFont, fs, ImVec2(centeredX, pos.y), ColorToU32(Colors::Foreground()), currentLabel);
            ImGui::Dummy(ImVec2(sz.x, sz.y));
            } else {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%s", currentLabel);
            ImGui::PopStyleColor();
            }
        }
        widgets::Space(12.0f);

        // Helper lambda to draw a battery row
        auto drawBatteryRow = [&](const char* rowLabel, float soc, float voltage, float currentStr = -999.0f) {
            ImVec4 batColor = GetBatteryColor(soc);
            float labelW = 32.0f;
            float voltW = currentStr != -999.0f ? 110.0f : 52.0f;
            float barW = avW - labelW - voltW - 16.0f;
            if (barW < 40.0f) barW = 40.0f;

            // Row label (M or AU)
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::SetWindowFontScale(1.3f);
            ImGui::Text("%s", rowLabel);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            ImGui::SameLine(labelW + 4.0f);

            // SOC bar with percentage overlay
            {
                ImVec2 barPos = ImGui::GetCursorScreenPos();
                float barH = 36.0f; // Thinner battery bar
                float rounding = 3.0f;

                // Background
                ImVec4 bgCol = ColorWithAlpha(Colors::Muted(), 0.5f);
                dl->AddRectFilled(barPos,
                    ImVec2(barPos.x + barW, barPos.y + barH),
                    ColorToU32(bgCol), rounding);
                // Fill
                float pct = std::clamp(soc / 100.0f, 0.0f, 1.0f);
                float fillW = barW * pct;
                if (pct > 0.0f && fillW < 2.0f) fillW = 2.0f;
                if (fillW > 0.0f) {
                    ImDrawFlags fillFlags = ImDrawFlags_RoundCornersLeft;
                    if (fillW >= barW - 0.5f) fillFlags = ImDrawFlags_RoundCornersAll;
                    float fillR = std::min(rounding, fillW * 0.5f);
                    dl->AddRectFilled(barPos,
                        ImVec2(barPos.x + fillW, barPos.y + barH),
                        ColorToU32(batColor), fillR, fillFlags);
                }
                // Border
                dl->AddRect(barPos,
                    ImVec2(barPos.x + barW, barPos.y + barH),
                    ColorToU32(Colors::MutedForeground()), rounding, 0, 1.0f);
                // % text centered in bar
                char socTxt[8];
                snprintf(socTxt, sizeof(socTxt), "%.0f%%", soc);
                ImVec2 socSz = ImGui::CalcTextSize(socTxt);
                dl->AddText(
                    ImVec2(barPos.x + (barW - socSz.x) * 0.5f,
                           barPos.y + (barH - socSz.y) * 0.5f),
                    ColorToU32(Colors::Foreground()), socTxt);

                ImGui::Dummy(ImVec2(barW, barH));
            }

            ImGui::SameLine(0, 8.0f);

            // Voltage and Optional Current
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::SetWindowFontScale(1.2f);
            if (currentStr != -999.0f) {
                ImGui::Text("%.0fV  %.0fA", voltage, currentStr);
            } else {
                ImGui::Text("%.0fV", voltage);
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        };

        drawBatteryRow("M",  state.mainBattery.soc, state.mainBattery.voltage);
        widgets::Space(6);
        drawBatteryRow("AU", state.suppBattery.soc,  state.suppBattery.voltage, state.suppBattery.current);

        widgets::Space(12);

        if (state.bpsFaultCode != 0) {
            int maxTIdx = -1, minVIdx = -1, maxVIdx = -1;
            float maxT=-999, minV=999, maxV=-999;
            for(int i=0; i<(int)state.moduleVoltages.size(); ++i) {
                if (state.moduleVoltages[i] < minV) { minV = state.moduleVoltages[i]; minVIdx = i; }
                if (state.moduleVoltages[i] > maxV) { maxV = state.moduleVoltages[i]; maxVIdx = i; }
            }
            for(int i=0; i<(int)state.moduleTemps.size(); ++i) {
                if (state.moduleTemps[i] > maxT) { maxT = state.moduleTemps[i]; maxTIdx = i; }
            }
            
            const char* strRsn = "";
            int modIdx = -1;
            float val = 0;
            if (state.bpsFaultCode == 1) { strRsn = "OVERVOLT"; modIdx = maxVIdx; val = maxV; }
            else if (state.bpsFaultCode == 2) { strRsn = "UNDERVOLT"; modIdx = minVIdx; val = minV; }
            else if (state.bpsFaultCode == 4) { strRsn = "OVERTEMP"; modIdx = maxTIdx; val = maxT; }
            
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Destructive());
            ImGui::SetWindowFontScale(1.1f);
            if (modIdx != -1) {
                if (state.bpsFaultCode == 4) ImGui::Text("BPS FAULT: Mod %d (%.1fC) %s", modIdx, val, strRsn);
                else ImGui::Text("BPS FAULT: Mod %d (%.2fV) %s", modIdx, val, strRsn);
            } else {
                ImGui::Text("BPS FAULT: %s", BpsFaultName(state.bpsFaultCode));
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            widgets::Space(6);
        }

        // MoCo : Heatsink temp, Voltage, Current
        {
            const float labelW = 56.0f;
            const float colStart = labelW + 6.0f;
            const float colW = std::max(40.0f, (avW - colStart) / 3.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::SetWindowFontScale(1.10f);
            ImGui::TextUnformatted("MoCo");
            ImGui::SetWindowFontScale(1.6f);
            ImGui::PopStyleColor();

            char hsTxt[24];
            char vTxt[24];
            char aTxt[24];
            snprintf(hsTxt, sizeof(hsTxt), "%.0fC", state.motorController.heatsinkTemp);
            snprintf(vTxt,  sizeof(vTxt),  "%.0fV", state.motorController.voltage);
            snprintf(aTxt,  sizeof(aTxt),  "%.0fA", std::abs(state.motorController.current));

            ImGui::SameLine(colStart);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::TextUnformatted(hsTxt);

            ImGui::SameLine(colStart + colW);
            ImGui::TextUnformatted(vTxt);

            ImGui::SameLine(colStart + colW * 2.0f);
            ImGui::TextUnformatted(aTxt);
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// contactors + IGN states

static void RenderButtonGrid(AppState& state, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, FaultAwareCardBg(state));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::BeginChild("##ButtonGrid", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    {

        // Grid layout:
        //   HP   HN   LV
        //   AP    A   AN
        //   MP    M   MM

        struct BtnDef {
            const char* label;
            bool* statePtr;
        };

        BtnDef buttons[3][3] = {
            { {"HP", &state.contactorStates.hvPositive},
              {"HN", &state.contactorStates.hvNegative},
              {"LV", &state.ignitionStates.lvEnabled} },
            { {"AP", &state.contactorStates.arrayPrecharge},
              {"A",  &state.ignitionStates.arrayEnabled},
              {"AN", &state.contactorStates.arrayContactor} },
            { {"MP", &state.contactorStates.motorPrecharge},
              {"M",  &state.ignitionStates.motorEnabled},
              {"MM", &state.contactorStates.motorContactor} },
        };

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImGuiIO& io = ImGui::GetIO();
                ImFont* labelFont = (io.Fonts->Fonts.Size > 2)
                                                                ? io.Fonts->Fonts[2] : nullptr;

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float gap = 1.0f;
        float btnW = (avail.x - gap * 2.0f) / 3.0f;
        float btnH = (avail.y - gap * 2.0f) / 5.0f;

        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                BtnDef& b = buttons[row][col];
                bool active = *b.statePtr;

                char id[32];
                snprintf(id, sizeof(id), "%s##grid%d%d", b.label, row, col);

                ImVec2 cellPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton(id, ImVec2(btnW, btnH));
            

                ImVec4 fillCol = active ? Colors::Primary() : Colors::Muted();
                ImVec4 textCol = active ? Colors::PrimaryForeground()
                                        : Colors::MutedForeground();
                ImVec4 outlineCol = ColorWithAlpha(Colors::MutedForeground(), 0.55f);

                ImVec2 center(cellPos.x + btnW * 0.5f, cellPos.y + btnH * 0.5f);
                float radius = std::min(btnW, btnH) * 0.40f;

                dl->AddCircleFilled(center, radius, ColorToU32(fillCol), 48);
                dl->AddCircle(center, radius, ColorToU32(outlineCol), 48, 2.0f);

                float fs = std::min(radius * 0.90f, 34.0f);
                ImVec2 tSz = labelFont
                    ? labelFont->CalcTextSizeA(fs, FLT_MAX, 0, b.label)
                    : ImGui::CalcTextSize(b.label);
                dl->AddText(labelFont, fs,
                            ImVec2(center.x - tSz.x * 0.5f,
                                   center.y - tSz.y * 0.5f),
                            ColorToU32(textCol), b.label);

                if (col < 2) ImGui::SameLine(0, gap);
            }
            if (row < 2) {
                ImGui::Dummy(ImVec2(0, gap));
            }
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// Speed Gauge
//   heartbeat icon (top), semi-circle arc, large speed number,
//   three status icons (cruise / brake / regen), turn arrows, FNR letters

static void RenderSpeedGauge(AppState& state, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, FaultAwareCardBg(state));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));

    ImGui::BeginChild("##SpeedGauge", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cs = ImGui::GetContentRegionAvail();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        float cX = origin.x + cs.x * 0.5f;   // horizontal center

        // Vertical budget
        float heartbeatH = 28.0f;
        float fnrH       = 92.0f;
        float iconsH     = 36.0f;
        float bottomPad  =  8.0f;
        float arcSpace   = cs.y - heartbeatH - iconsH - fnrH - bottomPad - 10.0f;
        if (arcSpace < 60.0f) arcSpace = 60.0f;

        float radius    = std::min(cs.x * 0.48f, arcSpace * 0.6f);
        float thickness = 14.0f;
        float arcCenterY = origin.y + heartbeatH + 8.0f + radius;
        ImVec2 center(cX, arcCenterY);

        //  hb
        {
            float t     = static_cast<float>(ImGui::GetTime());
            float pulse = 0.5f + 0.5f * sinf(t * 4.0f * static_cast<float>(M_PI));
            ImVec4 hbCol = ColorWithAlpha(Colors::Destructive(), 0.4f + 0.6f * pulse);
            // Move to the left so it doesn't overlap the arc/top area.
            icons::DrawHeart(dl, ImVec2(origin.x + 28.0f, origin.y + 22.0f),
                             30.0f, ColorToU32(hbCol));
        }

        // background arc
        float startAngle = static_cast<float>(M_PI);
        float sweepAngle = static_cast<float>(M_PI);

        dl->PathArcTo(center, radius, startAngle, startAngle + sweepAngle, 64);
        dl->PathStroke(ColorToU32(Colors::Muted()), 0, thickness);

        // progress arc
        float maxSpeed = 80.0f;
        float pct = std::max(0.0f, std::min(1.0f,
            static_cast<float>(state.speed) / maxSpeed));
        if (pct > 0.001f) {
            ImVec4 sCol = GetSpeedColor(state.speed);
            dl->PathArcTo(center, radius,
                          startAngle, startAngle + sweepAngle * pct, 64);
            dl->PathStroke(ColorToU32(sCol), 0, thickness);
        }

        //  Speed
        ImVec2 sSz;
        {
            char speedTxt[8];
            snprintf(speedTxt, sizeof(speedTxt), "%d", state.speed);
            ImGuiIO& io = ImGui::GetIO();
            ImFont* bigFont = (io.Fonts->Fonts.Size > 3)
                                  ? io.Fonts->Fonts[3] : nullptr;
            
            // Limit font size based on radius to avoid clashing
            float maxFontSize = radius * 2.2f;
            float fontSize = std::min((bigFont ? bigFont->FontSize : 48.0f) * 3.0f, maxFontSize);
            
            sSz = bigFont
                ? bigFont->CalcTextSizeA(fontSize, FLT_MAX, 0, speedTxt)
                : ImGui::CalcTextSize(speedTxt);
            float textY = center.y - sSz.y * 0.75f;
            dl->AddText(bigFont, fontSize,
                        ImVec2(cX - sSz.x * 0.5f, textY),
                        ColorToU32(Colors::Foreground()), speedTxt);
        }

        // Status icons below speed
        //    left-turn | cruise control | brake | regen | right-turn
        {
            // Dynamically scale icons so they don't overlap speed or FNR
            float maxIconSize = std::min(48.0f, (arcSpace - sSz.y) * 0.4f);
            float iconSize    = std::max(20.0f, maxIconSize);
            
            // Place icons nicely between the arc center and the FNR text
            float iconY       = center.y + sSz.y * 0.25f + iconSize * 0.5f + 4.0f;
            float iconSpacing = iconSize * 1.2f;

            // Left Turn Signal
            {
                ImVec4 lC = (state.turnSignal == TurnSignal::Left)
                    ? Colors::Accent() : Colors::MutedForeground();
                icons::DrawLeftArrow(dl, ImVec2(cX - iconSpacing * 2.0f, iconY),
                                     iconSize, ColorToU32(lC));
            }
            // Cruise Control
            {
                ImVec4 ccCol = state.cruise.enabled
                    ? Colors::Warning() : Colors::MutedForeground();
                icons::DrawCruiseControl(dl, ImVec2(cX - iconSpacing, iconY),
                                         iconSize, ColorToU32(ccCol));
            }
            // Brake
            {
                ImVec4 bkCol = state.brakeEngaged
                    ? Colors::Destructive() : Colors::MutedForeground();
                icons::DrawBrake(dl, ImVec2(cX, iconY),
                                 iconSize, ColorToU32(bkCol));
            }
            // Regen
            {
                ImVec4 rgCol = state.regenEnabled
                    ? Colors::Success() : Colors::MutedForeground();
                icons::DrawRegen(dl, ImVec2(cX + iconSpacing, iconY),
                                 iconSize, ColorToU32(rgCol));
            }
            // Right Turn Signal
            {
                ImVec4 rC = (state.turnSignal == TurnSignal::Right)
                    ? Colors::Accent() : Colors::MutedForeground();
                icons::DrawRightArrow(dl, ImVec2(cX + iconSpacing * 2.0f, iconY),
                                      iconSize, ColorToU32(rC));
            }
        }

        // FNR gear display below icons
        {
            float maxIconSize = std::min(48.0f, (arcSpace - sSz.y) * 0.4f);
            float iconSize    = std::max(20.0f, maxIconSize);
            float iconBottomY = center.y + sSz.y * 0.25f + iconSize + 4.0f;
            float fnrY = iconBottomY + 8.0f;
            float fnrH = 50.0f; // Give it less hardcoded vertical space
            const char* letters[] = {"F", "N", "R"};
            const Gear  vals[]    = {Gear::Forward, Gear::Neutral, Gear::Reverse};
            float spacing = 96.0f;
            float totalW  = spacing * 2.0f;
            float startX  = cX - totalW * 0.5f;

            ImGuiIO& io2 = ImGui::GetIO();
            ImFont* medFont = (io2.Fonts->Fonts.Size > 2)
                                  ? io2.Fonts->Fonts[2] : nullptr;
            ImFont* hugeFont = (io2.Fonts->Fonts.Size > 3)
                                  ? io2.Fonts->Fonts[3] : nullptr;
            ImFont* baseFont = hugeFont ? hugeFont : medFont;

            for (int i = 0; i < 3; i++) {
                bool sel = (state.gear == vals[i]);
                ImVec4 col = sel ? Colors::Primary()
                                 : Colors::MutedForeground();
                float maxFs = std::min(72.0f, fnrH * 1.2f);
                float fs = sel ? maxFs : maxFs * 0.75f;
                ImFont* useFont = baseFont;
                ImVec2 lSz = useFont
                    ? useFont->CalcTextSizeA(fs, FLT_MAX, 0, letters[i])
                    : ImGui::CalcTextSize(letters[i]);
                float x = startX + spacing * static_cast<float>(i)
                         - lSz.x * 0.5f;
                dl->AddText(useFont, fs,
                            ImVec2(x, fnrY + (fnrH - lSz.y) * 0.5f),
                            ColorToU32(col), letters[i]);
            }

            // Pedal Percentage Bar (inside FNR scope to access fnrY/fnrH)
            {
                float pbW = radius * 1.5f;
                float pbH = 8.0f;
                float pbY = fnrY + fnrH + 5.0f; 
                ImVec2 pPos(cX - pbW * 0.5f, pbY);
                
                // bg
                dl->AddRectFilled(pPos, ImVec2(pPos.x + pbW, pPos.y + pbH), ColorToU32(Colors::Muted()), pbH * 0.5f);
                
                float pct = std::clamp(state.pedalPercent / 100.0f, 0.0f, 1.0f);
                float fillW = pbW * pct;
                if (fillW > 2.0f) {
                    ImDrawFlags fillFlags = ImDrawFlags_RoundCornersLeft;
                    if (fillW >= pbW - 0.5f) fillFlags = ImDrawFlags_RoundCornersAll;
                    dl->AddRectFilled(pPos, ImVec2(pPos.x + fillW, pPos.y + pbH), ColorToU32(Colors::Accent()), pbH * 0.5f, fillFlags);
                }
            }
        }

        // CAN Fault display below FNR
        if (state.canFault || state.canFaultRecoverable) {
            float maxIconSize = std::min(48.0f, (arcSpace - sSz.y) * 0.4f);
            float iconSize    = std::max(20.0f, maxIconSize);
            float iconBottomY2 = center.y + sSz.y * 0.25f + iconSize + 4.0f;
            float fnrBottom = iconBottomY2 + 8.0f + 50.0f;
            float faultY = fnrBottom + 8.0f;

            ImGuiIO& ioF = ImGui::GetIO();
            ImFont* faultFont = (ioF.Fonts->Fonts.Size > 2)
                                    ? ioF.Fonts->Fonts[2] : nullptr;
            float faultFs = 60.0f;

            const bool unrecoverable = state.canFault;

            uint16_t faultId = unrecoverable ? state.canFaultId : state.canFaultRecoverableId;
            const std::string& faultName = unrecoverable ? state.canFaultName : state.canFaultRecoverableName;
            const std::string& faultMsg = unrecoverable ? state.canFaultMessage : state.canFaultRecoverableMessage;

            ImVec4 faultCol = unrecoverable ? Colors::Destructive() : Colors::Accent();

            const bool haveMeta = (faultId != 0) || !faultName.empty();
            const bool haveMsg = !faultMsg.empty();

            std::string faultLine;
            if (haveMeta) {
                char idBuf[16] = {0};
                if (faultId != 0) {
                    snprintf(idBuf, sizeof(idBuf), "0x%03X", static_cast<unsigned>(faultId));
                }

                faultLine = "CAN";
                if (faultId != 0) {
                    faultLine += " ";
                    faultLine += idBuf;
                }
                if (!faultName.empty()) {
                    faultLine += " ";
                    faultLine += faultName;
                }
            } else {
                faultLine = "CAN FAULT";
            }

            if (haveMsg) {
                faultLine += ": ";
                faultLine += faultMsg;
            }
            const char* faultTxt = faultLine.c_str();

            ImVec2 fSz = faultFont
                ? faultFont->CalcTextSizeA(faultFs, FLT_MAX, 0, faultTxt)
                : ImGui::CalcTextSize(faultTxt);
            dl->AddText(faultFont, faultFs,
                        ImVec2(cX - fSz.x * 0.5f, faultY),
                        ColorToU32(faultCol), faultTxt);
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

static const char* BpsFaultName(uint8_t code) {
    switch (code) {
        case 0:  return "No Fault";
        case 1:  return "Overvoltage";
        case 2:  return "Undervoltage";
        case 3:  return "Regen";
        case 4:  return "Overtemperature";
        case 5:  return "Elcon";
        case 6:  return "Array Pchg Timeout";
        case 7:  return "Watchdog";
        case 8:  return "HV+ Ctr Sense";
        case 9:  return "HV- Ctr Sense";
        case 10: return "Array Ctr Sense";
        case 11: return "Array Pchg Ctr Sense";
        case 12: return "Estop 1";
        case 13: return "Estop 2";
        case 14: return "Estop 3";
        case 15: return "Charging Overcurrent";
        case 16: return "Discharging Overcurrent";
        default: return "Unknown";
    }
}

static const char* VcuFaultName(uint8_t code) {
    switch (code) {
        case 0: return "No Fault";
        case 1: return "Motor Ctr Sense";
        case 2: return "Motor Pchg Ctr Sense";
        case 3: return "Motor Pchg Timeout";
        case 4: return "Motor Ctr Fault";
        case 5: return "Motor HV Overvoltage";
        case 6: return "Motor HV Undervoltage";
        default: return "Unknown";
    }
}

static const char* SuppChargerStatusStr(uint8_t s) {
    switch (s) {
        case 0: return "Disabled";
        case 1: return "Done";
        case 2: return "Charging";
        case 3: return "Fault";
        default: return "?";
    }
}

static std::vector<std::pair<std::string, AppState>> g_faultSnapshots;
static int g_selectedSnapshotIndex = -1; // -1 means live data

static void RenderBpsGrid(const AppState& s) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "BPS 32-MODULE GRID");
    
    int nMod = static_cast<int>(std::max(s.moduleVoltages.size(), s.moduleTemps.size()));

    if (ImGui::BeginTable("BpsGrid", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (int i = 0; i < 32; i++) {
            ImGui::TableNextColumn();
            if (i < nMod) {
                float v = i < s.moduleVoltages.size() ? s.moduleVoltages[i] : 0.0f;
                float t = i < s.moduleTemps.size() ? s.moduleTemps[i] : 0.0f;
                
                ImVec4 vCol = Colors::Foreground();
                if (v > 4.2f || v < 2.5f) vCol = Colors::Destructive();
                else if (v > 4.15f || v < 3.0f) vCol = Colors::Warning();
                
                ImVec4 tCol = Colors::Foreground();
                if (t > 45.0f) tCol = Colors::Destructive();
                else if (t > 35.0f) tCol = Colors::Warning();
                
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "#%d", i);
                ImGui::TextColored(vCol, "%.3fV", v);
                ImGui::TextColored(tCol, "%.1fC", t);
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "#%d", i);
                ImGui::Text("-");
                ImGui::Text("-");
            }
        }
        ImGui::EndTable();
    }
}

static void RenderDebugScreen(AppState& liveState) {
    auto avail = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.05f, 0.95f));
    ImGui::BeginChild("DebugScreenPanel", avail, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());

    // Use current state or snapshot
    const AppState& state = (g_selectedSnapshotIndex >= 0 && g_selectedSnapshotIndex < g_faultSnapshots.size()) 
                            ? g_faultSnapshots[g_selectedSnapshotIndex].second 
                            : liveState;

    // Header & Snapshot Controls
    ImGuiIO& dbgIo = ImGui::GetIO();
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text(" DEBUG");
    ImGui::SameLine();
    ImGui::SetWindowFontScale(1.0f);
    
    ImGui::SetNextItemWidth(400.0f);
    if (ImGui::BeginCombo("##Snapshots", g_selectedSnapshotIndex == -1 ? "[LIVE DATA]" : g_faultSnapshots[g_selectedSnapshotIndex].first.c_str())) {
        if (ImGui::Selectable("[LIVE DATA]", g_selectedSnapshotIndex == -1)) g_selectedSnapshotIndex = -1;
        for (int i = 0; i < g_faultSnapshots.size(); i++) {
            if (ImGui::Selectable(g_faultSnapshots[i].first.c_str(), g_selectedSnapshotIndex == i)) {
                g_selectedSnapshotIndex = i;
            }
        }
        ImGui::EndCombo();
    }
    
    if (ImGui::Button("Clear Snapshots")) {
        g_faultSnapshots.clear();
        g_selectedSnapshotIndex = -1;
    }
    
    ImGui::SameLine();
    if (state.simulationEnabled) { ImGui::TextColored(ImVec4(1,1,0,1), "[SIM]"); ImGui::SameLine(); }
    ImGui::Text("HB:%d %.0fFPS %.1fms", state.heartbeat, dbgIo.Framerate, dbgIo.DeltaTime * 1000.0f);
    ImGui::Separator();

    float colW = avail.x * 0.5f - 10.0f;
    auto flt = [](bool f) { return f ? "FLT" : "ok"; };

    // ===== LEFT COLUMN (INPUTS) =====
    ImGui::BeginChild("DebugL", ImVec2(colW, avail.y - 70), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextColored(Colors::Primary(), "INPUTS");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    widgets::Space(2);

    // LWS Standard (Steering)
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "STEERING SENSOR (LWS)");
    ImGui::Text(" Angle: %.1f deg  %s", state.steeringAngle, state.steeringSensorOK ? "OK" : "FAULT");
    widgets::Space(4);

    // Driver Inputs
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "DRIVER INPUTS");
    ImGui::Text(" Horn:%s Hazard:%s PTT:%s Cruise Set:%s Regen Act:%s",
        state.hornPressed ? "ON" : "-", state.hazardPressed ? "ON" : "-",
        state.pttPressed ? "ON" : "-", state.cruiseSet ? "ON" : "-",
        state.regenActivate ? "ON" : "-");
    ImGui::Text(" Gear: %s  Turn: %s", GearToString(state.gear), 
        state.turnSignal == TurnSignal::Left ? "L" : state.turnSignal == TurnSignal::Right ? "R" : "-");
    widgets::Space(4);

    // Accel/Brake Pedals
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "ACCEL / BRAKE PEDALS");
    ImGui::Text(" Accel: Total %.0f%%  Main %d%% Red %d%% V:%.2f/%.2f", state.pedalPercent, state.accelPosMain, state.accelPosRedundant, state.accelVoltMain, state.accelVoltRedundant);
    ImGui::Text("        Faults: Main:%s Red:%s", flt(state.accelMainFault), flt(state.accelRedundantFault));
    ImGui::Text(" Brake: Engaged:%s  Main %d%% Red %d%% V:%.2f/%.2f", state.brakeEngaged ? "YES" : "NO", state.brakePosMain, state.brakePosRedundant, state.brakeVoltMain, state.brakeVoltRedundant);
    ImGui::Text("        Faults: Main:%s Red:%s", flt(state.brakeMainFault), flt(state.brakeRedundantFault));
    ImGui::Text(" Pressure: %.0f / %.0f psi (%s/%s)", state.brakePressure1, state.brakePressure2, flt(state.brakePressure1Fault), flt(state.brakePressure2Fault));
    widgets::Space(4);
    
    // Hardware Switched Contactors / Ignitions 
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "CONTACTORS / IGNITION SWITCHES");
    ImGui::Text(" HV+:%s -:%s ArrP:%s Arr:%s MtrP:%s Mtr:%s",
        state.contactorStates.hvPositive ? "C" : "o", state.contactorStates.hvNegative ? "C" : "o",
        state.contactorStates.arrayPrecharge ? "C" : "o", state.contactorStates.arrayContactor ? "C" : "o",
        state.contactorStates.motorPrecharge ? "C" : "o", state.contactorStates.motorContactor ? "C" : "o");
    ImGui::Text(" Ignition: LV:%s  Array:%s  Motor:%s",
        state.ignitionStates.lvEnabled ? "ON" : "off",
        state.ignitionStates.arrayEnabled ? "ON" : "off",
        state.ignitionStates.motorEnabled ? "ON" : "off");
    widgets::Space(4);

    // CAN Faults (Aggregated Unrecoverable)
    ImGui::TextColored(Colors::Destructive(), "CAN FAULTS AGGREGATE");
    if (state.canFault)
        ImGui::Text(" Unrecoverable: 0x%03X %s", state.canFaultId, state.canFaultName.c_str());
    if (state.canFaultRecoverable)
        ImGui::Text(" Recoverable: 0x%03X %s", state.canFaultRecoverableId, state.canFaultRecoverableName.c_str());
    if (!state.canFault && !state.canFaultRecoverable)
        ImGui::Text(" None");
    
    ImGui::EndChild();

    // ===== RIGHT COLUMN (OUTPUTS) =====
    ImGui::SameLine();
    ImGui::BeginChild("DebugR", ImVec2(colW, avail.y - 70), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextColored(Colors::Accent(), "OUTPUTS & STATUS");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    widgets::Space(2);

    // Motor Controller Outputs
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "MOTOR CONTROLLER (MoCo)");
    ImGui::Text(" Vel: %d km/h  Bus: %.1fV %.1fA", state.speed, state.motorController.voltage, state.motorController.current);
    ImGui::Text(" Phase: %.1fA (B) %.1fA (C)  BEMF: %.1fV (Q) %.1fV (D)", state.motorController.phaseCurrentB, state.motorController.phaseCurrentC, state.motorController.backEmfQ, state.motorController.backEmfD);
    ImGui::Text(" Temp: Heatsink %.1fC  Precharge Motor: %.1fV", state.motorController.heatsinkTemp, state.prechargeMotorV);
    ImGui::TextColored(Colors::Warning(), " Limits Active: %s%s%s%s%s%s%s%s", 
      state.motorController.limitMotorCurrent ? "MotorCurrent " : "",
      state.motorController.limitVelocity ? "Velocity " : "",
      state.motorController.limitBusCurrent ? "BusCurrent " : "",
      state.motorController.limitBusVoltageUpper ? "BusVolUpper " : "",
      state.motorController.limitBusVoltageLower ? "BusVolLower " : "",
      state.motorController.limitIpmOrMotorTemp ? "Temp " : "",
      state.motorController.limitOutputVoltage ? "OutVolPWM " : "",
      (!state.motorController.limitMotorCurrent && !state.motorController.limitVelocity && !state.motorController.limitBusCurrent && !state.motorController.limitBusVoltageUpper && !state.motorController.limitBusVoltageLower && !state.motorController.limitIpmOrMotorTemp && !state.motorController.limitOutputVoltage) ? "None" : "");
    widgets::Space(4);

    // VCU Status & Precharge msg
    ImVec4 vcuFCol = state.vcuFaultCode != 0 ? Colors::Destructive() : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "VCU STATUS");
    ImGui::TextColored(vcuFCol, " Fault: %d (%s)  FSM State: %d", state.vcuFaultCode, VcuFaultName(state.vcuFaultCode), state.vcuFsmState);
    ImGui::Text(" Motor Ready:%s Pedals:%s Driver Input:%s", state.motorReadyToDrive?"Y":"N", state.vcuPedalsOK?"OK":"NO", state.vcuDriverInputOK?"OK":"NO");
    ImGui::Text(" Regen:%s (Active:%s)", state.vcuRegenOK?"OK":"NO", state.vcuRegenActive?"Y":"N");
    widgets::Space(4);

    // MPPT Solar
    float mpptPIn = state.mppt[0].vin * state.mppt[0].iin;
    float mpptPOut = state.mppt[0].vout * state.mppt[0].iout;
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "MPPT SOLAR");
    ImGui::Text(" In: %.1fV %.2fA (%.0fW)  Out: %.1fV %.2fA (%.0fW)",
        state.mppt[0].vin, state.mppt[0].iin, mpptPIn,
        state.mppt[0].vout, state.mppt[0].iout, mpptPOut);
    ImGui::Text(" Heatsink: %.0fC  Ambient: %.0fC  Fault:%d  Mode:%d",
        state.mppt[0].heatsinkTemp, state.mppt[0].ambientTemp,
        state.mppt[0].fault, state.mppt[0].mode);
    widgets::Space(4);

    // Cooling
    ImVec4 pumpCol = state.pumpFault ? Colors::Destructive() : Colors::Foreground();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "COOLING");
    ImGui::Text(" Coolant: %.1f / %.1fC  Flow: %.2f / %.2f L/min",
        state.coolantTemp1, state.coolantTemp2, state.flowRate1, state.flowRate2);
    ImGui::TextColored(pumpCol, " Pump: %d%% %s", state.pumpDuty, state.pumpFault?"FAULT":"OK");
    widgets::Space(4);

    // LV + Cameras + Lights
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "LV / CAMERAS / LIGHTS");
    ImGui::Text(" HV DCDC: Sel:%s Fault:%s Valid:%s",
        state.lvHvDcdcSelected?"Y":"N", state.lvHvDcdcFault?"YES":"no", state.lvHvDcdcValid?"Y":"N");
    ImGui::Text(" Supp Batt: Sel:%s Fault:%s Valid:%s",
        state.lvSuppBattSelected?"Y":"N", state.lvSuppBattFault?"YES":"no", state.lvSuppBattValid?"Y":"N");
    ImGui::Text(" Enable: SuppBatt:%s PSU:%s",
        state.lvEnSuppBattery?"ON":"off", state.lvEnPowerSupply?"ON":"off");
    ImGui::Text(" Supp Batt Info: SOC: %.1f%%  %.1fV  %.1fA  Charger: %s  DCDC: %.1fV %.0fmA",
        state.suppBattery.soc, state.suppBattery.voltage, state.suppBattery.current,
        SuppChargerStatusStr(state.suppChargerStatus), state.suppDcdcVoltage, state.suppDcdcCurrent);
    ImGui::Text(" Cameras: Backup:%s Left:%s Right:%s",
        state.cameraBackup?"Y":"N", state.cameraLeft?"Y":"N", state.cameraRight?"Y":"N");
    ImVec4 ctlCol = (state.lightingFaults||state.controlsLeaderFault) ? Colors::Destructive() : Colors::Foreground();
    ImGui::TextColored(ctlCol, " Light Faults: %d  Controls Faults: %d", state.lightingFaults, state.controlsLeaderFault);
    widgets::Space(4);

    // BPS Status
    ImVec4 bpsFCol = state.bpsFaultCode != 0 ? Colors::Destructive() : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "BPS STATUS");
    ImGui::TextColored(bpsFCol, " Fault: %d (%s)  Regen: %s  Charge: %s",
        state.bpsFaultCode, BpsFaultName(state.bpsFaultCode),
        state.bpsRegenOK ? "OK" : "NO", state.bpsChargeOK ? "OK" : "NO");
    
    // Render 32-Module Grid
    widgets::Space(4);
    RenderBpsGrid(state);

    ImGui::EndChild();

    ImGui::PopStyleColor(); // Text
    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}

void RenderDashboard(AppState& state) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar   |
        ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoCollapse   |
        ImGuiWindowFlags_NoScrollbar  |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, FaultAwareScreenBg(state));

    if (!ImGui::Begin("LHR Photon Dashboard", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    // --- FAULT SNAPSHOT DETECTION ---
    static uint8_t lastBpsFaultCode = 0;
    static uint8_t lastVcuFaultCode = 0;
    static uint16_t lastCanFaultId = 0;

    if (!state.showDebugScreen) { // Only record edge-triggered snapshot when not actively browsing snapshots
        if (state.bpsFaultCode != 0 && lastBpsFaultCode == 0) {
            char timeBuf[64];
            time_t now = time(nullptr);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", localtime(&now));
            std::string reason = std::string(timeBuf) + " BPS Fault: " + BpsFaultName(state.bpsFaultCode);
            g_faultSnapshots.push_back({reason, state});
        }
        if (state.vcuFaultCode != 0 && lastVcuFaultCode == 0) {
            char timeBuf[64];
            time_t now = time(nullptr);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", localtime(&now));
            std::string reason = std::string(timeBuf) + " VCU Fault: " + VcuFaultName(state.vcuFaultCode);
            g_faultSnapshots.push_back({reason, state});
        }
        if (state.canFault && state.canFaultId != lastCanFaultId) {
            char timeBuf[64];
            time_t now = time(nullptr);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", localtime(&now));
            std::string reason = std::string(timeBuf) + " CAN Fault: " + state.canFaultName;
            g_faultSnapshots.push_back({reason, state});
        }
    }

    lastBpsFaultCode = state.bpsFaultCode;
    lastVcuFaultCode = state.vcuFaultCode;
    lastCanFaultId = state.canFault ? state.canFaultId : 0;
    // --------------------------------


    float availW = ImGui::GetContentRegionAvail().x;
    float availH = ImGui::GetContentRegionAvail().y;
    
    // Invisible buttons for debug screen toggle
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##DebugToggleLeft", ImVec2(80, availH));
    bool leftHeld = ImGui::IsItemActive();
    
    ImGui::SetCursorPos(ImVec2(availW - 80, 0));
    ImGui::InvisibleButton("##DebugToggleRight", ImVec2(80, availH));
    bool rightHeld = ImGui::IsItemActive();
    
    static bool wasBothHeld = false;
    bool bothHeld = leftHeld && rightHeld;
    if (bothHeld && !wasBothHeld) {
        state.showDebugScreen = !state.showDebugScreen;
    }
    wasBothHeld = bothHeld;

    // Desktop shortcut: press 'D' to toggle debug screen
    if (ImGui::IsKeyPressed(ImGuiKey_D)) {
        state.showDebugScreen = !state.showDebugScreen;
    }
    
    // Reset cursor for actual layout
    ImGui::SetCursorPos(ImVec2(0, 0));

    if (state.showDebugScreen) {
        RenderDebugScreen(state);
    } else {
        float gap       = 4.0f;
        float rowTop    = availH * 0.50f;
        float rowBottom = availH - rowTop - gap;

        // Top row: wider camera views, narrower speed gauge
    float topColLeft   = availW * 0.34f;
    float topColRight  = availW * 0.32f;
    float topColCenter = availW - topColLeft - topColRight - gap * 2.0f;

    // Bottom row even proportions
    float botColLeft   = availW * 0.26f;
    float botColRight  = availW * 0.24f;
    float botColCenter = availW - botColLeft - botColRight - gap * 2.0f;

    RenderCameraView(state, "LEFT VIEW",
                     state.leftCameraTexture,
                     ImVec2(topColLeft, rowTop));
    ImGui::SameLine(0, gap);

    RenderSpeedGauge(state, ImVec2(topColCenter, rowTop));
    ImGui::SameLine(0, gap);

    RenderCameraView(state, "RIGHT VIEW",
                     state.rightCameraTexture,
                     ImVec2(topColRight, rowTop));

    ImGui::Dummy(ImVec2(0, gap)); 

    // bottom

    RenderBatteryPanel(state, ImVec2(botColLeft, rowBottom));
    ImGui::SameLine(0, gap);

    RenderCameraView(state, "REAR VIEW",
                     state.rearCameraTexture,
                     ImVec2(botColCenter, rowBottom));
    ImGui::SameLine(0, gap);

        RenderButtonGrid(state, ImVec2(botColRight, rowBottom));
    } // End normal dashboard render

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
}
