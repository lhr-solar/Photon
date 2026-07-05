#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

struct State {
    int photonPid{};
    std::string photonPath{};
    fs::path appDataDir{};
    fs::path downloadPath{};
    fs::path oldPhotonPath{};
} state;

fs::path getLocalAppDataPhotonDir() {
    char* local = nullptr;
    size_t len = 0;
    if (_dupenv_s(&local, &len, "LOCALAPPDATA") != 0 || !local) std::exit(1);
    fs::path dir = fs::path(local) / "Photon";
    free(local);
    fs::create_directories(dir);
    return dir;
}

void parseArgs(int argc,  char** argv){
    if (argc != 3) std::exit(1);

    state.photonPid = std::stoi(argv[1]);
    state.photonPath = argv[2];

    state.appDataDir = getLocalAppDataPhotonDir();
    state.downloadPath = state.appDataDir / "photon.exe";
    state.oldPhotonPath = state.appDataDir / "photon.exe_old";
};

void killProcess(int pid){
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!hProcess) return;
    TerminateProcess(hProcess, 0);
    WaitForSingleObject(hProcess, 10000);
    CloseHandle(hProcess);
}

void launchProcess(const fs::path& path){
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    std::string cmd = "\"" + path.string() + "\"";
    std::string workdir = path.parent_path().string();
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
        workdir.c_str(), &si, &pi );
};

void moveReplace(const fs::path& from, const fs::path& to){
    MoveFileExA( from.string().c_str(), to.string().c_str(),
        MOVEFILE_REPLACE_EXISTING |
        MOVEFILE_COPY_ALLOWED |
        MOVEFILE_WRITE_THROUGH
    );
}

int main(int argc, char** argv){
    parseArgs(argc, argv);
    killProcess(state.photonPid);
    moveReplace(state.photonPath, state.oldPhotonPath);
    moveReplace(state.downloadPath, state.photonPath);
    launchProcess(state.photonPath);
}
