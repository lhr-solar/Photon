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
#include "spsc.hpp"
#include "../parse/corsa.hpp"

class Network{
private:

public:
    Network();
    ~Network();
    void tcpReader();
    void udpReader();
    void serialReader();
    void localReader();
    void corsaReader();
    void parser();

    SPSCQueue<uint8_t> tcpQueue;
    SPSCQueue<uint8_t> udpQueue;
    SPSCQueue<uint8_t> serialQueue;
    SPSCQueue<uint8_t> localQueue;
    SPSCQueue<RTCarInfo> corsaQueue;
    std::string IP ="3.141.38.115";
    std::string LOCAL_IP = "127.0.0.1";
    unsigned CORSA_PORT = 9996;
    unsigned PORT = 9000;
    std::atomic<bool> running = true;

/* end of network class */
};
