#pragma once
#include <string>
#include "dbc_connector.hpp"

class DbcLoader {
public:
    // Parse a .dbc file and populate the given connector
    static bool loadFromFile(const std::string& path, DbcConnector& connector);
};
