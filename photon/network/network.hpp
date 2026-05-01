#pragma once
#include "../parse/parse.hpp"
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
    SetDBC,
    StartStreamForward,
    StopStreamForward,
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
    DBCKind dbc = DBCKind::VehicleWithUndisclosedName;
    char dbcPath[512]{};
};

struct NetworkResponse{
    NetworkResponseType type = NetworkResponseType::None;
    ProtocolKind protocol = ProtocolKind::None;
    bool networkStreamRunning = false;
    bool streamForwardRunning = false;
    char message[192]{};
};

struct Network{
    std::jthread handlerThread{};
    std::jthread parserThread{};
    std::jthread networkStreamThread{};
    std::jthread streamForwardThread{};
    Parse* parse{};
    Arena* arena{};
    SPMCQueue<NetworkCommand, 64>* guiCommands{};
    SPMCQueue<NetworkCommand, 64>::Reader guiCommandReader{};
    SPMCQueue<NetworkResponse, 64> guiResponses{};
    SPMCQueue<ProtocolError, 64> networkStreamStatus{};
    SPMCQueue<uint8_t, 4096> networkStream{};
    std::atomic<bool> networkStreamRunning = false;
    std::atomic<bool> streamForwardRunning = false;
    ProtocolConfig activeConfig{};

    void init(Parse* parse, SPMCQueue<NetworkCommand, 64>* guiCommands);
    void destroy();
    SPMCQueue<NetworkResponse, 64>::Reader getResponseReader();

private:
    void handler(std::stop_token stopToken);
    void parser(std::stop_token stopToken);
    void startParser();
    void stopParser();
    void startNetworkStream(const ProtocolConfig& config);
    void stopNetworkStream();
    void startStreamForward();
    void stopStreamForward();
    void forwardStream(std::stop_token stopToken);
    void publishResponse(NetworkResponseType type, ProtocolKind protocol, const char* message);
};
