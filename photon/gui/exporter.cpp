#include "exporter.hpp"
#include "../engine/include.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>

void Exporter::toFile(Arena& arena, std::filesystem::path filePath) {
  running.store(true);
  std::ofstream fileStream{filePath};
  if (!fileStream) return;
  fileStream << std::format("message_id,message_name,signal_name,time,value\n");
  for (const uint32_t message_id : arena.validIds) {
    if (message_id >= arena.messages.size() || !arena.messages[message_id]) continue;
    const auto& message = arena.messages[message_id];
    const std::string& message_name = message->name;
    for (uint32_t sg = 0; sg < message->signalCount; sg++) {
      const std::string signal_name = message->signals[sg]->name;
      uint32_t size{};
      void* data;
      arena.read(message_id, sg, &data, &size);
      uint32_t tSize{};
      void* tData;
      arena.readTime(message_id, &tData, &tSize);
      uint32_t adjSize = size / sizeof(double);
      for (int i = 0; i < adjSize; i++) {
        double val = ((double*)data)[i];
        double time = ((double*)tData)[i];
        fileStream << std::format("{},{},{},{},{}\n",
                        message_id, message_name, signal_name, time, val);
      };
    };
  };
  running.store(false);
};
