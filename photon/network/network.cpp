#include "network.hpp"
#include "../engine/include.hpp"
#include <vector>
#include <thread>

void Network::initThreads(){
    writer = std::jthread([this](std::stop_token stopToken){
            std::vector<double> vec = {0.1, 1.2, 2.3, 4.5, 8.9, 16.2};
            while(!stopToken.stop_requested()){
            if(!arena->write(0x001, 2, vec.data(), vec.size() * sizeof(double))) arena->clear(0x001);
            for(auto& v : vec) v = (v + 1.1);
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
            }
            });
    reader = std::jthread([this](std::stop_token stopToken){
            int i = 0;
            while(!stopToken.stop_requested()){
            void* rec{};
            uint32_t size{};
            arena->read(0x001, 2, &rec, &size);
            if(i < size/sizeof(double)){logs(((double*)rec)[i]); i += 6;}
            else { i = 0; }
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
            }
            });
};

void Network::init(Arena* arena){
    this->arena = arena;
    initThreads();
};

void Network::destroy(){
    writer.request_stop();
    reader.request_stop();
    if(writer.joinable()) writer.join();
    if(reader.joinable()) reader.join();
};
