#pragma once
#include <string>

#include "arena.hpp"

enum class DBCKind : uint32_t {
  VehicleWithUndisclosedName = 0,
  DaybreakMaster,
  Test,
  AssettoCorsa,
  File,
};

struct Parse {
  Arena arena{};
  DBCKind activeDBC = DBCKind::VehicleWithUndisclosedName;
  std::string activeDBCLabel = "vehicle-with-undisclosed-name";
  std::string activeDBCPath{};
  void init();
  bool loadDBC(DBCKind kind);
  bool loadDBCFile(const std::string& path);
  void destroy();

  static constexpr uint32_t dbcCount() { return 5; }
  static const char* dbcName(DBCKind kind);
  const char* currentDBCName() const;
};
