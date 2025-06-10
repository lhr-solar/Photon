#include "candb.hpp"
#include <cstring>

void CanStore::store(IdType id, uint8_t len, const uint8_t* payload){
    if(id >= MAX_IDS || !payload || len > 8)
        return;
    Entry &e = _entries[id];
    std::lock_guard<std::mutex> lock(e.mtx);
    e.frame.len = len;
    std::copy(payload, payload + len, e.frame.data.begin());
    if (len < 8)
        std::fill(e.frame.data.begin() + len, e.frame.data.end(), 0);
    e.valid = true;
}

bool CanStore::read(IdType id, CanFrame& out) const{
    if(id >= MAX_IDS)
        return false;
    const Entry &e = _entries[id];
    std::lock_guard<std::mutex> lock(e.mtx);
    if(!e.valid)
        return false;
    out = e.frame;
    return true;
}
