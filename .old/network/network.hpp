/*[ξ] the photon network interface*/
#pragma once
#include <array>
#include <map>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include "spsc.hpp"
#include "../parse/corsa.hpp"

#ifndef _WIN32
#include <termios.h>
#endif

enum class ConnectionState {
    Idle,
    Configuring,
    Connected,
    Error,
};

struct ConnectionStatusSnapshot {
    ConnectionState state = ConnectionState::Idle;
    std::string message;
    bool warning = false;
};

class Network{
private:
    void setCanStatus(ConnectionState state, const std::string& message, bool warning = false);
    ConnectionStatusSnapshot makeCanStatusSnapshot() const;

public:
    Network();
    ~Network();
    void tcpReader();
    void udpReader();
    void serialReader();
    void canReader();
    void localReader();
    void corsaReader();
    void parser();

    SPSCQueue<uint8_t> tcpQueue;
    SPSCQueue<uint8_t> udpQueue;
    SPSCQueue<uint8_t> serialQueue;
    SPSCQueue<uint8_t> canQueue;
    SPSCQueue<uint8_t> localQueue;
    SPSCQueue<RTCarInfo> corsaQueue;
    std::string IP ="3.141.38.115";
    std::string LOCAL_IP = "127.0.0.1";
    unsigned CORSA_PORT = 9996;
    unsigned PORT = 6500;
    std::string baudRate = "9600";
    std::string serialPort =
#ifdef _WIN32
        "COM1";
#else
        "/dev/ttyUSB0";
#endif
    std::string canInterface =
#ifdef _WIN32
        "PCAN_USBBUS1";
#else
        "can0";
#endif
    std::string canBitRate = "500000";
    std::string canDataBitRate = "";

    std::atomic<bool> running = true;
    std::atomic<ConnectionState> canState = ConnectionState::Idle;
    std::atomic<bool> canStatusWarning = false;
    mutable std::mutex canStatusMutex;
    std::string canStatusMessage;
    std::string currentBackend = "Assetto Corsa";
    std::thread currentSource_t;

    ConnectionStatusSnapshot getCanStatus() const;

/* end of network class */
};
