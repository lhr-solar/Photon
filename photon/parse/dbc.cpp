#include "parse.hpp"
#include "imgui.h"
#include "../engine/include.hpp"
#include <cstdint>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

    /*
uint64_t extractLe(const std::array<uint8_t, 8>& bytes, int dlc, int startBit, int bitLength){
    if (bitLength <= 0) return 0;
    if (dlc < 0) return 0;
    if (dlc > 8) dlc = 8;
    if (startBit < 0) return 0;
    if (startBit + bitLength > dlc * 8) return 0;

    uint64_t value = 0;
    for (int i = 0; i < bitLength; ++i) {
        const int srcBit = startBit + i;
        const int byteIdx = srcBit / 8;
        const int bitIdx = srcBit % 8;
        const uint64_t bit = (static_cast<uint64_t>(bytes[byteIdx]) >> bitIdx) & 0x1ULL;
        value |= (bit << i);
    }
    return value;
}

uint64_t extractBe(const std::array<uint8_t, 8>& bytes, int dlc, int startBit, int bitLength) {
    if (bitLength <= 0) return 0;
    if (dlc < 0) return 0;
    if (dlc > 8) dlc = 8;
    if (startBit < 0) return 0;
    if (bitLength > 64) return 0;

    uint64_t value = 0;
    int currentBit = startBit;
    for (int i = 0; i < bitLength; ++i) {
        const int byteIdx = currentBit / 8;
        const int bitIdx = currentBit % 8;
        if (byteIdx < 0 || byteIdx >= dlc) {
            return 0;
        }

        const uint64_t bit = (static_cast<uint64_t>(bytes[byteIdx]) >> bitIdx) & 0x1ULL;
        value = (value << 1) | bit;

        currentBit = (bitIdx == 0) ? (currentBit + 15) : (currentBit - 1);
    }
    return value;
}
*/

uint64_t extractLe(const std::array<uint8_t, 8>& bytes, int dlc, int startBit, int bitLength){
    if (bitLength <= 0) return 0;
    if (dlc < 0) return 0;
    if (dlc > 8) dlc = 8;
    if (startBit < 0) return 0;
    if (startBit + bitLength > dlc * 8) return 0;

    uint64_t v = 0;
    for (int i = 0; i < dlc; ++i) v = (v << 8) | uint64_t(bytes[i]);

    if (bitLength == 64) return v >> startBit;

    uint64_t mask = (1ULL << bitLength) - 1ULL;
    return (v >> startBit) & mask;
}

uint64_t extractBe(const std::array<uint8_t, 8>& bytes, int dlc, int startBit, int bitLength) {
    if (bitLength <= 0) return 0;
    if (dlc < 0) return 0;
    if (dlc > 8) dlc = 8;
    if (startBit < 0) return 0;

    const int totalBits = dlc * 8;
    if (startBit >= totalBits) return 0;
    if (bitLength > 64) return 0;

    const int byteIdx = startBit / 8;
    const int bitIdx  = startBit % 8;
    if (byteIdx >= dlc) return 0;

    uint64_t v = 0;
    for (int i = 0; i < dlc; ++i) v = (v << 8) | uint64_t(bytes[i]); // bytes[0] is MSB

    const int p0  = (totalBits - 1) - (byteIdx * 8 + (7 - bitIdx));   // bit position in v (LSB=0)
    const int low = p0 - bitLength + 1;
    if (low < 0) return 0;

    if (bitLength == 64) return v >> low;

    const uint64_t mask = (1ULL << bitLength) - 1ULL;
    return (v >> low) & mask;
}

int32_t signExtend(uint64_t raw, uint8_t bits){
    const uint64_t signBit = 1ULL << (bits - 1);
    const uint64_t mask = (1ULL << bits) - 1ULL;
    uint64_t v = raw & mask;
    if (v & signBit) {
        v |= ~mask;
    }
    return static_cast<int32_t>(v);
}

// least significant @ [0], so returns little endian
std::array<uint8_t, 8> unpackBytes(uint64_t value, int dlc){
    std::array<uint8_t, 8> bytes{};
    if (dlc <= 0) { return bytes; }
    const int byteCount = dlc > 8 ? 8 : dlc;
    for (int i = byteCount; i > 0; i--) {
        const int shift = 8 * (byteCount - i);
        bytes[byteCount - i] = static_cast<uint8_t>((value >> shift) & 0xFF);
    }
    return bytes;
}

uint64_t buildLittleEndianPayload(const std::array<uint8_t, 8>& bytes, int dlc){
    uint64_t payload = 0;
    const int byteCount = dlc > 8 ? 8 : (dlc < 0 ? 0 : dlc);
    for (int i = 0; i < byteCount; ++i) {
        payload |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    return payload;
}

bool parseDbcFromStream(CanStore& store, std::istream& stream, const std::string& sourceLabel) {
    std::string line;
    uint32_t currentId = 0;
    int messageCountLocal = 0;
    int signalCountLocal = 0;
    struct PendingValType {
        int canId = 0;
        std::string signalName;
        valType type = vINT;
    };
    std::vector<PendingValType> pendingValTypes;

    // === Main parsing loop ===
    while (std::getline(stream, line)) {
        // trim leading spaces/tabs
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty()) continue;

        // --- Parse BO_ lines ---
        if (line.rfind("BO_", 0) == 0) {
            uint32_t canId = 0;
            std::string name, sender;
            uint8_t dlc = 0;

            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> canId;

            std::string tmp;
            iss >> tmp;
            auto colon = tmp.find(':');
            if (colon == std::string::npos)
                continue;
            name = tmp.substr(0, colon);

            std::string dlcStr;
            iss >> dlcStr;
            try {
                dlc = static_cast<uint8_t>(std::stoi(dlcStr));
            } catch (...) {
                continue;
            }

            iss >> sender;

            {
                CanMessage& msg = store.canMessages[static_cast<int>(canId)];
                msg.canId = static_cast<int>(canId);
                msg.name = name;
                msg.dlc = dlc;
                msg.transmitter = sender;
                msg.signals.clear();
            }

            currentId = canId;
            messageCountLocal++;
            logs("[DBC] Registered message: " << name
                      << " (ID=" << canId << ")");
        }

        // --- Parse SG_ lines ---
        else if (line.find("SG_") != std::string::npos && currentId != 0) {
            std::istringstream iss(line);
            std::string tag, sigName;
            iss >> tag >> sigName; // SG_ <name>

            CanSignal sig{};
            sig.name = sigName;
            char c = 0;

            // find the colon
            while (iss >> c) {
                if (c == ':') break;
            }
            if (c != ':') continue;

            // parse 0|32@1+
            iss >> sig.startBit;
            iss.ignore(1, '|');
            iss >> sig.length;
            iss.ignore(1, '@');
            iss >> sig.endianness;
            iss >> c;
            sig.isSigned = (c == '-');

            // parse (scale,offset)
            if (iss >> c && c == '(') {
                iss >> sig.scale;
                iss.ignore(1, ',');
                iss >> sig.offset;
                iss.ignore(1, ')');
            }

            // parse [min|max]
            if (iss >> c && c == '[') {
                iss >> sig.min;
                iss.ignore(1, '|');
                iss >> sig.max;
                iss.ignore(1, ']');
            }

            // parse "unit"
            if (iss >> std::ws && iss.peek() == '"') {
                iss.get(); // consume "
                std::getline(iss, sig.unit, '"');
            }

            // receiver (may or may not be in brackets)
            std::string receiver;
            if (iss >> std::ws) {
                if (iss.peek() == '[') {
                    iss.get(); // [
                    std::getline(iss, receiver, ']');
                } else {
                    iss >> receiver; // plain token (Vector__XXX)
                }
                sig.receiver = receiver;
            }

            {
                auto it = store.canMessages.find(static_cast<int>(currentId));
                if (it != store.canMessages.end())
                    it->second.signals.push_back(sig);
            }

            signalCountLocal++;
            logs("[DBC] Registered signal: " << sigName
                      << " (ID=" << currentId << ")");
        }
        // --- Parse SIG_VALTYPE_ lines ---
        else if (line.rfind("SIG_VALTYPE_", 0) == 0) {
            std::istringstream iss(line);
            std::string tag;
            int canId = 0;
            std::string sigName;
            std::string colon;
            std::string typeStr;
            iss >> tag >> canId >> sigName >> colon >> typeStr;
            if (sigName.empty() || typeStr.empty()) {
                continue;
            }
            if (!typeStr.empty() && typeStr.back() == ';') {
                typeStr.pop_back();
            }

            int rawType = 0;
            try {
                rawType = std::stoi(typeStr);
            } catch (...) {
                continue;
            }

            valType parsedType = vINT;
            if (rawType == 1) {
                parsedType = vFLOAT;
            } else if (rawType == 2) {
                parsedType = vDOUBLE;
            }

            bool applied = false;
            auto msgIt = store.canMessages.find(canId);
            if (msgIt != store.canMessages.end()) {
                for (auto& sig : msgIt->second.signals) {
                    if (sig.name == sigName) {
                        sig.type = parsedType;
                        applied = true;
                        break;
                    }
                }
            }

            if (!applied) {
                pendingValTypes.push_back(PendingValType{canId, sigName, parsedType});
            }
        }
    }

    for (const auto& pending : pendingValTypes) {
        auto msgIt = store.canMessages.find(pending.canId);
        if (msgIt == store.canMessages.end()) {
            continue;
        }
        for (auto& sig : msgIt->second.signals) {
            if (sig.name == pending.signalName) {
                sig.type = pending.type;
                break;
            }
        }
    }

    logs("[DBC Loader] Parsed " << messageCountLocal
          << " messages and " << signalCountLocal
          << " signals from " << sourceLabel);
    logs("[DBC Loader] Current total messages in map: "
          << store.canMessages.size());

    store.dump();
    return (messageCountLocal > 0);
}

}  // namespace

bool CanStore::loadStateFromFile(std::string filePath){
    std::ifstream file(filePath);
    if (!file.is_open()) {
        logs("[DBC Loader] Failed to open " << filePath);
        return false;
    }
    return parseDbcFromStream(*this, file, filePath);
}

bool CanStore::loadStateFromHeader(const unsigned char* headerData, size_t headerSize) {
    if (headerData == nullptr || headerSize == 0) {
        logs("[DBC Loader] Header data is empty");
        return false;
    }

    std::string dbcText(reinterpret_cast<const char*>(headerData), headerSize);
    std::istringstream stream(dbcText);
    return parseDbcFromStream(*this, stream, "embedded header");
}

void CanStore::dump() {
    //std::lock_guard<std::mutex> lock(mapMutex);
    logs("========== DBC MAP ==========");
    for (const auto& [id, msg] : canMessages) {
                logs("CAN ID: " << id
                  << " | Name: " << msg.name
                  << " | DLC: " << msg.dlc
                  << " | Sender: " << msg.transmitter);
        for (const auto& sigName : msg.signals) {
                const char* typeLabel = "int";
                if (sigName.type == vFLOAT) {
                    typeLabel = "float";
                } else if (sigName.type == vDOUBLE) {
                    typeLabel = "double";
                }
                logs("   SG_ " << sigName.name
                      << " start=" << sigName.startBit
                      << " len=" << sigName.length
                      << " endian=" << sigName.endianness
                      << (sigName.isSigned ? " signed" : " unsigned")
                      << " type=" << typeLabel
                      << " offset,scale=" << sigName.offset << " " << sigName.scale);
        }
    }
        logs("=============================");
}

double CanMessage::decodeSignalValue(const std::array<uint8_t, 8>& value, const CanSignal& sg) const {
    const uint64_t raw = (sg.endianness == 0)
        ? extractBe(value, dlc, sg.startBit, sg.length)
        : extractLe(value, dlc, sg.startBit, sg.length);

    if(sg.type == vINT){
        uint64_t bits = static_cast<uint64_t>(raw);
        uint64_t parsedValue = std::bit_cast<uint64_t>(bits);
        parsedValue = static_cast<uint64_t>((parsedValue + sg.offset) * sg.scale);
        return static_cast<double>(parsedValue);
    }

    if(sg.type == vFLOAT){
        uint32_t bits = static_cast<uint32_t>(raw);
        float parsedValue = std::bit_cast<float>(bits);
        parsedValue = static_cast<float>((parsedValue + sg.offset) * sg.scale);
        return static_cast<double>(parsedValue);
    }

    if(sg.type == vDOUBLE){
        uint64_t bits = static_cast<uint64_t>(raw);
        double parsedValue = std::bit_cast<double>(bits);
        parsedValue = (parsedValue + sg.offset) * sg.scale;
        return parsedValue;
    }

    std::cout << canId << " " << sg.name << " failed" << std::endl;
    return 0.0;
}

std::vector<double> CanMessage::decodeSignalValues(const std::array<uint8_t, 8>& value) const {
    std::vector<double> decodedValues;
    decodedValues.reserve(signals.size());
    for(const auto& signal : signals){
        decodedValues.push_back(decodeSignalValue(value, signal));
    }
    return decodedValues;
}

void CanMessage::updateMessage(Parse* networkSource){
    std::array<uint8_t, 8> value;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    const bool haveNewData = networkSource->readSample(canId, value);
    time.push_back(time.back() + deltaTime);
    // metadata updates
    const auto now = std::chrono::system_clock::now();
    if (!haveNewData) {
        for (auto& sg : signals) { 
            sg.timeSinceMutation = std::chrono::duration_cast<std::chrono::milliseconds>(now-sg.lastTimeMutated);
        }
        timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTimeUpdated);
    } else {
        for(auto& sg : signals){
            sg.timeSinceMutation = std::chrono::duration_cast<std::chrono::milliseconds>(now - sg.lastTimeMutated);
            sg.lastTimeMutated = now;
        }
        timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTimeUpdated);
        lastTimeUpdated = now;
        double dt = std::chrono::duration<double>(timeSinceUpdate).count();
        dt = dt > 0.0 ? (5 + 2 * dlc) / dt : 0.0;
        dataRate = (dataRate + dt)/2;
        double amtThisUpdate = 5 + (dlc*2);
        dataTransfer = dataTransfer + amtThisUpdate; // # of bytes of a slcan packet for this message

        networkSource->canStore.totalBandwidth = networkSource->canStore.totalBandwidth + amtThisUpdate;
    }

    // data updates
    const int byteCount = dlc > 8 ? 8 : (dlc < 0 ? 0 : dlc);
    double totalBytes = static_cast<double>(time.size()) * sizeof(double);
    for(auto& sg : signals){
        sg.data.push_back(decodeSignalValue(value, sg));
        totalBytes += static_cast<double>(sg.data.size()) * sizeof(double);
    }

    storageSize = totalBytes / (1024.0 * 1024.0); // MiB
    bandwidthPercentage = dataTransfer/networkSource->canStore.totalBandwidth;
}
