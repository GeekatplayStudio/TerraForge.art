// Geekatplay TerraForge - fetching the free plants from inside the studio.
//
// The download itself is orchestrator/plant_fetch.py, the same program a
// person can run by hand; this starts it as a child process with no console,
// reads the one-line progress file it keeps, and rescans the library when it
// is done. Python rather than C++ because the work is a web API, checksums
// and picture editing (the cut-out merge), which the Python layer already
// does for the renders, and because a second implementation of the fetch
// would drift from the first.
#include "plant_library.hpp"
#include "paths.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace studio {

namespace {

// Never destroyed: the thread waiting on the child may still hold them while
// the application's statics are torn down on exit.
struct Fetch {
  std::mutex mtx;
  PlantFetchState state; // guarded by mtx
  std::string progress_path, log_path;
  std::atomic<bool> ended{false}; // set by the waiting thread, taken by plant_fetch_poll
#ifdef _WIN32
  HANDLE process = nullptr; // guarded by mtx
#else
  pid_t pid = -1;
#endif
};
Fetch &F() {
  static Fetch *f = new Fetch;
  return *f;
}

// The directory with orchestrator/ in it: beside the executable or one up
// (install_dir), the working directory or its parent (a build tree), or the
// source tree this build came from.
fs::path python_layer() {
  std::error_code ec;
  std::vector<fs::path> cands = {install_dir(), fs::current_path(ec)};
  cands.push_back(cands.back().parent_path());
#ifdef TF_SOURCE_DIR
  cands.push_back(fs::path(TF_SOURCE_DIR));
#endif
  for (const fs::path &p : cands)
    if (!p.empty() && fs::exists(p / "orchestrator" / "plant_fetch.py", ec)) return p;
  return {};
}

std::string last_line(const std::string &path) {
  std::ifstream f(path);
  std::string line, last;
  while (std::getline(f, line))
    if (!line.empty()) last = line;
  return last;
}

} // namespace

bool plant_fetch_start(const std::vector<std::string> &ids, const std::string &res,
                       std::string &err) {
  Fetch &f = F();
  {
    std::lock_guard<std::mutex> lk(f.mtx);
    if (f.state.running) {
      err = "a plant download is already running";
      return false;
    }
  }
  const fs::path root = python_layer();
  if (root.empty()) {
    err = "cannot find orchestrator/plant_fetch.py beside the application";
    return false;
  }
  std::error_code ec;
  const fs::path dest = plant_library_dir();
  fs::create_directories(dest, ec);
  const fs::path work = fs::temp_directory_path(ec) / "terraforge_plants";
  fs::create_directories(work, ec);
  f.progress_path = (work / "progress.txt").string();
  f.log_path = (work / "fetch.log").string();
  fs::remove(f.progress_path, ec);
  std::string id_list;
  for (const std::string &id : ids) id_list += (id_list.empty() ? "" : ",") + id;
  const std::string resolution = res.empty() ? "1k" : res;

#ifdef _WIN32
  auto q = [](const std::string &s) { return "\"" + s + "\""; };
  std::string args = " -m orchestrator.plant_fetch --dest " + q(dest.string()) + " --res " +
                     resolution + " --progress " + q(f.progress_path);
  if (!id_list.empty()) args += " --ids " + q(id_list);
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE}; // the child writes the log
  HANDLE log = CreateFileA(f.log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW | (log != INVALID_HANDLE_VALUE ? STARTF_USESTDHANDLES : 0);
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = si.hStdError = log;
  PROCESS_INFORMATION pi{};
  bool started = false;
  // "python" is what the renders use; "py" is the launcher a python.org
  // install puts on the path when "python" itself is not
  for (const char *exe : {"python", "py -3"}) {
    std::string cmd = std::string(exe) + args;
    if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                       root.string().c_str(), &si, &pi)) {
      started = true;
      break;
    }
  }
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  if (!started) {
    err = "could not start Python (is it installed and on PATH?)";
    return false;
  }
  CloseHandle(pi.hThread);
  {
    std::lock_guard<std::mutex> lk(f.mtx);
    f.process = pi.hProcess;
    f.state = PlantFetchState{};
    f.state.running = true;
    f.state.line = "starting the plant download...";
  }
  std::thread([hproc = pi.hProcess]() {
    Fetch &f = F();
    WaitForSingleObject(hproc, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(hproc, &code);
    std::lock_guard<std::mutex> lk(f.mtx);
    CloseHandle(hproc);
    f.process = nullptr;
    f.state.running = false;
    f.state.exit_code = (int)code;
    f.ended.store(true);
  }).detach();
#else
  std::vector<std::string> argv = {"python3",    "-m",    "orchestrator.plant_fetch", "--dest",
                                   dest.string(), "--res", resolution, "--progress", f.progress_path};
  if (!id_list.empty()) {
    argv.push_back("--ids");
    argv.push_back(id_list);
  }
  const std::string log_path = f.log_path, cwd = root.string();
  pid_t pid = fork();
  if (pid < 0) {
    err = "could not start Python";
    return false;
  }
  if (pid == 0) {
    setsid();
    if (chdir(cwd.c_str()) != 0) _exit(127);
    int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      dup2(fd, 1);
      dup2(fd, 2);
    }
    std::vector<char *> cargs;
    for (std::string &s : argv) cargs.push_back(s.data());
    cargs.push_back(nullptr);
    execvp(cargs[0], cargs.data());
    _exit(127);
  }
  {
    std::lock_guard<std::mutex> lk(f.mtx);
    f.pid = pid;
    f.state = PlantFetchState{};
    f.state.running = true;
    f.state.line = "starting the plant download...";
  }
  std::thread([pid]() {
    Fetch &f = F();
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    std::lock_guard<std::mutex> lk(f.mtx);
    f.pid = -1;
    f.state.running = false;
    f.state.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    f.ended.store(true);
  }).detach();
#endif
  return true;
}

PlantFetchState plant_fetch_state() {
  Fetch &f = F();
  std::lock_guard<std::mutex> lk(f.mtx);
  return f.state;
}

void plant_fetch_poll() {
  Fetch &f = F();
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  bool running;
  {
    std::lock_guard<std::mutex> lk(f.mtx);
    running = f.state.running;
  }
  if (running && now - last > std::chrono::milliseconds(400)) {
    last = now;
    const std::string line = last_line(f.progress_path);
    std::lock_guard<std::mutex> lk(f.mtx);
    if (!line.empty()) f.state.line = line;
  }
  if (!f.ended.exchange(false)) return;
  // the run is over: say how, and show what arrived
  const std::string progress = last_line(f.progress_path);
  const std::string tail = last_line(f.log_path);
  plant_library(true);
  std::lock_guard<std::mutex> lk(f.mtx);
  f.state.finished = true;
  if (f.state.exit_code == 0) f.state.line = progress.empty() ? "plants downloaded" : "plants " + progress;
  else f.state.line = "the plant download stopped: " + (tail.empty() ? progress : tail);
}

void plant_fetch_cancel() {
  Fetch &f = F();
  std::lock_guard<std::mutex> lk(f.mtx);
#ifdef _WIN32
  if (f.process) TerminateProcess(f.process, 2);
#else
  if (f.pid > 0) kill(-f.pid, SIGTERM);
#endif
}

} // namespace studio
