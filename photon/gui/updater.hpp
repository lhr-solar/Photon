#pragma once
#include <string>

struct Updater{
    int photonDownloadPercentage{};
    int installerDownloadPercentage{};
    std::string photonPath{};
    std::string installerPath = "C:\\Users\\romer\\Documents\\code\\updater\\a.exe";
    int ourPid{};
    int ourPath{};
    std::string version = "00.00.01";
    void getOurInfo();
    void downloadInstaller();
    void downloadNewPhoton();
    void launchInstaller();
};
