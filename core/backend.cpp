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
#include <atomic>
#include <utility>

#include "bps_dbc.hpp"
#include "controls_dbc.hpp"
#include "prohelion_wavesculptor22_dbc.hpp"
#include "tpee_mppt_A__dbc.hpp"
#include "tpee_mppt_B__dbc.hpp"

static CanStore can_store;
static DbcParser dbc;

static std::mutex dbc_mtx;
struct BuiltinDbc {
    const char* name;
    const unsigned char* data;
    size_t size;
    bool enabled;
};

static BuiltinDbc builtin_dbcs[] = {
    {"BPS", bps_dbc, bps_dbc_size, true},
    {"Wavesculptor22", prohelion_wavesculptor22_dbc, prohelion_wavesculptor22_dbc_size, true},
    {"MPPT A", tpee_mppt_A__dbc, tpee_mppt_A__dbc_size, true},
    {"MPPT B", tpee_mppt_B__dbc, tpee_mppt_B__dbc_size, true},
    {"Controls", controls_dbc, controls_dbc_size, true}
};
static std::mutex builtin_mtx;

struct DbcOp{
    enum Type { Load, Unload } type;
    std::string name;
    bool builtin = false;
};

static std::vector<DbcOp> dbc_requests;
static std::mutex dbc_request_mtx;
static std::vector<std::string> loaded_dbcs;
static std::mutex loaded_dbcs_mtx;
std::atomic<bool> new_dbc_flag(false);

static void rebuild_dbc(){
    DbcParser tmp;
    {
        std::lock_guard<std::mutex> lock(builtin_mtx);
        for(const auto &b : builtin_dbcs)
            if(b.enabled)
                tmp.loadFromMemory((const char*)b.data, b.size);
    }
    {
        std::lock_guard<std::mutex> lock(loaded_dbcs_mtx);
        for(const auto &p : loaded_dbcs)
            tmp.load(p);
    }
    std::lock_guard<std::mutex> lock(dbc_mtx);
    dbc = std::move(tmp);
}

std::atomic<bool> data_source_terminate(false);

const CanStore& get_can_store() { return can_store; }

void forward_dbc_load(const std::string& path){
    if(path.empty())
        return;
    std::lock_guard<std::mutex> lock(dbc_request_mtx);
    dbc_requests.push_back({DbcOp::Load, path});
    new_dbc_flag.store(true, std::memory_order_release);
}

void forward_dbc_unload(const std::string& path){
    if(path.empty())
        return;
    std::lock_guard<std::mutex> lock(dbc_request_mtx);
    dbc_requests.push_back({DbcOp::Unload, path});
    new_dbc_flag.store(true, std::memory_order_release);
}

void forward_builtin_dbc_load(const std::string& name){
    if(name.empty()) return;
    std::lock_guard<std::mutex> lock(dbc_request_mtx);
    dbc_requests.push_back({DbcOp::Load, name, true});
    new_dbc_flag.store(true, std::memory_order_release);
}

void forward_builtin_dbc_unload(const std::string& name){
    if(name.empty()) return;
    std::lock_guard<std::mutex> lock(dbc_request_mtx);
    dbc_requests.push_back({DbcOp::Unload, name, true});
    new_dbc_flag.store(true, std::memory_order_release);
}

std::vector<std::pair<std::string,bool>> list_builtin_dbcs(){
    std::lock_guard<std::mutex> lock(builtin_mtx);
    std::vector<std::pair<std::string,bool>> out;
    for(const auto &b : builtin_dbcs)
        out.emplace_back(b.name, b.enabled);
    return out;
}

std::vector<std::string> get_loaded_dbcs(){
    std::lock_guard<std::mutex> lock(loaded_dbcs_mtx);
    return loaded_dbcs;
}

bool backend_decode(uint32_t id, const CanFrame &frame, std::string &out){
    std::lock_guard<std::mutex> lock(dbc_mtx);
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
    while(!data_source_terminate.load()){
        size_t amount_read = serial.read(temp.data(), temp.size());
        if (amount_read > 0) ringBuffer.write(temp.data(), amount_read);
    }
}
void tcp_read(TcpSocket &socket, RingBuffer &ringBuffer){
    std::vector<uint8_t> temp(READ_CHUNK);
    while(!data_source_terminate.load()){
        size_t amount_read = socket.read(temp.data(), temp.size());
        if(amount_read > 0) ringBuffer.write(temp.data(), amount_read);
    }
}

void photon_proc(RingBuffer &ringBuffer){
    std::vector<uint8_t> temp(READ_CHUNK);
    while(true){
        size_t amount_read = ringBuffer.read(temp.data(), temp.size());
        parse((uint8_t*)temp.data(), amount_read);
    }
}

struct SerialSource {
    std::string fd;
    std::string baud;
    std::mutex mtx;
}serialSourceBuffer;

struct TcpSource {
    std::string fd;
    std::string port;
    std::mutex mtx;
}tcpSourceBuffer;

std::atomic<bool> new_source_flag(false);
std::atomic<int> sourceEnum(-1);

void kill_data_source(){
    sourceEnum.store(-1);
    new_source_flag.store(true, std::memory_order_release);
}

void forward_serial_source(std::string& fd, std::string& baud){
    if(fd.empty() || baud.empty())
        return;

    std::unique_lock<std::mutex> lock(serialSourceBuffer.mtx);
    serialSourceBuffer.fd = fd;
    serialSourceBuffer.baud = baud;
    sourceEnum.store(0);
    new_source_flag.store(true, std::memory_order_release);
}

void forward_tcp_source(std::string& fd, std::string& port){
    if(fd.empty() || port.empty())
        return;

    std::unique_lock<std::mutex> lock(tcpSourceBuffer.mtx);
    tcpSourceBuffer.fd = fd;
    tcpSourceBuffer.port = port;
    sourceEnum.store(1);
    new_source_flag.store(true, std::memory_order_release);
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

    for(int i = 2; i < argc; i++){ std::cout << "Decoding ";
        std::cout << argv[i] << std::endl;
        dbc.load(argv[i]);
    }

    rebuild_dbc();
    //dbc.can_parse_debug();
    
    std::unique_ptr<SerialPort> serial;
    std::unique_ptr<TcpSocket> tcp;

    RingBuffer ringBuffer;

    std::thread prod_t;

    std::thread photon_t(photon_proc, std::ref(ringBuffer));

    while(1){

          if(new_dbc_flag.load()){ // -- handle dbc operations  --
            new_dbc_flag.store(false, std::memory_order_release);
            std::vector<DbcOp> ops;
            {
                std::lock_guard<std::mutex> lock(dbc_request_mtx);
                ops.swap(dbc_requests);
            }
            bool rebuild = false;
            for(auto &op : ops){
                if(op.builtin){
                    std::lock_guard<std::mutex> lock(builtin_mtx);
                    for(auto &b : builtin_dbcs){
                        if(b.name == op.name){
                            b.enabled = (op.type == DbcOp::Load);
                            rebuild = true;
                            break;
                        }
                    }
                } else {
                    if(op.type == DbcOp::Load){
                        std::lock_guard<std::mutex> lock(loaded_dbcs_mtx);
                        if(std::find(loaded_dbcs.begin(), loaded_dbcs.end(), op.name) == loaded_dbcs.end()){
                            loaded_dbcs.push_back(op.name);
                            rebuild = true;
                        }
                    } else {
                        std::lock_guard<std::mutex> lock(loaded_dbcs_mtx);
                        auto it = std::find(loaded_dbcs.begin(), loaded_dbcs.end(), op.name);
                        if(it != loaded_dbcs.end()){
                            loaded_dbcs.erase(it);
                            rebuild = true;
                        }
                    }
                }
            }
            if(rebuild)
                rebuild_dbc();
        }

        if(new_source_flag.load()){ // -- new source --
            new_source_flag.store(false, std::memory_order_release);

            // -- kill old thread --
            if(prod_t.joinable()){
               data_source_terminate.store(true, std::memory_order_release);
               prod_t.join();
               data_source_terminate.store(false, std::memory_order_release);
            }

            // -- grab protocol --
            int prot = sourceEnum.load();

            // -- false alarm --
            if(prot == -1){
                // if we keep prot to -1, we just kill the thread
                continue;
            }

            std::string fd;
            unsigned cfg;

            // -- grab params --
            if((source_t)prot == local){
                std::lock_guard<std::mutex> lock(serialSourceBuffer.mtx);
                fd = serialSourceBuffer.fd;
                cfg = std::stoi(serialSourceBuffer.baud);
            }
            if((source_t)prot == remote){
                std::lock_guard<std::mutex> lock(tcpSourceBuffer.mtx);
                fd = tcpSourceBuffer.fd;
                cfg = std::stoi(tcpSourceBuffer.port);
            }

            // -- create threads --
            try{
                if((source_t)prot == local){
                    serial = std::make_unique<SerialPort>(fd, cfg);
                    prod_t = std::thread(serial_read, std::ref(*serial), std::ref(ringBuffer));
                }
                if((source_t)prot == remote){
                    tcp = std::make_unique<TcpSocket>(fd, cfg);
                    prod_t = std::thread(tcp_read, std::ref(*tcp), std::ref(ringBuffer));
                }
            } catch (const std::exception &e){
            }

        }
    }

    photon_t.join();
    return 0;
}
