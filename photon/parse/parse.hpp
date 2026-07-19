#pragma once
#include <string>

#include "arena.hpp"

enum class DBCType : uint32_t {
  Lonestar = 0,
  DaybreakMaster,
  Test,
  AssettoCorsa,
  CarCAN,
  File,
};

struct Parse {
  Arena arena{};
  // The kart needs the complete car-facing bundle. CarCAN alone omits BPS,
  // motor-controller, steering, and DAQ frames such as MC_VelocityMeasurement.
  DBCType activeDBC = DBCType::Lonestar;
  std::string activeDBCLabel = "Lonestar";
  std::string activeDBCPath = {};
  void init();
  bool loadDBC(DBCType kind);
  bool loadDBCFile(const std::string& path);
  void destroy();

  static constexpr uint32_t dbcCount() { return 6; }
  static const char* dbcName(DBCType kind);
  const char* currentDBCName() const;
};
