// Geekatplay TerraForge — file log + crash handlers. See crash_log.hpp.
#include "crash_log.hpp"
#include "console.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <crtdbg.h>
#include <stdlib.h>
#else
#include <cerrno>
#include <cstdint>
#include <execinfo.h> // backtrace(): libSystem on macOS, glibc on Linux
#include <fcntl.h>
#include <pthread.h>  // pthread_self: the "which thread" half of a report
#include <signal.h>   // sigaction; <csignal> alone need not declare it
#include <unistd.h>
#ifdef __APPLE__
#include <dlfcn.h>       // dladdr: the module+RVA shape, without linking libdl
#include <mach-o/dyld.h> // _NSGetExecutablePath
#endif
#endif

namespace fs = std::filesystem;

namespace studio {

namespace {

std::mutex g_file_mtx;
std::FILE *g_file = nullptr;
std::string g_dir, g_stamp;
std::atomic<bool> g_in_crash{false};

#ifndef _WIN32
// A signal handler may not allocate and may not take a lock, so everything it
// needs is worked out here, while the process is still healthy: the report's
// path as plain characters, and the log file's descriptor.
char g_crash_path[512] = {};
int g_log_fd = -1;
#endif

std::string stamp_now() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", &tm);
  return buf;
}

std::string exe_path() {
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  return std::string(buf, buf + n);
#elif defined(__APPLE__)
  // The size is an in/out parameter: the first call fails on purpose and
  // reports how much room the path needs.
  std::uint32_t n = 0;
  _NSGetExecutablePath(nullptr, &n);
  std::string buf(n ? n : 1u, '\0');
  if (_NSGetExecutablePath(buf.data(), &n) != 0) return {};
  buf.resize(std::strlen(buf.c_str()));
  return buf;
#else
  char buf[4096];
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
  // Without /proc (a container, a BSD) log_dir() falls back to ./logs, which
  // is where the app was started from — still findable, still writable.
  return n > 0 ? std::string(buf, buf + n) : std::string();
#endif
}

// Keep the folder from growing forever: the newest N of each kind stay.
void prune(const char *prefix, size_t keep) {
  std::vector<fs::path> files;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(g_dir, ec))
    if (e.is_regular_file(ec) && e.path().filename().string().rfind(prefix, 0) == 0)
      files.push_back(e.path());
  std::sort(files.begin(), files.end());
  while (files.size() > keep) {
    fs::remove(files.front(), ec);
    files.erase(files.begin());
  }
}

// Written by the crash handlers: plain C I/O only, no allocation beyond
// the one string, since the heap may be the thing that broke.
void write_crash(const char *reason, const std::string &detail) {
  if (g_in_crash.exchange(true)) return; // one report per process
  std::string bt = crash_log_backtrace(2);
  std::string path = g_dir + "/crash_" + g_stamp + ".txt";
  if (std::FILE *f = std::fopen(path.c_str(), "wb")) {
    std::fprintf(f, "Geekatplay TerraForge crash report\nreason: %s\n%s\n"
                    "thread: %lu\n\nstack (module+RVA; resolve with "
                    "scripts/resolve_crash.py):\n%s\n\nlog: terraforge_%s.log\n",
                 reason, detail.c_str(),
#ifdef _WIN32
                 (unsigned long)GetCurrentThreadId(),
#else
                 // an opaque pointer on macOS, an integer on glibc: printed
                 // only so two reports from one run can be told apart
                 (unsigned long)(uintptr_t)pthread_self(),
#endif
                 bt.c_str(), g_stamp.c_str());
    std::fclose(f);
  }
  std::lock_guard<std::mutex> lk(g_file_mtx);
  if (g_file) {
    std::fprintf(g_file, "\n=== CRASH: %s\n%s\n%s\n", reason, detail.c_str(),
                 bt.c_str());
    std::fflush(g_file);
  }
}

void on_terminate() {
  std::string what = "no active exception";
  if (std::exception_ptr p = std::current_exception()) {
    try {
      std::rethrow_exception(p);
    } catch (const std::exception &e) {
      what = std::string("uncaught std::exception: ") + e.what();
    } catch (...) {
      what = "uncaught non-std exception";
    }
  }
  write_crash("std::terminate", what);
  std::_Exit(3);
}

#ifdef _WIN32
void on_sigabrt(int) {
  write_crash("SIGABRT (abort() called)", "");
  std::_Exit(3);
}

void on_invalid_parameter(const wchar_t *expr, const wchar_t *func, const wchar_t *file,
                          unsigned line, uintptr_t) {
  // In a release CRT these are all null; the stack is what matters.
  char buf[512];
  std::snprintf(buf, sizeof buf, "expression=%ls function=%ls file=%ls line=%u",
                expr ? expr : L"?", func ? func : L"?", file ? file : L"?", line);
  write_crash("UCRT invalid parameter", buf);
  std::_Exit(3);
}

LONG WINAPI on_seh(EXCEPTION_POINTERS *ep) {
  char buf[256];
  const EXCEPTION_RECORD *r = ep->ExceptionRecord;
  std::snprintf(buf, sizeof buf, "code=0x%08lX address=%p", r->ExceptionCode,
                r->ExceptionAddress);
  std::string detail = buf;
  if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
    std::snprintf(buf, sizeof buf, " (%s at %p)",
                  r->ExceptionInformation[0] == 0 ? "read" : "write",
                  (void *)r->ExceptionInformation[1]);
    detail += buf;
  }
  write_crash("unhandled SEH exception", detail);
  return EXCEPTION_EXECUTE_HANDLER;
}
#else
// A POSIX fatal signal arrives on the broken thread with the heap and the
// locks in whatever state killed us, so write_crash() above — fopen, fprintf,
// std::string, a mutex — is not usable from here. Everything below sticks to
// the async-signal-safe calls: write(), open(), _exit(), and no allocation at
// all. The helpers fill a caller-owned buffer and say how much they used.
size_t put_str(char *dst, const char *s) {
  size_t n = 0;
  while (s[n]) {
    dst[n] = s[n];
    ++n;
  }
  return n;
}

size_t put_hex(char *dst, unsigned long long v) {
  char tmp[17];
  int n = 0;
  do {
    tmp[n++] = "0123456789abcdef"[v & 15];
    v >>= 4;
  } while (v);
  for (int i = 0; i < n; ++i) dst[i] = tmp[n - 1 - i];
  return (size_t)n;
}

void safe_write(int fd, const char *s, size_t n) {
  while (fd >= 0 && n) {
    ssize_t w = ::write(fd, s, n);
    if (w <= 0) {
      if (errno == EINTR) continue; // a signal interrupted us mid-report
      return;
    }
    s += w;
    n -= (size_t)w;
  }
}

const char *signal_name(int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS:  return "SIGBUS (bad address or alignment)";
    case SIGFPE:  return "SIGFPE (arithmetic fault)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGABRT: return "SIGABRT (abort() called)";
    default:      return "fatal signal";
  }
}

void on_fatal_signal(int sig, siginfo_t *info, void *) {
  if (g_in_crash.exchange(true)) ::_exit(3); // one report per process
  // Opened here, not at startup: open() is itself async-signal-safe, and a
  // file created up front would leave an empty crash report behind after
  // every clean run — the folder would say we crash every time.
  int fd = ::open(g_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  char buf[512], *p = buf;
  p += put_str(p, "Geekatplay TerraForge crash report\nreason: ");
  p += put_str(p, signal_name(sig));
  p += put_str(p, "\naddress: 0x");
  p += put_hex(p, (unsigned long long)(uintptr_t)(info ? info->si_addr : nullptr));
  p += put_str(p, "\n\nstack (backtrace_symbols_fd; scripts/resolve_crash.py\n"
                  "reads the Windows module+RVA form only — feed these\n"
                  "addresses to atos on macOS or addr2line on Linux):\n");
  safe_write(fd, buf, (size_t)(p - buf));
  safe_write(g_log_fd, buf, (size_t)(p - buf));
  void *frames[64];
  int n = ::backtrace(frames, 64);
  // backtrace_symbols_fd formats straight into the descriptor without
  // allocating, which is the whole reason it exists next to the pretty
  // backtrace_symbols() used from std::terminate.
  if (fd >= 0) {
    ::backtrace_symbols_fd(frames, n, fd);
    ::close(fd);
  }
  if (g_log_fd >= 0) ::backtrace_symbols_fd(frames, n, g_log_fd);
  // _exit, not exit: the atexit chain would run destructors over the state
  // that just killed us, and the shell wants the conventional 128+signal.
  ::_exit(128 + sig);
}
#endif

} // namespace

std::string log_dir() {
  if (!g_dir.empty()) return g_dir;
  if (const char *env = std::getenv("TERRAFORGE_LOG_DIR"); env && *env) {
    g_dir = env;
  } else {
    fs::path exe = exe_path();
    fs::path dir = exe.parent_path();
    if (dir.filename() == "build") dir = dir.parent_path();
    g_dir = (dir / "logs").string();
  }
  std::error_code ec;
  fs::create_directories(g_dir, ec);
  return g_dir;
}

std::string crash_log_backtrace(int skip) {
  std::string out;
#ifdef _WIN32
  void *frames[62];
  USHORT n = CaptureStackBackTrace((DWORD)skip, 62, frames, nullptr);
  static bool sym_ready = false;
  if (!sym_ready) {
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    sym_ready = SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  }
  char line[512];
  for (USHORT i = 0; i < n; ++i) {
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)frames[i], &mod);
    char modname[MAX_PATH] = "?";
    if (mod) GetModuleFileNameA(mod, modname, MAX_PATH);
    const char *base = std::strrchr(modname, '\\');
    base = base ? base + 1 : modname;
    uintptr_t rva = (uintptr_t)frames[i] - (uintptr_t)mod;
    // DbgHelp resolves exports/PDBs (system DLLs); our own MinGW build has
    // DWARF, which addr2line reads offline from the RVA.
    char symbuf[sizeof(SYMBOL_INFO) + 256] = {};
    SYMBOL_INFO *si = (SYMBOL_INFO *)symbuf;
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    DWORD64 disp = 0;
    const char *sym = "";
    if (sym_ready && SymFromAddr(GetCurrentProcess(), (DWORD64)frames[i], &disp, si))
      sym = si->Name;
    std::snprintf(line, sizeof line, "  #%02u %s+0x%llx %s\n", i, base,
                  (unsigned long long)rva, sym);
    out += line;
  }
#else
  void *frames[62];
  int n = ::backtrace(frames, 62);
  char line[512];
#ifdef __APPLE__
  // dladdr names the image and gives its load address, so a frame comes out
  // in the same module+RVA shape the Windows path writes. It allocates, so it
  // is not usable from a signal handler — this path runs from std::terminate,
  // where the heap is still trustworthy.
  for (int i = skip; i < n; ++i) {
    Dl_info di{};
    const char *base = "?";
    uintptr_t rva = (uintptr_t)frames[i];
    if (dladdr(frames[i], &di) && di.dli_fname) {
      const char *slash = std::strrchr(di.dli_fname, '/');
      base = slash ? slash + 1 : di.dli_fname;
      rva = (uintptr_t)frames[i] - (uintptr_t)di.dli_fbase;
    }
    std::snprintf(line, sizeof line, "  #%02d %s+0x%llx %s\n", i - skip, base,
                  (unsigned long long)rva, di.dli_sname ? di.dli_sname : "");
    out += line;
  }
#else
  // No dladdr here: it lives in libdl, which older glibc wants linked
  // explicitly and the build files are not this module's to change. glibc's
  // own symbol text already reads "module(symbol+0xoff) [0xaddr]", which
  // addr2line takes directly; scripts/resolve_crash.py understands the
  // Windows module+RVA form only.
  char **sym = ::backtrace_symbols(frames, n);
  for (int i = skip; i < n; ++i) {
    std::snprintf(line, sizeof line, "  #%02d %s\n", i - skip,
                  sym ? sym[i] : "?");
    out += line;
  }
  std::free(sym);
#endif
#endif
  return out;
}

void crash_log_init(int argc, char **argv) {
  g_stamp = stamp_now();
  std::string dir = log_dir();
  {
    std::lock_guard<std::mutex> lk(g_file_mtx);
    g_file = std::fopen((dir + "/terraforge_" + g_stamp + ".log").c_str(), "wb");
    if (g_file) {
      std::string cmd;
      for (int i = 0; i < argc; ++i) cmd += std::string(i ? " " : "") + argv[i];
      std::fprintf(g_file, "Geekatplay TerraForge log %s\nexe: %s\ncmd: %s\npid: %lu\n\n",
                   g_stamp.c_str(), exe_path().c_str(), cmd.c_str(),
#ifdef _WIN32
                   (unsigned long)GetCurrentProcessId());
#else
                   (unsigned long)::getpid());
#endif
      std::fflush(g_file);
    }
  }
  prune("terraforge_", 30);
  prune("crash_", 30);
  std::set_terminate(on_terminate); // portable: the same handler either side
#ifdef _WIN32
  std::signal(SIGABRT, on_sigabrt);
  _set_invalid_parameter_handler(on_invalid_parameter);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  SetUnhandledExceptionFilter(on_seh);
  // No "has stopped working" dialog: write the report and go.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#else
  // Both of these are the handler's, and the handler cannot build them: the
  // report path is formatted now, and the log keeps a raw descriptor because
  // fprintf is not async-signal-safe.
  std::snprintf(g_crash_path, sizeof g_crash_path, "%s/crash_%s.txt",
                dir.c_str(), g_stamp.c_str());
  g_log_fd = g_file ? ::fileno(g_file) : -1;
  // sigaction, not signal(): SA_SIGINFO is what carries the faulting address,
  // and SA_RESETHAND puts the default action back so a second fault inside
  // the handler kills the process instead of looping on itself. SIGABRT joins
  // them here — on POSIX it is a fatal signal like the rest, and this way it
  // gets the same stack.
  struct sigaction sa {};
  sa.sa_sigaction = on_fatal_signal;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
    sigaction(sig, &sa, nullptr);
  // A minidump has no POSIX equivalent worth writing by hand; the system's
  // own core file (ulimit -c) plays that part, and the text report above is
  // what gets attached to a bug report either way.
#endif
  log_info("app", "log file: " + dir + "/terraforge_" + g_stamp + ".log");
}

void crash_log_line(const std::string &line) {
  std::lock_guard<std::mutex> lk(g_file_mtx);
  if (!g_file) return;
  std::fwrite(line.data(), 1, line.size(), g_file);
  std::fputc('\n', g_file);
  std::fflush(g_file);
}

void crash_log_shutdown() {
  crash_log_line("=== clean exit");
  std::lock_guard<std::mutex> lk(g_file_mtx);
  if (g_file) std::fclose(g_file);
  g_file = nullptr;
}

} // namespace studio
