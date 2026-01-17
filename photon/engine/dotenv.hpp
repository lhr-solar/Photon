#pragma once
#include <fstream>
#include <string>
#include <cstdlib>

inline void load_dotenv(const char* path = ".env")
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

    #ifdef _WIN32
        _putenv_s(key.c_str(), val.c_str());
    #else
        setenv(key.c_str(), val.c_str(), 1);
    #endif
    }
}
