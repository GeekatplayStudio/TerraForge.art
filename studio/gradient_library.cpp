// Geekatplay TerraForge — the gradient library: built-in natural gradients
// and the user's own, as JSON files.
#include "gradient_library.hpp"
#include "console.hpp"
#include <json.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace studio {

namespace {

// Stops are (t, r, g, b); alpha is 1 throughout - a natural gradient is a
// colour, and presence is the mask's job.
struct B {
  const char *name;
  std::vector<gpx::GradientStop> stops;
};
gpx::GradientStop S(float t, float r, float g, float b) { return {t, r, g, b, 1.f}; }

// Named for the place, ordered low to high (t is the height or the mask
// value the map is read with). Colours are in linear terms of what the
// renderer expects - the same range FractalColor's default map uses.
const std::vector<B> &builtins() {
  static const std::vector<B> v = {
      {"Alpine", {S(0.00f, 0.16f, 0.24f, 0.10f), S(0.30f, 0.30f, 0.42f, 0.16f),
                  S(0.52f, 0.42f, 0.38f, 0.28f), S(0.72f, 0.50f, 0.47f, 0.42f),
                  S(0.86f, 0.78f, 0.78f, 0.76f), S(1.00f, 0.96f, 0.97f, 0.98f)}},
      {"Temperate hills", {S(0.00f, 0.20f, 0.30f, 0.12f), S(0.40f, 0.34f, 0.44f, 0.18f),
                           S(0.70f, 0.48f, 0.46f, 0.30f), S(1.00f, 0.62f, 0.58f, 0.46f)}},
      {"Desert", {S(0.00f, 0.55f, 0.42f, 0.26f), S(0.35f, 0.72f, 0.58f, 0.36f),
                  S(0.70f, 0.82f, 0.70f, 0.48f), S(1.00f, 0.92f, 0.84f, 0.64f)}},
      {"Red rock", {S(0.00f, 0.34f, 0.16f, 0.10f), S(0.40f, 0.58f, 0.26f, 0.14f),
                    S(0.75f, 0.74f, 0.42f, 0.24f), S(1.00f, 0.86f, 0.62f, 0.42f)}},
      {"Volcanic", {S(0.00f, 0.06f, 0.05f, 0.05f), S(0.45f, 0.16f, 0.13f, 0.12f),
                    S(0.80f, 0.30f, 0.24f, 0.20f), S(1.00f, 0.44f, 0.38f, 0.32f)}},
      {"Tundra", {S(0.00f, 0.28f, 0.30f, 0.22f), S(0.45f, 0.44f, 0.42f, 0.32f),
                  S(0.80f, 0.60f, 0.58f, 0.52f), S(1.00f, 0.88f, 0.90f, 0.92f)}},
      {"Wetland", {S(0.00f, 0.08f, 0.14f, 0.10f), S(0.35f, 0.16f, 0.26f, 0.12f),
                   S(0.70f, 0.30f, 0.36f, 0.16f), S(1.00f, 0.46f, 0.44f, 0.24f)}},
      {"Ocean depth", {S(0.00f, 0.01f, 0.04f, 0.10f), S(0.40f, 0.02f, 0.10f, 0.22f),
                       S(0.80f, 0.06f, 0.24f, 0.36f), S(1.00f, 0.30f, 0.56f, 0.60f)}},
      {"Autumn", {S(0.00f, 0.22f, 0.18f, 0.08f), S(0.35f, 0.52f, 0.30f, 0.10f),
                  S(0.70f, 0.78f, 0.44f, 0.12f), S(1.00f, 0.90f, 0.72f, 0.30f)}},
      {"Snowfield", {S(0.00f, 0.50f, 0.54f, 0.58f), S(0.50f, 0.78f, 0.80f, 0.84f),
                     S(1.00f, 0.98f, 0.98f, 1.00f)}},
      {"Sandstone strata", {S(0.00f, 0.50f, 0.36f, 0.24f), S(0.25f, 0.70f, 0.52f, 0.34f),
                            S(0.50f, 0.56f, 0.40f, 0.28f), S(0.75f, 0.78f, 0.62f, 0.42f),
                            S(1.00f, 0.62f, 0.46f, 0.32f)}},
      {"Granite", {S(0.00f, 0.30f, 0.30f, 0.31f), S(0.50f, 0.52f, 0.50f, 0.50f),
                   S(1.00f, 0.74f, 0.72f, 0.70f)}},
      {"Sunset sky", {S(0.00f, 0.10f, 0.08f, 0.22f), S(0.40f, 0.64f, 0.22f, 0.24f),
                      S(0.75f, 0.96f, 0.56f, 0.24f), S(1.00f, 1.00f, 0.88f, 0.60f)}},
      {"Greyscale", {S(0.00f, 0.f, 0.f, 0.f), S(1.00f, 1.f, 1.f, 1.f)}},
  };
  return v;
}

std::vector<GradientPreset> g_lib;
bool g_scanned = false;

std::string sanitize(std::string s) {
  for (char &c : s)
    if (std::strchr("\\/:*?\"<>|", c)) c = '_';
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s.empty() ? "Gradient" : s;
}

bool read_file(const fs::path &p, GradientPreset &out) {
  std::ifstream f(p);
  if (!f) return false;
  json j = json::parse(f, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("stops")) return false;
  out.name = j.value("name", p.stem().string());
  out.path = p.string();
  out.stops.clear();
  for (const json &s : j["stops"]) {
    gpx::GradientStop g;
    g.t = s.value("t", 0.f);
    g.r = s.value("r", 0.f);
    g.g = s.value("g", 0.f);
    g.b = s.value("b", 0.f);
    g.a = s.value("a", 1.f);
    out.stops.push_back(g);
  }
  return out.stops.size() >= 2;
}

} // namespace

std::string gradient_library_dir() {
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path dir = base ? fs::path(base) : fs::temp_directory_path();
  dir = dir / "GeekatplayTerraForge" / "gradients";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir.string();
}

void gradient_library_rescan() {
  g_lib.clear();
  g_scanned = true;
  for (const B &b : builtins()) {
    GradientPreset p;
    p.name = b.name;
    p.stops = b.stops;
    p.builtin = true;
    g_lib.push_back(std::move(p));
  }
  std::vector<GradientPreset> mine;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(gradient_library_dir(), ec)) {
    if (e.path().extension() != ".json") continue;
    GradientPreset p;
    if (read_file(e.path(), p)) mine.push_back(std::move(p));
  }
  std::sort(mine.begin(), mine.end(),
            [](const GradientPreset &a, const GradientPreset &b) { return a.name < b.name; });
  for (auto &p : mine) g_lib.push_back(std::move(p));
}

const std::vector<GradientPreset> &gradient_library() {
  if (!g_scanned) gradient_library_rescan();
  return g_lib;
}

const GradientPreset *gradient_library_find(const std::string &name) {
  for (const GradientPreset &p : gradient_library())
    if (p.name == name) return &p;
  return nullptr;
}

std::string gradient_library_save(const std::string &name,
                                  const std::vector<gpx::GradientStop> &stops,
                                  std::string &err) {
  if (stops.size() < 2) {
    err = "a gradient needs at least two stops";
    return {};
  }
  const std::string safe = sanitize(name);
  for (const B &b : builtins())
    if (safe == b.name) {
      err = "'" + safe + "' is a built-in gradient; choose another name";
      return {};
    }
  json j;
  j["name"] = safe;
  json arr = json::array();
  for (const gpx::GradientStop &s : stops)
    arr.push_back({{"t", s.t}, {"r", s.r}, {"g", s.g}, {"b", s.b}, {"a", s.a}});
  j["stops"] = arr;
  fs::path file = fs::path(gradient_library_dir()) / (safe + ".json");
  std::ofstream f(file);
  if (!f) {
    err = "cannot write " + file.string();
    return {};
  }
  f << j.dump(2);
  f.close();
  log_info("gradients", "saved '" + safe + "'");
  gradient_library_rescan();
  return file.string();
}

bool gradient_library_erase(const std::string &name, std::string &err) {
  const GradientPreset *p = gradient_library_find(name);
  if (!p) {
    err = "no gradient named '" + name + "'";
    return false;
  }
  if (p->builtin) {
    err = "'" + name + "' is built in";
    return false;
  }
  std::error_code ec;
  fs::remove(p->path, ec);
  if (ec) {
    err = "could not remove " + p->path;
    return false;
  }
  gradient_library_rescan();
  return true;
}

} // namespace studio
