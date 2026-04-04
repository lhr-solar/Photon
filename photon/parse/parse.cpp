#include "parse.hpp"
#include "arena.hpp"
#include "../engine/include.hpp"
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
std::string filePath = "assets/dbc/vehicle-with-undisclosed-name.dbc";
void Parse::init(){
    arenaConfig config{};
    buildConfig(filePath, config);
    arena.init(config);
    populateArena(filePath);
};

void Parse::buildConfig(const std::string& path, arenaConfig& config){
    std::vector<uint32_t> validIds{};
    std::array<uint32_t, MESSAGE_MAX> signalCounts{};
    std::ifstream stream(path); std::string line;
    uint32_t currentId{}, haveMsg{};
    while(std::getline(stream, line)){
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if(line.empty()) continue;
        if(line.rfind("BO_", 0) == 0){
            uint32_t canId{}, dlc{};
            std::string name{}, sender{}, tag{}, tmp{}, dlcStr{};
            std::istringstream iss(line);
            iss >> tag >> canId >> tmp >> dlcStr >> sender;
            if(canId > MESSAGE_MAX) continue;
            auto colon = tmp.find(':');
            if(colon == std::string::npos) continue;
            name = tmp.substr(0, colon);
            try{ dlc = static_cast<uint8_t>(std::stoi(dlcStr));
            } catch (...) { continue; }
            haveMsg = true;
            currentId = canId;
            validIds.push_back(canId);
        } else
        if(line.rfind("SG_ ", 0) == 0 && haveMsg) signalCounts[currentId]++;
    };
    config = {
        .arenaSize = MINIMUM_ARENA_SIZE * 8,
        .signalCounts = signalCounts,
        .validIds = validIds,
    };
};

// assumes that the arena is already in the correct form with allocations
// parses the file and populate the fields
void Parse::populateArena(const std::string& path){
    std::ifstream stream(path); std::string line;
    uint32_t currentId{}, currentIndex{};
    bool haveCurrentMessage = false;
    while(std::getline(stream, line)){
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if(line.empty()) continue;
        if(line.rfind("BO_", 0) == 0){
            uint32_t canId{}, dlc{};
            std::string name{}, sender{}, tag{}, tmp{}, dlcStr{};
            std::istringstream iss(line);
            iss >> tag >> canId >> tmp >> dlcStr >> sender;
            if(canId >= MESSAGE_MAX || !arena.messages[canId]) continue;
            auto colon = tmp.find(':');
            if(colon == std::string::npos) continue;
            name = tmp.substr(0, colon);
            try{ dlc = static_cast<uint8_t>(std::stoi(dlcStr));
            } catch (...) { continue; }
            currentId = canId;
            haveCurrentMessage = true;
            currentIndex = 0;
            Message* msg = arena.messages[canId];
            msg->id  = canId;
            msg->dlc = dlc;
            msg->name = name;
            msg->transmitter = sender;
        }else 
        if(line.rfind("SG_ ", 0) == 0 && haveCurrentMessage){
            Message* msg = arena.messages[currentId];
            if(!msg || currentIndex >= msg->signalCount) continue;
            Signal* sig = msg->signals[currentIndex++];
            std::istringstream iss(line);
            std::string tag{}, sigName{}; char c{};
            iss >> tag >> sigName;
            sig->name = sigName;
            while(iss >> c && c != ':'){}
            if(c != ':') continue;
            iss >> sig->startBit;
            iss.ignore(1, '|');
            iss >> sig->length;
            iss.ignore(1, '@');
            iss >> sig->endianness >> c;
            sig->isSigned = (c == '-');
            if(iss >> c && c == '('){
                iss >> sig->scale;
                iss.ignore(1, ',');
                iss >> sig->offset;
                iss.ignore(1, ')');
            }
            if(iss >> c && c == '['){
                iss >> sig->min;
                iss.ignore(1, '|');
                iss >> sig->max;
                iss.ignore(1, ']');
            }
            if(iss >> std::ws && iss.peek() == '"'){
                iss.get();
                std::getline(iss, sig->unit, '"');
            }
            if(iss >> std::ws) iss >> sig->receiver;
        }else 
        if(line.rfind("SIG_VALTYPE_", 0) == 0){
            std::istringstream iss(line);
            std::string tag{}, sigName{}, colon{}, typeStr{};
            uint32_t canId{}, rawType{};
            iss >> tag >> canId >> sigName >> colon >> typeStr;
            if(canId >= MESSAGE_MAX || !arena.messages[canId] || typeStr.empty()) continue;
            if(typeStr.back() == ';') typeStr.pop_back();
            try{ rawType = static_cast<uint32_t>(std::stoi(typeStr));
            } catch (...) { continue; }
            datatype type = vINT;
            if(rawType == 1) type = vFLOAT;
            if(rawType == 2) type = vDOUBLE;
            Message* msg = arena.messages[canId];
            for(size_t i{0uz}; i < msg->signalCount; i++)
                if(msg->signals[i] && msg->signals[i]->name == sigName){
                    msg->signals[i]->type = type;
                    break;
                }
        }
    };
};

void Parse::destroy(){
    arena.destroy();
};
