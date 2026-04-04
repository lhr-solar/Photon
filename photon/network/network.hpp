#pragma once
#include "../parse/arena.hpp"
#include "../parse/spmc.hpp"
#include "protocols.hpp"
#include <atomic>
#include <chrono>
#include <thread>

constexpr auto kIdleSleep = std::chrono::milliseconds(1);

enum class NetworkCommandType : uint32_t {
    None = 0,
    StartProtocol,
    StopProtocol,
    Shutdown,
};

enum class NetworkResponseType : uint32_t {
    None = 0,
    Info,
    Success,
    Error,
};

struct NetworkCommand{
    NetworkCommandType type = NetworkCommandType::None;
    ProtocolConfig config{};
};

struct NetworkResponse{
    NetworkResponseType type = NetworkResponseType::None;
    ProtocolKind protocol = ProtocolKind::None;
    bool networkStreamRunning = false;
    char message[192]{};
};

using GUICommandQueue = SPMCQueue<NetworkCommand, 64>;
using GUIResponseQueue = SPMCQueue<NetworkResponse, 64>;
using NetworkStreamStatusQueue = SPMCQueue<ProtocolError, 64>;
using NetworkStreamQueue = SPMCQueue<uint8_t, 4096>;

struct Network{
    std::jthread handlerThread{};
    std::jthread parserThread{};
    std::jthread networkStreamThread{};
    Arena* arena{};
    GUICommandQueue* guiCommands{};
    GUICommandQueue::Reader guiCommandReader{};
    GUIResponseQueue guiResponses{};
    NetworkStreamStatusQueue networkStreamStatus{};
    NetworkStreamQueue networkStream{};
    std::atomic<bool> networkStreamRunning = false;
    ProtocolConfig activeConfig{};

    void init(Arena* arena, GUICommandQueue* guiCommands);
    void destroy();
    GUIResponseQueue::Reader getResponseReader();

private:
    void handler(std::stop_token stopToken);
    void parser(std::stop_token stopToken);
    void startNetworkStream(const ProtocolConfig& config);
    void stopNetworkStream();
    void publishResponse(NetworkResponseType type, ProtocolKind protocol, const char* message);
};
