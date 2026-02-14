#include "parse.hpp"
#include "imgui.h"
#include "../engine/include.hpp"
#include <cstdint>
#include <array>
#include <fstream>
#include <iostream>

namespace {

uint64_t extractBitsLe(uint64_t value, uint8_t startBit, uint8_t bitCount){
    if (bitCount == 0 || startBit >= 64) { return 0; }
    if (bitCount >= 64) { return value; }
    const uint64_t mask = (bitCount == 64) ? ~0ULL : ((1ULL << bitCount) - 1ULL);
    return (value >> startBit) & mask;
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

std::array<uint8_t, 8> unpackBytes(uint64_t value, int dlc){
    std::array<uint8_t, 8> bytes{};
    if (dlc <= 0) { return bytes; }
    const int byteCount = dlc > 8 ? 8 : dlc;
    for (int i = 0; i < byteCount; ++i) {
        const int shift = 8 * (byteCount - 1 - i);
        bytes[i] = static_cast<uint8_t>((value >> shift) & 0xFF);
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

uint64_t extractBitsBe(const std::array<uint8_t, 8>& bytes, int dlc, int startBit, int bitCount){
    if (bitCount <= 0) { return 0; }
    uint64_t result = 0;
    const int byteCount = dlc > 8 ? 8 : (dlc < 0 ? 0 : dlc);
    int bitIndex = startBit;
    for (int i = 0; i < bitCount; ++i) {
        const int byteIndex = bitIndex / 8;
        if (byteIndex < 0 || byteIndex >= byteCount) { break; }
        const int bitInByte = 7 - (bitIndex % 8);
        const uint64_t bit = (bytes[byteIndex] >> bitInByte) & 0x1ULL;
        result = (result << 1) | bit;
        if (bitIndex % 8 == 0) { bitIndex += 15;} 
        else { --bitIndex; }
    }
    return result;
}

}  // namespace

bool CanStore::loadStateFromFile(std::string filePath){
    std::ifstream file(filePath);
    if (!file.is_open()) {
        logs("[DBC Loader] Failed to open " << filePath);
        return false;
    }

    std::string line;
    uint32_t currentId = 0;
    int messageCountLocal = 0;
    int signalCountLocal = 0;
    struct PendingValType {
        int canId = 0;
        std::string signalName;
        valType type = INT;
    };
    std::vector<PendingValType> pendingValTypes;

    // === Main parsing loop ===
    while (std::getline(file, line)) {
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

            // at this point, we have accumulated enough data to populate 1 can message
            // we should go about populating it
            {
                //std::lock_guard<std::mutex> lock(mapMutex);
                CanMessage& msg = canMessages[static_cast<int>(canId)];
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
                //std::lock_guard<std::mutex> lock(mapMutex);
                auto it = canMessages.find(static_cast<int>(currentId));
                if (it != canMessages.end())
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

            valType parsedType = INT;
            if (rawType == 1) {
                parsedType = FLOAT;
            } else if (rawType == 2) {
                parsedType = DOUBLE;
            }

            bool applied = false;
            auto msgIt = canMessages.find(canId);
            if (msgIt != canMessages.end()) {
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
        auto msgIt = canMessages.find(pending.canId);
        if (msgIt == canMessages.end()) {
            continue;
        }
        for (auto& sig : msgIt->second.signals) {
            if (sig.name == pending.signalName) {
                sig.type = pending.type;
                break;
            }
        }
    }

    // --- Summary + dump ---
    {
        //std::lock_guard<std::mutex> lock(mapMutex);
            logs("[DBC Loader] Parsed " << messageCountLocal
                  << " messages and " << signalCountLocal
                  << " signals from " << filePath);
            logs("[DBC Loader] Current total messages in map: "
                  << canMessages.size());
    }

    dump();
    return (messageCountLocal > 0);
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
                if (sigName.type == FLOAT) {
                    typeLabel = "float";
                } else if (sigName.type == DOUBLE) {
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

void CanMessage::updateMessage(Parse* networkSource){
    uint64_t encoded;
    ImGuiIO &io = ImGui::GetIO();
    float deltaTime = io.DeltaTime;
    const bool hasNewData = networkSource->readSample(canId, encoded);
    time.push_back(time.back() + deltaTime);
    const auto now = std::chrono::system_clock::now();
    if (!hasNewData) {
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

    const auto bytes = unpackBytes(encoded, dlc);
    const uint64_t littlePayload = buildLittleEndianPayload(bytes, dlc);
    const int byteCount = dlc > 8 ? 8 : (dlc < 0 ? 0 : dlc);
    double totalBytes = static_cast<double>(time.size()) * sizeof(double);

    for(auto& sg : signals){
        uint64_t raw = 0;
        if (sg.endianness == 0) {
            raw = extractBitsBe(bytes, byteCount, sg.startBit, sg.length);
        } else {
            raw = extractBitsLe(littlePayload,
                                static_cast<uint8_t>(sg.startBit),
                                static_cast<uint8_t>(sg.length));
        }

        int64_t signedRaw = sg.isSigned ? signExtend(raw, static_cast<uint8_t>(sg.length))
                                        : static_cast<int64_t>(raw);
        double physical = static_cast<double>(signedRaw) * sg.scale + sg.offset;

        sg.data.push_back(physical);
        totalBytes += static_cast<double>(sg.data.size()) * sizeof(double);
    }

    storageSize = totalBytes / (1024.0 * 1024.0); // MiB
    bandwidthPercentage = dataTransfer/networkSource->canStore.totalBandwidth;
}
