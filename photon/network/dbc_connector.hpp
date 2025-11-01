#pragma once
#include <string>
#include <unordered_map>
#include <iostream>

struct DbcSignalInfo {
    int startBit = 0;
    int length = 0;
    int endianness = 1;   // 1 = little
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minVal = 0.0;
    double maxVal = 0.0;
};

struct DbcMessageInfo {
    int canId = 0;
    std::string name;
    int dlc = 0;
    std::string transmitter;
    std::unordered_map<std::string, DbcSignalInfo> signals;
};

class DbcConnector {
public:
    std::unordered_map<int, DbcMessageInfo> dbcMap;

    void registerMessage(int canId, const std::string& name, int dlc, const std::string& transmitter);
    void registerSignal(int canId, const std::string& signalName,
                        int startBit, int length, int endianness, bool isSigned,
                        double factor, double offset, double minVal, double maxVal);
    void dump() const;
};
