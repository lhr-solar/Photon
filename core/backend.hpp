#pragma once

#include "candb.hpp"
#include <string>

int backend(int argc, char* argv[]);

const CanStore& get_can_store();

bool backend_decode(uint32_t id, const CanFrame& frame, std::string& out);
