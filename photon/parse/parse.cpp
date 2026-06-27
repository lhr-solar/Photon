#include "parse.hpp"

#include <array>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>

#include "../engine/include.hpp"
#include "arena.hpp"
#include "assettoCorsa_dbc.hpp"
#include "daybreak_master_dbc.hpp"
#include "test_dbc.hpp"
#include "lonestar_dbc.hpp"

struct DBCAsset {
  DBCType kind;
  const char* name;
  const unsigned char* data;
  std::size_t size;
};

const std::array<DBCAsset, Parse::dbcCount()> kDBCAssets{{
    {DBCType::Lonestar, "Lonestar",
     lonestar_dbc, lonestar_dbc_size},
    {DBCType::DaybreakMaster, "daybreak-master", daybreak_master_dbc, daybreak_master_dbc_size},
    {DBCType::Test, "test", test_dbc, test_dbc_size},
    {DBCType::AssettoCorsa, "assettoCorsa", assettoCorsa_dbc, assettoCorsa_dbc_size},
    {DBCType::File, "selected-file", nullptr, 0},
}};

const DBCAsset* dbcAsset(DBCType kind) {
  for (const DBCAsset& asset : kDBCAssets)
    if (asset.kind == kind) return &asset;
  return nullptr;
}

void buildConfig(std::istream& stream, arenaConfig& config) {
  std::vector<uint32_t> validIds{};
  std::array<uint32_t, MESSAGE_MAX> signalCounts{};
  std::string line;
  uint32_t currentId = 0;
  bool haveMsg = false;

  while (std::getline(stream, line)) {
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    if (line.empty()) continue;
    if (line.rfind("BO_", 0) == 0) {
      haveMsg = false;
      uint32_t canId = 0;
      uint32_t dlc = 0;
      std::string sender{};
      std::string tag{};
      std::string tmp{};
      std::string dlcStr{};
      std::istringstream iss(line);
      iss >> tag >> canId >> tmp >> dlcStr >> sender;
      if (canId >= MESSAGE_MAX) continue;
      if (tmp.find(':') == std::string::npos) continue;
      try {
        dlc = static_cast<uint8_t>(std::stoi(dlcStr));
      } catch (...) {
        continue;
      }
      (void)dlc;
      haveMsg = true;
      currentId = canId;
      validIds.push_back(canId);
    } else if (line.rfind("SG_ ", 0) == 0 && haveMsg) {
      signalCounts[currentId]++;
    }
  }

  config = {
      .arenaSize = MINIMUM_ARENA_SIZE * 8,
      .signalCounts = signalCounts,
      .validIds = validIds,
  };
}

void populateArena(Arena& arena, std::istream& stream) {
  std::string line;
  uint32_t currentId = 0;
  uint32_t currentIndex = 0;
  bool haveCurrentMessage = false;

  while (std::getline(stream, line)) {
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    if (line.empty()) continue;
    if (line.rfind("BO_", 0) == 0) {
      haveCurrentMessage = false;
      uint32_t canId = 0;
      uint32_t dlc = 0;
      std::string sender{};
      std::string tag{};
      std::string tmp{};
      std::string dlcStr{};
      std::istringstream iss(line);
      iss >> tag >> canId >> tmp >> dlcStr >> sender;
      if (canId >= MESSAGE_MAX || !arena.messages[canId]) continue;
      const auto colon = tmp.find(':');
      if (colon == std::string::npos) continue;
      try {
        dlc = static_cast<uint8_t>(std::stoi(dlcStr));
      } catch (...) {
        continue;
      }

      currentId = canId;
      haveCurrentMessage = true;
      currentIndex = 0;
      Message* msg = arena.messages[canId];
      msg->id = canId;
      msg->dlc = dlc;
      msg->name = tmp.substr(0, colon);
      msg->transmitter = sender;
    } else if (line.rfind("SG_ ", 0) == 0 && haveCurrentMessage) {
      Message* msg = arena.messages[currentId];
      if (!msg || currentIndex >= msg->signalCount) continue;
      Signal* sig = msg->signals[currentIndex++];
      std::istringstream iss(line);
      std::string tag{};
      std::string sigName{};
      char c = '\0';
      iss >> tag >> sigName;
      sig->name = sigName;
      while (iss >> c && c != ':') {
      }
      if (c != ':') continue;
      iss >> sig->startBit;
      iss.ignore(1, '|');
      iss >> sig->length;
      iss.ignore(1, '@');
      iss >> sig->endianness >> c;
      sig->isSigned = (c == '-');
      if (iss >> c && c == '(') {
        iss >> sig->scale;
        iss.ignore(1, ',');
        iss >> sig->offset;
        iss.ignore(1, ')');
      }
      if (iss >> c && c == '[') {
        iss >> sig->min;
        iss.ignore(1, '|');
        iss >> sig->max;
        iss.ignore(1, ']');
      }
      if (iss >> std::ws && iss.peek() == '"') {
        iss.get();
        std::getline(iss, sig->unit, '"');
      }
      if (iss >> std::ws) iss >> sig->receiver;
    } else if (line.rfind("SIG_VALTYPE_", 0) == 0) {
      std::istringstream iss(line);
      std::string tag{};
      std::string sigName{};
      std::string colon{};
      std::string typeStr{};
      uint32_t canId = 0;
      uint32_t rawType = 0;
      iss >> tag >> canId >> sigName >> colon >> typeStr;
      if (canId >= MESSAGE_MAX || !arena.messages[canId] || typeStr.empty()) continue;
      if (typeStr.back() == ';') typeStr.pop_back();
      try {
        rawType = static_cast<uint32_t>(std::stoi(typeStr));
      } catch (...) {
        continue;
      }

      datatype type = vINT;
      if (rawType == 1) type = vFLOAT;
      if (rawType == 2) type = vDOUBLE;
      Message* msg = arena.messages[canId];
      for (size_t i = 0; i < msg->signalCount; ++i)
        if (msg->signals[i] && msg->signals[i]->name == sigName) {
          msg->signals[i]->type = type;
          break;
        }
    }
  }
}

void Parse::init() { loadDBC(activeDBC); }

bool Parse::loadDBC(DBCType kind) {
  const DBCAsset* asset = dbcAsset(kind);
  if (!asset || !asset->data || asset->size == 0) return false;

  std::string dbcText(reinterpret_cast<const char*>(asset->data), asset->size);
  std::istringstream configStream(dbcText);
  arenaConfig config{};
  buildConfig(configStream, config);

  arena.destroy();
  arena.init(config);
  std::istringstream populateStream(dbcText);
  populateArena(arena, populateStream);
  activeDBC = kind;
  activeDBCLabel = asset->name;
  activeDBCPath.clear();
  return true;
}

bool Parse::loadDBCFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;

  std::string dbcText((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (dbcText.empty()) return false;

  std::istringstream configStream(dbcText);
  arenaConfig config{};
  buildConfig(configStream, config);

  arena.destroy();
  arena.init(config);
  std::istringstream populateStream(dbcText);
  populateArena(arena, populateStream);
  activeDBC = DBCType::File;
  activeDBCPath = path;
  const size_t slash = path.find_last_of("/\\");
  activeDBCLabel = slash == std::string::npos ? path : path.substr(slash + 1);
  return true;
}

void Parse::destroy() { arena.destroy(); }

const char* Parse::dbcName(DBCType kind) {
  const DBCAsset* asset = dbcAsset(kind);
  return asset ? asset->name : "unknown";
}

const char* Parse::currentDBCName() const { return activeDBCLabel.c_str(); }
