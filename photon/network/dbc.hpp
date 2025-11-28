#pragma once
#include <cstdint>
#include <chrono>
#include <vector>
#include <map>
#include "../gui/plot.hpp"

// Simple PODs mirroring assets/dbc/controls.dbc. Each struct exposes an
// updateSignals() helper that decodes a pre-parsed 64-bit payload for its CAN ID.
struct CanSignal {
    int startBit = 0;
    int length = 0;
    int endianness = 0;
    bool isSigned = false;
    double scale = 1.0;
    double offset = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::string name;
    std::string unit;
    std::string receiver;
    std::chrono::system_clock::time_point lastTimeMutated = std::chrono::system_clock::now();
    std::chrono::milliseconds timeSinceMutation;
    std::vector<double> data = {0.0};
};

struct CanMessage{
    int canId = 0;
    int dlc = 0;
    std::string name;
    std::string transmitter;
    std::chrono::system_clock::time_point lastTimeUpdated = std::chrono::system_clock::now();
    std::chrono::milliseconds timeSinceUpdate;
    double dataRate = 0;
    double storageSize = 0; // sum of all signals
    double dataTransfer = 0;
    double bandwidthPercentage = 0; // percentage of total data sent
    std::vector<double> time {0.0};
    std::vector<CanSignal> signals;
    void updateMessage(Network* networkSource);
};

struct CanStore{
    double totalBandwidth = 0.0; // total data stored
    std::map<int, CanMessage> canMessages;
    bool loadStateFromFile(std::string filePath);
    void dump();
};
