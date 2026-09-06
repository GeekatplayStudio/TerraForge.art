// Geekatplay TerraForge - checking GitHub for a newer version, and updating.
//
// The build carries the commit it was made from (TF_GIT_SHA, set by CMake)
// and the source tree it came from (TF_SOURCE_DIR). A check asks the GitHub
// API for the head of the configured branch; when it differs, the user is
// offered an update. Saying yes hands over to scripts/update.ps1 (or
// update.sh): the application exits, the script pulls the repository,
// rebuilds with the platform's build script, puts the new executable where
// this one runs from, and starts it again.
#pragma once
#include <string>

namespace studio {
struct App;

struct UpdateStatus {
  bool checking = false;
  bool checked = false;     // a check has completed (ok or not)
  bool available = false;   // GitHub's head differs from this build
  std::string local_sha;    // this build
  std::string remote_sha;   // GitHub's head
  std::string remote_date;  // commit date, as GitHub reports it
  std::string remote_message;
  std::string error;        // why a check failed
};

const UpdateStatus &update_status();

// Start a check in the background. Safe to call while one is running.
void update_check_async();

// Once a frame from app.cpp: collects the background check, and shows the
// "new version" dialog when one is found and the user has not dismissed it.
void update_ui(App &a);

// Hand over to the update script and close the application. Returns false
// (with a status message) when the script or the source tree is missing.
bool update_and_rebuild(App &a);

// The commit this build was made from, short, or "unknown".
std::string build_commit();
} // namespace studio
