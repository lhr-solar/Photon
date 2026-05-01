#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "imgui.h"

struct Parse;

struct PlotManager{
    struct PlotSourceRef{
        uint32_t messageId = 0;
        uint32_t signalIndex = 0;
        bool assigned = false;
    };

    struct SignalOption{
        PlotSourceRef ref{};
        std::string label{};
    };

    struct PlotTypeSpec{
        const char* label = "";
        int minSources = 1;
        int maxSources = 1;
        bool usesTimeAxis = true;
        bool is3D = false;
    };

    struct PlotWindow{
        int id = 0;
        int typeIndex = 0;
        std::vector<PlotSourceRef> sources{};
        std::string title{};
        bool open = true;
        bool useSource1TimeAsX = true;
        bool followLatest = true;
        bool hasView = false;
        double xMin = 0.0;
        double xMax = 0.0;
    };

    Parse* parse = nullptr;
    ImGuiID homeDockspaceID = 0;
    bool creatorOpen = false;
    bool creatorFocusSearch = false;
    int typeIndex = 0;
    std::vector<PlotSourceRef> pendingSources{};
    char search[128]{};
    std::vector<SignalOption> signalOptions{};
    std::vector<int> sourceMatches{};
    int selectedMatch = -1;
    int activeSourceIndex = 0;
    bool useSource1TimeAsX = true;
    int nextPlotId = 1;
    std::vector<PlotWindow> windows{};

    void init(Parse* parseTarget);
    void handleHotkeys(bool homeActive);
    void renderHome(ImGuiID dockspaceID, const ImVec2& contentMin, const ImVec2& contentMax);
    bool hasPlots() const { return !windows.empty(); }

private:
    static const PlotTypeSpec& specFor(int typeIndex);
    void openCreator();
    void refreshSignalOptions();
    void refreshMatches();
    void resetPendingSourcesForType();
    void renderCreator();
    void renderPlotWindows();
    void createPlot();
};
