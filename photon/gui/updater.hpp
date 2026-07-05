#pragma once
#include <atomic>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
using UpdaterProcessId = DWORD;
#else
using UpdaterProcessId = unsigned long;
#endif

struct Updater {
  std::atomic<int> photonDownloadPercentage{-1};
  std::atomic<int> installerDownloadPercentage{-1};
  std::atomic<bool> running{false};

  std::filesystem::path installerPath{};
  std::string installerURL =
      "https://github.com/lhr-solar/Photon/releases/download/windowsUpdater/photonUpdater.exe";

  std::filesystem::path photonPath{};
  std::string photonURL =
      "https://github.com/lhr-solar/Photon/releases/download/Win_Pre-Release/Photon.exe";

  UpdaterProcessId ourPid{};
  std::wstring ourPath{};

  std::string version = "00.00.01";
  std::string newVersion = "00.00.02";

  void getOurInfo();
  bool downloadInstaller();
  bool downloadNewPhoton();
  bool launchInstaller();
  void launchUpdater();
  void beginUpdate();
  void drawUI(bool updateAvailable);
};
