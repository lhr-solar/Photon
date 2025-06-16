#include "dbc.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

static std::string trim(const std::string& in){
    auto start = in.find_first_not_of(" \t\r\n");
    auto end = in.find_last_not_of(" \t\r\n");
    if(start == std::string::npos) return "";
    return in.substr(start, end - start + 1);
}

bool DbcParser::load(const std::string& path){
    std::ifstream file(path);
    if(!file){
        std::cout << "invalid file" << std::endl;
        return false;
    }
    std::string line;
    DbcMessage* current = nullptr;
    while(std::getline(file, line)){
        line = trim(line);
        if(line.empty()) continue;
        std::istringstream iss(line);
        std::string tok;
        iss >> tok;
        if(tok == "BO_"){
            uint32_t id; std::string name; char colon; unsigned dlc; std::string transmitter;
            iss >> id >> name >> colon >> dlc >> transmitter;
            DbcMessage msg; msg.id = id; msg.name = name; msg.dlc = static_cast<uint8_t>(dlc);
            _messages[id] = msg;
            current = &_messages[id];
        } else 
        if(tok == "SG_" && current){
            std::string name; iss >> name; // may include ":" after name
            if(!iss) continue;
            std::string sep; iss >> sep; // should be ':'
            std::string rest; std::getline(iss, rest);
            rest = trim(rest);
            // parse start|size@endian+sign
            std::istringstream iss2(rest);
            std::string posToken; iss2 >> posToken;
            size_t pipe = posToken.find('|');
            size_t at = posToken.find('@', pipe+1);
            uint16_t start = static_cast<uint16_t>(std::stoi(posToken.substr(0, pipe)));
            uint8_t size = static_cast<uint8_t>(std::stoi(posToken.substr(pipe+1, at-pipe-1)));
            bool little = posToken.at(at+1) == '1';
            bool sign = posToken.at(at+2) == '-';
            std::string factorToken; iss2 >> factorToken; // (factor,offset)
            double factor = 1.0, offset = 0.0;
            if(factorToken.size() > 2){
                factorToken = factorToken.substr(1, factorToken.size()-2); // remove ()
                size_t comma = factorToken.find(',');
                factor = std::stod(factorToken.substr(0, comma));
                offset = std::stod(factorToken.substr(comma+1));
            }
            DbcSignal sig; sig.name = name; sig.start_bit = start; sig.size = size;
            sig.little_endian = little; sig.is_signed = sign; sig.factor = factor; sig.offset = offset;
            current->signals.push_back(sig);
        } else 
        if(tok == "VAL_TABLE_"){
            std::string table; iss >> table;
            int64_t val; std::string desc;
            while(iss >> val){
                iss >> std::ws; char quote; iss >> quote; std::getline(iss, desc, '"');
                _value_tables[table][val] = desc;
                iss >> std::ws; char end; iss >> end; // consume closing quote char or semicolon
            }
        } else 
        if(tok == "VAL_"){
            uint32_t id; std::string sig; iss >> id >> sig;
            int64_t val; std::string desc;
            while(iss >> val){
                iss >> std::ws; char quote; iss >> quote; std::getline(iss, desc, '"');
                _value_tables[sig][val] = desc;
                iss >> std::ws; char end; iss >> end;
            }
        }
    }

    // attach value tables to signals
    for(auto &mp : _messages){
        for(auto &sig : mp.second.signals){
            auto it = _value_tables.find(sig.name);
            if(it != _value_tables.end()) sig.value_map = it->second;
        }
    }

    return true;
}

void DbcParser::can_parse_debug(){
    for(const auto &mp : _messages){
        const auto &m = mp.second;
        std::cout << "Message 0x" << std::hex << m.id << " " << m.name
                  << " DLC " << std::dec << static_cast<int>(m.dlc) << std::endl;
        for(const auto &sig : m.signals){
            std::cout << "  Signal " << sig.name
                      << " start=" << sig.start_bit
                      << " size=" << static_cast<int>(sig.size)
                      << (sig.little_endian ? " LE" : " BE")
                      << (sig.is_signed ? " signed" : " unsigned")
                      << " factor=" << sig.factor
                      << " offset=" << sig.offset << std::endl;
            if(!sig.value_map.empty()){
                std::cout << "    Values: ";
                bool first=true;
                for(const auto &kv : sig.value_map){
                    if(!first) std::cout << ", ";
                    first=false;
                    std::cout << kv.first << "=\"" << kv.second << "\"";
                }
                std::cout << std::endl;
            }
        }
    }
}

uint64_t DbcParser::extract_signal(const uint8_t* data, uint16_t start, uint8_t size, bool little) const{
    uint64_t val = 0;
    if(little){
        for(unsigned i=0;i<size;++i){
            unsigned pos = start + i;
            uint8_t byte = pos / 8;
            uint8_t bit = pos % 8;
            val |= ((uint64_t)((data[byte] >> bit) & 1) << i);
        }
    } else {
        for(unsigned i=0;i<size;++i){
            int pos = start - i;
            uint8_t byte = pos / 8;
            uint8_t bit = pos % 8;
            val = (val << 1) | ((data[byte] >> bit) & 1);
        }
    }
    return val;
}

int64_t DbcParser::sign_extend(uint64_t val, unsigned bits) const{
    if(bits == 0 || bits >= 64) return static_cast<int64_t>(val);
    uint64_t mask = 1ull << (bits - 1);
    return (val ^ mask) - mask;
}

bool DbcParser::decode(uint32_t id, const CanFrame& frame, std::string& out) const{
    auto it = _messages.find(id);
    if(it == _messages.end()) return false;
    std::ostringstream oss;
    const auto& msg = it->second;
    bool first = true;
    for(const auto& sig : msg.signals){
        uint64_t raw = extract_signal(frame.data.data(), sig.start_bit, sig.size, sig.little_endian);
        int64_t s = sig.is_signed ? sign_extend(raw, sig.size) : (int64_t)raw;
        double value = s * sig.factor + sig.offset;
        if(!first) oss << " ";
        first = false;
        oss << sig.name << ": ";
        auto v = sig.value_map.find(s);
        if(v != sig.value_map.end()) oss << v->second;
        else oss << value;
    }
    out = oss.str();
    return true;
}
