#include "protocols.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
void publishStatus(SPMCQueue<ProtocolError, 64>* statusBuffer, bool fatal, const char* message){
    if(!statusBuffer) return;
    statusBuffer->write([&](ProtocolError& status){
        status = {};
        status.fatal = fatal;
        std::snprintf(status.message, sizeof(status.message), "%s", message);
    });
}

void publishFailure(SPMCQueue<ProtocolError, 64>* statusBuffer, const char* name){
    char message[192]{};
    std::snprintf(message, sizeof(message), "Failed to Initiate %s", name);
    publishStatus(statusBuffer, true, message);
}

void writeFrame(SPMCQueue<uint8_t, 4096>* streamBuffer, const char* frame){
    if(!streamBuffer || !frame) return;
    for(const char* c = frame; *c != '\0'; ++c){
        const uint8_t byte = static_cast<uint8_t>(*c);
        streamBuffer->write([&](uint8_t& slot){ slot = byte; });
    }
}
}

const char* Protocols::name(ProtocolKind kind){
    switch(kind){
        case ProtocolKind::TCP: return "TCP";
        case ProtocolKind::UDP: return "UDP";
        case ProtocolKind::UART: return "UART";
        case ProtocolKind::SocketCAN: return "SocketCAN";
        default: return "None";
    }
}

void Protocols::publishFailure(SPMCQueue<ProtocolError, 64>* statusBuffer, const char* name){
    ::publishFailure(statusBuffer, name);
}

void Protocols::run(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const ProtocolConfig& config){
    switch(config.kind){
        case ProtocolKind::TCP:
            TCP(stopToken, statusBuffer, streamBuffer, config.tcp);
            return;
        case ProtocolKind::UDP:
            UDP(stopToken, statusBuffer, streamBuffer, config.udp);
            return;
        case ProtocolKind::UART:
            UART(stopToken, statusBuffer, streamBuffer, config.uart);
            return;
        case ProtocolKind::SocketCAN:
            SocketCAN(stopToken, statusBuffer, streamBuffer, config.socketCAN);
            return;
        case ProtocolKind::None:
        default:
            ::publishFailure(statusBuffer, "None");
            return;
    }
}

void Protocols::TCP(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const TCPConfig& config){
    (void)stopToken;
    (void)streamBuffer;
    (void)config;
    ::publishFailure(statusBuffer, "TCP");
}

void Protocols::UDP(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const UDPConfig& config){
    (void)stopToken;
    (void)streamBuffer;
    (void)config;
    ::publishFailure(statusBuffer, "UDP");
}

void Protocols::UART(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const UARTConfig& config){
    (void)config;
    publishStatus(statusBuffer, false, "UART Online");

    static constexpr const char* frames[] = {
        "t00181122334455667788\r",
        "t0028AABBCCDDEEFF0011\r",
        "t00380102030405060708\r",
    };

    size_t index = 0;
    while(!stopToken.stop_requested()){
        writeFrame(streamBuffer, frames[index]);
        index = (index + 1) % (sizeof(frames) / sizeof(frames[0]));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    publishStatus(statusBuffer, false, "UART Stopped");
}

void Protocols::SocketCAN(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const SocketCANConfig& config){
    (void)stopToken;
    (void)streamBuffer;
    (void)config;
    ::publishFailure(statusBuffer, "SocketCAN");
}
