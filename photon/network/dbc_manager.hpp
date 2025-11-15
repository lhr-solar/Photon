#pragma once
#include <string>
#include <unordered_map>
#include <filesystem>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

namespace fs = std::filesystem;

// ---------- DBC data structures ----------

struct DbcSignal {
    int startBit = 0;
    int length = 0;
    int endianness = 0;
    bool isSigned = false;
    double scale = 1.0;
    double offset = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::string unit;
    std::string receiver;
};


struct DbcMessage {
    int canId = 0;
    std::string name;
    int dlc = 0;
    std::string transmitter;
    std::unordered_map<std::string, DbcSignal> signals;
};

// ---------- DbcManager ----------

class DbcManager {
public:
    bool loadFromFile(const std::string& path);
    void dump();

    bool hasMessages();
    size_t messageCount();

    // Exposed for direct access if needed
    std::unordered_map<int, DbcMessage> dbcMap;
    std::mutex mapMutex;
};

// ---------- DbcWatcher ----------

class DbcWatcher {
public:
    DbcWatcher(DbcManager& manager, const std::string& directory, int intervalSeconds = 5);
    ~DbcWatcher();

    void start();
    void stop();

private:
    void monitorLoop();

    DbcManager& dbcManager;
    std::string directoryPath;
    int pollInterval;
    std::atomic<bool> running;
    std::thread workerThread;
    std::unordered_map<std::string, fs::file_time_type> fileTimestamps;
};
