#include "include.hpp"
#include "photon.hpp"
#include "arena.hpp"

int main() {
    logs("Starting");
    Photon photon;
    Arena arena{};
    std::array<uint32_t, MESSAGE_MAX> dataLengths{8};
    std::array<uint32_t, MESSAGE_MAX> signalCounts{16};
    std::vector<uint32_t> validIds = {0x000};
    arenaConfig config{
        .arenaSize = MINIMUM_ARENA_SIZE,
        .dataLengths = dataLengths,
        .signalCounts = signalCounts,
        .validIds = validIds,
    };
    arena.init(config);
    /*
    std::cout << std::hardware_destructive_interference_size << ::std::endl;

    photon.init();
    photon.renderLoop();
    photon.destroy();
    */
    //for(const auto& id : validIds) arena.destroy(id);
    arena.destroy(0);
    return 0;
}
