#include "dbc_loader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

bool DbcLoader::loadFromFile(const std::string& path, DbcConnector& connector) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[DBC Loader] Failed to open " << path << "\n";
        return false;
    }

    std::string line;
    uint32_t currentId = 0;

    while (std::getline(file, line)) {
        if (line.rfind("BO_", 0) == 0) {
            uint32_t canId = 0;
            std::string name, sender;
            uint8_t dlc = 0;

            std::istringstream iss(line);
            std::string tag; iss >> tag >> canId;
            std::string tmp; iss >> tmp;
            auto colon = tmp.find(':');
            if (colon == std::string::npos) continue;
            name = tmp.substr(0, colon);

            std::string dlcStr; iss >> dlcStr;
            dlc = static_cast<uint8_t>(std::stoi(dlcStr));
            iss >> sender;

            connector.registerMessage(canId, name, dlc, sender);
            currentId = canId;
        }
        else if (line.rfind("SG_", 0) == 0 && currentId != 0) {
            DbcSignalInfo sig{};
            std::istringstream iss(line);
            std::string tag, sigName; iss >> tag >> sigName;
            char colon; iss >> colon;
            iss >> sig.startBit;
            iss.ignore(1, '|');
            iss >> sig.length;
            char at; iss >> at;
            iss >> sig.endianness;
            char sign; iss >> sign;
            sig.isSigned = (sign == '-');
            connector.registerSignal(currentId, sigName, sig.startBit, sig.length,
                                     sig.endianness, sig.isSigned, 1.0, 0.0, 0.0, 0.0);
        }
    }

    std::cerr << "[DBC Loader] Successfully loaded: " << path << "\n";
    return true;
}
