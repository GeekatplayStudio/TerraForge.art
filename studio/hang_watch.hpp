// Geekatplay TerraForge — the hang watchdog.
//
// A crash leaves a report (crash_log.hpp). A hang leaves nothing: the window
// stops pumping messages, the user kills the process, and the log's last
// line is whatever happened just before - which is how the material
// library's double lock went unexplained for a day. The same-thread
// re-lock of the graph mutex does not fail, it waits forever, and every
// one of those looks identical from outside.
//
// So a second thread watches the main loop's heartbeat. When no frame has
// finished for `seconds`, it suspends the main thread for a moment, walks
// its stack, and writes logs/hang_<stamp>.txt with the frames as
// module+RVA (the same shape the crash reports use, resolved the same way
// by scripts/resolve_crash.py). If the frame later completes, the file is
// appended with how long the stall lasted, so a slow-but-finished
// evaluation reads differently from a deadlock. A window being moved or
// resized blocks the loop by design on Windows and is not reported.
#pragma once

namespace studio {

// Starts the watcher thread; `seconds` is the stall that counts as a hang
// (0 disables). Call from the main thread after the window exists.
void hang_watch_start(int seconds);

// Once per frame, from the main thread. Cheap: one atomic store.
void hang_watch_beat();

// A native modal dialog (file open/save) blocks the loop legitimately:
// bracket it with pause(true) / pause(false).
void hang_watch_pause(bool paused);

// Stop the thread before teardown.
void hang_watch_stop();

// Hangs reported this run, and the longest stall seen (seconds).
int hang_watch_count();
double hang_watch_longest();

} // namespace studio
