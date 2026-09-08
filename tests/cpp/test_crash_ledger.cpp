// Geekatplay TerraForge - the crash/hang ledger on a scratch directory:
// a crash report, a hang report, a log that exited cleanly, a log that did
// not, and the current session's log; which are listed, what each says,
// and that marking one fixed sticks and drops it from the summary.
#include "crash_ledger.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace studio;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void put(const fs::path &p, const std::string &text) {
  std::ofstream f(p, std::ios::binary);
  f << text;
}

static const CrashReport *find(const std::vector<CrashReport> &v, const char *file) {
  for (const CrashReport &r : v)
    if (r.file == file) return &r;
  return nullptr;
}

int main() {
  fs::path dir = fs::temp_directory_path() / "tf_crash_ledger_test";
  fs::remove_all(dir);
  fs::create_directories(dir);

  put(dir / "crash_20260902_220023.txt",
      "Geekatplay TerraForge crash report\nreason: std::terminate\nuncaught: x\n");
  put(dir / "hang_20260908_120000.txt",
      "Geekatplay TerraForge hang report\nreason: the main thread did not finish a frame for 6.2 s\n\nstack\n\nrecovered: the frame completed after 9.0 s\n");
  put(dir / "terraforge_20260908_090000.log", "start\nwork\n=== clean exit\n");
  put(dir / "terraforge_20260908_094858.log",
      "start\n[   72.064] info  [materials] base material 'Smoke' created\n");
  put(dir / "terraforge_20260908_130000.log", "start\nstill running\n");
  // a killed session that already has a hang report is explained by it
  put(dir / "terraforge_20260908_120000.log", "start\nwedged\n");
  // a big log whose last line is what matters
  std::string big(20000, 'x');
  big += "\n[  999.000] info  [api] the last thing\n";
  put(dir / "terraforge_20260907_010000.log", big);

  std::printf("listing...\n");
  auto reps = crash_reports(dir.string(), "20260908_130000");
  check(reps.size() == 4, "crash + hang + two unclean logs (current and explained excluded)");
  const CrashReport *c = find(reps, "crash_20260902_220023.txt");
  check(c && c->kind == "crash" && c->summary == "std::terminate", "the crash's reason line");
  const CrashReport *h = find(reps, "hang_20260908_120000.txt");
  check(h && h->kind == "hang" && h->summary.find("(recovered)") != std::string::npos,
        "a hang the loop came back from says so");
  const CrashReport *u = find(reps, "terraforge_20260908_094858.log");
  check(u && u->kind == "unclean" && u->summary.find("'Smoke' created") != std::string::npos,
        "an unclean log carries its last line");
  const CrashReport *b = find(reps, "terraforge_20260907_010000.log");
  check(b && b->summary.find("the last thing") != std::string::npos, "a large log is read from its tail");
  check(!find(reps, "terraforge_20260908_090000.log"), "a clean exit is not a report");
  check(!find(reps, "terraforge_20260908_130000.log"), "the current session is not a report");
  check(!find(reps, "terraforge_20260908_120000.log"), "a log explained by a hang report is not listed twice");
  check(reps.front().stamp >= reps.back().stamp, "newest first");
  check(!c->fixed && c->note.empty(), "nothing is fixed yet");

  std::printf("summary...\n");
  std::string s = crash_reports_summary(reps);
  check(s.find("4 open") == 0, "the summary counts the open ones");
  check(s.find("crash_mark_fixed") != std::string::npos, "and says how to close one");

  std::printf("marking fixed...\n");
  std::string err;
  check(crash_mark_fixed(dir.string(), "crash_20260902_220023.txt", "was debug_crash", err), "mark fixed");
  check(!crash_mark_fixed(dir.string(), "nope.txt", "", err) && !err.empty(), "an unknown file is refused");
  check(!crash_mark_fixed(dir.string(), "../x.txt", "", err), "a path is refused");
  reps = crash_reports(dir.string(), "20260908_130000");
  c = find(reps, "crash_20260902_220023.txt");
  check(c && c->fixed && c->note == "was debug_crash", "the mark sticks and keeps its note");
  check(crash_reports_summary(reps).find("3 open") == 0, "the fixed one leaves the summary");
  std::string j = crash_reports_json(reps);
  check(j.find("\"fixed\":true") != std::string::npos && j.find("\"kind\":\"hang\"") != std::string::npos,
        "the json carries kind and fixed");
  check(crash_reports("C:/definitely/not/here", "").empty(), "a missing directory is empty, not an error");

  fs::remove_all(dir);
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("crash ledger tests OK\n");
  return 0;
}
