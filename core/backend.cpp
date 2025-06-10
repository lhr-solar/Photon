#include "serial.hpp"
#include "tcp.hpp"
#include "ringbuffer.hpp"
#include "candb.hpp"
#include "dbc.hpp"
#include "config.hpp"
#include "backend.hpp"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>
#include <thread>
#include <array>
#include <memory>

static CanStore can_store;
static DbcParser dbc;

const CanStore& get_can_store() { return can_store; }

bool backend_decode(uint32_t id, const CanFrame &frame, std::string &out){
    return dbc.decode(id, frame, out);
}

enum class ParseState : uint8_t {
    WaitStart,
    Id,
    Len,
    DataHigh,
    DataLow,
    End
};

static inline uint8_t hex_value(uint8_t c){
    static const std::array<int8_t, 256> table = []{
        std::array<int8_t, 256> t{};
        t.fill(-1);
        for(uint8_t i = 0; i < 10; ++i)
            t['0' + i] = i;
        for(uint8_t i = 0; i < 6; ++i){
            t['A' + i] = 10 + i;
            t['a' + i] = 10 + i;
        }
        return t;
    }();
    return table[c];
}

inline void dispatch(uint32_t id, uint8_t len, const uint8_t* payload){
   can_store.store(static_cast<CanStore::IdType>(id), len, payload);
   
   /*
    std::cout << "ID:" << std::hex << id << " LEN:" << std::dec
              << static_cast<int>(len) << " DATA:";
    for(uint8_t i = 0; i < len; ++i)
        std::cout << std::uppercase << std::hex << std::setw(2)
                  << std::setfill('0') << static_cast<int>(payload[i]);
    std::cout << std::dec << std::nouppercase << std::setfill(' ') << std::endl;

    */
}

void parse(const uint8_t* data, size_t len){
    static ParseState state = ParseState::WaitStart;
    static uint32_t id = 0;
    static uint8_t id_digits = 0;
    static uint8_t dlen = 0;
    static uint8_t payload[8];
    static uint8_t index = 0;

    for(size_t i = 0; i < len; ++i){
        uint8_t c = data[i];
        switch(state){
            case ParseState::WaitStart:
                if(c == 't'){
                    id = 0;
                    id_digits = 0;
                    state = ParseState::Id;
                }
                break;
            case ParseState::Id: {
                uint8_t v = hex_value(c);
                if(v < 16){
                    id = (id << 4) | v;
                    if(++id_digits == 3)
                        state = ParseState::Len;
                }else{
                    state = ParseState::WaitStart;
                }
                break;
            }
            case ParseState::Len: {
                uint8_t v = hex_value(c);
                if(v < 16){
                    dlen = v;
                    index = 0;
                    state = dlen ? ParseState::DataHigh : ParseState::End;
                }else{
                    state = ParseState::WaitStart;
                }
                break;
            }
            case ParseState::DataHigh: {
                uint8_t v = hex_value(c);
                if(v < 16){
                    payload[index] = v << 4;
                    state = ParseState::DataLow;
                }else{
                    state = ParseState::WaitStart;
                }
                break;
            }
            case ParseState::DataLow: {
                uint8_t v = hex_value(c);
                if(v < 16){
                    payload[index] |= v;
                    if(++index == dlen)
                        state = ParseState::End;
                    else
                        state = ParseState::DataHigh;
                }else{
                    state = ParseState::WaitStart;
                }
                break;
            }
            case ParseState::End:
                if(c == '\r')
                    dispatch(id, dlen, payload);
                state = ParseState::WaitStart;
                break;
        }
    }
}

void serial_read(SerialPort &serial, RingBuffer &ringBuffer){
    std::vector<uint8_t> temp(READ_CHUNK);
    while(true){
        size_t amount_read = serial.read(temp.data(), temp.size());
        if (amount_read > 0) ringBuffer.write(temp.data(), amount_read);
    }
}
void tcp_read(TcpSocket &socket, RingBuffer &ringBuffer){
    std::vector<uint8_t> temp(READ_CHUNK);
    while(true){
        size_t amount_read = socket.read(temp.data(), temp.size());
        if(amount_read > 0) ringBuffer.write(temp.data(), amount_read);
    }

}

void photon_proc(RingBuffer &ringBuffer){
    std::vector<uint8_t> temp(READ_CHUNK);
    while(true){
        size_t amount_read = ringBuffer.read(temp.data(), temp.size());
        /*
        std::cout.write((char*)temp.data(), amount_read) << std::endl;
        std::cout.flush();
        */
        parse((uint8_t*)temp.data(), amount_read);
    }
}

void user_prompt(){
    std::string input;
    while(true){
        std::cout << "[!] Request CAN ID data (hex) or q to quit: ";
        if(!(std::cin >> input))
            return;
        if(input == "q" || input == "quit")
            return;
        unsigned id = 0;
        std::stringstream ss;
        ss << std::hex << input;
        if(!(ss >> id)){
            std::cout << "Invalid ID" << std::endl;
            continue;
        }
        CanFrame frame;
        if(can_store.read(static_cast<CanStore::IdType>(id), frame)){
            /*
            std::cout << "LEN: " << std::dec << static_cast<int>(frame.len)
                      << " DATA: ";
            for(uint8_t i = 0; i < frame.len; ++i)
                std::cout << std::uppercase << std::hex << std::setw(2)
                          << std::setfill('0') << static_cast<int>(frame.data[i]);

            std::cout << std::dec << std::nouppercase << std::setfill(' ') << std::endl;
            */
            std::string decoded;
            if(dbc.decode(id,frame,decoded)){
                std::cout << decoded << std::endl;
            } else {
                std::cout << "Len: " << std::dec << static_cast<int>(frame.len) << " Data: ";
                for(uint8_t i = 0; i < frame.len; ++i)
                    std::cout << std::uppercase << std::hex << std::setw(2)
                              << std::setfill('0') << static_cast<int>(frame.data[i]);
                std::cout << std::dec << std::nouppercase << std::setfill(' ')<<std::endl;
            }
        } else {
            std::cout << "No data for ID" << std::endl;
        }
    }
}

enum source_t {
    local,
    remote
};
int backend(int argc, char* argv[]){
    (void)argc;(void)argv;
    int res = 0;
    if (argc != 0)
        res = std::stoi(argv[1]);
    source_t source = (source_t)res;

    for(int i = 2; i < argc; i++){
        std::cout << "Decoding ";
        std::cout << argv[i] << std::endl;
        dbc.load(argv[i]);
    }

    dbc.load("./dbc/prohelion_wavesculptor22.dbc");
    dbc.load("./dbc/tpee_mppt[B].dbc");
    dbc.load("./dbc/tpee_mppt[A].dbc");
    dbc.load("./dbc/new_controls.dbc");

    dbc.can_parse_debug();
    
    std::unique_ptr<SerialPort> serial;
    std::unique_ptr<TcpSocket> tcp;

    RingBuffer ringBuffer;

    std::thread prod_t;

    if(source == local){
        std::string portName = "/dev/pts/7";//PORT;
        unsigned baud = 115200;
        serial = std::make_unique<SerialPort>(portName, baud);
        prod_t = std::thread(serial_read, std::ref(*serial), std::ref(ringBuffer));
    }

    if(source == remote){
        std::string serverIP = IP;
        unsigned port = 5700;
        tcp = std::make_unique<TcpSocket>(serverIP, port);
        prod_t = std::thread(tcp_read, std::ref(*tcp), std::ref(ringBuffer));
    }

    std::thread photon_t(photon_proc, std::ref(ringBuffer));

    //user_prompt();
    prod_t.join();
    photon_t.join();
    return 0;
}
