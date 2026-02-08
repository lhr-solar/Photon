#pragma once

#include <string>
#include <vector>

struct Network;

struct Plot{
    int canID;
    std::string windowName;
    std::string plotName;
    std::vector<std::vector<double>> data;
    double minValue = 0;
    double maxValue = 1;
    Plot(int canID, const char* windowName, const char* plotName);
    void update(Network* networkSource);
};

