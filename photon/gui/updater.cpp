#include "updater.hpp"

#ifdef PHOTON_HAS_CURL
#include <curl/curl.h>
#endif

#ifdef _WIN32
#include <winhttp.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "uiComponents.hpp"

static constexpr const char* kLatestReleaseUrl =
    "https://api.github.com/repos/lhr-solar/Photon/releases/latest";

#ifdef _WIN32
static constexpr const char* kPhotonAssetName = "Photon.exe";
static constexpr const char* kUpdaterAssetName = "photonUpdater.exe";
#else
static constexpr const char* kPhotonAssetName = "Photon.AppImage";
static constexpr const char* kUpdaterAssetName = "photonUpdater-linux-x64";
#endif

#if defined(_WIN32) || defined(PHOTON_HAS_CURL)
static constexpr bool kCanQueryReleases = true;

static std::string jsonUnescape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\\' || i + 1 >= text.size()) {
      out.push_back(text[i]);
      continue;
    }
    const char escaped = text[++i];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        out.push_back(escaped);
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      default:
        out.push_back(escaped);
        break;
    }
  }
  return out;
}

static std::string jsonStringValue(std::string_view json, std::string_view key,
                                   std::size_t start = 0) {
  const std::string needle = "\"" + std::string(key) + "\"";
  std::size_t keyPos = json.find(needle, start);
  if (keyPos == std::string_view::npos) return {};
  std::size_t colon = json.find(':', keyPos + needle.size());
  if (colon == std::string_view::npos) return {};
  std::size_t quote = json.find('"', colon + 1);
  if (quote == std::string_view::npos) return {};

  std::size_t end = quote + 1;
  bool escaped = false;
  for (; end < json.size(); ++end) {
    const char c = json[end];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') break;
  }
  if (end >= json.size()) return {};
  return jsonUnescape(json.substr(quote + 1, end - quote - 1));
}

static std::string releaseAssetUrl(std::string_view json, std::string_view assetName) {
  std::size_t pos = 0;
  while ((pos = json.find("\"browser_download_url\"", pos)) != std::string_view::npos) {
    const std::size_t objectStart = json.rfind('{', pos);
    const std::string url = jsonStringValue(json, "browser_download_url", pos);
    const std::string name = objectStart == std::string_view::npos
                                 ? std::string{}
                                 : jsonStringValue(json, "name", objectStart);
    if (name == assetName) return url;
    pos += 22;
  }
  return {};
}

static std::string displayVersion(std::string tag) {
  if (!tag.empty() && tag[0] == 'v') tag.erase(0, 1);
  return tag;
}

static size_t curlWriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const size_t bytes = size * nmemb;
  out->append(ptr, bytes);
  return bytes;
}
#endif

#ifdef PHOTON_HAS_CURL
static bool fetchLatestReleaseJson(std::string& response) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  CURL* curl = curl_easy_init();
  if (!curl) {
    curl_global_cleanup();
    return false;
  }

  curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
  headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

  curl_easy_setopt(curl, CURLOPT_URL, kLatestReleaseUrl);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Photon-Updater/0.1");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  const CURLcode result = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  return result == CURLE_OK && status >= 200 && status < 300;
}
#endif

#ifdef _WIN32
static bool fetchLatestReleaseJson(std::string& response) {
  HINTERNET session = WinHttpOpen(L"Photon-Updater/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;

  HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!connect) {
    WinHttpCloseHandle(session);
    return false;
  }

  HINTERNET request =
      WinHttpOpenRequest(connect, L"GET", L"/repos/lhr-solar/Photon/releases/latest", nullptr,
                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
  }

  const wchar_t* headers =
      L"Accept: application/vnd.github+json\r\n"
      L"X-GitHub-Api-Version: 2022-11-28\r\n"
      L"User-Agent: Photon-Updater/0.1\r\n";
  bool ok = WinHttpSendRequest(request, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr);

  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  ok = ok &&
       WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                           WINHTTP_NO_HEADER_INDEX) &&
       status >= 200 && status < 300;

  std::array<char, 4096> buffer{};
  while (ok) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;

    while (available > 0) {
      const DWORD toRead = std::min<DWORD>(available, DWORD(buffer.size()));
      DWORD read = 0;
      if (!WinHttpReadData(request, buffer.data(), toRead, &read)) {
        ok = false;
        break;
      }
      if (read == 0) break;
      response.append(buffer.data(), read);
      available -= read;
    }
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return ok;
}
#endif

void Updater::setReleaseInfo(std::string remoteVersion, std::string appURL,
                             std::string updaterURL) {
  if (remoteVersion.empty() || appURL.empty() || updaterURL.empty()) return;

  std::lock_guard lock(releaseMutex);
  if (remoteVersion == version) return;

  newVersion = std::move(remoteVersion);
  photonURL = std::move(appURL);
  installerURL = std::move(updaterURL);
  updateAvailable.store(true);
}

void Updater::versionSnapshot(std::string& current, std::string& remote) const {
  std::lock_guard lock(releaseMutex);
  current = version;
  remote = newVersion;
}

void Updater::downloadSnapshot(std::string& appURL, std::string& updaterURL) const {
  std::lock_guard lock(releaseMutex);
  appURL = photonURL;
  updaterURL = installerURL;
}

void Updater::queryReleaseInfoOnceAsync() {
  if (releaseQueryStarted.exchange(true)) return;

  std::thread([this] {
    std::string response;
    if (!fetchLatestReleaseJson(response)) return;

    const std::string nextVersion = displayVersion(jsonStringValue(response, "tag_name"));
    const std::string nextPhotonURL = releaseAssetUrl(response, kPhotonAssetName);
    const std::string nextUpdaterURL = releaseAssetUrl(response, kUpdaterAssetName);
    setReleaseInfo(nextVersion, nextPhotonURL, nextUpdaterURL);
  }).detach();
}

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
  const float animatedProgress = iam_tween_float(
      ImGui::GetItemID(), ImHashStr("update_progress"), progress, 0.18f,
      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, ImGui::GetIO().DeltaTime);

  draw->AddRectFilled(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.72f)),
                      rounding);
  draw->AddRect(min, max, PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.46f)), rounding);

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
      draw->AddRectFilledMultiColor(barMin, fillMax,
                                    PhotonUi::colorU32(ImVec4(0.44f, 0.55f, 1.0f, 0.92f)),
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
  draw->AddText({min.x + 14.0f, min.y + 39.0f}, PhotonUi::colorU32(palette.text), currentVersion);
  draw->PopClipRect();

  const ImVec2 arrowMin(centerX - 18.0f, min.y + 35.0f);
  const ImVec2 arrowMax(centerX + 18.0f, min.y + 61.0f);
  PhotonUi::drawIconCentered(draw, "\uea1f", arrowMin, arrowMax, 24.0f,
                             PhotonUi::colorU32(PhotonUi::withAlpha(palette.accent, 0.82f)), 0.0f);

  draw->PushClipRect({rightX, min.y + 38.0f}, {max.x - 14.0f, max.y - 10.0f}, true);
  draw->AddText({rightX, min.y + 39.0f}, PhotonUi::colorU32(palette.text), newVersion);
  draw->PopClipRect();
  ImGui::PopID();
}

void Updater::drawUI(bool updateAvailable) {
  const bool open = PhotonUi::beginModal(
      "Update", updateAvailable ? ImVec2{540.0f, 350.0f} : ImVec2{420.0f, 190.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    if (!updateAvailable) {
      const ImVec2 start = ImGui::GetCursorScreenPos();
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddText(start, PhotonUi::colorU32(palette.text),
                    kCanQueryReleases
                        ? "No update available at this time"
                        : "Update checks unavailable: libcurl was not found at build time");

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
    std::string currentVersion;
    std::string remoteVersion;
    versionSnapshot(currentVersion, remoteVersion);
    drawVersionTransition("VersionMeta", currentVersion.c_str(), remoteVersion.c_str(), palette);

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
    running = false;
    return;
  }

  if (!downloadNewPhoton()) {
    running = false;
    return;
  }

  launchInstaller();

  running = false;
}

bool Updater::downloadInstaller() {
  DownloadProgress progress(installerDownloadPercentage);
  std::string path = installerPath.string();
  std::string appURL;
  std::string updaterURL;
  downloadSnapshot(appURL, updaterURL);
  HRESULT hr = URLDownloadToFileA(nullptr, updaterURL.c_str(), path.c_str(), 0, &progress);
  if (hr == S_OK) return true;
  return false;
}

bool Updater::downloadNewPhoton() {
  DownloadProgress progress(photonDownloadPercentage);
  std::string path = photonPath.string();
  std::string appURL;
  std::string updaterURL;
  downloadSnapshot(appURL, updaterURL);
  HRESULT hr = URLDownloadToFileA(nullptr, appURL.c_str(), path.c_str(), 0, &progress);
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

  if (!ShellExecuteExW(&sei)) return false;

  if (sei.hProcess) CloseHandle(sei.hProcess);
  return true;
}

#else

void Updater::launchUpdater() {
  if (running.exchange(true)) return;
  photonDownloadPercentage.store(-1);
  installerDownloadPercentage.store(-1);
  getOurInfo();
  std::thread(&Updater::beginUpdate, this).detach();
}

std::filesystem::path getPhotonCacheDir() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    std::filesystem::path dir = std::filesystem::path(xdg) / "Photon";
    std::filesystem::create_directories(dir);
    return dir;
  }

  const char* home = std::getenv("HOME");
  if (!home || !*home) return {};
  std::filesystem::path dir = std::filesystem::path(home) / ".cache" / "Photon";
  std::filesystem::create_directories(dir);
  return dir;
}

std::filesystem::path currentPhotonPath() {
  if (const char* appImage = std::getenv("APPIMAGE"); appImage && *appImage) return appImage;

  std::array<char, 4096> path{};
  const ssize_t size = readlink("/proc/self/exe", path.data(), path.size() - 1);
  if (size <= 0) return {};
  path[static_cast<std::size_t>(size)] = '\0';
  return path.data();
}

void makeExecutable(const std::filesystem::path& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) return;
  chmod(path.c_str(), st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
}

static size_t curlWriteToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
  return std::fwrite(ptr, size, nmemb, static_cast<FILE*>(userdata));
}

static int curlProgress(void* userdata, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
  auto* percent = static_cast<std::atomic<int>*>(userdata);
  if (total > 0) percent->store(static_cast<int>((now * 100) / total));
  return 0;
}

bool downloadFile(const std::string& url, const std::filesystem::path& path,
                  std::atomic<int>& percentage) {
  if (url.empty() || path.empty()) return false;
  std::filesystem::create_directories(path.parent_path());

  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) return false;

  curl_global_init(CURL_GLOBAL_DEFAULT);
  CURL* curl = curl_easy_init();
  if (!curl) {
    std::fclose(file);
    curl_global_cleanup();
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Photon-Updater/0.1");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToFile);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &percentage);

  const CURLcode result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  std::fclose(file);

  if (result != CURLE_OK) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return false;
  }

  percentage.store(100);
  return true;
}

void Updater::getOurInfo() {
  ourPid = getpid();
  photonPath = getPhotonCacheDir() / "Photon.AppImage";
  installerPath = getPhotonCacheDir() / "photonUpdater-linux-x64";
  ourPath = currentPhotonPath().string();
}

void Updater::beginUpdate() {
  if (!downloadInstaller()) {
    running = false;
    return;
  }

  if (!downloadNewPhoton()) {
    running = false;
    return;
  }

  launchInstaller();
  running = false;
}

bool Updater::downloadInstaller() {
  std::string appURL;
  std::string updaterURL;
  downloadSnapshot(appURL, updaterURL);
  const bool ok = downloadFile(updaterURL, installerPath, installerDownloadPercentage);
  if (ok) makeExecutable(installerPath);
  return ok;
}

bool Updater::downloadNewPhoton() {
  std::string appURL;
  std::string updaterURL;
  downloadSnapshot(appURL, updaterURL);
  const bool ok = downloadFile(appURL, photonPath, photonDownloadPercentage);
  if (ok) makeExecutable(photonPath);
  return ok;
}

bool Updater::launchInstaller() {
  if (installerPath.empty() || ourPath.empty()) return false;

  pid_t child = fork();
  if (child < 0) return false;
  if (child > 0) return true;

  setsid();
  const std::string pid = std::to_string(ourPid);
  execl(installerPath.c_str(), installerPath.c_str(), pid.c_str(), ourPath.c_str(),
        static_cast<char*>(nullptr));
  _exit(127);
}

#endif
