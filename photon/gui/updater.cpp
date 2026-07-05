#include "updater.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "uiComponents.hpp"

void drawUpdateProgress(const char* id, int percentage, bool running,
                        const PhotonUi::Palette& palette) {
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 size(width, 58.0f);
  ImGui::InvisibleButton(id, size);

  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float rounding = 8.0f;
  const float progress = percentage < 0 ? 0.0f : std::clamp(percentage / 100.0f, 0.0f, 1.0f);
  const float animatedProgress =
      iam_tween_float(ImGui::GetItemID(), ImHashStr("update_progress"), progress, 0.18f,
                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade,
                      ImGui::GetIO().DeltaTime);

  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.72f)),
                      rounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.46f)),
                rounding);

  char status[64]{};
  if (percentage < 0 && running) {
    std::snprintf(status, sizeof(status), "Preparing download");
  } else if (percentage >= 0) {
    std::snprintf(status, sizeof(status), "%d%% downloaded", std::clamp(percentage, 0, 100));
  }

  const bool showStatus = status[0] != '\0';
  if (showStatus)
    draw->AddText({min.x + 14.0f, min.y + 10.0f}, PhotonUi::colorU32(palette.text), status);

  const float barY = showStatus ? min.y + 38.0f : min.y + 28.0f;
  const ImVec2 barMin(min.x + 14.0f, barY);
  const ImVec2 barMax(max.x - 14.0f, barY + 10.0f);
  draw->AddRectFilled(barMin, barMax, PhotonUi::colorU32(PhotonUi::withAlpha(palette.bg, 0.56f)),
                      5.0f);

  if (percentage >= 0) {
    const ImVec2 fillMax(barMin.x + (barMax.x - barMin.x) * animatedProgress, barMax.y);
    if (fillMax.x > barMin.x + 1.0f) {
      draw->PushClipRect(barMin, barMax, true);
      draw->AddRectFilledMultiColor(
          barMin, fillMax, PhotonUi::colorU32(ImVec4(0.44f, 0.55f, 1.0f, 0.92f)),
          PhotonUi::colorU32(ImVec4(0.88f, 0.35f, 0.90f, 0.96f)),
          PhotonUi::colorU32(ImVec4(0.72f, 0.42f, 1.0f, 0.96f)),
          PhotonUi::colorU32(ImVec4(0.36f, 0.74f, 1.0f, 0.92f)));
      draw->PopClipRect();
    }
  } else if (running) {
    const float t = static_cast<float>(ImGui::GetTime());
    const float span = barMax.x - barMin.x;
    const float sweep = span * 0.28f;
    const float x = barMin.x + std::fmod(t * 150.0f, span + sweep) - sweep;
    draw->PushClipRect(barMin, barMax, true);
    draw->AddRectFilled({x, barMin.y}, {x + sweep, barMax.y},
                        PhotonUi::colorU32(ImVec4(0.72f, 0.42f, 1.0f, 0.55f)), 5.0f);
    draw->PopClipRect();
  }
}

void drawVersionTransition(const char* id, const char* currentVersion, const char* newVersion,
                           const PhotonUi::Palette& palette) {
  ImGui::PushID(id);
  const ImVec2 size(ImGui::GetContentRegionAvail().x, 76.0f);
  ImGui::InvisibleButton("version", size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.56f)),
                      8.0f);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.34f)), 8.0f);

  const float centerX = (min.x + max.x) * 0.5f;
  const float rightX = centerX + 44.0f;
  draw->AddText({min.x + 14.0f, min.y + 12.0f}, PhotonUi::colorU32(palette.muted), "Version");
  draw->AddText({rightX, min.y + 12.0f}, PhotonUi::colorU32(palette.muted), "New");

  draw->PushClipRect({min.x + 14.0f, min.y + 38.0f}, {centerX - 42.0f, max.y - 10.0f}, true);
  draw->AddText({min.x + 14.0f, min.y + 39.0f}, PhotonUi::colorU32(palette.text),
                currentVersion);
  draw->PopClipRect();

  const ImVec2 arrowMin(centerX - 18.0f, min.y + 35.0f);
  const ImVec2 arrowMax(centerX + 18.0f, min.y + 61.0f);
  PhotonUi::drawIconCentered(draw, "\uea1f", arrowMin, arrowMax, 24.0f,
                             PhotonUi::colorU32(PhotonUi::withAlpha(palette.accent, 0.82f)),
                             0.0f);

  draw->PushClipRect({rightX, min.y + 38.0f}, {max.x - 14.0f, max.y - 10.0f}, true);
  draw->AddText({rightX, min.y + 39.0f}, PhotonUi::colorU32(palette.text), newVersion);
  draw->PopClipRect();
  ImGui::PopID();
}

void Updater::drawUI(bool updateAvailable) {
  const bool open = PhotonUi::beginModal("Update", updateAvailable ? ImVec2{540.0f, 350.0f}
                                                                   : ImVec2{420.0f, 190.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    if (!updateAvailable) {
      const ImVec2 start = ImGui::GetCursorScreenPos();
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddText(start, PhotonUi::colorU32(palette.text),
                    "No update available at this time");

      ImGui::Dummy({1.0f, 44.0f});
      ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 48.0f);
      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 110.0f);
      if (PhotonUi::button("CloseUpdate", "Close", {96.0f, 34.0f}, palette, false, "Close"))
        ImGui::CloseCurrentPopup();
      PhotonUi::endModal(open);
      return;
    }

    const bool updaterRunning = running.load();
    const int downloadPercentage = photonDownloadPercentage.load();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 headerMin = ImGui::GetCursorScreenPos();
    const ImVec2 iconMax(headerMin.x + 42.0f, headerMin.y + 42.0f);
    draw->AddRectFilled(headerMin, iconMax,
                        PhotonUi::colorU32(PhotonUi::withAlpha(palette.active, 0.72f)), 8.0f);
    PhotonUi::drawIconCentered(draw, "\ueb13", headerMin, iconMax, 24.0f,
                               PhotonUi::colorU32(palette.text), -1.0f);
    draw->AddText({headerMin.x + 54.0f, headerMin.y + 2.0f}, PhotonUi::colorU32(palette.text),
                  "Update available");
    draw->AddText({headerMin.x + 54.0f, headerMin.y + 25.0f}, PhotonUi::colorU32(palette.muted),
                  updaterRunning ? "Downloading the latest Photon build"
                                 : "A new version is available for download ");
    ImGui::Dummy({1.0f, 54.0f});

    if (updaterRunning || downloadPercentage >= 0) {
      drawUpdateProgress("PhotonUpdateProgress", downloadPercentage, updaterRunning, palette);
      ImGui::Spacing();
    }
    drawVersionTransition("VersionMeta", version.c_str(), newVersion.c_str(), palette);

    const float bottomY = ImGui::GetWindowHeight() - 52.0f;
    ImGui::SetCursorPosY(bottomY);
    const float closeW = 96.0f;
    const float downloadW = 150.0f;
    const float gap = 10.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - closeW - downloadW - gap - 14.0f);
    ImGui::BeginDisabled(updaterRunning);
    if (PhotonUi::button("Download", updaterRunning ? "Downloading" : "Download",
                         {downloadW, 34.0f}, palette, false, "Download update")) {
      launchUpdater();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, gap);
    if (PhotonUi::button("CloseUpdate", "Close", {96.0f, 34.0f}, palette, false, "Close"))
      ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
}

#ifdef _WIN32

#include <shellapi.h>
#include <urlmon.h>
#include <windows.h>

#include <thread>

struct DownloadProgress : IBindStatusCallback {
  std::atomic<int>& percent;
  explicit DownloadProgress(std::atomic<int>& p) : percent(p) {}

  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = (riid == IID_IUnknown || riid == IID_IBindStatusCallback)
               ? static_cast<IBindStatusCallback*>(this)
               : nullptr;
    return *ppv ? S_OK : E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE OnProgress(ULONG p, ULONG m, ULONG s, LPCWSTR t) override {
    unsigned long long cur = p, total = m;

    if (s == BINDSTATUS_64BIT_PROGRESS && t)
      if (swscanf_s(t, L"%llu,%llu", &cur, &total) != 2) return S_OK;

    if ((s == BINDSTATUS_DOWNLOADINGDATA || s == BINDSTATUS_ENDDOWNLOADDATA ||
         s == BINDSTATUS_64BIT_PROGRESS) &&
        total) {
      percent = int(double(cur) * 100.0 / double(total));
    }

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*, BINDINFO*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
};

static std::wstring quote(const std::wstring& s) { return L"\"" + s + L"\""; }

static void debugUpdater(const std::wstring& message) {
  OutputDebugStringW((L"Photon updater: " + message + L"\n").c_str());
}

void Updater::launchUpdater() {
  if (running.exchange(true)) return;
  photonDownloadPercentage.store(-1);
  installerDownloadPercentage.store(-1);
  getOurInfo();
  std::thread(&Updater::beginUpdate, this).detach();
}

std::filesystem::path getPhotonDownloadPath() {
  char* local = std::getenv("LOCALAPPDATA");
  std::filesystem::path dir = std::filesystem::path(local) / "Photon";
  std::filesystem::create_directories(dir);
  return dir / "photon.exe";
}

std::filesystem::path getInstallerDownloadPath() {
  char* local = std::getenv("LOCALAPPDATA");
  std::filesystem::path dir = std::filesystem::path(local) / "Photon";
  std::filesystem::create_directories(dir);
  return dir / "photonUpdater.exe";
}

void Updater::getOurInfo() {
  ourPid = GetCurrentProcessId();
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  ourPath = buffer;
  photonPath = getPhotonDownloadPath();
  installerPath = getInstallerDownloadPath();
}

void Updater::beginUpdate() {
  if (!downloadInstaller()) {
    debugUpdater(L"installer download failed");
    running = false;
    return;
  }

  if (!downloadNewPhoton()) {
    debugUpdater(L"Photon download failed");
    running = false;
    return;
  }

  if (!launchInstaller()) {
    debugUpdater(L"installer launch failed");
  }

  running = false;
}

bool Updater::downloadInstaller() {
  DownloadProgress progress(installerDownloadPercentage);
  std::string path = installerPath.string();
  HRESULT hr = URLDownloadToFileA(nullptr, installerURL.c_str(), path.c_str(), 0, &progress);
  if (hr == S_OK) return true;
  return false;
}

bool Updater::downloadNewPhoton() {
  DownloadProgress progress(photonDownloadPercentage);
  std::string path = photonPath.string();
  HRESULT hr = URLDownloadToFileA(nullptr, photonURL.c_str(), path.c_str(), 0, &progress);
  if (hr == S_OK) return true;
  return false;
}

bool Updater::launchInstaller() {
  const std::wstring exe = installerPath.wstring();
  const std::wstring parameters = std::to_wstring(ourPid) + L" " + quote(ourPath);
  std::wstring workingDirectory = installerPath.parent_path().wstring();

  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"runas";
  sei.lpFile = exe.c_str();
  sei.lpParameters = parameters.c_str();
  sei.lpDirectory = workingDirectory.c_str();
  sei.nShow = SW_SHOWNORMAL;

  if (!ShellExecuteExW(&sei)) {
    debugUpdater(L"ShellExecuteExW failed with error " + std::to_wstring(GetLastError()));
    return false;
  }

  if (sei.hProcess) CloseHandle(sei.hProcess);
  return true;
}

#else

void Updater::launchUpdater() {
  photonDownloadPercentage.store(-1);
  installerDownloadPercentage.store(-1);
  running.store(false);
}

void Updater::getOurInfo() {
  ourPid = 0;
  ourPath.clear();
}

void Updater::beginUpdate() { running.store(false); }

bool Updater::downloadInstaller() { return false; }

bool Updater::downloadNewPhoton() { return false; }

bool Updater::launchInstaller() { return false; }

#endif
