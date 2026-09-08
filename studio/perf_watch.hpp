// Geekatplay TerraForge — the performance watcher.
//
// The governor (perf.hpp) keeps the viewport responsive by lightening what
// costs the most. This is the other half: finding out *why* it cost. It
// watches the frame phases and a set of event counters the subsystems bump
// (evaluations, uploads, view draws, lock misses, frames that did work while
// nothing changed) and every few seconds turns them into findings - "the
// secondary views were redrawn 40 times a second while nothing moved", "the
// graph was evaluated 6 times in a second the user touched nothing" - which
// it logs and writes to logs/perf_watch.json for a person or an agent to
// act on. Nothing here changes what the application does; it only says
// where the time went and which of it was for nothing.
#pragma once
#include <string>

namespace studio {
struct App;

// Bump a named event counter. Cheap enough to call from a hot path.
void perf_count(const char *name, int n = 1);

// Once a frame, after the governor. Samples, and every WATCH_PERIOD_S
// writes the report if the watcher is on (config().perf.watch).
void perf_watch_tick(App &a);

// The latest report as JSON text (what perf_watch.json holds), and the
// findings alone as one line per finding - for the status line, the
// console and the `perf_report` op.
std::string perf_watch_report_json();
std::string perf_watch_findings_text();

// Force a report now, regardless of the period.
void perf_watch_report_now(App &a);

} // namespace studio
