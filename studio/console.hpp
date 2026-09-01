// Geekatplay TerraForge — the message log.
//
// Errors used to appear as a tooltip on hover, or a line in the status bar, or
// on stderr behind a windowed process, and every one of them was gone before
// it could be read, let alone copied. A message that cannot be quoted cannot
// be reported.
//
// Everything now lands here: shader link failures, node errors, file I/O,
// the AI bridge, the API, and anything that used to go to stderr. The panel
// keeps them, lets them be selected and copied, and writes them to a file.
#pragma once
#include <string>
#include <vector>

namespace studio {

enum class LogLevel { Trace = 0, Info, Warn, Error };

struct LogEntry {
  LogLevel level = LogLevel::Info;
  double time = 0;        // seconds since launch
  std::string category;   // "shader", "graph", "io", "ai", "api", "render"...
  std::string text;
  int repeat = 1;         // identical consecutive messages collapse into one
};

// Thread-safe: the evaluation worker and the API thread both log.
void log_add(LogLevel lvl, const char *category, const std::string &text);

inline void log_info(const char *cat, const std::string &t)  { log_add(LogLevel::Info,  cat, t); }
inline void log_warn(const char *cat, const std::string &t)  { log_add(LogLevel::Warn,  cat, t); }
inline void log_error(const char *cat, const std::string &t) { log_add(LogLevel::Error, cat, t); }
inline void log_trace(const char *cat, const std::string &t) { log_add(LogLevel::Trace, cat, t); }

// printf-style, for call sites that already build a message that way.
void log_fmt(LogLevel lvl, const char *category, const char *fmt, ...);

// A snapshot for the panel to draw from, so drawing never holds the log lock.
std::vector<LogEntry> log_snapshot();
size_t log_error_count();
size_t log_warn_count();
void log_clear();
// Everything, as one block of text, for the clipboard or a file.
std::string log_as_text(bool errors_only = false);
bool log_write_file(const std::string &path, bool errors_only = false);

// Captures anything written to stderr from this point on, so a message from a
// third-party library ends up in the same place as ours.
void log_capture_stderr();
// Drains anything written to stderr into the log. Called once per frame.
void log_pump();

struct App;
void draw_console(App &a);

} // namespace studio
