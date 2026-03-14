#include "dashboard.h"
#include "widgets.h"
#include "theme.h"
#include "icons.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>

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
        auto drawBatteryRow = [&](const char* rowLabel, float soc, float voltage) {
            ImVec4 batColor = GetBatteryColor(soc);
            float labelW = 32.0f;
            float voltW = 52.0f;
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
                float barH = 64.0f;
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

            // Voltage
            char vTxt[16];
            snprintf(vTxt, sizeof(vTxt), "%.0fV", voltage);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::SetWindowFontScale(1.2f);
            ImGui::Text("%s", vTxt);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
        };

        drawBatteryRow("M",  state.mainBattery.soc, state.mainBattery.voltage);
        widgets::Space(10);
        drawBatteryRow("AU", state.suppBattery.soc,  state.suppBattery.voltage);

        widgets::Space(12);

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
            float maxFontSize = radius * 1.5f;
            float fontSize = std::min((bigFont ? bigFont->FontSize : 48.0f) * 1.80f, maxFontSize);
            
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
            float fnrH = 50.0f; // Give it less hardcoded vertical space            const char* letters[] = {"F", "N", "R"};
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

    float availW = ImGui::GetContentRegionAvail().x;
    float availH = ImGui::GetContentRegionAvail().y;
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


    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
}
