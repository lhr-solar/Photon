#include "network.hpp"
#include "../engine/include.hpp"
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr size_t kMetricHistoryLimit = 120;

int hexValue(char c){
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool decodeFrame(const std::string& frame, uint16_t& canId, std::array<uint8_t, 8>& value){
    if(frame.empty() || frame.front() != 't') return false;
    if(frame.back() != '\r') return false;
    if(frame.size() < 5) return false;

    const std::size_t payloadLength = frame.size() - 1;
    int dataLength = hexValue(frame[4]);
    if(dataLength < 0 || dataLength > 8) return false;

    const std::size_t expectedLength = 5 + static_cast<std::size_t>(dataLength) * 2;
    if(payloadLength != expectedLength) return false;

    canId = 0;
    for(std::size_t i = 1; i <= 3; ++i){
        int nibble = hexValue(frame[i]);
        if(nibble < 0) return false;
        canId = static_cast<uint16_t>((canId << 4) | static_cast<uint16_t>(nibble));
    }

    value = {};
    for(int i = 0; i < dataLength; ++i){
        int h = hexValue(frame[5 + i * 2]);
        int l = hexValue(frame[6 + i * 2]);
        if(h < 0 || l < 0) return false;
        value[i] = static_cast<uint8_t>((h << 4) + l);
    }
    return true;
}

uint64_t extractLe(const std::array<uint8_t, 8>& bytes, int dlc, int startBit, int bitLength){
    if(bitLength <= 0 || dlc < 0 || startBit < 0) return 0;
    if(dlc > 8) dlc = 8;
    if(startBit + bitLength > dlc * 8) return 0;

    uint64_t v = 0;
    for(int i = 0; i < dlc; ++i) v = (v << 8) | uint64_t(bytes[i]);
    if(bitLength == 64) return v >> startBit;

    uint64_t mask = (1ULL << bitLength) - 1ULL;
    return (v >> startBit) & mask;
}

uint64_t extractBe(const std::array<uint8_t, 8>& bytes, int dlc, int startBit, int bitLength){
    if(bitLength <= 0 || dlc < 0 || startBit < 0) return 0;
    if(dlc > 8) dlc = 8;
    if(bitLength > 64) return 0;

    const int totalBits = dlc * 8;
    if(startBit >= totalBits) return 0;

    const int byteIdx = startBit / 8;
    const int bitIdx = startBit % 8;
    if(byteIdx >= dlc) return 0;

    uint64_t v = 0;
    for(int i = 0; i < dlc; ++i) v = (v << 8) | uint64_t(bytes[i]);

    const int p0 = (totalBits - 1) - (byteIdx * 8 + (7 - bitIdx));
    const int low = p0 - bitLength + 1;
    if(low < 0) return 0;
    if(bitLength == 64) return v >> low;

    const uint64_t mask = (1ULL << bitLength) - 1ULL;
    return (v >> low) & mask;
}

int64_t signExtend(uint64_t raw, uint8_t bits){
    if(bits == 0 || bits >= 64) return static_cast<int64_t>(raw);
    const uint64_t signBit = 1ULL << (bits - 1);
    const uint64_t mask = (1ULL << bits) - 1ULL;
    uint64_t v = raw & mask;
    if(v & signBit) v |= ~mask;
    return static_cast<int64_t>(v);
}

double decodeSignalValue(const Message& msg, const Signal& sig, const std::array<uint8_t, 8>& value){
    const uint64_t raw = (sig.endianness == 0)
        ? extractBe(value, static_cast<int>(msg.dlc), sig.startBit, sig.length)
        : extractLe(value, static_cast<int>(msg.dlc), sig.startBit, sig.length);

    if(sig.type == vINT){
        double parsedValue = sig.isSigned
            ? static_cast<double>(signExtend(raw, static_cast<uint8_t>(sig.length)))
            : static_cast<double>(raw);
        return (parsedValue + sig.offset) * sig.scale;
    }

    if(sig.type == vFLOAT){
        uint32_t bits = static_cast<uint32_t>(raw);
        float parsedValue = std::bit_cast<float>(bits);
        parsedValue = static_cast<float>((parsedValue + sig.offset) * sig.scale);
        return static_cast<double>(parsedValue);
    }

    if(sig.type == vDOUBLE){
        uint64_t bits = static_cast<uint64_t>(raw);
        double parsedValue = std::bit_cast<double>(bits);
        return (parsedValue + sig.offset) * sig.scale;
    }

    return 0.0;
}

bool handleFrame(Arena* arena, const std::string& frame, double timeValue){
    if(!arena) return false;
    //logs(frame);

    uint16_t canId = 0;
    std::array<uint8_t, 8> value{};
    if(!decodeFrame(frame, canId, value)) return false;
    if(canId >= arena->messages.size()) return false;

    Message* msg = arena->messages[canId];
    if(!msg || msg->signalCount == 0) return false;

    std::vector<double> decodedValues;
    decodedValues.reserve(msg->signalCount);
    for(uint32_t i = 0; i < msg->signalCount; ++i){
        Signal* sig = msg->signals[i];
        if(!sig) return false;
        decodedValues.push_back(decodeSignalValue(*msg, *sig, value));
    }

    const auto now = std::chrono::system_clock::now();
    for(uint32_t i = 0; i < msg->signalCount; ++i){
        Signal* sig = msg->signals[i];
        sig->timeSinceMutation = std::chrono::duration_cast<std::chrono::milliseconds>(now - sig->lastTimeMutated);
        sig->lastTimeMutated = now;
    }
    msg->timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - msg->lastTimeUpdated);
    msg->lastTimeUpdated = now;

    const double dt = std::chrono::duration<double>(msg->timeSinceUpdate).count();
    const double transfer = 5.0 + (static_cast<double>(msg->dlc) * 2.0);
    msg->dataRate = dt > 0.0 ? (msg->dataRate + (transfer / dt)) / 2.0 : msg->dataRate;
    msg->dataTransfer += transfer;
    arena->totalDataTransfer += transfer;
    msg->bandwidthPercentage = arena->totalDataTransfer > 0.0
        ? (msg->dataTransfer / arena->totalDataTransfer)
        : 0.0;
    msg->dataRateHistory.push_back(static_cast<float>(msg->dataRate));
    msg->dataTransferHistory.push_back(static_cast<float>(msg->dataTransfer));
    if(msg->dataRateHistory.size() > kMetricHistoryLimit) msg->dataRateHistory.erase(msg->dataRateHistory.begin());
    if(msg->dataTransferHistory.size() > kMetricHistoryLimit) msg->dataTransferHistory.erase(msg->dataTransferHistory.begin());

    return arena->appendFrame(canId, timeValue, decodedValues.data(), static_cast<uint32_t>(decodedValues.size()));
}
}

void Network::publishResponse(NetworkResponseType type, ProtocolKind protocol, const char* message){
    guiResponses.write([&](NetworkResponse& response){
        response = {};
        response.type = type;
        response.protocol = protocol;
        response.networkStreamRunning = networkStreamRunning.load(std::memory_order_acquire);
        if(message) std::strncpy(response.message, message, sizeof(response.message) - 1);
    });
}

void Network::startNetworkStream(const ProtocolConfig& config){
    stopNetworkStream();
    activeConfig = config;
    networkStreamRunning.store(true, std::memory_order_release);
    publishResponse(NetworkResponseType::Info, activeConfig.kind, "Starting Network Stream");
    networkStreamThread = std::jthread([this, config](std::stop_token stopToken){
        Protocols::run(stopToken, &networkStreamStatus, &networkStream, config);
    });
}

void Network::stopNetworkStream(){
    const ProtocolKind previous = activeConfig.kind;
    networkStreamRunning.store(false, std::memory_order_release);
    if(networkStreamThread.joinable()){
        networkStreamThread.request_stop();
        networkStreamThread.join();
    }
    activeConfig = {};
    if(previous != ProtocolKind::None) publishResponse(NetworkResponseType::Info, previous, "Network Stream Stopped");
}

void Network::handler(std::stop_token stopToken){
    auto statusReader = networkStreamStatus.getReader();
    publishResponse(NetworkResponseType::Info, ProtocolKind::None, "Network Handler Online");
    while(!stopToken.stop_requested()){
        bool didWork = false;

        if(NetworkCommand* command = guiCommandReader.readLast()){
            didWork = true;
            switch(command->type){
                case NetworkCommandType::StartProtocol:
                    startNetworkStream(command->config);
                    break;
                case NetworkCommandType::StopProtocol:
                    stopNetworkStream();
                    break;
                case NetworkCommandType::Shutdown:
                    stopNetworkStream();
                    publishResponse(NetworkResponseType::Info, ProtocolKind::None, "Network Shutdown Requested");
                    break;
                case NetworkCommandType::None:
                default:
                    break;
            }
        }

        while(ProtocolError* status = statusReader.read()){
            didWork = true;
            publishResponse(status->fatal ? NetworkResponseType::Error : NetworkResponseType::Info,
                activeConfig.kind, status->message);
        }
        if(!didWork) std::this_thread::sleep_for(kIdleSleep);
    }
    stopNetworkStream();
}

void Network::parser(std::stop_token stopToken){
    auto streamReader = networkStream.getReader();
    std::string frame;
    frame.reserve(32);
    bool collecting = false;
    const auto start = std::chrono::steady_clock::now();

    while(!stopToken.stop_requested()){
        bool didWork = false;
        while(uint8_t* byte = streamReader.read()){
            didWork = true;
            const char ch = static_cast<char>(*byte);

            if(ch == 't'){
                frame.clear();
                frame.push_back(ch);
                collecting = true;
                continue;
            }
            if(!collecting) continue;

            if(ch == '\r'){
                frame.push_back(ch);
                const double timeValue = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start).count();
                handleFrame(arena, frame, timeValue);
                frame.clear();
                collecting = false;
                continue;
            }

            if(ch == '\n') continue;
            frame.push_back(ch);
        }
        if(!didWork) std::this_thread::sleep_for(kIdleSleep);
    }
}

void Network::init(Arena* arena, GUICommandQueue* guiCommands){
    this->arena = arena;
    this->guiCommands = guiCommands;
    if(this->guiCommands) guiCommandReader = this->guiCommands->getReader();
    parserThread = std::jthread([this](std::stop_token stopToken){ parser(stopToken); });
    handlerThread = std::jthread([this](std::stop_token stopToken){ handler(stopToken); });
}

GUIResponseQueue::Reader Network::getResponseReader(){
    return guiResponses.getReader();
}

void Network::destroy(){
    if(handlerThread.joinable()) handlerThread.request_stop();
    if(parserThread.joinable()) parserThread.request_stop();
    if(networkStreamThread.joinable()) networkStreamThread.request_stop();
    if(handlerThread.joinable()) handlerThread.join();
    if(parserThread.joinable()) parserThread.join();
    if(networkStreamThread.joinable()) networkStreamThread.join();
    networkStreamRunning.store(false, std::memory_order_release);
}
