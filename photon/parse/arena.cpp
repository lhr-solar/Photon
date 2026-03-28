#include "arena.hpp"

void Arena::init(const arenaConfig& config){
    if(config.messageCount == 0) return;
    for(auto& m : messages) destroy(m->id);
    arenaSize = config.arenaSize;
    double sizePerMessage = (double)arenaSize / messages.size();
    // the minimum arena size is Page Size * MESSAGE_MAX * 32 == 250 MB
    // e.g. one page per signal
    // the amount of memory a message is given is dependent on the number of signals in that msg
    // it should also be page aligned

    // what do we need to allocate?
    // allocate memory per signal of course!
    // every signal is of size, sizeof(double) == 8
    for(const auto& idx : config.validIds){
        Message& Message = *messages[idx];
    }
};

void Arena::destroy(uint32_t id){ delete(messages[id]); }
