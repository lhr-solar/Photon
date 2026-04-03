#include "parse.hpp"
#include "arena.hpp"
#include "../engine/include.hpp"

void Parse::init(){
    // take in a DBC file
    // parse it, and use the parsed input to create the arena config
    // then init the arena
    std::array<uint32_t, MESSAGE_MAX> dataLengths{8};
    std::array<uint32_t, MESSAGE_MAX> signalCounts{32, 32, 32, 32, // real is 356 total
                                                   32, 32, 32, 32,
                                                   32, 32, 32, 32};
    std::vector<uint32_t> validIds = {0x000, 0x001, 0x002, 0x003, 
                                      0x004, 0x005, 0x006, 0x007, 
                                      0x008, 0x009, 0x00A, 0x00B};
    arenaConfig config{
        .arenaSize = MINIMUM_ARENA_SIZE * 8,
        .dataLengths = dataLengths,
        .signalCounts = signalCounts,
        .validIds = validIds,
    }; arena.init(config);
};
