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
#include <cctype>

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
        return BlendColor(bg, Colors::Destructive(), 0.10f);
    }
    if (state.canFaultRecoverable) {
        return BlendColor(bg, Colors::Accent(), 0.10f);
    }
    return bg;
}

static ImVec4 FaultAwareScreenBg(const AppState& state) {
    ImVec4 bg = Colors::Background();
    if (state.canFault) {
        return BlendColor(bg, Colors::Destructive(), 0.06f);
    }
    if (state.canFaultRecoverable) {
        return BlendColor(bg, Colors::Accent(), 0.06f);
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

struct BpsExtrema {
    int maxTempIdx = -1;
    int minVoltIdx = -1;
    int maxVoltIdx = -1;
    float maxTemp = 0.0f;
    float minVolt = 0.0f;
    float maxVolt = 0.0f;
};

static BpsExtrema ComputeBpsExtrema(const AppState& state) {
    BpsExtrema extrema{};
    float maxTemp = -FLT_MAX;
    float minVolt = FLT_MAX;
    float maxVolt = -FLT_MAX;

    for (int i = 0; i < static_cast<int>(state.moduleVoltages.size()); ++i) {
        const float v = state.moduleVoltages[static_cast<size_t>(i)];
        if (v < minVolt) {
            minVolt = v;
            extrema.minVoltIdx = i;
            extrema.minVolt = v;
        }
        if (v > maxVolt) {
            maxVolt = v;
            extrema.maxVoltIdx = i;
            extrema.maxVolt = v;
        }
    }

    for (int i = 0; i < static_cast<int>(state.moduleTemps.size()); ++i) {
        const float t = state.moduleTemps[static_cast<size_t>(i)];
        if (t > maxTemp) {
            maxTemp = t;
            extrema.maxTempIdx = i;
            extrema.maxTemp = t;
        }
    }

    return extrema;
}

static bool FormatBpsFaultDetail(const AppState& state, char* buffer, size_t bufferSize) {
    const uint8_t bpsFaultCode = (uint8_t)state.get("BPS_Fault");
    if (!buffer || bufferSize == 0 || bpsFaultCode == 0) {
        return false;
    }

    const BpsExtrema extrema = ComputeBpsExtrema(state);
    switch (bpsFaultCode) {
        case 1:
            if (extrema.maxVoltIdx >= 0) {
                snprintf(buffer, bufferSize, "BPS: Mod %d (%.2fV) OVERVOLT", extrema.maxVoltIdx, extrema.maxVolt);
                return true;
            }
            break;
        case 2:
            if (extrema.minVoltIdx >= 0) {
                snprintf(buffer, bufferSize, "BPS: Mod %d (%.2fV) UNDERVOLT", extrema.minVoltIdx, extrema.minVolt);
                return true;
            }
            break;
        case 4:
            if (extrema.maxTempIdx >= 0) {
                snprintf(buffer, bufferSize, "BPS: Mod %d (%.1fC) OVERTEMP", extrema.maxTempIdx, extrema.maxTemp);
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

static bool FormatBpsExtremaSummary(const AppState& state, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return false;
    }

    const BpsExtrema extrema = ComputeBpsExtrema(state);
    const char* lowFmt = extrema.minVoltIdx >= 0 ? "Lo #%d %.2fV" : "Lo --";
    const char* highFmt = extrema.maxVoltIdx >= 0 ? "Hi #%d %.2fV" : "Hi --";
    const char* hotFmt = extrema.maxTempIdx >= 0 ? "Hot #%d %.1fC" : "Hot --";

    char lowBuf[32];
    char highBuf[32];
    char hotBuf[32];

    if (extrema.minVoltIdx >= 0) snprintf(lowBuf, sizeof(lowBuf), lowFmt, extrema.minVoltIdx, extrema.minVolt);
    else snprintf(lowBuf, sizeof(lowBuf), "%s", lowFmt);

    if (extrema.maxVoltIdx >= 0) snprintf(highBuf, sizeof(highBuf), highFmt, extrema.maxVoltIdx, extrema.maxVolt);
    else snprintf(highBuf, sizeof(highBuf), "%s", highFmt);

    if (extrema.maxTempIdx >= 0) snprintf(hotBuf, sizeof(hotBuf), hotFmt, extrema.maxTempIdx, extrema.maxTemp);
    else snprintf(hotBuf, sizeof(hotBuf), "%s", hotFmt);

    snprintf(buffer, bufferSize, "%s  %s  %s", lowBuf, highBuf, hotBuf);
    return true;
}

static const char* BpsFaultName(uint8_t code);
static const char* VcuFaultName(uint8_t code);

// Forward declarations

static void RenderBatteryPanel(const AppState& state, const ImVec2& size);
static void RenderButtonGrid(AppState& state, const ImVec2& size);
static void RenderSpeedGauge(AppState& state, const ImVec2& size);

// Highest-priority active fault, short-form. BPS always wins; a specific motor
// controller cause is more actionable than the VCU's aggregate motor flag.
struct ActiveFaultInfo {
    std::string label;   // e.g. "BPS Fault"
    std::string detail;  // e.g. "Overtemp"
};

static bool GetActiveFault(const AppState& state, ActiveFaultInfo& out) {
    const uint8_t bpsFaultCode = (uint8_t)state.get("BPS_Fault");
    const uint8_t vcuFaultCode = (uint8_t)state.get("VCU_Fault");
    if (bpsFaultCode != 0) {
        out.label = "BPS Fault";
        out.detail = BpsFaultName(bpsFaultCode);
        return true;
    }
    size_t motorFaultCount = 0;
    for (const Fault& fault : state.faults) {
        if (fault.name != "Motor") continue;
        if (motorFaultCount == 0) out.detail = fault.message;
        ++motorFaultCount;
    }
    if (motorFaultCount != 0) {
        out.label = "Motor Fault";
        if (motorFaultCount > 1) {
            out.detail += " +" + std::to_string(motorFaultCount - 1) + " more";
        }
        return true;
    }
    if (vcuFaultCode != 0) {
        out.label = "VCU Fault";
        out.detail = VcuFaultName(vcuFaultCode);
        return true;
    }
    return false;
}

// Big knockout-style fault banner: a solid fault-colored plate sized to the
// text, with the text itself punched through in the page background color so
// it reads as a cutout rather than tinting the whole screen.
static void RenderFaultBanner(const AppState& state) {
    ActiveFaultInfo info;
    if (!GetActiveFault(state, info)) {
        return;
    }

    char text[128];
    snprintf(text, sizeof(text), "%s: %s", info.label.c_str(), info.detail.c_str());
    for (char* c = text; *c; ++c) {
        *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFont* bigFont = (io.Fonts->Fonts.Size > 2) ? io.Fonts->Fonts[2] : ImGui::GetFont();
    float fontSize = std::min(io.DisplaySize.x * 0.052f, bigFont->LegacySize * 2.35f) * 2.0f;
    ImVec2 textSz = bigFont->CalcTextSizeA(fontSize, FLT_MAX, 0, text);

    float padX = 14.0f, padY = 7.0f;
    const float maxPlateWidth = io.DisplaySize.x * 0.92f;
    if (textSz.x + padX * 2.0f > maxPlateWidth) {
        fontSize *= (maxPlateWidth - padX * 2.0f) / textSz.x;
        textSz = bigFont->CalcTextSizeA(fontSize, FLT_MAX, 0, text);
    }
    ImVec2 plateSz(textSz.x + padX * 2.0f, textSz.y + padY * 2.0f);
    ImVec2 plateMin((io.DisplaySize.x - plateSz.x) * 0.5f,
                     io.DisplaySize.y * 0.41f);
    ImVec2 plateMax(plateMin.x + plateSz.x, plateMin.y + plateSz.y);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(plateMin, plateMax, ColorToU32(Colors::Destructive()), 12.0f);

    // No bold TTF is loaded, so fake it: stamp the knockout text a couple of
    // times at whole-pixel offsets to thicken the strokes. Sub-pixel offsets
    // blur into mush under AA, so keep this to integer pixels and a small ring.
    const ImU32 knockoutColor = ColorToU32(Colors::Background());
    ImVec2 textOrigin(plateMin.x + padX, plateMin.y + padY);
    static const ImVec2 kBoldOffsets[] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
        {2.0f, 0.0f}, {0.0f, 2.0f}, {2.0f, 1.0f}, {1.0f, 2.0f},
    };
    for (const ImVec2& off : kBoldOffsets) {
        dl->AddText(bigFont, fontSize,
                    ImVec2(textOrigin.x + off.x, textOrigin.y + off.y),
                    knockoutColor, text);
    }
}


// Camera tile shared by the left, right, and rear live feeds.

static void RenderCameraView(const AppState& state, const char* label, ImTextureData* texture,
                             const ImVec2& size, bool mirrorX = false) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, FaultAwareCardBg(state));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

    ImGui::BeginChild(label, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (texture) {
            // Camera frames arrive with sky and floor inverted. Use the
            // native vertical orientation for every camera feed.
            ImGui::Image(texture->GetTexRef(), avail,
                         mirrorX ? ImVec2(1.0f, 0.0f) : ImVec2(0.0f, 0.0f),
                         mirrorX ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 1.0f));
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

static ImVec4 FaultColor(FaultSeverity severity) {
    switch (severity) {
        case FaultSeverity::Info: return Colors::Primary();
        case FaultSeverity::Warning: return Colors::Accent();
        case FaultSeverity::Critical: return Colors::Destructive();
    }
    return Colors::Destructive();
}

static std::string FaultDisplayText(const Fault& fault) {
    if (fault.name.empty()) {
        return fault.message.empty() ? "Fault" : fault.message;
    }
    if (fault.message.empty()) {
        return fault.name;
    }
    return fault.name + " - " + fault.message;
}

// Battery 

static void RenderBatteryPanel(const AppState& state, const ImVec2& size) {
    constexpr float kPanelFontScale = 1.55f;
    constexpr float kPanelWarningFontScale = 1.75f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, FaultAwareCardBg(state));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
    ImGui::BeginChild("##BatteryPanel", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetWindowFontScale(kPanelFontScale);
    {
        float avW = ImGui::GetContentRegionAvail().x;
        float avH = ImGui::GetContentRegionAvail().y;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Battery section ──────────────────────────────────────────────
        // SOC estimated from pack voltage using nonlinear 32S Li-ion equation:
        //   SOC = 100 * ((V - 83.2) / 51.2) ^ 0.55   clamped [0, 100]
        // Main battery — bar (smaller) + pack current to the right
        {
            const float mainVoltage  = (float)state.get("Main_Battery_Voltage");
            const float packCurrent  = (float)state.get("Main_Battery_Current");

            // Nonlinear SOC estimate for 32S Li-ion pack
            float socEst = 0.0f;
            {
                float ratio = (mainVoltage - 83.2f) / 51.2f;
                if (ratio > 0.0f)
                    socEst = 100.0f * std::pow(ratio, 0.55f);
                socEst = std::clamp(socEst, 0.0f, 100.0f);
            }

            const ImGuiIO& io2 = ImGui::GetIO();
            ImFont* medFont = (io2.Fonts->Fonts.Size > 2) ? io2.Fonts->Fonts[2] : nullptr;

            // Layout: [label] [bar] [voltage] [current (big)]
            const float labelW   = 48.0f;
            const float voltW    = 52.0f;
            const float gap      = 6.0f;
            const float currentW = 80.0f;
            const float barW     = std::max(40.0f, avW - labelW - voltW - currentW - gap * 3.0f);
            const float barH     = 22.0f;
            const float rowH     = barH;

            // "Main" label
            ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Main");
            ImGui::PopStyleColor();
            ImGui::SameLine(labelW + gap);

            // SOC bar
            {
                ImVec2 barPos = ImGui::GetCursorScreenPos();
                barPos.y += (rowH - barH) * 0.5f;
                ImVec4 batColor  = GetBatteryColor(socEst);
                float  rounding  = 3.0f;

                dl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                                  ColorToU32(ColorWithAlpha(Colors::Muted(), 0.5f)), rounding);

                float fillW = barW * (socEst / 100.0f);
                if (fillW > 1.0f) {
                    ImDrawFlags ff = (fillW >= barW - 0.5f)
                                   ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft;
                    dl->AddRectFilled(barPos, ImVec2(barPos.x + fillW, barPos.y + barH),
                                      ColorToU32(batColor), std::min(rounding, fillW * 0.5f), ff);
                }
                dl->AddRect(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                            ColorToU32(ColorWithAlpha(Colors::MutedForeground(), 0.42f)), rounding);

                // SOC % centered in bar
                char socTxt[8];
                snprintf(socTxt, sizeof(socTxt), "%.0f%%", socEst);
                ImVec2 sSz = ImGui::CalcTextSize(socTxt);
                dl->AddText(ImVec2(barPos.x + (barW - sSz.x) * 0.5f,
                                   barPos.y + (barH - sSz.y) * 0.5f),
                            ColorToU32(Colors::Foreground()), socTxt);
                if (fillW > 0.5f) {
                    dl->PushClipRect(barPos, ImVec2(barPos.x + fillW, barPos.y + barH), true);
                    dl->AddText(ImVec2(barPos.x + (barW - sSz.x) * 0.5f,
                                       barPos.y + (barH - sSz.y) * 0.5f),
                                ColorToU32(Colors::PrimaryForeground()), socTxt);
                    dl->PopClipRect();
                }
                ImGui::Dummy(ImVec2(barW, rowH));
            }

            // Voltage
            ImGui::SameLine(0, gap);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("%.0fV", mainVoltage);
            ImGui::PopStyleColor();

            // Pack current — larger font to the right
            ImGui::SameLine(0, gap);
            {
                char aTxt[16];
                snprintf(aTxt, sizeof(aTxt), "%.0f A", packCurrent);
                float fs = 32.0f;
                ImVec2 pos = ImGui::GetCursorScreenPos();
                if (medFont) {
                    ImVec2 sz = medFont->CalcTextSizeA(fs, FLT_MAX, 0, aTxt);
                    pos.y += (rowH - sz.y) * 0.5f;
                    dl->AddText(medFont, fs, pos, ColorToU32(Colors::Foreground()), aTxt);
                    ImGui::Dummy(ImVec2(currentW, rowH));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
                    ImGui::Text("%s", aTxt);
                    ImGui::PopStyleColor();
                }
            }
        }

        widgets::Space(2);

        // Aux battery — no bar, just label + voltage + current inline
        {
            const float auxVoltage = (float)state.get("Supplemental_Battery_Voltage");
            const float auxCurrent = (float)(state.get("Supplemental_Battery_Current") * 0.001);
            const float auxSoc     = (float)state.get("Supplemental_Battery_SOC");

            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Aux");
            ImGui::PopStyleColor();
            ImGui::SameLine(48.0f + 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.0f V  %.0f A  %.0f%%", auxVoltage, auxCurrent, auxSoc);
            ImGui::PopStyleColor();
        }

        widgets::Space(0);

        // Two-column labeled grid: label on left, value on right
        // Columns: [label col] [value col] [label col] [value col]
        {
            const float valColW  = std::max(60.0f, avW * 0.22f);
            const float lblColW  = std::max(70.0f, avW * 0.27f);
            const float col0     = 0.0f;
            const float col1     = col0 + lblColW;
            const float col2     = col1 + valColW + 8.0f;
            const float col3     = col2 + lblColW;

            const float mcHeatsinkTemp = (float)state.get("MC_HeatsinkTemp");
            const float mcMotorTemp    = (float)state.get("MC_MotorTemp");

            ImVec4 mtCol = mcMotorTemp > 90.0f ? Colors::Destructive()
                         : mcMotorTemp > 70.0f ? Colors::Warning()
                                               : Colors::Foreground();

            // Row 1: Heatsink Temp | Motor Temp
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Heatsink");
            ImGui::PopStyleColor();
            ImGui::SameLine(col1);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.0f \xC2\xB0""C", mcHeatsinkTemp);
            ImGui::PopStyleColor();
            ImGui::SameLine(col2);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Motor Temp");
            ImGui::PopStyleColor();
            ImGui::SameLine(col3);
            ImGui::PushStyleColor(ImGuiCol_Text, mtCol);
            ImGui::Text("%.0f \xC2\xB0""C", mcMotorTemp);
            ImGui::PopStyleColor();

            // Row 2: Bus Voltage | Bus Current
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Bus Voltage");
            ImGui::PopStyleColor();
            ImGui::SameLine(col1);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.0f V", state.get("MC_BusVoltage"));
            ImGui::PopStyleColor();
            ImGui::SameLine(col2);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Bus Current");
            ImGui::PopStyleColor();
            ImGui::SameLine(col3);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.0f A", state.get("MC_BusCurrent"));
            ImGui::PopStyleColor();
            
        }

        widgets::Space(4);

        {
            const float valColW  = std::max(60.0f, avW * 0.22f);
            const float lblColW  = std::max(70.0f, avW * 0.27f);
            const float col0     = 0.0f;
            const float col1     = col0 + lblColW;
            const float col2     = col1 + valColW + 8.0f;
            const float col3     = col2 + lblColW;

            const float driveVel = (float)state.get("MC_MotorVelocitySetpoint");
            const float driveCur = (float)state.get("MC_MotorCurrentSetpoint") * 100.0f;
            const float powerCmd = (float)state.get("MC_MotorPowerSetpoint") * 100.0f;
            const float prechargeMotorV = (float)state.get("VCU_Precharge_Motor_Voltage");
            const float prechargeBattV  = (float)state.get("VCU_Precharge_Battery_Voltage");

            // Row 1: Drive Velocity | Drive Current
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Drive Vel");
            ImGui::PopStyleColor();
            ImGui::SameLine(col1);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.0f rpm", driveVel);
            ImGui::PopStyleColor();
            ImGui::SameLine(col2);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Drive Cur");
            ImGui::PopStyleColor();
            ImGui::SameLine(col3);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.0f %%", driveCur);
            ImGui::PopStyleColor();

            // Row 2: Power Cmd | (empty)
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Power Cmd");
            ImGui::PopStyleColor();
            ImGui::SameLine(col1);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.0f %%", powerCmd);
            ImGui::PopStyleColor();

            widgets::Space(0);

            // Row 3: Precharge Motor | Precharge Battery
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Pre Motor");
            ImGui::PopStyleColor();
            ImGui::SameLine(col1);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.1f V", prechargeMotorV);
            ImGui::PopStyleColor();
            ImGui::SameLine(col2);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
            ImGui::Text("Pre Batt");
            ImGui::PopStyleColor();
            ImGui::SameLine(col3);
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
            ImGui::Text("%.1f V", prechargeBattV);
            ImGui::PopStyleColor();
        }

        // ── Motor Limit Flags ────────────────────────────────────────────
        {
            const bool limitMotorCurrent     = state.getBool("MC_LIMIT_MotorCurrent");
            const bool limitVelocity         = state.getBool("MC_LIMIT_Velocity");
            const bool limitBusCurrent       = state.getBool("MC_LIMIT_BusCurrent");
            const bool limitBusVoltageUpper  = state.getBool("MC_LIMIT_BusVoltageUpper");
            const bool limitBusVoltageLower  = state.getBool("MC_LIMIT_BusVoltageLower");
            const bool limitMotorTemp        = state.getBool("MC_LIMIT_MotorTemp");
            const bool limitOutputVoltage    = state.getBool("MC_LIMIT_OutputVoltagePWM");
            const bool anyLimit = limitMotorCurrent || limitVelocity || limitBusCurrent ||
                                  limitBusVoltageUpper || limitBusVoltageLower ||
                                  limitMotorTemp || limitOutputVoltage;
            if (anyLimit) {
                widgets::Space(4);
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::Warning());
                ImGui::TextUnformatted("MOTOR LIMITS ACTIVE:");
                if (limitMotorCurrent)    { ImGui::SameLine(); ImGui::TextUnformatted(" Motor A"); }
                if (limitVelocity)        { ImGui::SameLine(); ImGui::TextUnformatted(" Velocity"); }
                if (limitBusCurrent)      { ImGui::SameLine(); ImGui::TextUnformatted(" Bus A"); }
                if (limitBusVoltageUpper) { ImGui::SameLine(); ImGui::TextUnformatted(" Bus Hi V"); }
                if (limitBusVoltageLower) { ImGui::SameLine(); ImGui::TextUnformatted(" Bus Lo V"); }
                if (limitMotorTemp)       { ImGui::SameLine(); ImGui::TextUnformatted(" Temp"); }
                if (limitOutputVoltage)   { ImGui::SameLine(); ImGui::TextUnformatted(" PWM"); }
                ImGui::PopStyleColor();
            }
        }

        widgets::Space(4);

        // Faults already surfaced by the big banner (see RenderFaultBanner)
        // don't need to repeat in this list.
        ActiveFaultInfo bannerInfo;
        bool bannerActive = GetActiveFault(state, bannerInfo);
        std::vector<const Fault*> listFaults;
        for (const Fault& fault : state.faults) {
            if (bannerActive) {
                if (bannerInfo.label == "BPS Fault" && fault.name == "BPS") continue;
                if (bannerInfo.label == "Motor Fault" && fault.name == "Motor") continue;
                if (bannerInfo.label == "VCU Fault" && fault.name == "VCU") continue;
            }
            listFaults.push_back(&fault);
        }

        if (!listFaults.empty()) {
            int visibleRows = std::min<int>(static_cast<int>(listFaults.size()), 4);
            for (int i = 0; i < visibleRows; ++i) {
                const Fault& fault = *listFaults[static_cast<size_t>(i)];
                std::string faultText = FaultDisplayText(fault);
                ImGui::PushStyleColor(ImGuiCol_Text, FaultColor(fault.severity));
                ImGui::SetWindowFontScale(1.18f);
                ImGui::TextUnformatted(faultText.c_str());
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
            }
            if (listFaults.size() > static_cast<size_t>(visibleRows)) {
                ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
                ImGui::Text("+%zu more", listFaults.size() - static_cast<size_t>(visibleRows));
                ImGui::PopStyleColor();
            }
            widgets::Space(2);
        }

        const float mainBatteryAvgTemp = (float)state.get("Main_Battery_Avg_Temperature");
        if (mainBatteryAvgTemp > 0.0f) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Warning());
            ImGui::Text("Avg Batt Temp: %.1f \xC2\xB0""C", mainBatteryAvgTemp);
            ImGui::PopStyleColor();
            widgets::Space(2);
        }

        {
            // Show extrema as labeled rows        {
            // Show extrema as labeled rows rather than a packed single line
            const BpsExtrema extrema = ComputeBpsExtrema(state);
            if (extrema.minVoltIdx >= 0 || extrema.maxVoltIdx >= 0 || extrema.maxTempIdx >= 0) {
                const float valColW  = std::max(60.0f, avW * 0.22f);
                const float lblColW  = std::max(70.0f, avW * 0.27f);
                const float col1     = lblColW;
                const float col2     = col1 + valColW + 8.0f;
                const float col3     = col2 + lblColW;

                if (extrema.minVoltIdx >= 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
                    ImGui::Text("Low Cell");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(col1);
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
                    ImGui::Text("#%d  %.2f V", extrema.minVoltIdx, extrema.minVolt);
                    ImGui::PopStyleColor();
                }
                if (extrema.maxVoltIdx >= 0) {
                    ImGui::SameLine(col2);
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
                    ImGui::Text("High Cell");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(col3);
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors::Foreground());
                    ImGui::Text("#%d  %.2f V", extrema.maxVoltIdx, extrema.maxVolt);
                    ImGui::PopStyleColor();
                }
                if (extrema.maxTempIdx >= 0) {
                    ImVec4 hotCol = extrema.maxTemp > 50.0f ? Colors::Destructive()
                                 : extrema.maxTemp > 35.0f ? Colors::Warning()
                                                           : Colors::Foreground();
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors::MutedForeground());
                    ImGui::Text("Hottest");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(col1);
                    ImGui::PushStyleColor(ImGuiCol_Text, hotCol);
                    ImGui::Text("#%d  %.1f \xC2\xB0""C", extrema.maxTempIdx, extrema.maxTemp);
                    ImGui::PopStyleColor();
                }
                widgets::Space(4);
            }
        }

        if ((uint8_t)state.get("BPS_Fault") != 0) {
            char detailTxt[96];
            const bool hasDetail = FormatBpsFaultDetail(state, detailTxt, sizeof(detailTxt));
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::Destructive());
            ImGui::SetWindowFontScale(kPanelWarningFontScale);
            if (hasDetail) {
                ImGui::TextUnformatted(detailTxt);
                widgets::Space(6);
            }
            ImGui::SetWindowFontScale(kPanelFontScale);
            ImGui::PopStyleColor();
        }

        // Stale-link warning: flashes if we haven't decoded a BPS or VCU
        // status signal in a while, since the last-known fault state above
        // can otherwise look falsely "all clear" once the CAN link drops.
        {
            constexpr double kStaleThresholdS = 10.0;
            bool bpsStale = state.bpsMsgAgeSeconds < 0.0 || state.bpsMsgAgeSeconds > kStaleThresholdS;
            bool vcuStale = state.vcuMsgAgeSeconds < 0.0 || state.vcuMsgAgeSeconds > kStaleThresholdS;
            if (bpsStale || vcuStale) {
                const char* staleTxt = (bpsStale && vcuStale) ? "NO BPS/VCU DATA"
                                     : bpsStale               ? "NO BPS DATA"
                                                               : "NO VCU DATA";
                float blink = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 5.0f);
                if (blink > 0.5f) {
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors::Warning());
                    ImGui::SetWindowFontScale(kPanelWarningFontScale);
                    ImGui::TextUnformatted(staleTxt);
                    ImGui::SetWindowFontScale(kPanelFontScale);
                    ImGui::PopStyleColor();
                } else {
                    widgets::Space(ImGui::GetTextLineHeight());
                }
            }
        }
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar(); // ItemSpacing

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// Contactors (top) + Ignition (bottom) in one panel with a visible divider.
//
// Contactor grid (3x2):        Ignition row (1x3):
//   H+   H-                      LV  A  M
//   AP   AC
//   MP   MC

static void RenderButtonGrid(AppState& state, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, FaultAwareCardBg(state));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::BeginChild("##ButtonGrid", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    {
        struct BtnDef {
            const char* label;
            bool* statePtr;
        };

        bool hvPositive = state.getBool("HV_Plus_Contactor_State");
        bool hvNegative = state.getBool("HV_Minus_Contactor_State");
        bool arrayPrecharge = state.getBool("Array_Precharge_Contactor_State");
        bool arrayContactor = state.getBool("Array_Contactor_State");
        bool motorPrecharge = state.getBool("Motor_Precharge_Contactor_State");
        bool motorContactor = state.getBool("Motor_Contactor_State");
        // Ignition_Off == 0 means LV is enabled; default to disabled if absent.
        bool lvEnabled = state.signals.count("Ignition_Off") && state.get("Ignition_Off") == 0.0;
        bool arrayEnabled = state.getBool("Ignition_Array");
        bool motorEnabled = state.getBool("Ignition_Motor");

        BtnDef contactors[3][2] = {
            { {"HV+",        &hvPositive},
              {"HV-",        &hvNegative} },
            { {"Array Pre",  &arrayPrecharge},
              {"Array",      &arrayContactor} },
            { {"Motor Pre",  &motorPrecharge},
              {"Motor",      &motorContactor} },
        };

        BtnDef ignition[3] = {
            {"Low Voltage", &lvEnabled},
            {"Array",       &arrayEnabled},
            {"Motor",       &motorEnabled},
        };

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        ImFont* labelFont = (io.Fonts->Fonts.Size > 2)
                                ? io.Fonts->Fonts[2] : nullptr;

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float gap        = 1.0f;
        float headerH    = 16.0f;
        float dividerH   = 8.0f;
        // 3 contactor rows + 1 ignition row = 4 button rows, plus 2 headers
        // and 1 divider. Spread whatever's left across the buttons evenly.
        float buttonTotalH = avail.y - (headerH * 2.0f) - dividerH - gap * 3.0f;
        if (buttonTotalH < 80.0f) buttonTotalH = 80.0f;
        float btnH = buttonTotalH / 4.0f;

        auto drawLabel = [&](const char* text) {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float labelFs = 13.0f;
            ImVec2 tSz = ImGui::CalcTextSize(text);
            dl->AddText(ImVec2(pos.x + (avail.x - tSz.x) * 0.5f, pos.y + (headerH - tSz.y) * 0.5f),
                        ColorToU32(Colors::MutedForeground()), text);
            ImGui::Dummy(ImVec2(avail.x, headerH));
            (void)labelFs;
        };

        auto drawButton = [&](const BtnDef& b, float bw, const char* idSuffix) {
            bool active = *b.statePtr;
            char id[48];
            snprintf(id, sizeof(id), "%s##%s", b.label, idSuffix);

            ImVec2 cellPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(id, ImVec2(bw, btnH));

            ImVec4 fillCol = active ? Colors::Primary() : Colors::Muted();
            ImVec4 textCol = active ? Colors::PrimaryForeground()
                                    : Colors::MutedForeground();
            ImVec4 outlineCol = active
                ? ColorWithAlpha(Colors::PrimaryForeground(), 0.45f)
                : ColorWithAlpha(Colors::MutedForeground(), 0.34f);
            // Rounded rectangle so longer labels actually fit.
            float pad = 3.0f;
            ImVec2 rectMin(cellPos.x + pad, cellPos.y + pad);
            ImVec2 rectMax(cellPos.x + bw - pad, cellPos.y + btnH - pad);
            float rounding = std::min((bw - 2*pad) * 0.15f, (btnH - 2*pad) * 0.35f);

            dl->AddRectFilled(rectMin, rectMax, ColorToU32(fillCol), rounding);
            dl->AddRect(rectMin, rectMax, ColorToU32(outlineCol), rounding, 0, 1.0f);

            // Auto-shrink font so the full label fits the button width.
            float fs = std::min(btnH * 0.45f, 22.0f);
            if (labelFont) {
                ImVec2 probe = labelFont->CalcTextSizeA(fs, FLT_MAX, 0, b.label);
                float textBudget = (bw - 2*pad) - 10.0f;
                if (probe.x > textBudget && probe.x > 0.0f) {
                    fs *= (textBudget / probe.x);
                }
                if (fs < 10.0f) fs = 10.0f;
            }
            ImVec2 tSz = labelFont
                ? labelFont->CalcTextSizeA(fs, FLT_MAX, 0, b.label)
                : ImGui::CalcTextSize(b.label);
            ImVec2 center(cellPos.x + bw * 0.5f, cellPos.y + btnH * 0.5f);
            dl->AddText(labelFont, fs,
                        ImVec2(center.x - tSz.x * 0.5f,
                               center.y - tSz.y * 0.5f),
                        ColorToU32(textCol), b.label);
        };

        // CONTACTORS
        drawLabel("CONTACTORS");
        float contactorBtnW = (avail.x - gap) / 2.0f;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 2; col++) {
                char suffix[16];
                snprintf(suffix, sizeof(suffix), "ct%d%d", row, col);
                drawButton(contactors[row][col], contactorBtnW, suffix);
                if (col < 1) ImGui::SameLine(0, gap);
            }
            if (row < 2) ImGui::Dummy(ImVec2(0, gap));
        }

        // Divider
        ImGui::Dummy(ImVec2(0, dividerH * 0.5f));
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float inset = 8.0f;
            ImVec4 lineCol = ColorWithAlpha(Colors::MutedForeground(), 0.35f);
            dl->AddLine(ImVec2(pos.x + inset, pos.y),
                        ImVec2(pos.x + avail.x - inset, pos.y),
                        ColorToU32(lineCol), 1.0f);
        }
        ImGui::Dummy(ImVec2(0, dividerH * 0.5f));

        // IGNITION
        drawLabel("IGNITION");
        float ignBtnW = (avail.x - gap * 2.0f) / 3.0f;
        for (int col = 0; col < 3; col++) {
            char suffix[16];
            snprintf(suffix, sizeof(suffix), "ig%d", col);
            drawButton(ignition[col], ignBtnW, suffix);
            if (col < 2) ImGui::SameLine(0, gap);
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
        origin.y -= 12.0f;

        float cX = origin.x + cs.x * 0.5f;   // horizontal center

        // Vertical budget
        float heartbeatH = 28.0f;
        float fnrH       = 92.0f;
        float iconsH     = 58.0f;
        float bottomPad  =  8.0f;
        float arcSpace   = cs.y - heartbeatH - iconsH - fnrH - bottomPad - 10.0f;
        if (arcSpace < 60.0f) arcSpace = 60.0f;

        float radius    = std::min(cs.x * 0.48f, arcSpace * 0.6f);
        float thickness = 14.0f;
        // Keep the heartbeat lower than the main speedometer cluster. The
        // cluster itself is anchored independently so the speed, status icons,
        // and F/N/R controls can move upward together.
        constexpr float kHeartbeatOffsetY = 42.0f;
        constexpr float kSpeedometerOffsetY = 16.0f;
        float arcCenterY = origin.y + kSpeedometerOffsetY + radius;
        ImVec2 center(cX, arcCenterY);

        //  hb
        {
            float t     = static_cast<float>(ImGui::GetTime());
            float pulse = 0.5f + 0.5f * sinf(t * 4.0f * static_cast<float>(M_PI));
            ImVec4 hbCol = ColorWithAlpha(Colors::Destructive(), 0.4f + 0.6f * pulse);
            // Move to the left so it doesn't overlap the arc/top area.
            icons::DrawHeart(dl, ImVec2(origin.x + 28.0f, origin.y + kHeartbeatOffsetY),
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
            float fontSize = std::min((bigFont ? bigFont->LegacySize : 48.0f) * 3.0f, maxFontSize);

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

            // Turn/hazard flash: ~1.5 Hz square wave. Hazard lights both arrows.
            bool hazard = state.getBool("Hazard_Pressed");
            bool flashOn = (static_cast<int>(ImGui::GetTime() * 3.0) % 2) == 0;

            // Left Turn Signal
            {
                bool leftActive = (state.turnSignal == TurnSignal::Left) || hazard;
                ImVec4 lC = (leftActive && flashOn)
                    ? Colors::Accent() : Colors::MutedForeground();
                icons::DrawLeftArrow(dl, ImVec2(cX - iconSpacing * 2.0f, iconY),
                                     iconSize, ColorToU32(lC));
            }
            // Cruise Control
            {
                ImVec4 ccCol = state.getBool("Cruise_Enable")
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
                ImVec4 rgCol = state.getBool("Regen_Enable")
                    ? Colors::Success() : Colors::MutedForeground();
                icons::DrawRegen(dl, ImVec2(cX + iconSpacing, iconY),
                                 iconSize, ColorToU32(rgCol));
            }
            // Right Turn Signal
            {
                bool rightActive = (state.turnSignal == TurnSignal::Right) || hazard;
                ImVec4 rC = (rightActive && flashOn)
                    ? Colors::Accent() : Colors::MutedForeground();
                icons::DrawRightArrow(dl, ImVec2(cX + iconSpacing * 2.0f, iconY),
                                      iconSize, ColorToU32(rC));
            }
        }

        // Brake pressure readout below icons
        float brakeReadoutBottomY = 0.0f;
        {
            float maxIconSize = std::min(48.0f, (arcSpace - sSz.y) * 0.4f);
            float iconSize    = std::max(20.0f, maxIconSize);
            float iconBottomY = center.y + sSz.y * 0.25f + iconSize + 4.0f;
            brakeReadoutBottomY = iconBottomY;
        }

        // FNR gear display below icons
        {
            float maxIconSize = std::min(48.0f, (arcSpace - sSz.y) * 0.4f);
            float iconSize    = std::max(20.0f, maxIconSize);
            float iconBottomY = center.y + sSz.y * 0.25f + iconSize + 4.0f;
            float fnrY = std::max(iconBottomY + 8.0f, brakeReadoutBottomY + 4.0f);
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

                float pedalPct = std::clamp((float)state.get("AccelPedal_Main_Pos") / 100.0f, 0.0f, 1.0f);
                const int throttlePct = static_cast<int>(std::lround(pedalPct * 100.0f));
                char throttleTxt[8];
                snprintf(throttleTxt, sizeof(throttleTxt), "%d%%", throttlePct);

                float throttleFs = std::min(28.0f, std::max(18.0f, pbH * 2.5f));
                ImVec2 throttleSz = medFont
                    ? medFont->CalcTextSizeA(throttleFs, FLT_MAX, 0, throttleTxt)
                    : ImGui::CalcTextSize(throttleTxt);
                constexpr float textGap = 6.0f;
                const float groupW = pbW + textGap + throttleSz.x;
                ImVec2 pPos(cX - groupW * 0.5f, pbY);

                // bg
                dl->AddRectFilled(pPos, ImVec2(pPos.x + pbW, pPos.y + pbH),
                                  ColorToU32(Colors::Muted()), pbH * 0.5f);

                float fillW = pbW * pedalPct;
                if (fillW > 2.0f) {
                    ImDrawFlags fillFlags = ImDrawFlags_RoundCornersLeft;
                    if (fillW >= pbW - 0.5f) fillFlags = ImDrawFlags_RoundCornersAll;
                    dl->AddRectFilled(pPos, ImVec2(pPos.x + fillW, pPos.y + pbH),
                                      ColorToU32(Colors::Accent()), pbH * 0.5f, fillFlags);
                }

                dl->AddText(medFont, throttleFs,
                            ImVec2(pPos.x + pbW + textGap, pPos.y + (pbH - throttleSz.y) * 0.5f),
                            ColorToU32(Colors::Accent()), throttleTxt);
                // break pressure 1: front
                // break pressure 2: rear
                const float brakePressure1 = (float)state.get("Brake_Pressure_1");
                const float brakePressure2 = (float)state.get("Brake_Pressure_2");
                char pressureTxt[48];
                snprintf(
                    pressureTxt,
                    sizeof(pressureTxt),
                    "FRONT %.0f   REAR %.0f",
                    std::max(0.0f, brakePressure1),
                    std::max(0.0f, brakePressure2));
                ImVec4 pressureCol =
                    (state.getBool("Brake_Pressure_1_Fault") || state.getBool("Brake_Pressure_2_Fault"))
                        ? Colors::Warning()
                        : Colors::MutedForeground();
                float pressureFs = std::min(26.0f, std::max(18.0f, iconSize * 0.55f));
                float pressureY = pbY + pbH + 2.0f;
                ImVec2 pSz = medFont
                    ? medFont->CalcTextSizeA(pressureFs, FLT_MAX, 0, pressureTxt)
                    : ImGui::CalcTextSize(pressureTxt);

                dl->AddText(
                    medFont,
                    pressureFs,
                    ImVec2(cX - pSz.x * 0.5f, pressureY),
                    ColorToU32(pressureCol),
                    pressureTxt);
            }
        }

    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

static const char* BpsFaultName(uint8_t code) {
    switch (code) {
        case 0:  return "OK";
        case 1:  return "Overvoltage";
        case 2:  return "Undervoltage";
        case 3:  return "Regen";
        case 4:  return "Overtemperature";
        case 5:  return "Elcon";
        case 6:  return "Array Precharge Timeout";
        case 7:  return "Internal Watchdog";
        case 8:  return "Segment Watchdog";
        case 9:  return "HV Plus Contactor Sense";
        case 10: return "HV Minus Contactor Sense";
        case 11: return "Array Contactor Sense";
        case 12: return "Array Pchg Contactor Sense";
        case 13: return "Estop 1";
        case 14: return "Estop 2";
        case 15: return "Estop 3";
        case 16: return "Charging Overcurrent";
        case 17: return "Discharging Overcurrent";
        case 18: return "Amperes Watchdog";
        default:
            static thread_local char unknown[32];
            snprintf(unknown, sizeof(unknown), "Unknown %u", static_cast<unsigned>(code));
            return unknown;
    }
}

static const char* VcuFaultName(uint8_t code) {
    // McQueen VCU_Status replaced the old single-byte VCU_Fault enum with five
    // independent *_FAULT_DETECTED bits. ui.cpp packs them into a bitfield:
    //   bit0=BPS, bit1=Controls, bit2=Motor, bit3=Pedals, bit4=Steering.
    // Decode into a +-joined list so the dashboard still shows something human-
    // readable even when multiple subsystems are faulted simultaneously.
    if (code == 0) return "No Fault";
    static thread_local char buf[96];
    buf[0] = '\0';
    auto append = [&](const char* s) {
        if (buf[0] != '\0') strncat(buf, "+", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, s, sizeof(buf) - strlen(buf) - 1);
    };
    if (code & (1u << 0)) append("BPS");
    if (code & (1u << 1)) append("Ctrl");
    if (code & (1u << 2)) append("Motor");
    if (code & (1u << 3)) append("Pedal");
    if (code & (1u << 4)) append("Steer");
    return buf;
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

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    ImGui::SetWindowFontScale(0.85f);

    if (ImGui::BeginTable("BpsGrid", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < 32; i++) {
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "#%d", i);
            if (i < nMod) {
                float v = i < (int)s.moduleVoltages.size() ? s.moduleVoltages[i] : 0.0f;
                float t = i < (int)s.moduleTemps.size()    ? s.moduleTemps[i]    : 0.0f;

                ImVec4 vCol = Colors::Foreground();
                if      (v > 4.2f  || v < 2.5f) vCol = Colors::Destructive();
                else if (v > 4.15f || v < 3.0f) vCol = Colors::Warning();

                ImVec4 tCol = Colors::Foreground();
                if      (t > 45.0f) tCol = Colors::Destructive();
                else if (t > 35.0f) tCol = Colors::Warning();

                ImGui::TextColored(vCol, "%.2fV", v);
                ImGui::SameLine();
                ImGui::TextColored(tCol, "%.0fC", t);
            } else {
                ImGui::TextDisabled("-  -");
            }
        }
        ImGui::EndTable();
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar(2);
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
    const bool steeringSensorOK = state.signals.count("LWS_Fault") && state.get("LWS_Fault") == 0.0;
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "STEERING SENSOR (LWS)");
    ImGui::Text(" Angle: %.1f deg  %s", state.get("LWS_Angle"), steeringSensorOK ? "OK" : "FAULT");
    widgets::Space(4);

    // Driver Inputs
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "DRIVER INPUTS");
    ImGui::Text(" Horn:%s Hazard:%s PTT:%s Cruise Set:%s Regen Act:%s",
        state.getBool("Horn_Pressed") ? "ON" : "-", state.getBool("Hazard_Pressed") ? "ON" : "-",
        state.getBool("PushToTalk_Pressed") ? "ON" : "-", state.getBool("Cruise_Set") ? "ON" : "-",
        state.getBool("Regen_Activate") ? "ON" : "-");
    ImGui::Text(" Gear: %s  Turn: %s", GearToString(state.gear),
        state.turnSignal == TurnSignal::Left ? "L" : state.turnSignal == TurnSignal::Right ? "R" : "-");
    widgets::Space(4);

    // Accel/Brake Pedals
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "ACCEL / BRAKE PEDALS");
    ImGui::Text(" Accel: Total %.0f%%  Main %d%% Red %d%% V:%.2f/%.2f", state.get("AccelPedal_Main_Pos"),
        (uint8_t)state.get("AccelPedal_Main_Pos"), (uint8_t)state.get("AccelPedal_Redundant_Pos"),
        state.get("Accel_Pos_Voltage_Main"), state.get("Accel_Pos_Voltage_Redundant"));
    ImGui::Text("        Faults: Main:%s Red:%s", flt(state.getBool("AccelPedal_Main_Fault")), flt(state.getBool("AccelPedal_Redundant_Fault")));
    ImGui::Text(" Brake: Engaged:%s  Main %d%% Red %d%% V:%.2f/%.2f", state.brakeEngaged ? "YES" : "NO",
        (uint8_t)state.get("BrakePedal_Main_Pos"), (uint8_t)state.get("BrakePedal_Redundant_Pos"),
        state.get("Brake_Pos_Voltage_Main"), state.get("Brake_Pos_Voltage_Redundant"));
    ImGui::Text("        Faults: Main:%s Red:%s", flt(state.getBool("BrakePedal_Main_Fault")), flt(state.getBool("BrakePedal_Redundant_Fault")));
    ImGui::Text(" Pressure: %.0f / %.0f psi (%s/%s)", state.get("Brake_Pressure_1"), state.get("Brake_Pressure_2"),
        flt(state.getBool("Brake_Pressure_1_Fault")), flt(state.getBool("Brake_Pressure_2_Fault")));
    widgets::Space(4);

    // Hardware Switched Contactors / Ignitions
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "CONTACTORS / IGNITION SWITCHES");
    ImGui::Text(" HV+:%s -:%s ArrP:%s Arr:%s MtrP:%s Mtr:%s",
        state.getBool("HV_Plus_Contactor_State") ? "C" : "o", state.getBool("HV_Minus_Contactor_State") ? "C" : "o",
        state.getBool("Array_Precharge_Contactor_State") ? "C" : "o", state.getBool("Array_Contactor_State") ? "C" : "o",
        state.getBool("Motor_Precharge_Contactor_State") ? "C" : "o", state.getBool("Motor_Contactor_State") ? "C" : "o");
    ImGui::Text(" Ignition: LV:%s  Array:%s  Motor:%s",
        (state.signals.count("Ignition_Off") && state.get("Ignition_Off") == 0.0) ? "ON" : "off",
        state.getBool("Ignition_Array") ? "ON" : "off",
        state.getBool("Ignition_Motor") ? "ON" : "off");
    widgets::Space(4);

    ImGui::TextColored(Colors::Destructive(), "FAULTS");
    if (state.faults.empty()) {
        ImGui::Text(" None");
    } else {
        for (const Fault& fault : state.faults) {
            std::string faultText = FaultDisplayText(fault);
            ImGui::Text(" %s", faultText.c_str());
        }
    }
    
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
    const bool limitMotorCurrent = state.getBool("MC_LIMIT_MotorCurrent");
    const bool limitVelocity = state.getBool("MC_LIMIT_Velocity");
    const bool limitBusCurrent = state.getBool("MC_LIMIT_BusCurrent");
    const bool limitBusVoltageUpper = state.getBool("MC_LIMIT_BusVoltageUpper");
    const bool limitBusVoltageLower = state.getBool("MC_LIMIT_BusVoltageLower");
    const bool limitIpmOrMotorTemp = state.getBool("MC_LIMIT_MotorTemp");
    const bool limitOutputVoltage = state.getBool("MC_LIMIT_OutputVoltagePWM");
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "MOTOR CONTROLLER (MoCo)");
    ImGui::Text(" Vel: %d mph  Bus: %.1fV %.1fA", state.speed, state.get("MC_BusVoltage"), state.get("MC_BusCurrent"));
    ImGui::Text(" Phase: %.1fA (B) %.1fA (C)  BEMF: %.1fV (Q) %.1fV (D)", state.get("MC_PhaseCurrentB"), state.get("MC_PhaseCurrentC"), state.get("MC_BEMFq"), state.get("MC_BEMFd"));
    ImGui::Text(" Temp: Heatsink %.1fC  Precharge Motor: %.1fV", state.get("MC_HeatsinkTemp"), state.get("VCU_Precharge_Motor_Voltage"));
    ImGui::TextColored(Colors::Warning(), " Limits Active: %s%s%s%s%s%s%s%s",
      limitMotorCurrent ? "MotorCurrent " : "",
      limitVelocity ? "Velocity " : "",
      limitBusCurrent ? "BusCurrent " : "",
      limitBusVoltageUpper ? "BusVolUpper " : "",
      limitBusVoltageLower ? "BusVolLower " : "",
      limitIpmOrMotorTemp ? "Temp " : "",
      limitOutputVoltage ? "OutVolPWM " : "",
      (!limitMotorCurrent && !limitVelocity && !limitBusCurrent && !limitBusVoltageUpper && !limitBusVoltageLower && !limitIpmOrMotorTemp && !limitOutputVoltage) ? "None" : "");
    widgets::Space(4);

    // VCU Status & Precharge msg
    const uint8_t vcuFaultCode = (uint8_t)state.get("VCU_Fault");
    const bool vcuPedalsOK = state.signals.count("VCU_Pedals_Watchdog") && state.get("VCU_Pedals_Watchdog") == 0.0;
    const bool vcuDriverInputOK = state.signals.count("VCU_Driver_Input_Watchdog") && state.get("VCU_Driver_Input_Watchdog") == 0.0;
    ImVec4 vcuFCol = vcuFaultCode != 0 ? Colors::Destructive() : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "VCU STATUS");
    ImGui::TextColored(vcuFCol, " Fault: %d (%s)  FSM State: %d", vcuFaultCode, VcuFaultName(vcuFaultCode), (uint8_t)state.get("VCU_FSM_State"));
    ImGui::Text(" Motor Ready:%s Pedals:%s Driver Input:%s", state.getBool("Motor_Ready")?"Y":"N", vcuPedalsOK?"OK":"NO", vcuDriverInputOK?"OK":"NO");
    ImGui::Text(" Regen:%s (Active:%s)", state.getBool("VCU_Regen_OK")?"OK":"NO", state.getBool("VCU_Regen_Active")?"Y":"N");
    widgets::Space(4);

    // MPPT Solar
    float mpptVin = (float)state.get("MPPT_Input_Voltage");
    float mpptIin = (float)state.get("MPPT_Input_Current");
    float mpptVout = (float)state.get("MPPT_Output_Voltage");
    float mpptIout = (float)state.get("MPPT_Output_Current");
    float mpptPIn = mpptVin * mpptIin;
    float mpptPOut = mpptVout * mpptIout;
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "MPPT SOLAR");
    ImGui::Text(" In: %.1fV %.2fA (%.0fW)  Out: %.1fV %.2fA (%.0fW)",
        mpptVin, mpptIin, mpptPIn, mpptVout, mpptIout, mpptPOut);
    ImGui::Text(" Heatsink: %.0fC  Ambient: %.0fC  Fault:%d  Mode:%d",
        state.get("MPPT_HeatsinkTemperature"), state.get("MPPT_AmbientTemperature"),
        (uint8_t)state.get("MPPT_Fault"), (uint8_t)state.get("MPPT_Mode"));
    widgets::Space(4);

    // Cooling
    const bool pumpFault = state.getBool("Pump_Fault");
    ImVec4 pumpCol = pumpFault ? Colors::Destructive() : Colors::Foreground();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "COOLING");
    ImGui::Text(" Coolant: %.1f / %.1fC  Flow: %.2f / %.2f L/min",
        state.get("Coolant_Temperature_1"), state.get("Coolant_Temperature_2"), state.get("FlowRate_1"), state.get("FlowRate_2"));
    ImGui::TextColored(pumpCol, " Pump: %d%% %s", (uint8_t)state.get("Pump_DutyCycle"), pumpFault?"FAULT":"OK");
    widgets::Space(4);

    // LV + Cameras + Lights
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.9f, 1.0f), "LV / CAMERAS / LIGHTS");
    ImGui::Text(" HV DCDC: Sel:%s Fault:%s Valid:%s",
        state.getBool("LTC4421_HVDCDC_Selected")?"Y":"N", state.getBool("LTC4421_HVDCDC_Fault")?"YES":"no", state.getBool("LTC4421_HVDCDC_Valid")?"Y":"N");
    ImGui::Text(" Supp Batt: Sel:%s Fault:%s Valid:%s",
        state.getBool("LTC4421_SuppBatt_Selected")?"Y":"N", state.getBool("LTC4421_SuppBatt_Fault")?"YES":"no", state.getBool("LTC4421_SuppBatt_Valid")?"Y":"N");
    ImGui::Text(" Enable: SuppBatt:%s PSU:%s",
        state.getBool("LV_EN_SupplementalBattery")?"ON":"off", state.getBool("LV_EN_PowerSupply")?"ON":"off");
    ImGui::Text(" Supp Batt Info: SOC: %.1f%%  %.1fV  %.1fA  Charger: %s  DCDC: %.1fV %.1fA",
        state.get("Supplemental_Battery_SOC"), state.get("Supplemental_Battery_Voltage"), state.get("Supplemental_Battery_Current") * 0.001,
        SuppChargerStatusStr((uint8_t)state.get("SuppCharger_Status")), state.get("Supplemental_DCDC_Voltage"), state.get("Supplemental_DCDC_Current") * 0.001);
    ImGui::Text(" Cameras: Backup:%s Left:%s Right:%s",
        state.getBool("Camera_Status_Backup")?"Y":"N", state.getBool("Camera_Status_Left")?"Y":"N", state.getBool("Camera_Status_Right")?"Y":"N");
    const uint8_t lightingFaults = (uint8_t)state.get("Controls_Lighting_Fault");
    const uint8_t controlsLeaderFault = (uint8_t)state.get("Controls_Leader_Fault");
    ImVec4 ctlCol = (lightingFaults||controlsLeaderFault) ? Colors::Destructive() : Colors::Foreground();
    ImGui::TextColored(ctlCol, " Light Faults: %d  Controls Faults: %d", lightingFaults, controlsLeaderFault);
    widgets::Space(4);

    // BPS Status
    const uint8_t bpsFaultCode = (uint8_t)state.get("BPS_Fault");
    ImVec4 bpsFCol = bpsFaultCode != 0 ? Colors::Destructive() : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "BPS STATUS");
    ImGui::TextColored(bpsFCol, " Fault: %d (%s)  Regen: %s  Charge: %s",
        bpsFaultCode, BpsFaultName(bpsFaultCode),
        state.getBool("BPS_Regen_OK") ? "OK" : "NO", state.getBool("BPS_Charge_OK") ? "OK" : "NO");
    const float mainBatteryAvgTemp = (float)state.get("Main_Battery_Avg_Temperature");
    if (mainBatteryAvgTemp > 0.0f) {
        ImGui::Text(" Avg Temp: %.1fC", mainBatteryAvgTemp);
    }
    {
        char extremaTxt[96];
        if (FormatBpsExtremaSummary(state, extremaTxt, sizeof(extremaTxt))) {
            ImGui::Text(" Cells: %s", extremaTxt);
        }
    }
    {
        char detailTxt[96];
        if (FormatBpsFaultDetail(state, detailTxt, sizeof(detailTxt))) {
            ImGui::TextColored(Colors::Destructive(), " %s", detailTxt);
        }
    }
    
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
    // The canvas system already called SetNextWindowPos/Size with the correct
    // content region (full screen minus sidebar and title bar). Don't override.
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

    const uint8_t bpsFaultCode = (uint8_t)state.get("BPS_Fault");
    const uint8_t vcuFaultCode = (uint8_t)state.get("VCU_Fault");

    if (!state.showDebugScreen) { // Only record edge-triggered snapshot when not actively browsing snapshots
        constexpr size_t kMaxFaultSnapshots = 100;
        auto pushSnapshot = [](const std::string& reason, const AppState& s) {
            if (g_faultSnapshots.size() >= kMaxFaultSnapshots) {
                g_faultSnapshots.erase(g_faultSnapshots.begin());
            }
            g_faultSnapshots.push_back({reason, s});
        };
        if (bpsFaultCode != 0 && lastBpsFaultCode == 0) {
            char timeBuf[64];
            time_t now = time(nullptr);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", localtime(&now));
            std::string reason = std::string(timeBuf) + " BPS Fault: " + BpsFaultName(bpsFaultCode);
            pushSnapshot(reason, state);
        }
        if (vcuFaultCode != 0 && lastVcuFaultCode == 0) {
            char timeBuf[64];
            time_t now = time(nullptr);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", localtime(&now));
            std::string reason = std::string(timeBuf) + " VCU Fault: " + VcuFaultName(vcuFaultCode);
            pushSnapshot(reason, state);
        }
        if (state.canFault && state.canFaultId != lastCanFaultId) {
            char timeBuf[64];
            time_t now = time(nullptr);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", localtime(&now));
            std::string reason = std::string(timeBuf) + " CAN Fault: " + state.canFaultName;
            pushSnapshot(reason, state);
        }
    }

    lastBpsFaultCode = bpsFaultCode;
    lastVcuFaultCode = vcuFaultCode;
    lastCanFaultId = state.canFault ? state.canFaultId : 0;
    // --------------------------------


    float availW = ImGui::GetContentRegionAvail().x;
    float availH = ImGui::GetContentRegionAvail().y;
    
    // Top-corner chord: tap top-left then top-right (or vice versa) within
    // CHORD_WINDOW_S to toggle the debug screen. Uses raw mouse-position
    // checks instead of InvisibleButton so it isn't shadowed by widgets drawn
    // on top later in the frame. Single-pointer-friendly (works on touch).
    {
        constexpr float CORNER_PX = 80.0f;
        constexpr double CHORD_WINDOW_S = 1.0;
        static double lastLeftClickT  = -1e9;
        static double lastRightClickT = -1e9;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 mp = io.MousePos;
            ImVec2 ds = io.DisplaySize;
            double nowT = ImGui::GetTime();
            if (mp.y >= 0 && mp.y <= CORNER_PX) {
                if (mp.x >= 0 && mp.x <= CORNER_PX)              lastLeftClickT  = nowT;
                if (mp.x >= ds.x - CORNER_PX && mp.x <= ds.x)    lastRightClickT = nowT;
            }
            if ((nowT - lastLeftClickT)  < CHORD_WINDOW_S &&
                (nowT - lastRightClickT) < CHORD_WINDOW_S) {
                state.showDebugScreen = !state.showDebugScreen;
                lastLeftClickT = lastRightClickT = -1e9;
            }
        }
    }

    // Desktop shortcut: press 'D' to toggle debug screen
    if (ImGui::IsKeyPressed(ImGuiKey_D)) {
        state.showDebugScreen = !state.showDebugScreen;
    }
    
    // Reset cursor for actual layout
    ImGui::SetCursorPos(ImVec2(0, 0));

    if (state.showDebugScreen) {
        RenderDebugScreen(state);
    } else {
        float gap       = 0.0f;
        // Split the height evenly so top and bottom rows are the same size.
        float rowTop    = (availH - gap) * 0.5f;
        float rowBottom = rowTop;

        // Top row: symmetric left/right cam tiles around the centered speed gauge.
    float topColSide   = availW * 0.33f;
    float topColLeft   = topColSide;
    float topColRight  = topColSide;
    float topColCenter = availW - topColLeft - topColRight - gap * 2.0f;

    // Bottom row even proportions
    float botColLeft   = availW * 0.26f;
    float botColRight  = availW * 0.24f;
    float botColCenter = availW - botColLeft - botColRight - gap * 2.0f;

    // Bottom row uses the same side / center widths as the top row so the
    // two rows line up vertically. Compute them before positioning.
    float rearBotW = topColCenter;
    float sideW    = topColSide;
    float colCenterX = topColLeft + gap;
    float colRightX  = colCenterX + topColCenter + gap;
    float topY       = 0.0f;
    float botY       = rowTop + gap;

    // Top row -- explicit positions so ImGui's cursor tracking can't drift.
    ImGui::SetCursorPos(ImVec2(0.0f, topY));
    RenderCameraView(state, "LEFT VIEW",
                     state.rightCameraTexture,
                     ImVec2(topColLeft, rowTop));

    ImGui::SetCursorPos(ImVec2(colCenterX, topY));
    RenderSpeedGauge(state, ImVec2(topColCenter, rowTop));

    ImGui::SetCursorPos(ImVec2(colRightX, topY));
    RenderCameraView(state, "RIGHT VIEW",
                     state.leftCameraTexture,
                     ImVec2(topColRight, rowTop));

    // Bottom row -- mirror of the top row at botY.
    ImGui::SetCursorPos(ImVec2(0.0f, botY));
    RenderBatteryPanel(state, ImVec2(sideW, rowBottom));

    ImGui::SetCursorPos(ImVec2(colCenterX, botY));
    RenderCameraView(state, "REAR VIEW",
                     state.rearCameraTexture,
                     ImVec2(rearBotW, rowBottom),
                     true);

    ImGui::SetCursorPos(ImVec2(colRightX, botY));
    RenderButtonGrid(state, ImVec2(sideW, rowBottom));
    } // End normal dashboard render

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    RenderFaultBanner(state);
}
}
