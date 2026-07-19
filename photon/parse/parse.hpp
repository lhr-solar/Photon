#pragma once
#include <string>

#include "arena.hpp"

enum class DBCType : uint32_t {
  HighNoon = 0,
  AssettoCorsa,
  File,
};

struct Parse {
  Arena arena{};
  DBCType activeDBC = DBCType::HighNoon;
  std::string activeDBCLabel = "HighNoon";
  std::string activeDBCPath = {};
  void init();
  bool loadDBC(DBCType kind);
  bool loadDBCFile(const std::string& path);
  void destroy();

  static constexpr uint32_t dbcCount() { return 3; }
  static const char* dbcName(DBCType kind);
  const char* currentDBCName() const;
};
