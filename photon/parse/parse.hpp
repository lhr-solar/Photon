#pragma once
#include "arena.hpp"
#include <string>

enum class DBCKind : uint32_t {
    VehicleWithUndisclosedName = 0,
    DaybreakMaster,
    Test,
    File,
};

struct Parse{
    Arena arena{};
    DBCKind activeDBC = DBCKind::VehicleWithUndisclosedName;
    std::string activeDBCLabel = "vehicle-with-undisclosed-name";
    std::string activeDBCPath{};
    void init();
    bool loadDBC(DBCKind kind);
    bool loadDBCFile(const std::string& path);
    void destroy();

    static constexpr uint32_t dbcCount(){ return 4; }
    static const char* dbcName(DBCKind kind);
    const char* currentDBCName() const;
};
