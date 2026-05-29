#include <mutex>
#define NOMINMAX
#include <algorithm>
#include "network.hpp"
#include "tcp.hpp"
#include "spsc.hpp"
#include <fcntl.h>
#include <cstddef>
#include <charconv>
#include <map>
#include <fstream>
#include <thread>
#include <iostream>
#include <sstream>
#include <cctype>
#include <cmath>
#include <cstring>
#include <array>

#define QUEUE_CAPACITY 2048
#define BUFFER_CAPACITY 1024

static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

template <typename T>
static bool decodeIntelByteAlignedFloat(const DbcSignal& sig,
                                        const uint8_t bytes[8],
                                        int dlc,
                                        T& out) {
    constexpr int byteWidth = static_cast<int>(sizeof(T));
    if (sig.endianness != 1 || sig.length != byteWidth * 8 || (sig.startBit % 8) != 0) {
        return false;
    }

    const int startByte = sig.startBit / 8;
    if (startByte < 0 || (startByte + byteWidth) > dlc) {
        return false;
    }

    std::array<uint8_t, sizeof(T)> rawBytes{};
    for (int i = 0; i < byteWidth; ++i) {
        rawBytes[static_cast<size_t>(i)] = bytes[startByte + i];
    }

    std::memcpy(&out, rawBytes.data(), sizeof(T));
    return std::isfinite(static_cast<double>(out));
}

Network::Network() : tcpQueue(QUEUE_CAPACITY), uartQueue(QUEUE_CAPACITY) {}

Network::~Network() {
    running = false;
}

// ---------------------- producer ----------------------

void Network::producer() {
    std::cerr << "[DEBUG] Using IP=" << IP
              << " PORT=" << PORT << "\n";

    TcpSocket socket(IP, PORT);
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);

    std::cerr << "[+] Attempting connection to "
              << IP << ":" << PORT << "...\n";
    std::cerr << "[+] Connected (TcpSocket constructor succeeded).\n";

    while (running) {
        auto bytesRead = socket.read(buffer.data(), buffer.size());
        if (bytesRead <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (bytesRead > 0) {
            for (size_t i = 0; i < (size_t)bytesRead; ++i) {
                while (!tcpQueue.try_push(buffer[i]))
                    std::this_thread::yield();
            }
        }
    }
}

// ---------------------- parser ----------------------

void Network::parser() {
    std::string frame;
    frame.reserve(256);
    bool collecting = false;

    while (running) {
        if (auto* byte = tcpQueue.front()) {
            char ch = static_cast<char>(*byte);
            tcpQueue.pop();

            if (!collecting && (std::isprint(ch) || ch == '\r'))
                collecting = true;
            if (!collecting)
                continue;

            if (ch == '\r') {
                if (!frame.empty()) {
                    while (!frame.empty() &&
                          (frame.back() == '\r' ||
                           frame.back() == '\n')) {
                        frame.pop_back();
                    }

                    if (!frame.empty() &&
                        frame.rfind("t", 0) == 0) {
                        handleFrame(frame);
                    }
                }
                frame.clear();
                collecting = false;
                continue;
            }

            frame.push_back(ch);
        } else {
            std::this_thread::yield();
        }
    }
}

// ---------------------- decodeFrame ----------------------

bool Network::decodeFrame(const std::string& frame,
                          uint16_t& canId,
                          uint64_t& value) {
    if (frame.empty() || frame.front() != 't'
        || frame.size() < 5)
        return false;

    int dataLength = hexValue(frame[4]);
    if (dataLength < 0 || dataLength > 8)
        return false;

    const size_t expectedLength =
        5 + static_cast<size_t>(dataLength) * 2;
    if (frame.size() != expectedLength)
        return false;

    canId = 0;
    for (size_t i = 1; i <= 3; ++i) {
        int nibble = hexValue(frame[i]);
        if (nibble < 0) return false;
        canId = static_cast<uint16_t>((canId << 4) | nibble);
    }

    value = 0;
    for (int i = 0; i < dataLength; ++i) {
        int hi = hexValue(frame[5 + i * 2]);
        int lo = hexValue(frame[6 + i * 2]);
        if (hi < 0 || lo < 0) return false;
        uint8_t byte = static_cast<uint8_t>((hi << 4) | lo);
        value = (value << 8) | byte;
    }

    return true;
}

// ---------------------- handleFrame ----------------------

void Network::handleFrame(const std::string& frame) {
    uint16_t canId;
    uint64_t value;

    if (!decodeFrame(frame, canId, value)) {
        std::cerr << "[parser] Invalid SLCAN frame → "
                  << frame << "\n";
        return;
    }

    writeSample(canId, value);

    std::cerr << "[SLCAN] CAN ID = " << canId
            << "  (0x" << std::uppercase << std::hex << canId << std::dec << ")"
            << " | DLC = " << int(frame[4] - '0')
            << " | DATA = 0x" << std::uppercase << std::hex << value << std::dec
            << "\n";

    // Interpret using DBC if available
    if (dbcManager.hasMessages()) {
        interpretDBCFrame(canId, value, frame[4] - '0');  // DLC from frame[4]
    }
}

// ---------------------- sample helpers ----------------------

Network::sample& Network::ensureSample(uint16_t canId) {
    std::lock_guard<std::mutex> guard(sampleMapMutex);
    auto& ptr = sampleMap[canId];
    if (!ptr) {
        ptr = std::make_unique<sample>();
    }
    return *ptr;
}

bool Network::readSample(uint16_t canId, uint64_t& outValue) {
    std::unique_lock<std::mutex> mapLock(sampleMapMutex);
    auto it = sampleMap.find(canId);
    if (it == sampleMap.end()) {
        outValue = 0;
        return false;
    }
    sample* entry = it->second.get();
    mapLock.unlock();

    std::lock_guard<std::mutex> valueGuard(entry->lock);
    outValue = entry->point;
    bool wasNew = entry->hasNew;
    entry->hasNew = false;
    return wasNew;
}

void Network::writeSample(uint16_t canId, uint64_t value) {
    sample& entry = ensureSample(canId);
    std::lock_guard<std::mutex> valueGuard(entry.lock);
    entry.point = value;
    entry.hasNew = true;
}

// ---------------------------------------------------------
// Decode raw CAN payload using the DBC map
// ---------------------------------------------------------
void Network::interpretDBCFrame(uint16_t canId, uint64_t rawValue, int dlc) {
    std::lock_guard<std::mutex> lock(dbcManager.mapMutex);

    auto it = dbcManager.dbcMap.find(canId);
    if (it == dbcManager.dbcMap.end()) {
        return;
    }

    const DbcMessage& msg = it->second;

    // ---- COLLECT INTO A VECTOR ----
    std::vector<std::string> collected;
    collected.push_back(msg.name);

    // Convert rawValue → bytes
    uint8_t bytes[8] = {0};
    uint64_t tmp = rawValue;
    for (int i = dlc - 1; i >= 0; --i) {
        bytes[i] = static_cast<uint8_t>(tmp & 0xFF);
        tmp >>= 8;
    }

    // Iterate signals
    for (const auto& [sigName, sig] : msg.signals) {

        uint64_t rawSignal = 0;

        if (sig.endianness == 1) {
            // Intel / little-endian: walk bytes low → high.
            int byteIndex = sig.startBit / 8;
            int bitOffset = sig.startBit % 8;
            int bitsLeft = sig.length;
            int dstPos = 0;
            while (bitsLeft > 0 && byteIndex < dlc) {
                int bitsInByte = std::min(8 - bitOffset, bitsLeft);
                uint32_t mask = ((1u << bitsInByte) - 1u) << bitOffset;
                uint8_t extracted = static_cast<uint8_t>((bytes[byteIndex] & mask) >> bitOffset);
                rawSignal |= (static_cast<uint64_t>(extracted) << dstPos);

                bitsLeft -= bitsInByte;
                dstPos += bitsInByte;
                byteIndex++;
                bitOffset = 0;
            }
        } else {
            // Motorola / big-endian: start bit is MSB; walk bit-by-bit toward
            // bit 0 of the current byte, then jump to bit 7 of the next byte.
            int bitPos = sig.startBit;
            for (int i = 0; i < sig.length; ++i) {
                int byteIndex = bitPos / 8;
                int bitInByte = bitPos % 8;
                if (byteIndex >= dlc) break;
                uint64_t bit = (bytes[byteIndex] >> bitInByte) & 0x1u;
                rawSignal |= bit << (sig.length - 1 - i);
                if (bitInByte == 0) bitPos += 15; // next byte, bit 7
                else                bitPos -= 1;
            }
        }

        double physValue = 0.0;
        if (sig.isFloat && sig.length == 32) {
            float f = 0.0f;
            if (!decodeIntelByteAlignedFloat(sig, bytes, dlc, f)) {
                uint32_t r = static_cast<uint32_t>(rawSignal);
                std::memcpy(&f, &r, sizeof(f));
            }
            physValue = sig.scale * static_cast<double>(f) + sig.offset;
        } else if (sig.isFloat && sig.length == 64) {
            double d = 0.0;
            if (!decodeIntelByteAlignedFloat(sig, bytes, dlc, d)) {
                std::memcpy(&d, &rawSignal, sizeof(d));
            }
            physValue = sig.scale * d + sig.offset;
        } else if (sig.isSigned && sig.length > 0 && sig.length < 64) {
            // Sign-extend the rawSignal from sig.length bits to 64.
            uint64_t signBit = 1ULL << (sig.length - 1);
            int64_t s = static_cast<int64_t>(rawSignal);
            if (rawSignal & signBit) {
                s |= ~((1ULL << sig.length) - 1ULL);
            }
            physValue = sig.scale * static_cast<double>(s) + sig.offset;
        } else {
            physValue = sig.scale * static_cast<double>(rawSignal) + sig.offset;
        }

        if (!std::isfinite(physValue)) {
            std::cerr << "[DBC] Skipping non-finite value for signal " << sigName << "\n";
            continue;
        }

        // ---- STORE PARSED VALUE ----
        {
            std::lock_guard<std::mutex> parsedLock(parsedSignalsMutex);
            parsedSignals[sigName] = physValue;
        }

        // ---- MAKE A STRING FOR THIS SIGNAL ----
        std::ostringstream ss;
        ss << sigName << " = " << physValue;
        if (!sig.unit.empty())
            ss << " " << sig.unit;

        collected.push_back(ss.str());

        // ---- Print OUT (original behavior) ----
        std::cerr << "    " << ss.str() << "\n";
    }

    // ---- STORE IN HISTORY ----
    {
        std::lock_guard<std::mutex> histLock(decodedHistoryMutex);
        decodedHistory.push_back({canId, rawValue, collected});
        if (decodedHistory.size() > MAX_HISTORY)
            decodedHistory.pop_front();
    }
}

// ---------------------- UART ----------------------

#ifndef WIN32
#include <termios.h>
#include <unistd.h>
void Network::uartProducer(){
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);
    int _fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);
    while(running && _fd < 0){
        std::cout << "[!] Attempting connection on /dev/ttyACM0" << std::endl;
        _fd = open("/dev/ttyACM0", O_RDWR | O_NOCTTY);
        if (_fd < 0) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (_fd < 0) return;

    struct termios tty = {};
    tcgetattr(_fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(_fd, TCSANOW, &tty);
    tcflush(_fd, TCIFLUSH);

    while(running){
        ssize_t n = read(_fd, buffer.data(), buffer.size());
        if (n > 0) {
            for(int i = 0; i < n; i++){
                while (!uartQueue.try_push(buffer[i]))
                    std::this_thread::yield();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    close(_fd);
};

static inline int retCANID(const char* str){
    static const std::map<std::string, uint16_t> map = {
        {"gX: ", 0x400}, 
        {"gY: ", 0x401},
        {"gZ: ", 0x402},
        {"aX: ", 0x403},
        {"aY: ", 0x404},
        {"aZ: ", 0x405},
        };
    auto it = map.find(str);
    if(it == map.end()) return -1;
    return it->second;
}

void Network::uartConsumer(){
    std::string canId;
    std::string value;
    bool collectingCAN = false;
    bool collectingVal = false;
    while(running){
        if(unsigned char* l = uartQueue.front()){
            unsigned char val = *l;
            uartQueue.pop();
            if((val == 'g') || (val == 'a')){
                canId.clear();
                value.clear();
                collectingCAN = true;
                collectingVal = false;
                canId.push_back(val);
                continue;
            }
            if(collectingCAN && val == ' '){
                canId.push_back(val);
                collectingCAN = false;
                collectingVal = true;
                continue;
            }
            if(collectingCAN){
                canId.push_back(val);
            }
            if(collectingVal && ((val == ' ') || (val == '\n'))){
                int v;
                auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
                if (ec == std::errc()) {
                    int id = retCANID(canId.c_str());
                    if (id != -1) writeSample(static_cast<uint16_t>(id), v);
                }
                canId.clear(); value.clear(); collectingVal = false;
            }
            if(collectingVal){
                value.push_back(val);
            }
        } else {
            std::this_thread::yield();
        }
    }
}
#else
void Network::uartConsumer(){
    while(running) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
void Network::uartProducer(){
    while(running) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
#endif

void Network::candumpParser() {
#ifndef WIN32
    std::cerr << "[Network] Starting candumpParser thread (Linux popen mode)\n";
    
    // candump -L can0 outputs: (timestamp) can0 ID#DATA
    // Example: (1612345678.123456) can0 0C0#0000000000000000
    FILE* pipe = popen("candump -L can0", "r");
    if (!pipe) {
        std::cerr << "[Network] Failed to execute candump -L can0\n";
        return;
    }

    char line[256];
    while (running && fgets(line, sizeof(line), pipe)) {
        std::string s(line);
        
        // Find the '#' separator
        size_t hashPos = s.find('#');
        if (hashPos == std::string::npos) continue;

        // Find the space before the ID
        size_t spacePos = s.find_last_of(' ', hashPos);
        if (spacePos == std::string::npos) continue;

        // Extract ID (hex)
        std::string idStr = s.substr(spacePos + 1, hashPos - spacePos - 1);
        uint16_t canId = 0;
        try {
            canId = static_cast<uint16_t>(std::stoul(idStr, nullptr, 16));
        } catch (...) { continue; }

        // Extract Data (hex)
        std::string dataStr = s.substr(hashPos + 1);
        // Remove trailing newline/whitespace
        while (!dataStr.empty() && (dataStr.back() == '\r' || dataStr.back() == '\n' || dataStr.back() == ' ')) {
            dataStr.pop_back();
        }

        if (dataStr.empty()) continue;

        uint64_t value = 0;
        int dlc = (int)dataStr.length() / 2;
        if (dlc > 8) dlc = 8;

        try {
            // candump -L ID#DATA provides data as a hex string.
            // Example: 0C0#1122334455667788
            // We need to parse this into a 64-bit integer such that the first byte (11)
            // is the most significant byte if we want to match the existing big-endian
            // construction used in decodeFrame.
            
            for (int i = 0; i < dlc; ++i) {
                std::string byteStr = dataStr.substr(i * 2, 2);
                uint8_t byte = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
                value = (value << 8) | byte;
            }
        } catch (...) { continue; }

        writeSample(canId, value);

        std::cerr << "[CANDUMP] CAN ID = " << canId
                << "  (0x" << std::uppercase << std::hex << canId << std::dec << ")"
                << " | DLC = " << dlc
                << " | DATA = 0x" << std::uppercase << std::hex << value << std::dec
                << "\n";

        if (dbcManager.hasMessages()) {
            interpretDBCFrame(canId, value, dlc);
        }
    }

    pclose(pipe);
    std::cerr << "[Network] candumpParser thread exiting\n";
#else
    std::cerr << "[Network] candumpParser not supported on Windows\n";
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif
}

// ---------------------- DBC wrappers ----------------------

bool Network::loadDBC(const std::string& path) {
    return dbcManager.loadFromFile(path);
}

void Network::printDBCMap() {
    dbcManager.dump();
}

bool Network::readParsedSignal(const std::string& sigName, double& outValue) {
    std::lock_guard<std::mutex> lock(parsedSignalsMutex);
    auto it = parsedSignals.find(sigName);
    if (it == parsedSignals.end()) {
        return false;
    }
    outValue = it->second;
    return true;
}
