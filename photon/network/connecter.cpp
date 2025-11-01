#include "dbc_connector.hpp"

void DbcConnector::registerMessage(int canId, const std::string& name, int dlc, const std::string& transmitter) {
    DbcMessageInfo& msg = dbcMap[canId];
    msg.canId = canId;
    msg.name = name;
    msg.dlc = dlc;
    msg.transmitter = transmitter;
    msg.signals.clear();

    std::cerr << "[DBC] Registered new message: " << name
              << " (CAN ID " << canId << ")\n";
}

void DbcConnector::registerSignal(int canId,
                                  const std::string& signalName,
                                  int startBit, int length,
                                  int endianness, bool isSigned,
                                  double factor, double offset,
                                  double minVal, double maxVal) {
    auto it = dbcMap.find(canId);
    if (it == dbcMap.end()) {
        std::cerr << "[DBC] Error: SG_ before BO_ for CAN ID " << canId << "\n";
        return;
    }

    DbcSignalInfo sig{ startBit, length, endianness, isSigned, factor, offset, minVal, maxVal };
    it->second.signals[signalName] = sig;

    std::cerr << "[DBC] Registered SG_ " << signalName
              << " (CAN ID " << canId << ")\n";
}

void DbcConnector::dump() const {
    std::cout << "========== DBC MAP ==========\n";
    for (const auto& [id, msg] : dbcMap) {
        std::cout << "[DBC] CAN ID: " << id
                  << " | Name: " << msg.name
                  << " | DLC: " << msg.dlc
                  << " | Sender: " << msg.transmitter << "\n";

        if (msg.signals.empty()) {
            std::cout << "    (No signals)\n";
        } else {
            for (const auto& [sigName, sig] : msg.signals) {
                std::cout << "    SG_ " << sigName
                          << " : start=" << sig.startBit
                          << " len=" << sig.length
                          << " endian=" << sig.endianness
                          << (sig.isSigned ? " signed" : " unsigned")
                          << " scale=" << sig.factor
                          << " offset=" << sig.offset
                          << " [" << sig.minVal << "|" << sig.maxVal << "]\n";
            }
        }
    }
    std::cout << "=============================\n";
}

