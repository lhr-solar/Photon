#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct State {
  pid_t photonPid{};
  std::filesystem::path photonPath{};
  std::filesystem::path downloadPath{};
  std::filesystem::path oldPhotonPath{};
} state;

std::filesystem::path cacheDir() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    std::filesystem::path dir = std::filesystem::path(xdg) / "Photon";
    std::filesystem::create_directories(dir);
    return dir;
  }

  const char* home = std::getenv("HOME");
  if (!home || !*home) std::exit(1);
  std::filesystem::path dir = std::filesystem::path(home) / ".cache" / "Photon";
  std::filesystem::create_directories(dir);
  return dir;
}

void parseArgs(int argc, char** argv) {
  if (argc != 3) std::exit(1);

  state.photonPid = static_cast<pid_t>(std::stol(argv[1]));
  state.photonPath = argv[2];
  const std::filesystem::path cache = cacheDir();
  state.downloadPath = cache / "Photon.AppImage";
  state.oldPhotonPath = cache / "Photon.AppImage.old";
}

bool processAlive(pid_t pid) { return pid > 0 && kill(pid, 0) == 0; }

void stopProcess(pid_t pid) {
  if (pid <= 0) return;

  kill(pid, SIGTERM);
  for (int i = 0; i < 100; ++i) {
    if (!processAlive(pid)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  kill(pid, SIGKILL);
  for (int i = 0; i < 100; ++i) {
    if (!processAlive(pid)) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void makeExecutable(const std::filesystem::path& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) std::exit(1);
  chmod(path.c_str(), st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
}

void moveReplace(const std::filesystem::path& from, const std::filesystem::path& to) {
  std::error_code ignored;
  std::filesystem::remove(to, ignored);
  std::filesystem::rename(from, to);
}

void launchProcess(const std::filesystem::path& path) {
  pid_t child = fork();
  if (child != 0) return;

  setsid();
  std::filesystem::current_path(path.parent_path());
  execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
  _exit(127);
}

int main(int argc, char** argv) {
  parseArgs(argc, argv);
  makeExecutable(state.downloadPath);
  stopProcess(state.photonPid);
  moveReplace(state.photonPath, state.oldPhotonPath);
  moveReplace(state.downloadPath, state.photonPath);
  makeExecutable(state.photonPath);
  launchProcess(state.photonPath);
  return 0;
}
