/*[ξ] the photon network interface*/
#include "network.hpp"
#include "tcp.hpp"
#include "udp.hpp" 
#include "spsc.hpp"
#include <fcntl.h>
#include <cstddef>
#include <charconv>
#include <map>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>
#ifndef _WIN32
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/can/netlink.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "../engine/include.hpp"

namespace {
#ifndef _WIN32
speed_t toPosixBaud(const std::string& baudRate) {
    const int baud = std::atoi(baudRate.c_str());
    switch (baud) {
        case 600: return B600;
        case 1200: return B1200;
        case 1800:
#ifdef B1800
            return B1800;
#else
            return B1200;
#endif
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        default: return B9600;
    }
}
#else
DWORD toWindowsBaud(const std::string& baudRate) {
    const int baud = std::atoi(baudRate.c_str());
    switch (baud) {
        case 600: return CBR_600;
        case 1200: return CBR_1200;
        case 1800: return 1800;
        case 2400: return CBR_2400;
        case 4800: return CBR_4800;
        case 9600: return CBR_9600;
        case 19200: return CBR_19200;
        case 38400: return CBR_38400;
        default: return CBR_9600;
    }
}
#endif

std::string toHex(uint16_t value, size_t width) {
    std::string out(width, '0');
    for(size_t i = 0; i < width; ++i){
        const size_t shift = (width - i - 1) * 4;
        const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0xFu);
        out[i] = static_cast<char>((nibble < 10) ? ('0' + nibble) : ('a' + (nibble - 10)));
    }
    return out;
}

std::string toHex(uint8_t value, size_t width) {
    std::string out(width, '0');
    for(size_t i = 0; i < width; ++i){
        const size_t shift = (width - i - 1) * 4;
        const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0xFu);
        out[i] = static_cast<char>((nibble < 10) ? ('0' + nibble) : ('a' + (nibble - 10)));
    }
    return out;
}

void pushFrameBytes(SPSCQueue<uint8_t>& queue, const std::string& frame) {
    for(const char ch : frame){
        while(!queue.try_push(static_cast<uint8_t>(ch))){
            logs("[!] Network Buffer full!");
            std::this_thread::yield();
        }
    }
}

#ifndef _WIN32
struct CanBitTimingConfig {
    uint32_t arbitration = 500000U;
    uint32_t data = 0U;
    bool hasData = false;
    bool arbitrationFallback = false;
    bool dataFallback = false;
};

std::string sanitizeBitrateString(const std::string& input) {
    std::string sanitized = input;
    sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(), [](unsigned char ch){
        return std::isspace(ch) || ch == '_' || ch == ',';
    }), sanitized.end());
    return sanitized;
}

uint32_t parseBitrateValue(const std::string& raw, uint32_t fallback, bool& usedFallback) {
    const std::string sanitized = sanitizeBitrateString(raw);
    if(sanitized.empty()){
        usedFallback = true;
        return fallback;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(sanitized.c_str(), &end, 10);
    if(end != nullptr && *end == '\0' && parsed > 0 && parsed <= 8000000UL){
        usedFallback = false;
        return static_cast<uint32_t>(parsed);
    }

    usedFallback = true;
    return fallback;
}

CanBitTimingConfig parseCanBitTiming(const std::string& arbitrationRate, const std::string& dataRate) {
    CanBitTimingConfig timing{};
    timing.arbitration = parseBitrateValue(arbitrationRate, 500000U, timing.arbitrationFallback);

    timing.hasData = !sanitizeBitrateString(dataRate).empty();
    if(timing.hasData){
        timing.data = parseBitrateValue(dataRate, 0U, timing.dataFallback);
        if(timing.data == 0U){
            timing.hasData = false;
            timing.dataFallback = true;
        }
    }
    return timing;
}

struct ConfigureResult {
    bool configured = false;
    bool permissionDenied = false;
    bool notFound = false;
    int errorCode = 0;
    std::string errorText;
};

struct InterfaceState {
    bool exists = false;
    bool up = false;
    bool running = false;
    int errorCode = 0;
};

rtattr* addAttribute(nlmsghdr* header, size_t maxLength, int type, const void* data, size_t dataLength) {
    const size_t alignedLength = NLMSG_ALIGN(header->nlmsg_len);
    const size_t totalLength = RTA_LENGTH(dataLength);
    if(alignedLength + totalLength > maxLength){
        return nullptr;
    }
    auto* attr = reinterpret_cast<rtattr*>(reinterpret_cast<char*>(header) + alignedLength);
    attr->rta_type = type;
    attr->rta_len = totalLength;
    if(data != nullptr && dataLength > 0){
        std::memcpy(RTA_DATA(attr), data, dataLength);
    }
    header->nlmsg_len = alignedLength + totalLength;
    return attr;
}

rtattr* beginNestedAttribute(nlmsghdr* header, size_t maxLength, int type) {
    return addAttribute(header, maxLength, type, nullptr, 0);
}

void endNestedAttribute(nlmsghdr* header, rtattr* attr) {
    if(header == nullptr || attr == nullptr){
        return;
    }
    const auto base = reinterpret_cast<char*>(header);
    const auto current = reinterpret_cast<char*>(header) + NLMSG_ALIGN(header->nlmsg_len);
    attr->rta_len = static_cast<unsigned short>(current - reinterpret_cast<char*>(attr));
}

bool setInterfaceState(const std::string& interfaceName, bool bringUp, int& errorOut) {
    errorOut = 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        errorOut = errno;
        return false;
    }

    ifreq ifr = {};
    std::snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", interfaceName.c_str());
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) < 0){
        close(fd);
        errorOut = errno;
        return false;
    }

    if(bringUp){
        ifr.ifr_flags |= static_cast<short>(IFF_UP);
    } else {
        ifr.ifr_flags &= static_cast<short>(~IFF_UP);
    }

    const bool ok = (ioctl(fd, SIOCSIFFLAGS, &ifr) == 0);
    if(!ok){
        errorOut = errno;
    }
    close(fd);
    return ok;
}

bool applyCanSettings(unsigned int interfaceIndex, const CanBitTimingConfig& timing, int& errorOut) {
    struct {
        nlmsghdr header;
        ifinfomsg info;
        char buffer[512];
    } request{};

    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
    request.header.nlmsg_type = RTM_NEWLINK;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    request.header.nlmsg_seq = 1;
    request.info.ifi_family = AF_UNSPEC;
    request.info.ifi_index = static_cast<int>(interfaceIndex);
    request.info.ifi_change = 0xFFFFFFFFu;

    rtattr* linkInfo = beginNestedAttribute(&request.header, sizeof(request), IFLA_LINKINFO);
    if(linkInfo == nullptr){
        return false;
    }
    static constexpr char kInterfaceKind[] = "can";
    if(addAttribute(&request.header, sizeof(request), IFLA_INFO_KIND, kInterfaceKind, sizeof(kInterfaceKind)) == nullptr){
        return false;
    }

    rtattr* linkData = beginNestedAttribute(&request.header, sizeof(request), IFLA_INFO_DATA);
    if(linkData == nullptr){
        return false;
    }

    can_bittiming arbitrationTiming = {};
    arbitrationTiming.bitrate = timing.arbitration;
    if(addAttribute(&request.header, sizeof(request), IFLA_CAN_BITTIMING, &arbitrationTiming, sizeof(arbitrationTiming)) == nullptr){
        return false;
    }

    if(timing.hasData){
        can_bittiming dataTiming = {};
        dataTiming.bitrate = timing.data;
        if(addAttribute(&request.header, sizeof(request), IFLA_CAN_DATA_BITTIMING, &dataTiming, sizeof(dataTiming)) == nullptr){
            return false;
        }
        can_ctrlmode ctrlMode = {};
        ctrlMode.mask = CAN_CTRLMODE_FD;
        ctrlMode.flags = CAN_CTRLMODE_FD;
        if(addAttribute(&request.header, sizeof(request), IFLA_CAN_CTRLMODE, &ctrlMode, sizeof(ctrlMode)) == nullptr){
            return false;
        }
    }

    endNestedAttribute(&request.header, linkData);
    endNestedAttribute(&request.header, linkInfo);

    sockaddr_nl addr = {};
    addr.nl_family = AF_NETLINK;
    iovec iov = { &request, request.header.nlmsg_len };
    msghdr msg = {};
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    const int nlSock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if(nlSock < 0){
        return false;
    }

    bool success = false;
    errorOut = 0;
    if(sendmsg(nlSock, &msg, 0) >= 0){
        char response[512];
        iovec riov = { response, sizeof(response) };
        msghdr rmsg = {};
        rmsg.msg_name = &addr;
        rmsg.msg_namelen = sizeof(addr);
        rmsg.msg_iov = &riov;
        rmsg.msg_iovlen = 1;
        const ssize_t len = recvmsg(nlSock, &rmsg, 0);
        if(len >= 0){
            success = true;
            int msgLen = static_cast<int>(len);
            for(nlmsghdr* hdr = reinterpret_cast<nlmsghdr*>(response);
                NLMSG_OK(hdr, static_cast<unsigned int>(msgLen));
                hdr = NLMSG_NEXT(hdr, msgLen)){
                if(hdr->nlmsg_type == NLMSG_ERROR){
                    const nlmsgerr* err = reinterpret_cast<nlmsgerr*>(NLMSG_DATA(hdr));
                    success = (err->error == 0);
                    if(err->error != 0){
                        errorOut = -err->error;
                    }
                    break;
                }
            }
        }
    }

    close(nlSock);
    return success;
}

ConfigureResult configureCanInterface(const std::string& interfaceName, const CanBitTimingConfig& timing) {
    ConfigureResult result{};
    const unsigned int interfaceIndex = if_nametoindex(interfaceName.c_str());
    if(interfaceIndex == 0U){
        result.notFound = true;
        result.errorCode = (errno != 0) ? errno : ENODEV;
        result.errorText = std::strerror(result.errorCode);
        return result;
    }

    int stateError = 0;
    const bool broughtDown = setInterfaceState(interfaceName, false, stateError);
    if(!broughtDown && (stateError == EPERM || stateError == EACCES)){
        result.permissionDenied = true;
        result.errorCode = stateError;
        result.errorText = std::strerror(stateError);
        return result;
    }

    int applyError = 0;
    result.configured = applyCanSettings(interfaceIndex, timing, applyError);
    if(!result.configured && applyError != 0){
        result.errorCode = applyError;
        result.errorText = std::strerror(applyError);
        result.permissionDenied = (applyError == EPERM || applyError == EACCES);
    }

    int bringUpError = 0;
    const bool broughtUp = setInterfaceState(interfaceName, true, bringUpError);
    if(!broughtUp && bringUpError != 0){
        result.errorCode = bringUpError;
        result.errorText = std::strerror(bringUpError);
        result.permissionDenied = result.permissionDenied || (bringUpError == EPERM || bringUpError == EACCES);
    }

    result.configured = result.configured && broughtUp;
    return result;
}

InterfaceState queryInterfaceState(const std::string& interfaceName) {
    InterfaceState state{};
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0){
        state.errorCode = errno;
        return state;
    }

    ifreq ifr = {};
    std::snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", interfaceName.c_str());
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) == 0){
        state.exists = true;
        state.up = (ifr.ifr_flags & IFF_UP) != 0;
        state.running = (ifr.ifr_flags & IFF_RUNNING) != 0;
    } else {
        state.errorCode = errno;
    }
    close(fd);
    return state;
}
#else
using TPCANHandle = WORD;
using TPCANStatus = DWORD;
using TPCANMessageType = BYTE;
using TPCANBaudrate = WORD;
using TPCANParameter = BYTE;

constexpr TPCANStatus kPcanErrorOk = 0x00000U;
constexpr TPCANStatus kPcanErrorQrcvEmpty = 0x00020U;
constexpr TPCANParameter kPcanReceiveEvent = 0x03U;

constexpr TPCANMessageType kPcanMessageStandard = 0x00U;
constexpr TPCANMessageType kPcanMessageRtr = 0x01U;
constexpr TPCANMessageType kPcanMessageExtended = 0x02U;
constexpr TPCANMessageType kPcanMessageStatus = 0x80U;

constexpr TPCANBaudrate kPcanBaud1M = 0x0014U;
constexpr TPCANBaudrate kPcanBaud800K = 0x0016U;
constexpr TPCANBaudrate kPcanBaud500K = 0x001CU;
constexpr TPCANBaudrate kPcanBaud250K = 0x011CU;
constexpr TPCANBaudrate kPcanBaud125K = 0x031CU;
constexpr TPCANBaudrate kPcanBaud100K = 0x432FU;
constexpr TPCANBaudrate kPcanBaud95K = 0xC34EU;
constexpr TPCANBaudrate kPcanBaud83K = 0x852BU;
constexpr TPCANBaudrate kPcanBaud50K = 0x472FU;
constexpr TPCANBaudrate kPcanBaud47K = 0x1414U;
constexpr TPCANBaudrate kPcanBaud33K = 0x8B2FU;
constexpr TPCANBaudrate kPcanBaud20K = 0x532FU;
constexpr TPCANBaudrate kPcanBaud10K = 0x672FU;
constexpr TPCANBaudrate kPcanBaud5K = 0x7F7FU;

struct TPCANMsg {
    DWORD ID;
    TPCANMessageType MSGTYPE;
    BYTE LEN;
    BYTE DATA[8];
};

using CanInitializeFn = TPCANStatus (__stdcall *)(TPCANHandle, TPCANBaudrate, BYTE, DWORD, WORD);
using CanReadFn = TPCANStatus (__stdcall *)(TPCANHandle, TPCANMsg*, void*);
using CanUninitializeFn = TPCANStatus (__stdcall *)(TPCANHandle);
using CanGetErrorTextFn = TPCANStatus (__stdcall *)(TPCANStatus, WORD, LPSTR);
using CanSetValueFn = TPCANStatus (__stdcall *)(TPCANHandle, TPCANParameter, void*, DWORD);

struct PcanApi {
    HMODULE library = nullptr;
    CanInitializeFn initialize = nullptr;
    CanReadFn read = nullptr;
    CanUninitializeFn uninitialize = nullptr;
    CanGetErrorTextFn getErrorText = nullptr;
    CanSetValueFn setValue = nullptr;
};

PcanApi loadPcanApi() {
    PcanApi api;
    api.library = LoadLibraryA("PCANBasic.dll");
    if(api.library == nullptr){
        return api;
    }
    api.initialize = reinterpret_cast<CanInitializeFn>(GetProcAddress(api.library, "CAN_Initialize"));
    api.read = reinterpret_cast<CanReadFn>(GetProcAddress(api.library, "CAN_Read"));
    api.uninitialize = reinterpret_cast<CanUninitializeFn>(GetProcAddress(api.library, "CAN_Uninitialize"));
    api.getErrorText = reinterpret_cast<CanGetErrorTextFn>(GetProcAddress(api.library, "CAN_GetErrorText"));
    api.setValue = reinterpret_cast<CanSetValueFn>(GetProcAddress(api.library, "CAN_SetValue"));
    if(api.initialize == nullptr || api.read == nullptr || api.uninitialize == nullptr || api.setValue == nullptr){
        FreeLibrary(api.library);
        api = {};
    }
    return api;
}

void unloadPcanApi(PcanApi& api) {
    if(api.library != nullptr){
        FreeLibrary(api.library);
        api = {};
    }
}

std::string pcanStatusText(const PcanApi& api, TPCANStatus status) {
    if(api.getErrorText == nullptr){
        std::ostringstream oss;
        oss << "status=0x" << std::hex << status;
        return oss.str();
    }
    char buffer[256] = {};
    if(api.getErrorText(status, 0x09U, buffer) != kPcanErrorOk){
        std::ostringstream oss;
        oss << "status=0x" << std::hex << status;
        return oss.str();
    }
    return buffer;
}

TPCANHandle channelHandleFromName(const std::string& name) {
    static const std::unordered_map<std::string, TPCANHandle> kHandleMap = {
        {"PCAN_USBBUS1", 0x51U}, {"PCAN_USBBUS2", 0x52U}, {"PCAN_USBBUS3", 0x53U}, {"PCAN_USBBUS4", 0x54U},
        {"PCAN_USBBUS5", 0x55U}, {"PCAN_USBBUS6", 0x56U}, {"PCAN_USBBUS7", 0x57U}, {"PCAN_USBBUS8", 0x58U},
        {"PCAN_USBBUS9", 0x509U}, {"PCAN_USBBUS10", 0x50AU}, {"PCAN_USBBUS11", 0x50BU}, {"PCAN_USBBUS12", 0x50CU},
        {"PCAN_USBBUS13", 0x50DU}, {"PCAN_USBBUS14", 0x50EU}, {"PCAN_USBBUS15", 0x50FU}, {"PCAN_USBBUS16", 0x510U},
    };
    const auto it = kHandleMap.find(name);
    return (it == kHandleMap.end()) ? 0x00U : it->second;
}

struct PcanBaudrateSelection {
    TPCANBaudrate value;
    std::string sanitizedInput;
    bool defaulted = false;
};

PcanBaudrateSelection pcanBaudrateFromString(const std::string& bitRate) {
    std::string sanitized = bitRate;
    sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(), [](unsigned char ch){
        return std::isspace(ch) || ch == '_';
    }), sanitized.end());

    static const std::unordered_map<std::string, TPCANBaudrate> kBitrates = {
        {"5000", kPcanBaud5K},
        {"10000", kPcanBaud10K},
        {"20000", kPcanBaud20K},
        {"33333", kPcanBaud33K},
        {"50000", kPcanBaud50K},
        {"47619", kPcanBaud47K},
        {"83333", kPcanBaud83K},
        {"95000", kPcanBaud95K},
        {"100000", kPcanBaud100K},
        {"125000", kPcanBaud125K},
        {"250000", kPcanBaud250K},
        {"500000", kPcanBaud500K},
        {"800000", kPcanBaud800K},
        {"1000000", kPcanBaud1M},
    };
    const auto it = kBitrates.find(sanitized);
    if(it != kBitrates.end()){
        return {it->second, sanitized, false};
    }
    if(sanitized.size() > 2 && sanitized[0] == '0' && (sanitized[1] == 'x' || sanitized[1] == 'X')){
        char* end = nullptr;
        const unsigned long rawValue = std::strtoul(sanitized.c_str(), &end, 16);
        if(end != nullptr && *end == '\0' && rawValue <= 0xFFFFUL){
            return {static_cast<TPCANBaudrate>(rawValue), sanitized, false};
        }
    }
    return {kPcanBaud500K, "500000", true};
}
#endif
}

void Network::setCanStatus(ConnectionState state, const std::string& message, bool warning) {
    canState.store(state, std::memory_order_relaxed);
    canStatusWarning.store(warning, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(canStatusMutex);
    canStatusMessage = message;
}

ConnectionStatusSnapshot Network::makeCanStatusSnapshot() const {
    ConnectionStatusSnapshot snapshot{};
    snapshot.state = canState.load(std::memory_order_relaxed);
    snapshot.warning = canStatusWarning.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(canStatusMutex);
    snapshot.message = canStatusMessage;
    return snapshot;
}

ConnectionStatusSnapshot Network::getCanStatus() const {
    return makeCanStatusSnapshot();
}

#define QUEUE_CAPACITY 4096
Network::Network() : 
    tcpQueue    (QUEUE_CAPACITY), 
    udpQueue    (QUEUE_CAPACITY), 
    serialQueue (QUEUE_CAPACITY), 
    canQueue    (QUEUE_CAPACITY),
    localQueue  (QUEUE_CAPACITY),
    corsaQueue  (QUEUE_CAPACITY){
    running = true;
    setCanStatus(ConnectionState::Idle, "Idle");
}
Network::~Network(){
    running = false;
    currentSource_t.join();
};

#define BUFFER_CAPACITY 1024
void Network::tcpReader(){
    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    while(running){
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (bytesRead > 0) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(bytesRead); i++) {
                while (!tcpQueue.try_push(buffer[i])) { logs("[!] Network Buffer full!"); std::this_thread::yield(); }
            }
        }
    }
}

void Network::udpReader(){
};

void Network::localReader(){
};

void Network::corsaReader(){
    std::vector<RTCarInfo> buffer(1);
    constexpr int maxConsecutiveReadFailures = 3;

    while(running){
        UdpSocket socket(LOCAL_IP, CORSA_PORT);
        int consecutiveReadFailures = 0;

        while(running){
            auto bytesRead = socket.read(buffer.data(), buffer.size());
            if(bytesRead == 0){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if(bytesRead < 0){
                consecutiveReadFailures++;
                if(consecutiveReadFailures >= maxConsecutiveReadFailures){
                    logs("[!] Corsa UDP read failed repeatedly; restarting reader socket");
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    break;
                }
                continue;
            }

            consecutiveReadFailures = 0;
            std::size_t count = static_cast<std::size_t>(bytesRead) / sizeof(RTCarInfo);
            for(std::size_t i = 0; i < count; i++){
                while(!corsaQueue.try_push(buffer[i])){ logs("[!] Network Buffer full!"); std::this_thread::yield();}
            }
        }
    }
};

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
void Network::serialReader(){
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    int _fd = open(serialPort.data(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    while((_fd < 0) && running){
        logs("[!] Attempting connection on " << serialPort);
        _fd = open(serialPort.data(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if(_fd < 0) return;
    struct termios tty = {};
    if(tcgetattr(_fd, &tty) != 0){
        close(_fd);
        return;
    }
    cfmakeraw(&tty);
    speed_t speed = toPosixBaud(baudRate);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(_fd, TCSANOW, &tty);
    tcflush(_fd, TCIFLUSH);

    while(running){
        ssize_t n = read(_fd, buffer.data(), buffer.size());
        if(n < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            break;
        }
        if(n == 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        for(int i = 0; i < n; i++){
            serialQueue.push(buffer[i]);
        }
    }
    close(_fd);
};

void Network::canReader(){
    bool hasLoggedInitialConnection = false;
    bool attemptedConfigure = false;
    bool configureOk = false;
    bool configureWarning = false;
    while(running){
        CanBitTimingConfig timing = parseCanBitTiming(canBitRate, canDataBitRate);
        if(!attemptedConfigure){
            std::ostringstream status;
            status << "Configuring " << canInterface << " @ " << timing.arbitration;
            if(timing.hasData){
                status << " / " << timing.data;
            }
            if(timing.arbitrationFallback || timing.dataFallback){
                status << " (using defaults)";
            }
            setCanStatus(ConnectionState::Configuring, status.str(), timing.arbitrationFallback || timing.dataFallback);

            const ConfigureResult cfg = configureCanInterface(canInterface, timing);
            configureOk = cfg.configured;
            attemptedConfigure = true;
            configureWarning = timing.arbitrationFallback || timing.dataFallback || !configureOk;
            if(cfg.notFound){
                logs("[!] CAN interface " << canInterface << " not found; will retry");
                setCanStatus(ConnectionState::Error, "CAN interface " + canInterface + " not found", true);
                attemptedConfigure = false;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            if(cfg.permissionDenied){
                std::ostringstream err;
                err << "Cannot configure " << canInterface << " (need CAP_NET_ADMIN). "
                    << "Preconfigure with: sudo ip link set " << canInterface
                    << " down type can bitrate " << timing.arbitration;
                if(timing.hasData){
                    err << " dbitrate " << timing.data << " fd on";
                }
                err << " && sudo ip link set " << canInterface << " up";
                setCanStatus(ConnectionState::Error, err.str(), true);
                configureWarning = true;
            } else if(!configureOk){
                std::ostringstream err;
                err << "Failed to configure " << canInterface;
                if(cfg.errorCode != 0){
                    err << " (" << cfg.errorText << ")";
                }
                setCanStatus(ConnectionState::Error, err.str(), true);
                logs("[!] Failed to configure CAN interface " << canInterface << " at bitrate " << canBitRate
                     << "; continuing with the interface's existing configuration");
            }
        }

        const int socketFd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
        if(socketFd < 0){
            logs("[!] Failed to open CAN socket on " << canInterface);
            setCanStatus(ConnectionState::Error, "Failed to open CAN socket on " + canInterface, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        struct ifreq ifr = {};
        std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", canInterface.c_str());
        if(ioctl(socketFd, SIOCGIFINDEX, &ifr) < 0){
            logs("[!] Attempting connection on " << canInterface);
            setCanStatus(ConnectionState::Error, "Unable to look up interface index for " + canInterface, true);
            close(socketFd);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        sockaddr_can addr = {};
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if(bind(socketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0){
            logs("[!] Attempting connection on " << canInterface);
            setCanStatus(ConnectionState::Error, "Failed to bind CAN socket on " + canInterface, true);
            close(socketFd);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        const InterfaceState ifState = queryInterfaceState(canInterface);
        std::ostringstream connectedMsg;
        connectedMsg << "Listening on " << canInterface << " @ " << timing.arbitration;
        if(timing.hasData){
            connectedMsg << " / " << timing.data;
        }
        if(!configureOk){
            connectedMsg << " (existing config in use)";
        }

        bool warn = configureWarning;
        if(!ifState.exists){
            connectedMsg << " (interface state unknown)";
            warn = true;
        } else if(!ifState.up || !ifState.running){
            connectedMsg << " (interface down or no carrier)";
            warn = true;
            logs("[!] CAN interface " << canInterface << " is down or not running; frames may not arrive");
        }

        const bool linkActive = ifState.running;
        setCanStatus(linkActive ? ConnectionState::Connected : ConnectionState::Error,
                     connectedMsg.str(), warn || !linkActive);
        if(linkActive && !hasLoggedInitialConnection){
            logs("[+] Connected to CAN interface " << canInterface);
            hasLoggedInitialConnection = true;
        } else if(!linkActive && !hasLoggedInitialConnection){
            logs("[!] Opened CAN socket on " << canInterface << " but interface is down or not running");
        }

        while(running){
            can_frame frame = {};
            const ssize_t bytesRead = read(socketFd, &frame, sizeof(frame));
            if(bytesRead < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                setCanStatus(ConnectionState::Error, "CAN read failed on " + canInterface + " (" + std::strerror(errno) + ")", true);
                break;
            }
            if(bytesRead == 0){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if(static_cast<size_t>(bytesRead) < sizeof(can_frame)){ continue; }
            if((frame.can_id & CAN_EFF_FLAG) != 0 || (frame.can_id & CAN_RTR_FLAG) != 0){ continue; }

            std::string serialized;
            serialized.reserve(1 + 3 + 1 + static_cast<size_t>(frame.can_dlc) * 2 + 1);
            serialized += 't';
            serialized += toHex(static_cast<uint16_t>(frame.can_id & CAN_SFF_MASK), 3);
            serialized += toHex(static_cast<uint8_t>(frame.can_dlc & 0xFu), 1);
            for(uint8_t i = 0; i < frame.can_dlc && i < 8; ++i){
                serialized += toHex(frame.data[i], 2);
            }
            serialized += '\r';
            pushFrameBytes(canQueue, serialized);
        }

        close(socketFd);
        if(running){
            const char* retryReason = linkActive
                ? "Reconnecting to "
                : "Interface down; retrying ";
            setCanStatus(linkActive ? ConnectionState::Configuring : ConnectionState::Error,
                         std::string(retryReason) + canInterface, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    setCanStatus(ConnectionState::Idle, "CAN reader stopped");
}

#else
void Network::serialReader(){
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    auto makeDevicePath = [](const std::string& portName)->std::string{
        if(portName.rfind("\\\\.\\", 0) == 0) return portName;
        return std::string("\\\\.\\") + portName;
    };

    HANDLE portHandle = INVALID_HANDLE_VALUE;
    while(running && portHandle == INVALID_HANDLE_VALUE){
        const std::string portPath = makeDevicePath(serialPort);
        portHandle = CreateFileA(
            portPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if(portHandle == INVALID_HANDLE_VALUE){
            logs("[!] Attempting connection on " << serialPort);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    if(portHandle == INVALID_HANDLE_VALUE) return;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if(!GetCommState(portHandle, &dcb)){
        CloseHandle(portHandle);
        return;
    }
    dcb.BaudRate = toWindowsBaud(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if(!SetCommState(portHandle, &dcb)){
        CloseHandle(portHandle);
        return;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(portHandle, &timeouts);

    PurgeComm(portHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    while(running){
        DWORD bytesRead = 0;
        if(!ReadFile(portHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if(bytesRead == 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        for(DWORD i = 0; i < bytesRead; ++i){
            serialQueue.push(buffer[static_cast<size_t>(i)]);
        }
    }

    CloseHandle(portHandle);
};

void Network::canReader(){
    setCanStatus(ConnectionState::Configuring, "Initializing PCAN interface " + canInterface);
    PcanApi api = loadPcanApi();
    if(api.library == nullptr){
        logs("[!] PCANBasic.dll not found; Windows CAN input requires the PEAK PCAN-Basic runtime");
        setCanStatus(ConnectionState::Error, "PCANBasic.dll not found", true);
        while(running){
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        return;
    }

    bool hasLoggedInitialConnection = false;
    bool hasLoggedInitFailure = false;
    const TPCANHandle channel = channelHandleFromName(canInterface);
    const PcanBaudrateSelection bitrate = pcanBaudrateFromString(canBitRate);
    if(channel == 0x00U){
        logs("[!] Unknown PCAN channel " << canInterface);
        setCanStatus(ConnectionState::Error, "Unknown PCAN channel " + canInterface);
        unloadPcanApi(api);
        while(running){
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        return;
    }
    if(bitrate.defaulted){
        logs("[!] Unknown PCAN bitrate \"" << canBitRate << "\"; defaulting to 500000 (BTR0BTR1=0x"
             << std::hex << bitrate.value << std::dec << ")");
        std::ostringstream warn;
        warn << "Defaulting PCAN bitrate to 500000 (BTR0BTR1=0x" << std::hex << bitrate.value << std::dec << ")";
        setCanStatus(ConnectionState::Configuring, warn.str(), true);
    }

    while(running){
        setCanStatus(ConnectionState::Configuring, "Connecting to " + canInterface + " @ " + bitrate.sanitizedInput);
        const TPCANStatus initStatus = api.initialize(channel, bitrate.value, 0, 0, 0);
        if(initStatus != kPcanErrorOk){
            if(!hasLoggedInitFailure){
                logs("[!] Failed to initialize " << canInterface << " at bitrate " << bitrate.sanitizedInput
                     << " (BTR0BTR1=0x" << std::hex << bitrate.value << std::dec
                     << ", " << pcanStatusText(api, initStatus) << ")");
                hasLoggedInitFailure = true;
            }
            setCanStatus(ConnectionState::Error, "Failed to initialize " + canInterface + ": " + pcanStatusText(api, initStatus));
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        hasLoggedInitFailure = false;
        if(!hasLoggedInitialConnection){
            logs("[+] Connected to CAN interface " << canInterface << " at bitrate " << bitrate.sanitizedInput
                 << " (BTR0BTR1=0x" << std::hex << bitrate.value << std::dec << ")");
            hasLoggedInitialConnection = true;
        }
        std::ostringstream status;
        status << "Listening on " << canInterface << " @ " << bitrate.sanitizedInput
               << " (BTR0BTR1=0x" << std::hex << bitrate.value << std::dec << ")";
        setCanStatus(ConnectionState::Connected, status.str(), bitrate.defaulted);

        HANDLE receiveEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if(receiveEvent == nullptr){
            logs("[!] Failed to create PCAN receive event for " << canInterface);
            setCanStatus(ConnectionState::Error, "Failed to create PCAN receive event");
            api.uninitialize(channel);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        if(api.setValue(channel, kPcanReceiveEvent, &receiveEvent, sizeof(receiveEvent)) != kPcanErrorOk){
            logs("[!] Failed to register PCAN receive event for " << canInterface);
            setCanStatus(ConnectionState::Error, "Failed to register PCAN receive event");
            CloseHandle(receiveEvent);
            api.uninitialize(channel);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        while(running){
            const DWORD waitResult = WaitForSingleObject(receiveEvent, 100);
            if(waitResult == WAIT_TIMEOUT){
                continue;
            }
            if(waitResult != WAIT_OBJECT_0){
                logs("[!] Failed while waiting for PCAN receive event on " << canInterface);
                break;
            }

            while(running){
                TPCANMsg frame = {};
                const TPCANStatus readStatus = api.read(channel, &frame, nullptr);
                if(readStatus == kPcanErrorQrcvEmpty){
                    break;
                }
                if(readStatus != kPcanErrorOk){
                    logs("[!] PCAN read failed on " << canInterface << " (" << pcanStatusText(api, readStatus) << ")");
                    setCanStatus(ConnectionState::Error, "PCAN read failed: " + pcanStatusText(api, readStatus));
                    goto pcan_reader_cleanup;
                }
                if((frame.MSGTYPE & kPcanMessageStatus) != 0 || (frame.MSGTYPE & kPcanMessageRtr) != 0){
                    continue;
                }
                if((frame.MSGTYPE & kPcanMessageExtended) != 0 || frame.ID > 0x7FFu){
                    continue;
                }

                std::string serialized;
                serialized.reserve(1 + 3 + 1 + static_cast<size_t>(frame.LEN) * 2 + 1);
                serialized += 't';
                serialized += toHex(static_cast<uint16_t>(frame.ID & 0x7FFu), 3);
                serialized += toHex(static_cast<uint8_t>(frame.LEN & 0x0Fu), 1);
                for(uint8_t i = 0; i < frame.LEN && i < 8; ++i){
                    serialized += toHex(frame.DATA[i], 2);
                }
                serialized += '\r';
                pushFrameBytes(canQueue, serialized);
            }
        }

pcan_reader_cleanup:
        api.setValue(channel, kPcanReceiveEvent, nullptr, 0);
        CloseHandle(receiveEvent);
        api.uninitialize(channel);
        if(running){
            setCanStatus(ConnectionState::Configuring, "Reconnecting to " + canInterface);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    unloadPcanApi(api);
    setCanStatus(ConnectionState::Idle, "CAN reader stopped");
}
#endif
