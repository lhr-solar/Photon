#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <windows.h>

struct Updater {
    std::atomic<int> photonDownloadPercentage{-1};
    std::atomic<int> installerDownloadPercentage{-1};
    std::atomic<bool> running{false};

    std::filesystem::path installerPath =
        L"C:\\Users\\romer\\Documents\\code\\updater\\a.exe";

    std::string installerURL{};

    std::filesystem::path photonPath =
        L"C:\\Users\\romer\\Downloads\\photon.exe";

    std::string photonURL =
        "https://github.com/lhr-solar/Photon/releases/download/Win_Pre-Release/Photon.exe";

    DWORD ourPid{};
    std::wstring ourPath{};

    std::string version = "00.00.01";

    void getOurInfo();
    bool downloadInstaller();
    bool downloadNewPhoton();
    void launchInstaller();
    void launchUpdater();
    void beginUpdate();
};
