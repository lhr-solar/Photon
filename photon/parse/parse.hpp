#pragma once
#include <string>

#include "arena.hpp"

enum class DBCType : uint32_t {
  HighNoonTelemetry = 0,
  Lonestar,
  DaybreakMaster,
  Test,
  AssettoCorsa,
  File,
};

struct Parse {
  Arena arena{};
  DBCType activeDBC = DBCType::HighNoonTelemetry;
  std::string activeDBCLabel = "High Noon Telemetry";
  std::string activeDBCPath = {};
  void init();
  bool loadDBC(DBCType kind);
  bool loadDBCFile(const std::string& path);
  void destroy();

  static constexpr uint32_t dbcCount() { return 6; }
  static const char* dbcName(DBCType kind);
  const char* currentDBCName() const;
};
