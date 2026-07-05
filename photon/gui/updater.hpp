#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
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
  std::atomic<bool> releaseQueryStarted{false};
  std::atomic<bool> updateAvailable{false};
  mutable std::mutex releaseMutex;

  std::filesystem::path installerPath{};
  std::string installerURL =
      "https://github.com/lhr-solar/Photon/releases/download/windowsUpdater/photonUpdater.exe";

  std::filesystem::path photonPath{};
  std::string photonURL =
      "https://github.com/lhr-solar/Photon/releases/download/Win_Pre-Release/Photon.exe";

  UpdaterProcessId ourPid{};
  std::wstring ourPath{};

  std::string version = "00.00.00";
  std::string newVersion{}; 

  void getOurInfo();
  bool downloadInstaller();
  bool downloadNewPhoton();
  bool launchInstaller();
  void launchUpdater();
  void beginUpdate();
  void drawUI(bool updateAvailable);
  void queryReleaseInfoOnceAsync();
  void setReleaseInfo(std::string remoteVersion, std::string appURL, std::string updaterURL);
  void versionSnapshot(std::string& current, std::string& remote) const;
  void downloadSnapshot(std::string& appURL, std::string& updaterURL) const;
};
