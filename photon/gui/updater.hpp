#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
using UpdaterProcessId = DWORD;
using UpdaterPathString = std::wstring;
#else
using UpdaterProcessId = int;
using UpdaterPathString = std::string;
#endif

struct Updater {
  std::atomic<int> photonDownloadPercentage{-1};
  std::atomic<int> installerDownloadPercentage{-1};
  std::atomic<bool> running{false};
  std::atomic<bool> releaseQueryStarted{false};
  std::atomic<bool> updateAvailable{false};
  mutable std::mutex releaseMutex;

  std::filesystem::path installerPath{};
  std::string installerURL{};

  std::filesystem::path photonPath{};
  std::string photonURL{};

  UpdaterProcessId ourPid{};
  UpdaterPathString ourPath{};

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
