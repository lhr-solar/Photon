#include "dbc_manager.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <cctype>
#include <utility>

namespace {

int dbcMessageKey(uint32_t rawId) {
    // Vector DBCs encode extended-frame flags in the high bits. Keep the bus ID
    // portion for lookups so flagged messages can still match incoming CAN IDs.
    if ((rawId & 0xE0000000u) != 0) {
        return static_cast<int>(rawId & 0x1FFFFFFFu);
    }
    return static_cast<int>(rawId);
}

std::string unescapeDbcString(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    bool escaped = false;
    for (char c : raw) {
        if (escaped) {
            out.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string quotedTextFromLine(const std::string& line) {
    size_t firstQuote = line.find('"');
    if (firstQuote == std::string::npos) {
        return {};
    }

    size_t pos = firstQuote + 1;
    bool escaped = false;
    while (pos < line.size()) {
        char c = line[pos];
        if (escaped) {
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            return unescapeDbcString(line.substr(firstQuote + 1, pos - firstQuote - 1));
        }
        ++pos;
    }

    return unescapeDbcString(line.substr(firstQuote + 1));
}

std::unordered_map<int64_t, std::string> parseValueDescriptions(const std::string& text) {
    std::unordered_map<int64_t, std::string> descriptions;
    size_t pos = 0;

    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size() || text[pos] == ';') {
            break;
        }

        size_t valueStart = pos;
        if (text[pos] == '-' || text[pos] == '+') {
            ++pos;
        }
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos == valueStart || (pos == valueStart + 1 && (text[valueStart] == '-' || text[valueStart] == '+'))) {
            break;
        }

        int64_t value = 0;
        try {
            value = std::stoll(text.substr(valueStart, pos - valueStart));
        } catch (...) {
            break;
        }

        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (pos >= text.size() || text[pos] != '"') {
            break;
        }

        ++pos;
        bool escaped = false;
        std::string description;
        while (pos < text.size()) {
            char c = text[pos++];
            if (escaped) {
                description.push_back(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                break;
            } else {
                description.push_back(c);
            }
        }
        descriptions[value] = description;
    }

    return descriptions;
}

bool attachSignalValueDescriptions(std::unordered_map<int, DbcMessage>& dbcMap,
                                   int messageKey,
                                   const std::string& sigName,
                                   std::unordered_map<int64_t, std::string> descriptions) {
    auto it = dbcMap.find(messageKey);
    if (it != dbcMap.end()) {
        auto sit = it->second.signals.find(sigName);
        if (sit != it->second.signals.end()) {
            sit->second.valueDescriptions = std::move(descriptions);
            return true;
        }
    }

    for (auto& [unusedId, msg] : dbcMap) {
        (void)unusedId;
        auto sit = msg.signals.find(sigName);
        if (sit != msg.signals.end()) {
            sit->second.valueDescriptions = std::move(descriptions);
            return true;
        }
    }

    return false;
}

bool attachSignalComment(std::unordered_map<int, DbcMessage>& dbcMap,
                         int messageKey,
                         const std::string& sigName,
                         std::string comment) {
    auto it = dbcMap.find(messageKey);
    if (it != dbcMap.end()) {
        auto sit = it->second.signals.find(sigName);
        if (sit != it->second.signals.end()) {
            sit->second.comment = std::move(comment);
            return true;
        }
    }

    for (auto& [unusedId, msg] : dbcMap) {
        (void)unusedId;
        auto sit = msg.signals.find(sigName);
        if (sit != msg.signals.end()) {
            sit->second.comment = std::move(comment);
            return true;
        }
    }

    return false;
}

std::string describeValue(const DbcSignal& sig, double value) {
    if (!std::isfinite(value)) {
        return {};
    }

    double rounded = std::round(value);
    if (std::fabs(value - rounded) > 0.000001) {
        return {};
    }

    auto it = sig.valueDescriptions.find(static_cast<int64_t>(rounded));
    if (it == sig.valueDescriptions.end()) {
        return {};
    }

    return it->second;
}

}

// ---------- DbcManager Implementation ----------

bool DbcManager::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[DBC Loader] Failed to open " << path << "\n";
        return false;
    }

    std::string line;
    int currentId = 0;
    bool haveCurrentMessage = false;
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

            int messageKey = dbcMessageKey(canId);

            {
                std::lock_guard<std::mutex> lock(mapMutex);
                DbcMessage& msg = dbcMap[messageKey];
                msg.canId = messageKey;
                msg.name = name;
                msg.dlc = dlc;
                msg.transmitter = sender;
                msg.signals.clear();
            }

            currentId = messageKey;
            haveCurrentMessage = true;
            messageCountLocal++;
            std::cerr << "[DBC] Registered message: " << name
                      << " (ID=" << messageKey << ")\n";
        }

        // --- Parse SG_ lines ---
        else if (line.rfind("SG_", 0) == 0 && haveCurrentMessage) {
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
                auto it = dbcMap.find(currentId);
                if (it != dbcMap.end())
                    it->second.signals[sigName] = sig;
            }

            signalCountLocal++;
            std::cerr << "[DBC] Registered signal: " << sigName
                      << " (ID=" << currentId << ")\n";
        }

        // --- Parse VAL_ lines (integer value descriptions) ---
        // Format: VAL_ <can_id> <signal_name> <value> "description" ... ;
        else if (line.rfind("VAL_", 0) == 0 && line.rfind("VAL_TABLE_", 0) != 0) {
            std::istringstream iss(line);
            std::string tag, sigName;
            uint32_t mid = 0;
            iss >> tag >> mid >> sigName;

            std::string rest;
            std::getline(iss, rest);
            auto descriptions = parseValueDescriptions(rest);
            if (descriptions.empty()) {
                continue;
            }

            std::lock_guard<std::mutex> lock(mapMutex);
            attachSignalValueDescriptions(dbcMap, dbcMessageKey(mid), sigName, std::move(descriptions));
        }

        // --- Parse signal comments ---
        // Format: CM_ SG_ <can_id> <signal_name> "comment";
        else if (line.rfind("CM_ SG_", 0) == 0) {
            std::istringstream iss(line);
            std::string tag, sgTag, sigName;
            uint32_t mid = 0;
            iss >> tag >> sgTag >> mid >> sigName;

            std::string comment = quotedTextFromLine(line);
            if (comment.empty()) {
                continue;
            }

            std::lock_guard<std::mutex> lock(mapMutex);
            attachSignalComment(dbcMap, dbcMessageKey(mid), sigName, std::move(comment));
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
            auto it = dbcMap.find(dbcMessageKey(mid));
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

std::string DbcManager::describeSignalValue(const std::string& sigName, double value) {
    std::lock_guard<std::mutex> lock(mapMutex);
    for (const auto& [unusedId, msg] : dbcMap) {
        (void)unusedId;
        auto sit = msg.signals.find(sigName);
        if (sit == msg.signals.end()) {
            continue;
        }

        std::string description = describeValue(sit->second, value);
        if (!description.empty()) {
            return description;
        }
    }

    return {};
}

std::string DbcManager::signalComment(const std::string& sigName) {
    std::lock_guard<std::mutex> lock(mapMutex);
    for (const auto& [unusedId, msg] : dbcMap) {
        (void)unusedId;
        auto sit = msg.signals.find(sigName);
        if (sit != msg.signals.end() && !sit->second.comment.empty()) {
            return sit->second.comment;
        }
    }

    return {};
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
                      << " values=" << s.valueDescriptions.size()
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
