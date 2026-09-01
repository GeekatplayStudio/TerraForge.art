// Geekatplay TerraForge — the message log (storage side).
#include "console.hpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <mutex>

namespace studio {

namespace {

std::mutex g_mtx;
std::vector<LogEntry> g_lines;
size_t g_errors = 0, g_warns = 0;
// Bounded, because a shader relinking every frame can produce thousands of
// identical lines and an unbounded log would be the memory leak it exists to
// report. Identical consecutive messages collapse first, so the cap is rarely
// what actually stops it.
constexpr size_t MAX_LINES = 4000;

double now_seconds() {
  using clock = std::chrono::steady_clock;
  static const clock::time_point t0 = clock::now();
  return std::chrono::duration<double>(clock::now() - t0).count();
}

const char *level_tag(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Info:  return "info ";
    default:              return "trace";
  }
}

} // namespace

void log_add(LogLevel lvl, const char *category, const std::string &text) {
  if (text.empty()) return;
  std::lock_guard<std::mutex> lk(g_mtx);
  // A message repeated back to back is one event that happened many times,
  // not many events. Collapsing keeps a per-frame failure readable.
  if (!g_lines.empty()) {
    LogEntry &last = g_lines.back();
    if (last.level == lvl && last.text == text &&
        last.category == (category ? category : "")) {
      ++last.repeat;
      last.time = now_seconds();
      return;
    }
  }
  if (lvl == LogLevel::Error) ++g_errors;
  if (lvl == LogLevel::Warn) ++g_warns;
  LogEntry e;
  e.level = lvl;
  e.time = now_seconds();
  e.category = category ? category : "";
  e.text = text;
  g_lines.push_back(std::move(e));
  if (g_lines.size() > MAX_LINES)
    g_lines.erase(g_lines.begin(), g_lines.begin() + (g_lines.size() - MAX_LINES));
}

void log_fmt(LogLevel lvl, const char *category, const char *fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  log_add(lvl, category, buf);
}

std::vector<LogEntry> log_snapshot() {
  std::lock_guard<std::mutex> lk(g_mtx);
  return g_lines;
}
size_t log_error_count() {
  std::lock_guard<std::mutex> lk(g_mtx);
  return g_errors;
}
size_t log_warn_count() {
  std::lock_guard<std::mutex> lk(g_mtx);
  return g_warns;
}
void log_clear() {
  std::lock_guard<std::mutex> lk(g_mtx);
  g_lines.clear();
  g_errors = g_warns = 0;
}

std::string log_as_text(bool errors_only) {
  std::vector<LogEntry> snap = log_snapshot();
  std::string out;
  char stamp[64];
  for (const LogEntry &e : snap) {
    if (errors_only && e.level != LogLevel::Error) continue;
    std::snprintf(stamp, sizeof stamp, "[%9.3f] %s ", e.time, level_tag(e.level));
    out += stamp;
    if (!e.category.empty()) out += "[" + e.category + "] ";
    out += e.text;
    if (e.repeat > 1) out += "  (x" + std::to_string(e.repeat) + ")";
    out += "\n";
  }
  return out;
}

bool log_write_file(const std::string &path, bool errors_only) {
  std::ofstream f(path);
  if (!f) return false;
  f << log_as_text(errors_only);
  return (bool)f;
}

// ------------------------------------------------------------ stderr capture
// Third-party code writes to stderr and the app is a windowed process, so
// those messages go nowhere at all. Redirecting the stream into a pipe and
// draining it each frame puts them in the same list as everything else.
namespace {
std::FILE *g_err_pipe_read = nullptr;
int g_saved_stderr = -1;
} // namespace

} // namespace studio

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
namespace studio {
namespace {
int g_pipe_fd[2] = {-1, -1};
}

void log_capture_stderr() {
  if (g_pipe_fd[0] != -1) return;
  if (_pipe(g_pipe_fd, 1 << 16, _O_BINARY) != 0) return;
  g_saved_stderr = _dup(_fileno(stderr));
  _dup2(g_pipe_fd[1], _fileno(stderr));
  setvbuf(stderr, nullptr, _IONBF, 0); // unbuffered, or nothing arrives
}

// Drained by the panel each frame. Non-blocking: the read end is only touched
// when the OS says bytes are waiting, because a blocking read here would hang
// the UI thread whenever nothing had been written.
static void drain_stderr() {
  if (g_pipe_fd[0] == -1) return;
  for (;;) {
    long avail = 0;
    HANDLE h = (HANDLE)_get_osfhandle(g_pipe_fd[0]);
    DWORD bytes = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &bytes, nullptr) || bytes == 0)
      return;
    char buf[4096];
    int n = _read(g_pipe_fd[0], buf, (unsigned)std::min<DWORD>(bytes, sizeof buf - 1));
    if (n <= 0) return;
    buf[n] = 0;
    // split on newlines so one write does not become one giant entry
    std::string chunk(buf, buf + n);
    size_t start = 0;
    while (start < chunk.size()) {
      size_t nl = chunk.find('\n', start);
      std::string line = chunk.substr(start, nl == std::string::npos
                                                 ? std::string::npos
                                                 : nl - start);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
      if (!line.empty()) log_add(LogLevel::Warn, "stderr", line);
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
    (void)avail;
  }
}
} // namespace studio
#else
namespace studio {
void log_capture_stderr() {}
static void drain_stderr() {}
} // namespace studio
#endif

namespace studio {
void log_pump() { drain_stderr(); }
} // namespace studio
