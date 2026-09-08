// Geekatplay TerraForge — the crash/hang ledger (see crash_ledger.hpp).
#include "crash_ledger.hpp"
#include <json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace studio {

namespace {

const char *LEDGER = "crash_ledger.json";

json read_ledger(const fs::path &dir) {
  std::ifstream f(dir / LEDGER);
  if (!f) return json::object();
  json j = json::parse(f, nullptr, false);
  return j.is_object() ? j : json::object();
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
    s.pop_back();
  return s;
}

// The "reason:" line of a report, or its first non-empty line after the
// title.
std::string report_reason(const fs::path &p) {
  std::ifstream f(p);
  std::string line, first;
  int n = 0;
  while (std::getline(f, line) && n++ < 12) {
    line = trim(line);
    if (line.rfind("reason:", 0) == 0) {
      std::string r = line.substr(7);
      r.erase(0, r.find_first_not_of(' '));
      return trim(r);
    }
    if (first.empty() && n > 1 && !line.empty()) first = line;
  }
  return first;
}

// The last non-empty line of a log, read from its tail only - a log can be
// megabytes and this runs at startup.
std::string log_last_line(const fs::path &p, bool &clean_exit) {
  clean_exit = false;
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  f.seekg(0, std::ios::end);
  const std::streamoff size = f.tellg();
  const std::streamoff take = std::min<std::streamoff>(size, 4096);
  f.seekg(size - take);
  std::string tail((size_t)take, '\0');
  f.read(tail.data(), take);
  if (tail.find("=== clean exit") != std::string::npos) clean_exit = true;
  std::string last;
  std::istringstream ss(tail);
  std::string line;
  while (std::getline(ss, line)) {
    line = trim(line);
    if (!line.empty()) last = line;
  }
  if (size > take && !last.empty()) return last; // a partial first line was skipped
  return last;
}

std::string stamp_of(const std::string &name) {
  // crash_<stamp>.txt, hang_<stamp>.txt, terraforge_<stamp>.log
  const size_t us = name.find('_');
  const size_t dot = name.rfind('.');
  if (us == std::string::npos || dot == std::string::npos || dot <= us + 1) return {};
  return name.substr(us + 1, dot - us - 1);
}

} // namespace

std::vector<CrashReport> crash_reports(const std::string &dir_s,
                                       const std::string &current_stamp) {
  std::vector<CrashReport> out;
  const fs::path dir(dir_s);
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return out;
  const json ledger = read_ledger(dir);
  const json fixed = ledger.value("fixed", json::object());
  for (auto &e : fs::directory_iterator(dir, ec)) {
    if (!e.is_regular_file()) continue;
    const std::string name = e.path().filename().string();
    CrashReport r;
    r.file = name;
    r.stamp = stamp_of(name);
    if (name.rfind("crash_", 0) == 0 && e.path().extension() == ".txt") {
      r.kind = "crash";
      r.summary = report_reason(e.path());
    } else if (name.rfind("hang_", 0) == 0 && e.path().extension() == ".txt") {
      r.kind = "hang";
      r.summary = report_reason(e.path());
      // a hang the loop came back from is a stall: still worth a look,
      // and the file says so
      std::ifstream f(e.path());
      std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      if (all.find("\nrecovered:") != std::string::npos) r.summary += " (recovered)";
    } else if (name.rfind("terraforge_", 0) == 0 && e.path().extension() == ".log") {
      if (r.stamp == current_stamp) continue;
      bool clean = false;
      std::string last = log_last_line(e.path(), clean);
      if (clean) continue;
      // a report of its own already explains this session
      bool explained = fs::exists(dir / ("crash_" + r.stamp + ".txt"), ec) ||
                       fs::exists(dir / ("hang_" + r.stamp + ".txt"), ec);
      if (explained) continue;
      r.kind = "unclean";
      r.summary = "no clean exit; last line: " + last;
    } else {
      continue;
    }
    if (fixed.contains(name) && fixed[name].is_object()) {
      r.fixed = true;
      r.note = fixed[name].value("note", "");
    }
    out.push_back(std::move(r));
  }
  std::sort(out.begin(), out.end(),
            [](const CrashReport &a, const CrashReport &b) { return a.stamp > b.stamp; });
  return out;
}

bool crash_mark_fixed(const std::string &dir_s, const std::string &file,
                      const std::string &note, std::string &err) {
  const fs::path dir(dir_s);
  std::error_code ec;
  if (file.empty() || file.find('/') != std::string::npos ||
      file.find('\\') != std::string::npos) {
    err = "a report is named by its file name alone";
    return false;
  }
  if (!fs::exists(dir / file, ec)) {
    err = "no report named " + file + " in " + dir_s;
    return false;
  }
  json ledger = read_ledger(dir);
  if (!ledger.contains("fixed") || !ledger["fixed"].is_object())
    ledger["fixed"] = json::object();
  ledger["fixed"][file] = {{"note", note}};
  std::ofstream f(dir / LEDGER);
  if (!f) {
    err = "cannot write " + (dir / LEDGER).string();
    return false;
  }
  f << ledger.dump(2) << "\n";
  return true;
}

std::string crash_reports_json(const std::vector<CrashReport> &reports) {
  json arr = json::array();
  for (const CrashReport &r : reports)
    arr.push_back({{"file", r.file}, {"kind", r.kind}, {"stamp", r.stamp},
                   {"summary", r.summary}, {"fixed", r.fixed}, {"note", r.note}});
  return arr.dump();
}

std::string crash_reports_summary(const std::vector<CrashReport> &reports) {
  int open = 0;
  std::string detail;
  for (const CrashReport &r : reports) {
    if (r.fixed) continue;
    ++open;
    if (open <= 4) {
      if (!detail.empty()) detail += "; ";
      detail += r.kind + " " + r.stamp + " (" + r.summary.substr(0, 90) + ")";
    }
  }
  if (!open) return {};
  std::string s = std::to_string(open) + " open crash/hang report" + (open == 1 ? "" : "s") +
                  " from earlier sessions: " + detail;
  if (open > 4) s += "; ...";
  s += " - {\"op\":\"crash_reports\"} lists them, {\"op\":\"crash_mark_fixed\",\"file\":...} closes one";
  return s;
}

} // namespace studio
