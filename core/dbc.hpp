#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "candb.hpp"

struct DbcSignal {
    std::string name;
    uint16_t start_bit = 0;
    uint8_t size = 0;
    bool little_endian = true;
    bool is_signed = false;
    double factor = 1.0;
    double offset = 0.0;
    std::unordered_map<int64_t, std::string> value_map;
};

struct DbcMessage {
    uint32_t id = 0;
    std::string name;
    uint8_t dlc = 0;
    std::vector<DbcSignal> signals;
};

class DbcParser {
public:
    bool load(const std::string& path);
    bool loadFromMemory(const char* data, size_t size);
    bool decode(uint32_t id, const CanFrame& frame, std::string& out) const;
    void can_parse_debug();

private:
    uint64_t extract_signal(const uint8_t* data, uint16_t start, uint8_t size, bool little_endian) const;
    int64_t sign_extend(uint64_t val, unsigned bits) const;

    std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> _value_tables;
    std::unordered_map<uint32_t, DbcMessage> _messages;
};

// we currently have 3 unordered maps
// value_map
// _value_tables
// _messages
