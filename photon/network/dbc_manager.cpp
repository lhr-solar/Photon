#include "dbc_manager.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

// ---------- DbcManager Implementation ----------

bool DbcManager::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[DBC Loader] Failed to open " << path << "\n";
        return false;
    }

    std::string line;
    uint32_t currentId = 0;
    int messageCountLocal = 0;
    int signalCountLocal = 0;

    // === Main parsing loop ===
    while (std::getline(file, line)) {
        // trim leading spaces/tabs
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty()) continue;

        // --- Parse BO_ lines ---
        if (line.rfind("BO_", 0) == 0) {
            uint32_t canId = 0;
            std::string name, sender;
            uint8_t dlc = 0;

            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> canId;

            std::string tmp;
            iss >> tmp;
            auto colon = tmp.find(':');
            if (colon == std::string::npos)
                continue;
            name = tmp.substr(0, colon);

            std::string dlcStr;
            iss >> dlcStr;
            try {
                dlc = static_cast<uint8_t>(std::stoi(dlcStr));
            } catch (...) {
                continue;
            }

            iss >> sender;

            {
                std::lock_guard<std::mutex> lock(mapMutex);
                DbcMessage& msg = dbcMap[static_cast<int>(canId)];
                msg.canId = static_cast<int>(canId);
                msg.name = name;
                msg.dlc = dlc;
                msg.transmitter = sender;
                msg.signals.clear();
            }

            currentId = canId;
            messageCountLocal++;
            std::cerr << "[DBC] Registered message: " << name
                      << " (ID=" << canId << ")\n";
        }

        // --- Parse SG_ lines ---
        else if (line.find("SG_") != std::string::npos && currentId != 0) {
            std::istringstream iss(line);
            std::string tag, sigName;
            iss >> tag >> sigName; // SG_ <name>

            DbcSignal sig{};
            char c = 0;

            // find the colon
            while (iss >> c) {
                if (c == ':') break;
            }
            if (c != ':') continue;

            // parse 0|32@1+
            iss >> sig.startBit;
            iss.ignore(1, '|');
            iss >> sig.length;
            iss.ignore(1, '@');
            iss >> sig.endianness;
            iss >> c;
            sig.isSigned = (c == '-');

            // parse (scale,offset)
            if (iss >> c && c == '(') {
                iss >> sig.scale;
                iss.ignore(1, ',');
                iss >> sig.offset;
                iss.ignore(1, ')');
            }

            // parse [min|max]
            if (iss >> c && c == '[') {
                iss >> sig.min;
                iss.ignore(1, '|');
                iss >> sig.max;
                iss.ignore(1, ']');
            }

            // parse "unit"
            if (iss >> std::ws && iss.peek() == '"') {
                iss.get(); // consume "
                std::getline(iss, sig.unit, '"');
            }

            // receiver (may or may not be in brackets)
            std::string receiver;
            if (iss >> std::ws) {
                if (iss.peek() == '[') {
                    iss.get(); // [
                    std::getline(iss, receiver, ']');
                } else {
                    iss >> receiver; // plain token (Vector__XXX)
                }
                sig.receiver = receiver;
            }

            {
                std::lock_guard<std::mutex> lock(mapMutex);
                auto it = dbcMap.find(static_cast<int>(currentId));
                if (it != dbcMap.end())
                    it->second.signals[sigName] = sig;
            }

            signalCountLocal++;
            std::cerr << "[DBC] Registered signal: " << sigName
                      << " (ID=" << currentId << ")\n";
        }

        // --- Parse SIG_VALTYPE_ lines (float / double signal annotations) ---
        // Format: SIG_VALTYPE_ <can_id> <signal_name> : <type>;
        //   type 1 = IEEE float32, type 2 = IEEE float64
        else if (line.rfind("SIG_VALTYPE_", 0) == 0) {
            std::istringstream iss(line);
            std::string tag, sigName;
            uint32_t mid = 0;
            int type = 0;
            iss >> tag >> mid >> sigName;
            char c = 0;
            while (iss >> c) {
                if (c == ':') break;
            }
            if (c != ':') continue;
            iss >> type;

            std::lock_guard<std::mutex> lock(mapMutex);
            auto it = dbcMap.find(static_cast<int>(mid));
            if (it != dbcMap.end()) {
                auto sit = it->second.signals.find(sigName);
                if (sit != it->second.signals.end()) {
                    sit->second.isFloat = (type == 1 || type == 2);
                }
            }
        }
    }

    // --- Summary + dump ---
    {
        std::lock_guard<std::mutex> lock(mapMutex);
        std::cerr << "[DBC Loader] Parsed " << messageCountLocal
                  << " messages and " << signalCountLocal
                  << " signals from " << path << "\n";
        std::cerr << "[DBC Loader] Current total messages in map: "
                  << dbcMap.size() << "\n";
    }

    dump();
    return (messageCountLocal > 0);
}

// ---------- Utility Methods ----------

bool DbcManager::hasMessages() {
    std::lock_guard<std::mutex> lock(mapMutex);
    return !dbcMap.empty();
}

size_t DbcManager::messageCount() {
    std::lock_guard<std::mutex> lock(mapMutex);
    return dbcMap.size();
}

void DbcManager::dump() {
    std::lock_guard<std::mutex> lock(mapMutex);
    std::cout << "========== DBC MAP ==========\n";
    for (const auto& [id, msg] : dbcMap) {
        std::cout << "CAN ID: " << id
                  << " | Name: " << msg.name
                  << " | DLC: " << msg.dlc
                  << " | Sender: " << msg.transmitter << "\n";
        for (const auto& [sigName, s] : msg.signals) {
            std::cout << "   SG_ " << sigName
                      << " start=" << s.startBit
                      << " len=" << s.length
                      << " endian=" << s.endianness
                      << (s.isSigned ? " signed" : " unsigned")
                      << "\n";
        }
    }
    std::cout << "=============================\n";
}

// ---------- DbcWatcher Implementation ----------

DbcWatcher::DbcWatcher(DbcManager& manager,
                       const std::string& directory,
                       int intervalSeconds)
    : dbcManager(manager),
      directoryPath(directory),
      pollInterval(intervalSeconds),
      running(false) {}

DbcWatcher::~DbcWatcher() {
    stop();
}

void DbcWatcher::start() {
    if (running.load()) return;
    running.store(true);
    workerThread = std::thread(&DbcWatcher::monitorLoop, this);
    std::cerr << "[DBC Watcher] Monitoring started on: "
              << directoryPath << "\n";
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
                if (entry.path().extension() != ".dbc") continue;

                std::string path = entry.path().string();
                auto lastWrite = fs::last_write_time(entry.path());

                if (fileTimestamps.find(path) == fileTimestamps.end()
                    || fileTimestamps[path] != lastWrite) {

                    fileTimestamps[path] = lastWrite;
                    std::cerr << "[DBC Watcher] Detected new/modified: "
                              << path << "\n";

                    if (dbcManager.loadFromFile(path))
                        std::cerr << "[DBC Watcher] Loaded DBC: "
                                  << path << "\n";
                    else
                        std::cerr << "[DBC Watcher] Failed to load: "
                                  << path << "\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[DBC Watcher] Error: " << e.what() << "\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(pollInterval));
    }
}
