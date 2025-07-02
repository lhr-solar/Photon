#pragma once

#include "candb.hpp"
#include "dbc.hpp"
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

int backend(int argc, char* argv[]);

const CanStore& get_can_store();
void clear_can_store();

bool backend_decode(uint32_t id, const CanFrame& frame, std::string& out);
bool backend_decode_sep(uint32_t id, const CanFrame& frame, std::string& out, const char* sep);
bool backend_decode_signals(uint32_t id, const CanFrame& frame, std::vector<std::pair<std::string, double>> &out);
std::unordered_map<uint32_t, DbcMessage> backend_get_messages();

void forward_serial_source(std::string& fd, std::string& baud);
void forward_tcp_source(std::string& fd, std::string& port);
void kill_data_source();
bool is_data_source_connected();
bool read_update_avail();

void forward_dbc_load(const std::string& path);
void forward_dbc_unload(const std::string& path);
std::vector<std::string> get_loaded_dbcs();


void forward_builtin_dbc_load(const std::string& name);
void forward_builtin_dbc_unload(const std::string& name);
std::vector<std::pair<std::string,bool>> list_builtin_dbcs();
