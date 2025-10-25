/* [ξ] DBC Connector Interface
   Connects parsed DBC definitions to their respective CAN IDs.
   Acts like a runtime dictionary for DBC → CAN lookup.
*/

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

struct DbcSignalInfo {
    int startBit;
    int length;
    int endianness;   // 1 = little, 0 = big
    bool isSigned;
    double factor;
    double offset;
    double minVal;
    double maxVal;
};

struct DbcMessageInfo {
    int canId;
    std::string name;
    int dlc;
    std::string transmitter;

    // Each message holds multiple signal definitions
    std::unordered_map<std::string, DbcSignalInfo> signals;
};

class DbcConnector {
public:
    // Top-level: map CAN IDs → DBC message info
    std::unordered_map<int, std::shared_ptr<DbcMessageInfo>> dbcMap;

    // Register a new CAN message (BO_ line)
    void registerMessage(int canId, const std::string& name, int dlc, const std::string& transmitter) {
        auto msg = std::make_shared<DbcMessageInfo>();
        msg->canId = canId;
        msg->name = name;
        msg->dlc = dlc;
        msg->transmitter = transmitter;
        dbcMap[canId] = msg;
    }

    // Register a new signal for a given CAN ID (SG_ line)
    void registerSignal(int canId,
                        const std::string& signalName,
                        int startBit,
                        int length,
                        int endianness,
                        bool isSigned,
                        double factor,
                        double offset,
                        double minVal,
                        double maxVal) {
        if (dbcMap.find(canId) == dbcMap.end()) {
            std::cerr << "[Connector] Error: CAN ID " << canId << " not registered before SG_ line\n";
            return;
        }

        DbcSignalInfo sig{ startBit, length, endianness, isSigned, factor, offset, minVal, maxVal };
        dbcMap[canId]->signals[signalName] = sig;
    }

    // Print all stored mappings for debugging
    void dump() const {
        for (const auto& [id, msg] : dbcMap) {
            std::cout << "CAN ID: " << id << " | Name: " << msg->name
                      << " | DLC: " << msg->dlc
                      << " | Transmitter: " << msg->transmitter << "\n";

            for (const auto& [sigName, sig] : msg->signals) {
                std::cout << "  └─ Signal: " << sigName
                          << " (StartBit: " << sig.startBit
                          << ", Len: " << sig.length
                          << ", Endian: " << sig.endianness
                          << ", Signed: " << sig.isSigned
                          << ", Factor: " << sig.factor
                          << ", Offset: " << sig.offset
                          << ", Min: " << sig.minVal
                          << ", Max: " << sig.maxVal
                          << ")\n";
            }
        }
    }
};
