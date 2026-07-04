#include "updater.hpp"

#include <windows.h>
#include <urlmon.h>

#include <iostream>
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

        if ((s == BINDSTATUS_DOWNLOADINGDATA ||
             s == BINDSTATUS_ENDDOWNLOADDATA ||
             s == BINDSTATUS_64BIT_PROGRESS) && total) {
            percent = int(double(cur) * 100.0 / double(total));
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*, BINDINFO*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
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
    downloadInstaller();
    downloadNewPhoton();
    launchInstaller();
    running = false;
}

bool Updater::downloadInstaller() {
    DownloadProgress progress(installerDownloadPercentage);
    std::string path = installerPath.string();
    HRESULT hr = URLDownloadToFileA( nullptr, installerURL.c_str(), path.c_str(), 0, &progress );
    if (hr == S_OK) return true;
    return false;
}

bool Updater::downloadNewPhoton() {
    DownloadProgress progress(photonDownloadPercentage);
    std::string path = photonPath.string();
    HRESULT hr = URLDownloadToFileA( nullptr, photonURL.c_str(), path.c_str(), 0, &progress );
    if (hr == S_OK) return true;
    return false;
}

void Updater::launchInstaller() {
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::wstring exe = installerPath.wstring();

    std::wstring cmd =
        quote(exe) + L" " +
        std::to_wstring(ourPid) + L" " +
        quote(ourPath);

    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
        nullptr, &si, &pi );

    if (!ok) return;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
