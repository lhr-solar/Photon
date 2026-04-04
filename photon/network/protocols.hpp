#pragma once
#include <cstdint>
#include <cstring>
#include <thread>
#include "../parse/spmc.hpp"

enum class ProtocolKind : uint32_t {
    None = 0,
    TCP,
    UDP,
    UART,
    SocketCAN,
    AssettoCorsa,
};

struct TCPConfig{
    char ip[64] = "127.0.0.1";
    uint16_t port = 9000;
};

struct UDPConfig{
    char ip[64] = "127.0.0.1";
    uint16_t port = 9001;
    char subscribeMessage[128] = "subscribe";
};

#ifdef _WIN32
struct UARTConfig{
    char device[128] = "COM1";
    uint32_t baudRate = 115200;
};

struct SocketCANConfig{
    bool useBtr = false;
    uint32_t bitrateKbps = 500;
    float samplePointPercent = 87.5f;
    uint32_t prescaler = 1;
    uint32_t btr0 = 0x00;
    uint32_t btr1 = 0x1C;
    char channel[32] = "auto";
    bool listenOnly = false;
    bool busoffReset = false;
};
#else
struct UARTConfig{
    char device[128] = "/dev/ttyUSB0";
    uint32_t baudRate = 115200;
};

struct SocketCANConfig{
    bool useBtr = false;
    uint32_t bitrateKbps = 500;
    float samplePointPercent = 87.5f;
    uint32_t prescaler = 1;
    uint32_t btr0 = 0x00;
    uint32_t btr1 = 0x1C;
    char interfaceName[128] = "can0";
};
#endif

struct ProtocolConfig{
    ProtocolKind kind = ProtocolKind::None;
    TCPConfig tcp{};
    UDPConfig udp{};
    UARTConfig uart{};
    SocketCANConfig socketCAN{};
};

struct ProtocolError{
    bool fatal = false;
    char message[192]{};
};

struct Protocols{
    static const char* name(ProtocolKind kind);
    static void publishFailure(SPMCQueue<ProtocolError, 64>* statusBuffer, const char* name);
    static void run(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const ProtocolConfig& config);
    static void TCP(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const TCPConfig& config);
    static void UDP(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const UDPConfig& config);
    static void UART(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const UARTConfig& config);
    static void SocketCAN(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer,
        const SocketCANConfig& config);
    static void AssettoCorsa(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer);
};
