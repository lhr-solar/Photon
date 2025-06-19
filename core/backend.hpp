#pragma once

#include "candb.hpp"
#include <string>

int backend(int argc, char* argv[]);

const CanStore& get_can_store();

bool backend_decode(uint32_t id, const CanFrame& frame, std::string& out);

void forward_serial_source(std::string& fd, std::string& baud);
void forward_tcp_source(std::string& fd, std::string& port);
void kill_data_source();
