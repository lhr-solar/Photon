#pragma once
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>
#include "network.hpp"

namespace fs = std::filesystem;

class DbcWatcher {
public:
    DbcWatcher(Network& net, const std::string& directory, int intervalSeconds = 5);
    ~DbcWatcher();

    void start();
    void stop();

private:
    void monitorLoop();

    Network& network;
    std::string directoryPath;
    int pollInterval;
    std::atomic<bool> running;
    std::thread workerThread;

    // Keep track of last modification times
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps;
};
