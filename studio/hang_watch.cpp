// Geekatplay TerraForge — the hang watchdog (see hang_watch.hpp).
#include "hang_watch.hpp"
#include "console.hpp"
#include "crash_log.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace studio {

namespace {

using clock_t_ = std::chrono::steady_clock;

std::atomic<long long> g_last_beat_ns{0}; // steady clock, nanoseconds
std::atomic<bool> g_paused{false};
std::atomic<bool> g_stop{false};
std::atomic<int> g_hangs{0};
double g_longest = 0.0;
int g_seconds = 0;
std::thread g_thread;
std::mutex g_cv_mtx;
std::condition_variable g_cv;
#ifdef _WIN32
HANDLE g_main = nullptr;
DWORD g_main_tid = 0;
#endif

long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             clock_t_::now().time_since_epoch())
      .count();
}

std::string stamp() {
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

#ifdef _WIN32
// Windows blocks the message loop itself while the window is dragged or
// resized (the modal size loop) - a stall by design, not a hang.
bool in_move_size() {
  GUITHREADINFO gti{};
  gti.cbSize = sizeof gti;
  if (!GetGUIThreadInfo(g_main_tid, &gti)) return false;
  return (gti.flags & (GUI_INMOVESIZE | GUI_INMENUMODE)) != 0;
}

// The main thread's stack, taken while it is suspended. Symbolised the way
// crash_log does it: module+RVA, plus a name when DbgHelp has one.
std::string main_thread_stack() {
  std::string out;
  if (SuspendThread(g_main) == (DWORD)-1) return "  (could not suspend the main thread)\n";
  CONTEXT ctx{};
  ctx.ContextFlags = CONTEXT_FULL;
  if (!GetThreadContext(g_main, &ctx)) {
    ResumeThread(g_main);
    return "  (could not read the main thread's context)\n";
  }
  static bool sym_ready = false;
  if (!sym_ready) {
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    sym_ready = SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  }
  STACKFRAME64 sf{};
  DWORD machine;
#if defined(_M_X64) || defined(__x86_64__)
  machine = IMAGE_FILE_MACHINE_AMD64;
  sf.AddrPC.Offset = ctx.Rip;
  sf.AddrFrame.Offset = ctx.Rbp;
  sf.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
  machine = IMAGE_FILE_MACHINE_ARM64;
  sf.AddrPC.Offset = ctx.Pc;
  sf.AddrFrame.Offset = ctx.Fp;
  sf.AddrStack.Offset = ctx.Sp;
#else
  machine = IMAGE_FILE_MACHINE_I386;
  sf.AddrPC.Offset = ctx.Eip;
  sf.AddrFrame.Offset = ctx.Ebp;
  sf.AddrStack.Offset = ctx.Esp;
#endif
  sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
  char line[512];
  for (int i = 0; i < 62; ++i) {
    if (!StackWalk64(machine, GetCurrentProcess(), g_main, &sf, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
      break;
    if (sf.AddrPC.Offset == 0) break;
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)sf.AddrPC.Offset, &mod);
    char modname[MAX_PATH] = "?";
    if (mod) GetModuleFileNameA(mod, modname, MAX_PATH);
    const char *base = std::strrchr(modname, '\\');
    base = base ? base + 1 : modname;
    unsigned long long rva = sf.AddrPC.Offset - (unsigned long long)(uintptr_t)mod;
    char symbuf[sizeof(SYMBOL_INFO) + 256] = {};
    SYMBOL_INFO *si = (SYMBOL_INFO *)symbuf;
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    DWORD64 disp = 0;
    const char *sym = "";
    if (sym_ready && SymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &disp, si))
      sym = si->Name;
    std::snprintf(line, sizeof line, "  #%02d %s+0x%llx %s\n", i, base, rva, sym);
    out += line;
  }
  ResumeThread(g_main);
  return out;
}
#else
bool in_move_size() { return false; }
std::string main_thread_stack() {
  return "  (no cross-thread stack walk on this platform; attach a debugger "
         "or send SIGABRT for a crash report)\n";
}
#endif

void watch() {
  std::string report_path;
  long long stall_started = 0;
  while (!g_stop.load()) {
    {
      std::unique_lock<std::mutex> lk(g_cv_mtx);
      g_cv.wait_for(lk, std::chrono::milliseconds(250), [] { return g_stop.load(); });
    }
    if (g_stop.load()) break;
    const long long last = g_last_beat_ns.load();
    if (last == 0 || g_paused.load()) continue;
    const double stalled = (now_ns() - last) / 1e9;
    if (!report_path.empty()) {
      // a hang already reported: has the loop come back?
      if (g_last_beat_ns.load() != stall_started) {
        const double total = (g_last_beat_ns.load() - stall_started) / 1e9;
        if (total > g_longest) g_longest = total;
        if (std::FILE *f = std::fopen(report_path.c_str(), "ab")) {
          std::fprintf(f, "\nrecovered: the frame completed after %.1f s "
                          "(a stall, not a deadlock)\n", total);
          std::fclose(f);
        }
        char msg[200];
        std::snprintf(msg, sizeof msg, "main thread came back after %.1f s", total);
        log_warn("hang", msg);
        report_path.clear();
      }
      continue;
    }
    if (stalled < g_seconds) continue;
    if (in_move_size()) continue;
    // A hang. Write the report first, then say so - the log line goes
    // through the console's mutex, which the stuck thread may hold.
    stall_started = last;
    ++g_hangs;
    std::string bt = main_thread_stack();
    report_path = log_dir() + "/hang_" + stamp() + ".txt";
    if (std::FILE *f = std::fopen(report_path.c_str(), "wb")) {
      std::fprintf(f, "Geekatplay TerraForge hang report\nreason: the main "
                      "thread did not finish a frame for %.1f s\n\nmain thread "
                      "stack (module+RVA; resolve with "
                      "scripts/resolve_crash.py):\n%s\n\nIf this file has no "
                      "'recovered' line the process never came back: a "
                      "deadlock (the graph mutex taken twice on one thread is "
                      "the usual one), or a wait on something that never "
                      "arrived.\n",
                   stalled, bt.c_str());
      std::fclose(f);
    }
    char msg[300];
    std::snprintf(msg, sizeof msg, "main thread has not finished a frame for %.1f s; report: %s",
                  stalled, report_path.c_str());
    log_warn("hang", msg);
  }
}

} // namespace

void hang_watch_start(int seconds) {
  if (seconds <= 0 || g_thread.joinable()) return;
  g_seconds = seconds;
  g_stop.store(false);
  g_last_beat_ns.store(now_ns());
#ifdef _WIN32
  g_main_tid = GetCurrentThreadId();
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                  &g_main, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                               THREAD_QUERY_INFORMATION,
                  FALSE, 0);
#endif
  g_thread = std::thread(watch);
  log_info("hang", "watchdog on: a frame that takes more than " +
                       std::to_string(seconds) + " s writes logs/hang_<stamp>.txt");
}

void hang_watch_beat() { g_last_beat_ns.store(now_ns()); }

void hang_watch_pause(bool paused) {
  g_paused.store(paused);
  if (!paused) g_last_beat_ns.store(now_ns());
}

void hang_watch_stop() {
  if (!g_thread.joinable()) return;
  {
    std::lock_guard<std::mutex> lk(g_cv_mtx);
    g_stop.store(true);
  }
  g_cv.notify_all();
  g_thread.join();
#ifdef _WIN32
  if (g_main) CloseHandle(g_main);
  g_main = nullptr;
#endif
}

int hang_watch_count() { return g_hangs.load(); }
double hang_watch_longest() { return g_longest; }

} // namespace studio
