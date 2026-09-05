// Geekatplay TerraForge — autosave and crash recovery.
//
// A creator application that can lose an afternoon of work to one crash has
// its priorities wrong, and now that a project file carries the whole scene
// (scene_io), saving one costs milliseconds. So the application saves itself:
// every couple of minutes when something has changed, into a rotation of
// three files, and on the next start after an unclean exit it offers the
// newest one back.
//
// Unclean exit is detected the boring way that works: a lock file written at
// startup and removed on orderly shutdown. If it is still there when we
// start, the last session did not end on its own terms.
#pragma once
#include <string>

namespace studio {
struct App;

// Where the autosaves and the lock live (%LOCALAPPDATA%/GeekatplayTerraForge/
// autosave). Overridable for tests.
std::string autosave_dir();
void autosave_set_dir(const std::string &dir);

// Call once per frame. Cheap when there is nothing to do: it saves only when
// the undo history has moved since the last autosave and `interval_s` has
// passed, into autosave_1/2/3.gpxt in rotation.
void autosave_tick(App &a, double now_seconds, double interval_s = 120.0);
// Wait for a pending disk write on orderly shutdown (also used by tests).
void autosave_flush();

// Startup / shutdown bookkeeping around the lock file.
void autosave_session_begin(); // write the lock (and remember if one existed)
void autosave_session_end();   // remove it: this was an orderly exit

// True when the previous session ended uncleanly AND an autosave exists to
// offer; `path_out` names the newest one.
bool autosave_crash_recovery_available(std::string &path_out);

// The dialog marks its answer through this, so asking once is enough.
void autosave_mark_recovery_answered();

// The "Restore last session?" dialog. Call once per frame from the UI; it
// shows itself only when recovery is available and not yet answered.
void autosave_recovery_dialog(App &a);

// pure helper, exposed for the tests: which slot to write next given the
// slot written last (0-based, 3 slots)
int autosave_next_slot(int last_slot);

} // namespace studio
