#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <functional>
#include <cstdint>

struct SignalKeyHash {
    size_t operator()(const std::pair<uint32_t, std::string>& k) const noexcept {
        return std::hash<uint32_t>{}(k.first) ^ (std::hash<std::string>{}(k.second) << 1);
    }
};

using SignalKey = std::pair<uint32_t, std::string>;
using PlotMap = std::unordered_map<SignalKey, std::string, SignalKeyHash>;

struct DbcPlotRegistry {
    std::unordered_map<std::string, PlotMap> dbcs;

    void register_plot(const std::string& dbc, uint32_t id, const std::string& signal, const std::string& plot) {
        dbcs[dbc][SignalKey(id, signal)] = plot;
    }

    std::string get_plot(const std::string& dbc, uint32_t id, const std::string& signal) const {
        auto dit = dbcs.find(dbc);
        if(dit == dbcs.end())
            return {};
        auto it = dit->second.find(SignalKey(id, signal));
        if(it == dit->second.end())
            return {};
        return it->second;
    }
};

extern DbcPlotRegistry g_plot_registry;
void init_default_plot_registry();

using PlotSignal = std::pair<std::string, std::string>;
using SignalDataMap = std::unordered_map<std::string, std::vector<double>>;
using PlotDrawFn = std::function<void(const std::string&, const std::vector<PlotSignal>&, const SignalDataMap&, const SignalDataMap&)>;

struct PlotDrawerRegistry {
    std::unordered_map<std::string, PlotDrawFn> drawers;
    PlotDrawFn default_drawer;

    PlotDrawerRegistry();
    void register_drawer(const std::string& name, PlotDrawFn fn);
    PlotDrawFn get_drawer(const std::string& name) const;
};

extern PlotDrawerRegistry g_plot_drawers;
void init_default_plot_drawers();
