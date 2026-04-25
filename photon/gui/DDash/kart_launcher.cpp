#include "kart_launcher.h"

#include "imgui.h"

#include <chrono>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
using socket_t = int;
#define CLOSESOCK ::close
#endif

#include <cstdlib>

namespace kart {

namespace {

// Install location. Override at runtime with PHOTON_SMK_DIR (the directory
// containing the assets/ folder and the executable). Defaults below match the
// developer setup on Windows and the Yocto image layout.
#ifdef _WIN32
constexpr const char* DEFAULT_SMK_DIR = "C:\\Users\\alper\\super-mario-kart";
constexpr const char* SMK_EXE_NAME    = "super_mario_kart.exe";
#else
constexpr const char* DEFAULT_SMK_DIR = "/opt/super-mario-kart";
constexpr const char* SMK_EXE_NAME    = "super_mario_kart";
#endif
constexpr uint16_t    PORT    = 48655;

std::string smkDir() {
    if (const char* env = std::getenv("PHOTON_SMK_DIR")) return env;
    return DEFAULT_SMK_DIR;
}
std::string smkExe() {
#ifdef _WIN32
    return smkDir() + "\\" + SMK_EXE_NAME;
#else
    return smkDir() + "/" + SMK_EXE_NAME;
#endif
}

constexpr float CORNER_PX = 80.0f;       // size of corner hit-box
constexpr double CHORD_WINDOW_S = 1.0;   // both corners must click within this

using Clock = std::chrono::steady_clock;

bool        s_running = false;
double      s_lastBlClick = -1e9;
double      s_lastBrClick = -1e9;
socket_t    s_sock = (socket_t)-1;
sockaddr_in s_dest{};

#ifdef _WIN32
PROCESS_INFORMATION s_pi{};
#else
pid_t s_pid = -1;
#endif

double nowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(Clock::now().time_since_epoch()).count();
}

bool ensureChildAlive() {
    if (!s_running) return false;
#ifdef _WIN32
    DWORD code = 0;
    if (GetExitCodeProcess(s_pi.hProcess, &code) && code != STILL_ACTIVE) {
        CloseHandle(s_pi.hProcess);
        CloseHandle(s_pi.hThread);
        s_pi = {};
        s_running = false;
        return false;
    }
#else
    int status = 0;
    pid_t r = waitpid(s_pid, &status, WNOHANG);
    if (r == s_pid) {
        s_pid = -1;
        s_running = false;
        return false;
    }
#endif
    return true;
}

bool spawnSmk() {
    if (s_running) return true;
#ifdef _WIN32
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string exe = smkExe();
    std::string dir = smkDir();
    std::string cmd = std::string("\"") + exe + "\"";
    BOOL ok = CreateProcessA(
        exe.c_str(),
        cmd.data(),
        nullptr, nullptr, FALSE,
        0,
        nullptr,
        dir.c_str(),
        &si, &pi);
    if (!ok) return false;
    s_pi = pi;
    s_running = true;
    return true;
#else
    std::string exe = smkExe();
    std::string dir = smkDir();
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (chdir(dir.c_str()) != 0) _exit(127);
        char* const argv[] = { const_cast<char*>(exe.c_str()), nullptr };
        execv(exe.c_str(), argv);
        _exit(127);
    }
    s_pid = pid;
    s_running = true;
    return true;
#endif
}

void ensureSocket() {
    if (s_sock != (socket_t)-1) return;
#ifdef _WIN32
    static bool s_wsa = false;
    if (!s_wsa) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        s_wsa = true;
    }
#endif
    s_sock = (socket_t)socket(AF_INET, SOCK_DGRAM, 0);
    s_dest = {};
    s_dest.sin_family = AF_INET;
    s_dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    s_dest.sin_port = htons(PORT);
}

void sendInputs(const Inputs& in) {
    ensureSocket();
    if (s_sock == (socket_t)-1) return;
    float buf[4] = {in.steering, in.throttle, in.brake, in.drift};
    sendto(s_sock, (const char*)buf, sizeof(buf), 0,
           (const sockaddr*)&s_dest, sizeof(s_dest));
}

void detectCornerChord() {
    if (s_running) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 p = io.MousePos;
    ImVec2 d = io.DisplaySize;
    if (p.x < 0 || p.y < 0 || d.x <= 0 || d.y <= 0) return;

    double now = nowSeconds();
    bool inBl = (p.x <= CORNER_PX) && (p.y >= d.y - CORNER_PX);
    bool inBr = (p.x >= d.x - CORNER_PX) && (p.y >= d.y - CORNER_PX);
    if (inBl) s_lastBlClick = now;
    if (inBr) s_lastBrClick = now;

    if ((now - s_lastBlClick) < CHORD_WINDOW_S &&
        (now - s_lastBrClick) < CHORD_WINDOW_S) {
        spawnSmk();
        s_lastBlClick = s_lastBrClick = -1e9;
    }
}

} // namespace

bool isRunning() { return s_running; }

void update(const Inputs& in) {
    ensureChildAlive();
    detectCornerChord();
    if (s_running) sendInputs(in);
}

} // namespace kart
