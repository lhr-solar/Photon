#pragma once
#include <string>

#include "arena.hpp"

enum class DBCType : uint32_t {
  HighNoonTelemetry = 0,
  HighNoonAWS,
  Lonestar,
  DaybreakMaster,
  Test,
  AssettoCorsa,
  File,
};

struct Parse {
  Arena arena{};
  DBCType activeDBC = DBCType::HighNoonAWS;
  std::string activeDBCLabel = "High Noon AWS Telemetry";
  std::string activeDBCPath = {};
  void init();
  bool loadDBC(DBCType kind);
  bool loadDBCFile(const std::string& path);
  void destroy();

  static constexpr uint32_t dbcCount() { return 7; }
  static const char* dbcName(DBCType kind);
  const char* currentDBCName() const;
};
