#pragma once

#include <array>
#include <cstdint>
#include <mutex>

struct CanFrame {
    uint8_t len = 0;
    std::array<uint8_t, 8> data{};
};

class CanStore {
    public: 
        static constexpr std::size_t MAX_IDS = 4096;
        using IdType = std::uint16_t;

        CanStore() = default;

        void store(IdType id, uint8_t len, const uint8_t* payload);
        bool read(IdType id, CanFrame& out) const;

    private:
        struct Entry{
            CanFrame frame;
            bool valid = false;
            mutable std::mutex mtx;
        };

        std::array<Entry, MAX_IDS> _entries{};
};
