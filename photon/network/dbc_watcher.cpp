#include "dbc_watcher.hpp"
#include <iostream>

DbcWatcher::DbcWatcher(Network& net, const std::string& directory, int intervalSeconds)
    : network(net), directoryPath(directory), pollInterval(intervalSeconds), running(false) {}

DbcWatcher::~DbcWatcher() {
    stop();
}

void DbcWatcher::start() {
    if (running.load()) return;
    running.store(true);
    workerThread = std::thread(&DbcWatcher::monitorLoop, this);
    std::cerr << "[DBC Watcher] Monitoring started on: " << directoryPath << "\n";
}

void DbcWatcher::stop() {
    running.store(false);
    if (workerThread.joinable())
        workerThread.join();
    std::cerr << "[DBC Watcher] Monitoring stopped.\n";
}

void DbcWatcher::monitorLoop() {
    using namespace std::chrono_literals;

    while (running.load()) {
        try {
            for (const auto& entry : fs::directory_iterator(directoryPath)) {
                if (!entry.is_regular_file()) continue;

                std::string path = entry.path().string();
                if (entry.path().extension() != ".dbc") continue;

                auto lastWrite = fs::last_write_time(entry.path());

                // ✅ replaced .contains with .find for C++17 compatibility
                if (fileTimestamps.find(path) == fileTimestamps.end() || fileTimestamps[path] != lastWrite) {
                    fileTimestamps[path] = lastWrite;
                    std::cerr << "[DBC Watcher] Detected new or modified file: " << path << "\n";

                    if (network.loadDBC(path)) {
                        std::cerr << "[DBC Watcher] Loaded DBC: " << path << "\n";
                    } else {
                        std::cerr << "[DBC Watcher] Failed to load DBC: " << path << "\n";
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[DBC Watcher] Error: " << e.what() << "\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(pollInterval));
    }
}
