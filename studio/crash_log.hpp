// Geekatplay TerraForge — the log on disk, and what happens when we crash.
//
// The console panel keeps messages in memory; a crash takes them with it.
// The event log then says only "ucrtbase.dll, 0xc0000409" — which is what
// abort(), std::terminate and a UCRT invalid-parameter all look like from
// outside, and none of them pass through a normal exception filter.
//
// So every log line is also appended to logs/terraforge_<stamp>.log under
// the project root and flushed at once, and handlers are installed for the
// three exits that leave no trace: std::terminate (with the exception's
// what()), SIGABRT, and the UCRT invalid-parameter path. Each writes
// logs/crash_<stamp>.txt with the reason and a stack as module+RVA, which
// scripts/resolve_crash.py turns into file:line with addr2line.
#pragma once
#include <string>

namespace studio {

// Where the logs go: $TERRAFORGE_LOG_DIR, else <project>/logs when the exe
// runs from <project>/build, else <exe dir>/logs. Created on first use.
std::string log_dir();

// Opens this run's log file and installs the crash handlers. Call first
// thing in main(), before anything can fail.
void crash_log_init(int argc, char **argv);

// Append one already-formatted line to the file (console.cpp calls this
// from log_add). Cheap: one fwrite + fflush.
void crash_log_line(const std::string &line);

// Flush and close cleanly; the last line says the exit was orderly.
void crash_log_shutdown();

// The current thread's stack as text, one "module+0xRVA" per line. Used by
// the handlers and available to any suspicious code path.
std::string crash_log_backtrace(int skip = 0);

} // namespace studio
