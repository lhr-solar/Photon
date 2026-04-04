#include "protocols.hpp"
#include <chrono>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
#include <thread>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <winsock2.h>
#include <Ws2tcpip.h>
using SocketHandle = SOCKET;
#else
#include <linux/can.h>
#include <linux/can/netlink.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketHandle = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

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

struct ClassicTiming{
    uint16_t btr = 0;
    int bitrate = 0;
    int samplePointX10 = -1;
    int prescaler = -1;
    int sjw = 1;
};

bool decodeClassicBtr(uint32_t btr0, uint32_t btr1, ClassicTiming& timing){
    constexpr int kClockHz = 16000000;
    if(btr0 > 0xFF || btr1 > 0xFF) return false;
    const int brp = static_cast<int>(btr0 & 0x3F) + 1;
    const int sjw = static_cast<int>((btr0 >> 6) & 0x03) + 1;
    const int tseg1 = static_cast<int>(btr1 & 0x0F) + 1;
    const int tseg2 = static_cast<int>((btr1 >> 4) & 0x07) + 1;
    const int totalTq = 1 + tseg1 + tseg2;
    if(totalTq <= 0) return false;

    timing = {};
    timing.btr = static_cast<uint16_t>((btr1 << 8) | btr0);
    timing.bitrate = kClockHz / (2 * brp * totalTq);
    timing.samplePointX10 = ((1 + tseg1) * 1000) / totalTq;
    timing.prescaler = brp;
    timing.sjw = sjw;
    return true;
}

bool synthesizeClassicBtr(uint32_t bitrateKbps, float samplePointPercent, uint32_t prescaler, ClassicTiming& timing){
    constexpr int kClockHz = 16000000;
    if(bitrateKbps == 0 || prescaler == 0) return false;
    const int bitrate = static_cast<int>(bitrateKbps * 1000U);
    const int brp = static_cast<int>(prescaler);
    const int samplePointX10 = static_cast<int>(samplePointPercent * 10.0f + 0.5f);
    if(samplePointX10 <= 0 || samplePointX10 >= 1000) return false;

    const int totalTq = kClockHz / (2 * brp * bitrate);
    if(totalTq < 8 || totalTq > 25) return false;

    int tseg1 = static_cast<int>((static_cast<double>(samplePointX10) / 1000.0) * totalTq + 0.5) - 1;
    tseg1 = std::clamp(tseg1, 1, 16);
    int tseg2 = totalTq - 1 - tseg1;
    if(tseg2 < 1){
        tseg2 = 1;
        tseg1 = totalTq - 2;
    }
    if(tseg2 > 8 || tseg1 < 1 || tseg1 > 16) return false;

    const uint32_t btr0 = (static_cast<uint32_t>(0) << 6) | ((prescaler - 1) & 0x3F);
    const uint32_t btr1 = (static_cast<uint32_t>(tseg2 - 1) << 4) | static_cast<uint32_t>((tseg1 - 1) & 0x0F);
    return decodeClassicBtr(btr0, btr1, timing);
}

bool resolveClassicTiming(const SocketCANConfig& config, ClassicTiming& timing){
    return config.useBtr
        ? decodeClassicBtr(config.btr0, config.btr1, timing)
        : synthesizeClassicBtr(config.bitrateKbps, config.samplePointPercent, config.prescaler, timing);
}

enum platform{
    eIPhoneDevice = 0,
};

enum operationId{
    HANDSHAKE = 0,
    SUBSCRIBE_UPDATE = 1,
};

struct handshake{
    platform id;
    int ver;
    operationId opId;
};

struct handshakeResponse{
    char carName[50];
    char driverName[50];
    int identifier;
    int version;
    char trackName[50];
    char trackConfig[50];
};

struct RTCarInfo{
    char identifier;
    int size;
    float speed_Kmh;
    float speed_Mph;
    float speed_Ms;
    bool isAbsEnabled;
    bool isAbsInAction;
    bool isTcInAction;
    bool isTcEnabled;
    bool isInPit;
    bool isEngineLimiterOn;
    float accG_vertical;
    float accG_horizontal;
    float accG_frontal;
    int lapTime;
    int lastLap;
    int bestLap;
    int lapCount;
    float gas;
    float brake;
    float clutch;
    float engineRPM;
    float steer;
    int gear;
    float cgHeight;
    float wheelAngularSpeed[4];
    float slipAngle[4];
    float slipAngle_ContactPatch[4];
    float slipRatio[4];
    float tyreSlip[4];
    float ndSlip[4];
    float load[4];
    float Dy[4];
    float Mz[4];
    float tyreDirtyLevel[4];
    float camberRAD[4];
    float tyreRadius[4];
    float tyreLoadedRadius[4];
    float suspensionHeight[4];
    float carPositionNormalized;
    float carSlope;
    float carCoordinates[3];
};

struct FieldInfo{
    std::size_t offset;
    std::size_t size;
};

#define FIELD(struct_type, member) FieldInfo{offsetof(struct_type, member), sizeof(((struct_type*)0)->member)}
#define FIELD_PART(struct_type, member, byte_offset, byte_size) FieldInfo{offsetof(struct_type, member) + static_cast<std::size_t>(byte_offset), static_cast<std::size_t>(byte_size)}

constexpr std::array<FieldInfo, 58> kRTCarInfoFields = {{
    FIELD(RTCarInfo, identifier),
    FIELD(RTCarInfo, size),
    FIELD(RTCarInfo, speed_Kmh),
    FIELD(RTCarInfo, speed_Mph),
    FIELD(RTCarInfo, speed_Ms),
    FIELD(RTCarInfo, isAbsEnabled),
    FIELD(RTCarInfo, isAbsInAction),
    FIELD(RTCarInfo, isTcInAction),
    FIELD(RTCarInfo, isTcEnabled),
    FIELD(RTCarInfo, isInPit),
    FIELD(RTCarInfo, isEngineLimiterOn),
    FIELD(RTCarInfo, accG_vertical),
    FIELD(RTCarInfo, accG_horizontal),
    FIELD(RTCarInfo, accG_frontal),
    FIELD(RTCarInfo, lapTime),
    FIELD(RTCarInfo, lastLap),
    FIELD(RTCarInfo, bestLap),
    FIELD(RTCarInfo, lapCount),
    FIELD(RTCarInfo, gas),
    FIELD(RTCarInfo, brake),
    FIELD(RTCarInfo, clutch),
    FIELD(RTCarInfo, engineRPM),
    FIELD(RTCarInfo, steer),
    FIELD(RTCarInfo, gear),
    FIELD(RTCarInfo, cgHeight),
    FIELD_PART(RTCarInfo, wheelAngularSpeed, 0, 8),
    FIELD_PART(RTCarInfo, wheelAngularSpeed, 8, 8),
    FIELD_PART(RTCarInfo, slipAngle, 0, 8),
    FIELD_PART(RTCarInfo, slipAngle, 8, 8),
    FIELD_PART(RTCarInfo, slipAngle_ContactPatch, 0, 8),
    FIELD_PART(RTCarInfo, slipAngle_ContactPatch, 8, 8),
    FIELD_PART(RTCarInfo, slipRatio, 0, 8),
    FIELD_PART(RTCarInfo, slipRatio, 8, 8),
    FIELD_PART(RTCarInfo, tyreSlip, 0, 8),
    FIELD_PART(RTCarInfo, tyreSlip, 8, 8),
    FIELD_PART(RTCarInfo, ndSlip, 0, 8),
    FIELD_PART(RTCarInfo, ndSlip, 8, 8),
    FIELD_PART(RTCarInfo, load, 0, 8),
    FIELD_PART(RTCarInfo, load, 8, 8),
    FIELD_PART(RTCarInfo, Dy, 0, 8),
    FIELD_PART(RTCarInfo, Dy, 8, 8),
    FIELD_PART(RTCarInfo, Mz, 0, 8),
    FIELD_PART(RTCarInfo, Mz, 8, 8),
    FIELD_PART(RTCarInfo, tyreDirtyLevel, 0, 8),
    FIELD_PART(RTCarInfo, tyreDirtyLevel, 8, 8),
    FIELD_PART(RTCarInfo, camberRAD, 0, 8),
    FIELD_PART(RTCarInfo, camberRAD, 8, 8),
    FIELD_PART(RTCarInfo, tyreRadius, 0, 8),
    FIELD_PART(RTCarInfo, tyreRadius, 8, 8),
    FIELD_PART(RTCarInfo, tyreLoadedRadius, 0, 8),
    FIELD_PART(RTCarInfo, tyreLoadedRadius, 8, 8),
    FIELD_PART(RTCarInfo, suspensionHeight, 0, 8),
    FIELD_PART(RTCarInfo, suspensionHeight, 8, 8),
    FIELD(RTCarInfo, carPositionNormalized),
    FIELD(RTCarInfo, carSlope),
    FIELD_PART(RTCarInfo, carCoordinates, 0, 4),
    FIELD_PART(RTCarInfo, carCoordinates, 4, 4),
    FIELD_PART(RTCarInfo, carCoordinates, 8, 4),
}};

bool setNonBlocking(SocketHandle sock){
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if(flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void closeSocket(SocketHandle sock){
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

void forwardAssettoFrame(SPMCQueue<uint8_t, 4096>* streamBuffer, const RTCarInfo& packet){
    const std::byte* base = reinterpret_cast<const std::byte*>(&packet);
    for(std::size_t i = 0; i < kRTCarInfoFields.size(); ++i){
        const FieldInfo& field = kRTCarInfoFields[i];
        if(field.size > 8) continue;

        char frame[32]{};
        int length = std::snprintf(frame, sizeof(frame), "t%03X%1X", static_cast<unsigned>(i), static_cast<unsigned>(field.size));
        const uint8_t* payload = reinterpret_cast<const uint8_t*>(base + field.offset);
        for(std::size_t b = 0; b < field.size && length > 0 && length < static_cast<int>(sizeof(frame) - 3); ++b)
            length += std::snprintf(frame + length, sizeof(frame) - static_cast<std::size_t>(length), "%02x", payload[field.size - b - 1]);
        if(length <= 0 || length >= static_cast<int>(sizeof(frame) - 1)) continue;
        frame[length++] = '\r';
        frame[length] = '\0';
        writeFrame(streamBuffer, frame);
    }
}

bool initAssettoSocket(SocketHandle& sock, sockaddr_in& server, char* error, std::size_t errorSize){
#ifdef _WIN32
    static bool wsaStarted = false;
    if(!wsaStarted){
        WSADATA wsa{};
        const int err = WSAStartup(MAKEWORD(2, 2), &wsa);
        if(err != 0){
            std::snprintf(error, errorSize, "WSAStartup failed: %d", err);
            return false;
        }
        wsaStarted = true;
    }
#endif
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock == INVALID_SOCKET){
#ifdef _WIN32
        std::snprintf(error, errorSize, "Assetto socket creation failed: %d", WSAGetLastError());
#else
        std::snprintf(error, errorSize, "Assetto socket creation failed: %s", std::strerror(errno));
#endif
        return false;
    }
    if(!setNonBlocking(sock)){
#ifdef _WIN32
        std::snprintf(error, errorSize, "failed to set Assetto socket non-blocking: %d", WSAGetLastError());
#else
        std::snprintf(error, errorSize, "failed to set Assetto socket non-blocking: %s", std::strerror(errno));
#endif
        closeSocket(sock);
        sock = INVALID_SOCKET;
        return false;
    }
    server = {};
    server.sin_family = AF_INET;
    server.sin_port = htons(9996);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);
    return true;
}

bool performAssettoHandshake(SocketHandle sock, sockaddr_in& server, bool& subscribed){
    if(subscribed) return true;
    auto sendControlMessage = [&](operationId op){
        handshake msg{};
        msg.id = eIPhoneDevice;
        msg.ver = 1;
        msg.opId = op;
        return sendto(sock, reinterpret_cast<const char*>(&msg), sizeof(msg), 0,
            reinterpret_cast<const sockaddr*>(&server), sizeof(server)) != SOCKET_ERROR;
    };

    if(!sendControlMessage(HANDSHAKE)) return false;
    std::array<char, 2048> buffer{};
    handshakeResponse response{};
    socklen_t slen = sizeof(server);
    const int received = recvfrom(sock, buffer.data(), static_cast<int>(buffer.size()), 0,
        reinterpret_cast<sockaddr*>(&server), &slen);
    if(received < static_cast<int>(sizeof(response))) return false;
    std::memcpy(&response, buffer.data(), sizeof(response));
    if(!sendControlMessage(SUBSCRIBE_UPDATE)) return false;
    subscribed = true;
    return true;
}

#ifdef _WIN32
using TPCANHandle = WORD;
using TPCANStatus = DWORD;
using TPCANParameter = BYTE;
using TPCANMessageType = BYTE;
using TPCANBaudrate = WORD;

struct TPCANMsg{
    DWORD ID;
    TPCANMessageType MSGTYPE;
    BYTE LEN;
    BYTE DATA[8];
};

struct TPCANTimestamp{
    DWORD millis;
    WORD millis_overflow;
    WORD micros;
};

struct TPCANChannelInformation{
    TPCANHandle channel_handle;
    BYTE device_type;
    BYTE controller_number;
    DWORD device_features;
    char device_name[33];
    DWORD device_id;
    DWORD channel_condition;
};

static constexpr WORD kEnglishLang = 0x09;
static constexpr TPCANHandle PCAN_NONEBUS = 0x00U;
static constexpr TPCANHandle PCAN_PCIBUS1 = 0x41U;
static constexpr TPCANHandle PCAN_USBBUS1 = 0x51U;
static constexpr TPCANHandle PCAN_LANBUS1 = 0x801U;
static constexpr TPCANStatus PCAN_ERROR_OK = 0x00000U;
static constexpr TPCANStatus PCAN_ERROR_QRCVEMPTY = 0x00020U;
static constexpr TPCANParameter PCAN_BUSOFF_AUTORESET = 0x07U;
static constexpr TPCANParameter PCAN_LISTEN_ONLY = 0x08U;
static constexpr TPCANParameter PCAN_ATTACHED_CHANNELS_COUNT = 0x2AU;
static constexpr TPCANParameter PCAN_ATTACHED_CHANNELS = 0x2BU;
static constexpr BYTE PCAN_PARAMETER_OFF = 0x00U;
static constexpr BYTE PCAN_PARAMETER_ON = 0x01U;
static constexpr DWORD PCAN_CHANNEL_AVAILABLE = 0x01U;
static constexpr DWORD PCAN_CHANNEL_OCCUPIED = 0x02U;
static constexpr DWORD PCAN_CHANNEL_PCANVIEW = PCAN_CHANNEL_AVAILABLE | PCAN_CHANNEL_OCCUPIED;
static constexpr TPCANMessageType PCAN_MESSAGE_STANDARD = 0x00U;
static constexpr TPCANMessageType PCAN_MESSAGE_RTR = 0x01U;
static constexpr TPCANMessageType PCAN_MESSAGE_EXTENDED = 0x02U;
static constexpr TPCANMessageType PCAN_MESSAGE_ERRFRAME = 0x40U;
static constexpr TPCANMessageType PCAN_MESSAGE_STATUS = 0x80U;
static constexpr TPCANBaudrate PCAN_BAUD_1M = 0x0014U;
static constexpr TPCANBaudrate PCAN_BAUD_800K = 0x0016U;
static constexpr TPCANBaudrate PCAN_BAUD_500K = 0x001CU;
static constexpr TPCANBaudrate PCAN_BAUD_250K = 0x011CU;
static constexpr TPCANBaudrate PCAN_BAUD_125K = 0x031CU;
static constexpr TPCANBaudrate PCAN_BAUD_100K = 0x432FU;
static constexpr TPCANBaudrate PCAN_BAUD_95K = 0xC34EU;
static constexpr TPCANBaudrate PCAN_BAUD_83K = 0x852BU;
static constexpr TPCANBaudrate PCAN_BAUD_50K = 0x472FU;
static constexpr TPCANBaudrate PCAN_BAUD_47K = 0x1414U;
static constexpr TPCANBaudrate PCAN_BAUD_33K = 0x8B2FU;
static constexpr TPCANBaudrate PCAN_BAUD_20K = 0x532FU;
static constexpr TPCANBaudrate PCAN_BAUD_10K = 0x672FU;
static constexpr TPCANBaudrate PCAN_BAUD_5K = 0x7F7FU;

struct PcanBasicApi{
    using InitializeFn = TPCANStatus(__stdcall*)(TPCANHandle, TPCANBaudrate, BYTE, DWORD, WORD);
    using UninitializeFn = TPCANStatus(__stdcall*)(TPCANHandle);
    using ReadFn = TPCANStatus(__stdcall*)(TPCANHandle, TPCANMsg*, TPCANTimestamp*);
    using GetValueFn = TPCANStatus(__stdcall*)(TPCANHandle, TPCANParameter, void*, DWORD);
    using SetValueFn = TPCANStatus(__stdcall*)(TPCANHandle, TPCANParameter, void*, DWORD);
    using GetErrorTextFn = TPCANStatus(__stdcall*)(TPCANStatus, WORD, LPSTR);

    HMODULE module{};
    InitializeFn initialize{};
    UninitializeFn uninitialize{};
    ReadFn read{};
    GetValueFn getValue{};
    SetValueFn setValue{};
    GetErrorTextFn getErrorText{};
};

bool loadPcanBasic(PcanBasicApi& api, char* error, std::size_t errorSize){
    api.module = LoadLibraryA("PCANBasic.dll");
    if(!api.module){
        std::snprintf(error, errorSize, "failed to load DLL, ensure PCAN drivers are installed!");
        return false;
    }

    api.initialize = reinterpret_cast<PcanBasicApi::InitializeFn>(GetProcAddress(api.module, "CAN_Initialize"));
    api.uninitialize = reinterpret_cast<PcanBasicApi::UninitializeFn>(GetProcAddress(api.module, "CAN_Uninitialize"));
    api.read = reinterpret_cast<PcanBasicApi::ReadFn>(GetProcAddress(api.module, "CAN_Read"));
    api.getValue = reinterpret_cast<PcanBasicApi::GetValueFn>(GetProcAddress(api.module, "CAN_GetValue"));
    api.setValue = reinterpret_cast<PcanBasicApi::SetValueFn>(GetProcAddress(api.module, "CAN_SetValue"));
    api.getErrorText = reinterpret_cast<PcanBasicApi::GetErrorTextFn>(GetProcAddress(api.module, "CAN_GetErrorText"));
    if(api.initialize && api.uninitialize && api.read && api.getValue && api.setValue && api.getErrorText) return true;

    std::snprintf(error, errorSize, "failed to resolve PCANBasic.dll symbols");
    FreeLibrary(api.module);
    api = {};
    return false;
}

void unloadPcanBasic(PcanBasicApi& api){
    if(api.module) FreeLibrary(api.module);
    api = {};
}

void lowerString(char* text){
    if(!text) return;
    for(; *text != '\0'; ++text){
        if(*text >= 'A' && *text <= 'Z') *text = static_cast<char>(*text - 'A' + 'a');
    }
}

TPCANBaudrate basePcanBitrate(uint32_t bitrateKbps){
    switch(bitrateKbps){
        case 1000: return PCAN_BAUD_1M;
        case 800: return PCAN_BAUD_800K;
        case 500: return PCAN_BAUD_500K;
        case 250: return PCAN_BAUD_250K;
        case 125: return PCAN_BAUD_125K;
        case 100: return PCAN_BAUD_100K;
        case 95: return PCAN_BAUD_95K;
        case 83: return PCAN_BAUD_83K;
        case 50: return PCAN_BAUD_50K;
        case 47: return PCAN_BAUD_47K;
        case 33: return PCAN_BAUD_33K;
        case 20: return PCAN_BAUD_20K;
        case 10: return PCAN_BAUD_10K;
        case 5: return PCAN_BAUD_5K;
        default: return static_cast<TPCANBaudrate>(0);
    }
}

bool resolvePcanTiming(const SocketCANConfig& config, ClassicTiming& timing){
    if(config.useBtr) return resolveClassicTiming(config, timing);

    const TPCANBaudrate base = basePcanBitrate(config.bitrateKbps);
    if(base == 0) return false;

    if(config.samplePointPercent == 87.5f && config.prescaler == 1){
        timing = {};
        timing.btr = base;
        return decodeClassicBtr(base & 0xFFU, (base >> 8) & 0xFFU, timing);
    }
    return resolveClassicTiming(config, timing);
}

bool formatPcanError(PcanBasicApi& api, TPCANStatus status, const char* operation, char* error, std::size_t errorSize){
    char description[160]{};
    if(api.getErrorText && api.getErrorText(status, kEnglishLang, description) == PCAN_ERROR_OK){
        std::snprintf(error, errorSize, "%s: %s", operation, description);
        return false;
    }
    std::snprintf(error, errorSize, "%s: PCAN error 0x%lX", operation, static_cast<unsigned long>(status));
    return false;
}

std::size_t channelBaseOffset(const char* prefix){
    if(std::strcmp(prefix, "usb") == 0 || std::strcmp(prefix, "usbbus") == 0) return PCAN_USBBUS1;
    if(std::strcmp(prefix, "pci") == 0 || std::strcmp(prefix, "pcibus") == 0) return PCAN_PCIBUS1;
    if(std::strcmp(prefix, "lan") == 0 || std::strcmp(prefix, "lanbus") == 0) return PCAN_LANBUS1;
    return 0;
}

bool parseNamedChannel(const char* raw, TPCANHandle& handle){
    if(!raw || raw[0] == '\0') return false;
    char value[32]{};
    std::snprintf(value, sizeof(value), "%s", raw);
    lowerString(value);

    static constexpr const char* prefixes[] = {"usb", "usbbus", "pci", "pcibus", "lan", "lanbus"};
    for(const char* prefix : prefixes){
        const std::size_t length = std::strlen(prefix);
        if(std::strncmp(value, prefix, length) != 0) continue;
        char* end = nullptr;
        const unsigned long index = std::strtoul(value + length, &end, 10);
        if(!end || *end != '\0' || index == 0 || index > 16) return false;
        handle = static_cast<TPCANHandle>(channelBaseOffset(prefix) + index - 1);
        return true;
    }
    return false;
}

bool listChannels(PcanBasicApi& api, TPCANChannelInformation* channels, DWORD& count, char* error, std::size_t errorSize){
    count = 0;
    TPCANStatus status = api.getValue(PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS_COUNT, &count, sizeof(count));
    if(status != PCAN_ERROR_OK) return formatPcanError(api, status, "CAN_GetValue(PCAN_ATTACHED_CHANNELS_COUNT)", error, errorSize);
    if(count == 0) return true;

    status = api.getValue(PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS, channels, count * sizeof(TPCANChannelInformation));
    if(status != PCAN_ERROR_OK) return formatPcanError(api, status, "CAN_GetValue(PCAN_ATTACHED_CHANNELS)", error, errorSize);
    return true;
}

bool resolvePcanChannel(PcanBasicApi& api, const SocketCANConfig& config, TPCANHandle& handle, char* error, std::size_t errorSize){
    TPCANChannelInformation channels[32]{};
    DWORD count = 32;
    if(!listChannels(api, channels, count, error, errorSize)) return false;
    if(count == 0){
        std::snprintf(error, errorSize, "no attached PCAN channels found");
        return false;
    }

    char requested[32]{};
    std::snprintf(requested, sizeof(requested), "%s", config.channel);
    lowerString(requested);
    if(std::strcmp(requested, "auto") == 0){
        for(DWORD i = 0; i < count; ++i){
            if(channels[i].channel_condition == PCAN_CHANNEL_AVAILABLE
                || channels[i].channel_condition == PCAN_CHANNEL_PCANVIEW
                || channels[i].channel_condition == PCAN_CHANNEL_OCCUPIED) {
                handle = channels[i].channel_handle;
                return true;
            }
        }
        handle = channels[0].channel_handle;
        return true;
    }

    if(parseNamedChannel(config.channel, handle)) return true;

    char* end = nullptr;
    const unsigned long value = std::strtoul(config.channel, &end, 0);
    if(end && *end == '\0' && value <= 0xFFFFUL){
        handle = static_cast<TPCANHandle>(value);
        return true;
    }

    std::snprintf(error, errorSize, "unable to resolve channel '%s'", config.channel);
    return false;
}

bool configurePcanChannel(PcanBasicApi& api, TPCANHandle handle, const SocketCANConfig& config, char* error, std::size_t errorSize){
    BYTE listenOnly = config.listenOnly ? PCAN_PARAMETER_ON : PCAN_PARAMETER_OFF;
    TPCANStatus status = api.setValue(handle, PCAN_LISTEN_ONLY, &listenOnly, sizeof(listenOnly));
    if(status != PCAN_ERROR_OK) return formatPcanError(api, status, "CAN_SetValue(PCAN_LISTEN_ONLY)", error, errorSize);

    BYTE autoReset = config.busoffReset ? PCAN_PARAMETER_ON : PCAN_PARAMETER_OFF;
    status = api.setValue(handle, PCAN_BUSOFF_AUTORESET, &autoReset, sizeof(autoReset));
    if(status != PCAN_ERROR_OK) return formatPcanError(api, status, "CAN_SetValue(PCAN_BUSOFF_AUTORESET)", error, errorSize);
    return true;
}

bool forwardPcanFrame(SPMCQueue<uint8_t, 4096>* streamBuffer, const TPCANMsg& msg){
    if((msg.MSGTYPE & (PCAN_MESSAGE_EXTENDED | PCAN_MESSAGE_RTR | PCAN_MESSAGE_STATUS | PCAN_MESSAGE_ERRFRAME)) != 0) return false;

    char text[32]{};
    int length = std::snprintf(text, sizeof(text), "t%03lX%1u",
        static_cast<unsigned long>(msg.ID & 0x7FFU), static_cast<unsigned int>(msg.LEN));
    for(BYTE i = 0; i < msg.LEN && length > 0 && length < static_cast<int>(sizeof(text) - 3); ++i){
        length += std::snprintf(text + length, sizeof(text) - static_cast<std::size_t>(length), "%02X", msg.DATA[i]);
    }
    if(length <= 0 || length >= static_cast<int>(sizeof(text) - 1)) return false;
    text[length++] = '\r';
    text[length] = '\0';
    writeFrame(streamBuffer, text);
    return true;
}
#else
void appendSocketCANPermissionsHint(char* error, std::size_t errorSize){
    if(!error || errorSize == 0) return;
    static constexpr const char* hint =
        ", ensure binary has network permissions, use: "
        "\"sudo setcap cap_net_admin,cap_net_raw=ep /path/to/executable\"";
    const std::size_t used = std::strlen(error);
    if(used >= errorSize - 1) return;
    std::snprintf(error + used, errorSize - used, "%s", hint);
}

int ifIndexForName(const char* ifname){
    const unsigned int index = if_nametoindex(ifname);
    return index == 0 ? -1 : static_cast<int>(index);
}

void addAttr(nlmsghdr* header, std::size_t maxLength, uint16_t type, const void* data, std::size_t dataLength){
    const std::size_t length = RTA_LENGTH(dataLength);
    const std::size_t newLength = NLMSG_ALIGN(header->nlmsg_len) + RTA_ALIGN(length);
    if(newLength > maxLength) return;

    auto* attr = reinterpret_cast<rtattr*>(reinterpret_cast<char*>(header) + NLMSG_ALIGN(header->nlmsg_len));
    attr->rta_type = type;
    attr->rta_len = static_cast<unsigned short>(length);
    if(dataLength > 0 && data) std::memcpy(RTA_DATA(attr), data, dataLength);
    header->nlmsg_len = static_cast<unsigned int>(newLength);
}

rtattr* beginNest(nlmsghdr* header, std::size_t maxLength, uint16_t type){
    auto* nest = reinterpret_cast<rtattr*>(reinterpret_cast<char*>(header) + NLMSG_ALIGN(header->nlmsg_len));
    addAttr(header, maxLength, type, nullptr, 0);
    return nest;
}

void endNest(nlmsghdr* header, rtattr* nest){
    nest->rta_len = static_cast<unsigned short>(
        reinterpret_cast<char*>(header) + NLMSG_ALIGN(header->nlmsg_len) - reinterpret_cast<char*>(nest));
}

bool sendNetlinkAck(const nlmsghdr* request, char* error, std::size_t errorSize){
    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;

    const int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if(fd < 0){
        std::snprintf(error, errorSize, "socket(NETLINK_ROUTE) failed: %s", std::strerror(errno));
        return false;
    }

    iovec io{const_cast<nlmsghdr*>(request), request->nlmsg_len};
    msghdr message{};
    message.msg_name = &address;
    message.msg_namelen = sizeof(address);
    message.msg_iov = &io;
    message.msg_iovlen = 1;

    if(sendmsg(fd, &message, 0) < 0){
        std::snprintf(error, errorSize, "sendmsg(netlink) failed: %s", std::strerror(errno));
        close(fd);
        return false;
    }

    alignas(nlmsghdr) char buffer[8192];
    iovec responseIO{buffer, sizeof(buffer)};
    msghdr response{};
    response.msg_name = &address;
    response.msg_namelen = sizeof(address);
    response.msg_iov = &responseIO;
    response.msg_iovlen = 1;

    const ssize_t received = recvmsg(fd, &response, 0);
    const int savedErrno = errno;
    close(fd);
    if(received < 0){
        std::snprintf(error, errorSize, "recvmsg(netlink) failed: %s", std::strerror(savedErrno));
        return false;
    }

    int remaining = static_cast<int>(received);
    for(nlmsghdr* header = reinterpret_cast<nlmsghdr*>(buffer); NLMSG_OK(header, remaining);
        header = NLMSG_NEXT(header, remaining)) {
        if(header->nlmsg_type != NLMSG_ERROR) continue;
        auto* reply = reinterpret_cast<nlmsgerr*>(NLMSG_DATA(header));
        if(reply->error == 0) return true;
        std::snprintf(error, errorSize, "netlink config failed: %s", std::strerror(-reply->error));
        if(reply->error == -EPERM || reply->error == -EACCES) appendSocketCANPermissionsHint(error, errorSize);
        return false;
    }

    std::snprintf(error, errorSize, "netlink config failed: no ACK received");
    return false;
}

bool configureSocketCAN(const SocketCANConfig& config, bool up, char* error, std::size_t errorSize){
    struct Request {
        nlmsghdr header;
        ifinfomsg info;
        char buffer[256];
    } request{};

    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
    request.header.nlmsg_type = RTM_NEWLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    request.header.nlmsg_seq = 1;
    request.info.ifi_family = AF_UNSPEC;
    request.info.ifi_index = ifIndexForName(config.interfaceName);
    if(request.info.ifi_index <= 0){
        std::snprintf(error, errorSize, "if_nametoindex(%s) failed: %s",
            config.interfaceName, std::strerror(errno));
        return false;
    }

    request.info.ifi_change |= IFF_UP;
    if(up){
        request.info.ifi_flags |= IFF_UP;
        ClassicTiming timing{};
        if(!resolveClassicTiming(config, timing)){
            std::snprintf(error, errorSize, "invalid SocketCAN timing configuration");
            return false;
        }
        if(timing.bitrate > 0){
            auto* linkInfo = beginNest(&request.header, sizeof(request), IFLA_LINKINFO);
            const char kind[] = "can";
            addAttr(&request.header, sizeof(request), IFLA_INFO_KIND, kind, sizeof(kind));
            auto* infoData = beginNest(&request.header, sizeof(request), IFLA_INFO_DATA);
            can_bittiming bitrate{};
            bitrate.bitrate = static_cast<__u32>(timing.bitrate);
            if(timing.samplePointX10 > 0) bitrate.sample_point = static_cast<__u32>(timing.samplePointX10);
            if(timing.prescaler > 0) bitrate.brp = static_cast<__u32>(timing.prescaler);
            if(timing.sjw > 0) bitrate.sjw = static_cast<__u32>(timing.sjw);
            addAttr(&request.header, sizeof(request), IFLA_CAN_BITTIMING, &bitrate, sizeof(bitrate));
            endNest(&request.header, infoData);
            endNest(&request.header, linkInfo);
        }
    } else{
        request.info.ifi_flags &= ~IFF_UP;
    }

    return sendNetlinkAck(&request.header, error, errorSize);
}

bool openSocketCAN(const SocketCANConfig& config, int& fd, char* error, std::size_t errorSize){
    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(fd < 0){
        std::snprintf(error, errorSize, "socket(PF_CAN) failed: %s", std::strerror(errno));
        if(errno == EPERM || errno == EACCES) appendSocketCANPermissionsHint(error, errorSize);
        return false;
    }

    ifreq request{};
    std::strncpy(request.ifr_name, config.interfaceName, IFNAMSIZ - 1);
    if(ioctl(fd, SIOCGIFINDEX, &request) < 0){
        std::snprintf(error, errorSize, "ioctl(SIOCGIFINDEX, %s) failed: %s",
            config.interfaceName, std::strerror(errno));
        close(fd);
        fd = -1;
        return false;
    }

    timeval timeout{};
    timeout.tv_usec = 100000;
    if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){
        std::snprintf(error, errorSize, "setsockopt(SO_RCVTIMEO) failed: %s", std::strerror(errno));
        close(fd);
        fd = -1;
        return false;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = request.ifr_ifindex;
    if(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0){
        std::snprintf(error, errorSize, "bind(%s) failed: %s", config.interfaceName, std::strerror(errno));
        if(errno == EPERM || errno == EACCES) appendSocketCANPermissionsHint(error, errorSize);
        close(fd);
        fd = -1;
        return false;
    }

    return true;
}

bool forwardSocketCANFrame(SPMCQueue<uint8_t, 4096>* streamBuffer, const can_frame& frame){
    if((frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0) return false;

    char text[32]{};
    int length = std::snprintf(text, sizeof(text), "t%03X%1X",
        frame.can_id & CAN_SFF_MASK, frame.len);
    for(uint8_t i = 0; i < frame.len && length > 0 && length < static_cast<int>(sizeof(text) - 3); ++i){
        length += std::snprintf(text + length, sizeof(text) - static_cast<std::size_t>(length), "%02X", frame.data[i]);
    }
    if(length <= 0 || length >= static_cast<int>(sizeof(text) - 1)) return false;
    text[length++] = '\r';
    text[length] = '\0';
    writeFrame(streamBuffer, text);
    return true;
}
#endif
}

const char* Protocols::name(ProtocolKind kind){
    switch(kind){
        case ProtocolKind::TCP: return "TCP";
        case ProtocolKind::UDP: return "UDP";
        case ProtocolKind::UART: return "UART";
        case ProtocolKind::SocketCAN: return "SocketCAN";
        case ProtocolKind::AssettoCorsa: return "Assetto Corsa";
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
        case ProtocolKind::AssettoCorsa:
            AssettoCorsa(stopToken, statusBuffer, streamBuffer);
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
#ifdef _WIN32
    char error[192]{};
    publishStatus(statusBuffer, false, "SocketCAN Loading PCANBasic");

    PcanBasicApi api{};
    if(!loadPcanBasic(api, error, sizeof(error))){
        publishStatus(statusBuffer, true, error);
        return;
    }

    publishStatus(statusBuffer, false, "SocketCAN Resolving PCAN Channel");
    TPCANHandle handle = PCAN_NONEBUS;
    if(!resolvePcanChannel(api, config, handle, error, sizeof(error))){
        unloadPcanBasic(api);
        publishStatus(statusBuffer, true, error);
        return;
    }

    ClassicTiming timing{};
    if(!resolvePcanTiming(config, timing)){
        std::snprintf(error, sizeof(error), "invalid PCAN timing configuration");
        unloadPcanBasic(api);
        publishStatus(statusBuffer, true, error);
        return;
    }
    TPCANStatus status = api.initialize(handle, static_cast<TPCANBaudrate>(timing.btr), 0, 0, 0);
    if(status != PCAN_ERROR_OK){
        formatPcanError(api, status, "CAN_Initialize", error, sizeof(error));
        unloadPcanBasic(api);
        publishStatus(statusBuffer, true, error);
        return;
    }

    if(!configurePcanChannel(api, handle, config, error, sizeof(error))){
        api.uninitialize(handle);
        unloadPcanBasic(api);
        publishStatus(statusBuffer, true, error);
        return;
    }

    publishStatus(statusBuffer, false, "SocketCAN Online");
    while(!stopToken.stop_requested()){
        TPCANMsg msg{};
        TPCANTimestamp timestamp{};
        status = api.read(handle, &msg, &timestamp);
        if(status == PCAN_ERROR_QRCVEMPTY){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if(status != PCAN_ERROR_OK){
            formatPcanError(api, status, "CAN_Read", error, sizeof(error));
            publishStatus(statusBuffer, true, error);
            break;
        }
        forwardPcanFrame(streamBuffer, msg);
    }

    api.uninitialize(handle);
    unloadPcanBasic(api);
    publishStatus(statusBuffer, false, "SocketCAN Stopped");
#else
    (void)streamBuffer;
    char error[192]{};

    publishStatus(statusBuffer, false, "SocketCAN Configuring Interface");
    if(!configureSocketCAN(config, false, error, sizeof(error)) && error[0] != '\0'){
        publishStatus(statusBuffer, false, error);
    }
    if(!configureSocketCAN(config, true, error, sizeof(error))){
        publishStatus(statusBuffer, true, error);
        return;
    }

    int fd = -1;
    if(!openSocketCAN(config, fd, error, sizeof(error))){
        publishStatus(statusBuffer, true, error);
        return;
    }

    publishStatus(statusBuffer, false, "SocketCAN Online");
    while(!stopToken.stop_requested()){
        can_frame frame{};
        const ssize_t bytesRead = read(fd, &frame, sizeof(frame));
        if(bytesRead == static_cast<ssize_t>(sizeof(frame))){
            forwardSocketCANFrame(streamBuffer, frame);
            continue;
        }
        if(bytesRead < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;

        std::snprintf(error, sizeof(error), "read(can_frame) failed: %s", std::strerror(errno));
        publishStatus(statusBuffer, true, error);
        break;
    }

    close(fd);
    publishStatus(statusBuffer, false, "SocketCAN Stopped");
#endif
}

void Protocols::AssettoCorsa(std::stop_token stopToken,
        SPMCQueue<ProtocolError, 64>* statusBuffer,
        SPMCQueue<uint8_t, 4096>* streamBuffer){
    char error[192]{};
    SocketHandle sock = INVALID_SOCKET;
    sockaddr_in server{};
    if(!initAssettoSocket(sock, server, error, sizeof(error))){
        publishStatus(statusBuffer, true, error);
        return;
    }

    publishStatus(statusBuffer, false, "Assetto Corsa Connecting");
    bool subscribed = false;
    int consecutiveReadFailures = 0;
    constexpr int kMaxConsecutiveReadFailures = 3;
    bool publishedOnline = false;

    while(!stopToken.stop_requested()){
        if(!performAssettoHandshake(sock, server, subscribed)){
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if(!publishedOnline){
            publishStatus(statusBuffer, false, "Assetto Corsa Online");
            publishedOnline = true;
        }

        RTCarInfo packet{};
        socklen_t slen = sizeof(server);
        const int bytesRead = recvfrom(sock, reinterpret_cast<char*>(&packet), sizeof(packet), 0,
            reinterpret_cast<sockaddr*>(&server), &slen);
        if(bytesRead == 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if(bytesRead == SOCKET_ERROR){
#ifdef _WIN32
            const int err = WSAGetLastError();
            if(err == WSAEWOULDBLOCK){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            consecutiveReadFailures++;
#else
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            consecutiveReadFailures++;
#endif
            if(consecutiveReadFailures >= kMaxConsecutiveReadFailures){
                publishStatus(statusBuffer, false, "Assetto Corsa UDP read failed repeatedly; restarting reader socket");
                closeSocket(sock);
                sock = INVALID_SOCKET;
                subscribed = false;
                consecutiveReadFailures = 0;
                publishedOnline = false;
                if(!initAssettoSocket(sock, server, error, sizeof(error))){
                    publishStatus(statusBuffer, true, error);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            continue;
        }
        consecutiveReadFailures = 0;
        if(bytesRead >= static_cast<int>(sizeof(RTCarInfo))) forwardAssettoFrame(streamBuffer, packet);
    }

    if(sock != INVALID_SOCKET) closeSocket(sock);
    publishStatus(statusBuffer, false, "Assetto Corsa Stopped");
}
