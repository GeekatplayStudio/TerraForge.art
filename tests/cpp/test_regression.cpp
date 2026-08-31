// Geekatplay TerraForge — regression lock (test tier 5).
//
// This suite exists to answer one question: "did we lose anything?" It is the
// safety net for the P0 framework refactor, so it is deliberately blunt —
// it compares today's build against recorded baselines and fails on any
// silent removal.
//
//   1. Node census      — every node type ever shipped must still exist, in the
//                         same category. Nodes may be added, never removed.
//   2. Attribute census — every attribute of every node must still exist with
//                         the same type, so old projects keep loading.
//   3. Golden corpus    — committed .gpxt projects must re-evaluate to the same
//                         bits they did when they were recorded.
//   4. Feature manifest — every shipped user-visible capability names a test
//                         that must still exist in the sources.
//
// Run with --update to re-record the baselines after an intentional change.
// Doing so is a deliberate, reviewable act: the diff shows exactly what moved.
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    ++g_checks;                                                                 \
    if (!(cond)) {                                                              \
      std::printf("  [FAIL] %s\n", std::string(msg).c_str());                   \
      g_failures++;                                                             \
    }                                                                           \
  } while (0)

// ---------------------------------------------------------------- utilities
static fs::path manifest_dir() {
  // the binary lives in build/, the manifest with the sources
  for (fs::path p : {fs::path("tests/manifest"), fs::path("../tests/manifest")})
    if (fs::exists(p)) return p;
  fs::create_directories("tests/manifest");
  return "tests/manifest";
}

static fs::path projects_dir() {
  for (fs::path p : {fs::path("tests/projects"), fs::path("../tests/projects")})
    if (fs::exists(p)) return p;
  return "tests/projects";
}

static fs::path tests_src_dir() {
  for (fs::path p : {fs::path("tests/cpp"), fs::path("../tests/cpp")})
    if (fs::exists(p)) return p;
  return "tests/cpp";
}

static std::vector<std::string> read_lines(const fs::path &p) {
  std::vector<std::string> out;
  std::ifstream f(p);
  std::string line;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.pop_back();
    if (!line.empty() && line[0] != '#') out.push_back(line);
  }
  return out;
}

static void write_lines(const fs::path &p, const std::vector<std::string> &v,
                        const char *header) {
  std::ofstream f(p);
  f << "# " << header << "\n";
  f << "# Regenerate with: regression_tests --update  (review the diff!)\n";
  for (const std::string &s : v) f << s << "\n";
}

static std::string read_file(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
}

// FNV-1a over raw float bytes: stable across runs and platforms with IEEE
// floats, which is exactly the determinism guarantee we already enforce.
static uint64_t hash_floats(const std::vector<float> &v) {
  uint64_t h = 1469598103934665603ull;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(v.data());
  size_t n = v.size() * sizeof(float);
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

static const char *attr_type_name(gpx::AttrType t) {
  switch (t) {
    case gpx::AttrType::Float: return "float";
    case gpx::AttrType::Int: return "int";
    case gpx::AttrType::Bool: return "bool";
    case gpx::AttrType::Choice: return "choice";
    case gpx::AttrType::Seed: return "seed";
    case gpx::AttrType::Range: return "range";
    case gpx::AttrType::Vec2: return "vec2";
    case gpx::AttrType::Color: return "color";
    case gpx::AttrType::Gradient: return "gradient";
    case gpx::AttrType::Filename: return "filename";
    case gpx::AttrType::Text: return "text";
    case gpx::AttrType::Field: return "field";
  }
  return "?";
}

// ------------------------------------------------------------- node census
// "type|category" per line. A node may never disappear: old projects name it.
static std::vector<std::string> current_node_census() {
  std::vector<std::string> out;
  for (const gpx::NodeDef *d : gpx::NodeRegistry::instance().all())
    out.push_back(d->type + "|" + d->category);
  std::sort(out.begin(), out.end());
  return out;
}

static void check_node_census(bool update) {
  std::printf("node census...\n");
  fs::path p = manifest_dir() / "nodes.txt";
  std::vector<std::string> now = current_node_census();
  if (update || !fs::exists(p)) {
    write_lines(p, now, "Every node type ever shipped, as type|category.");
    std::printf("  recorded %d node types\n", (int)now.size());
    return;
  }
  std::vector<std::string> recorded = read_lines(p);
  std::set<std::string> now_set(now.begin(), now.end());
  std::map<std::string, std::string> now_cat;
  for (const std::string &s : now) {
    size_t bar = s.find('|');
    now_cat[s.substr(0, bar)] = s.substr(bar + 1);
  }
  for (const std::string &r : recorded) {
    size_t bar = r.find('|');
    std::string type = r.substr(0, bar), cat = r.substr(bar + 1);
    auto it = now_cat.find(type);
    CHECK(it != now_cat.end(),
          "node '" + type + "' was REMOVED (old projects would break)");
    if (it != now_cat.end())
      CHECK(it->second == cat, "node '" + type + "' changed category: " + cat +
                                   " -> " + it->second);
  }
  int added = (int)now.size() - (int)recorded.size();
  std::printf("  %d recorded, %d present (%+d)\n", (int)recorded.size(),
              (int)now.size(), added);
}

// -------------------------------------------------------- attribute census
// "Node.attr|type" per line. Removing or retyping an attribute silently
// changes how an old project evaluates, so both are failures.
static std::vector<std::string> current_attr_census() {
  std::vector<std::string> out;
  gpx::Graph g;
  g.resolution = 16;
  for (const gpx::NodeDef *d : gpx::NodeRegistry::instance().all()) {
    gpx::Node *n = g.add_node(d->type);
    if (!n) continue;
    for (const gpx::Attribute &a : n->attrs.items)
      out.push_back(d->type + "." + a.key + "|" + attr_type_name(a.type));
    g.remove_node(n->id);
  }
  std::sort(out.begin(), out.end());
  return out;
}

static void check_attr_census(bool update) {
  std::printf("attribute census...\n");
  fs::path p = manifest_dir() / "attributes.txt";
  std::vector<std::string> now = current_attr_census();
  if (update || !fs::exists(p)) {
    write_lines(p, now, "Every node attribute, as Node.attr|type.");
    std::printf("  recorded %d attributes\n", (int)now.size());
    return;
  }
  std::set<std::string> now_set(now.begin(), now.end());
  std::map<std::string, std::string> now_type;
  for (const std::string &s : now) {
    size_t bar = s.find('|');
    now_type[s.substr(0, bar)] = s.substr(bar + 1);
  }
  std::vector<std::string> recorded = read_lines(p);
  for (const std::string &r : recorded) {
    size_t bar = r.find('|');
    std::string key = r.substr(0, bar), type = r.substr(bar + 1);
    auto it = now_type.find(key);
    CHECK(it != now_type.end(), "attribute '" + key + "' was REMOVED");
    if (it != now_type.end())
      CHECK(it->second == type,
            "attribute '" + key + "' changed type: " + type + " -> " + it->second);
  }
  std::printf("  %d recorded, %d present\n", (int)recorded.size(),
              (int)now.size());
}

// ------------------------------------------------------------ golden corpus
// Committed projects must re-evaluate to the same bits. This is the real
// "old files still work, and still look the same" guarantee.
static void check_golden_projects(bool update) {
  std::printf("golden project corpus...\n");
  fs::path pdir = projects_dir();
  if (!fs::exists(pdir)) {
    std::printf("  (no corpus yet)\n");
    return;
  }
  std::vector<fs::path> files;
  for (const auto &e : fs::directory_iterator(pdir))
    if (e.path().extension() == ".gpxt") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    std::printf("  (no corpus yet)\n");
    return;
  }

  std::vector<std::string> lines;
  fs::path gp = manifest_dir() / "goldens.txt";
  std::map<std::string, std::string> recorded;
  if (!update && fs::exists(gp))
    for (const std::string &L : read_lines(gp)) {
      size_t bar = L.find('|');
      if (bar != std::string::npos)
        recorded[L.substr(0, bar)] = L.substr(bar + 1);
    }

  for (const fs::path &f : files) {
    gpx::Graph g;
    std::string err;
    std::string text = read_file(f);
    bool ok = gpx::graph_from_json(g, text, err);
    CHECK(ok, "project '" + f.filename().string() + "' loads: " + err);
    if (!ok) continue;
    // fixed small resolution keeps the corpus fast; determinism is what we
    // are testing, not visual fidelity
    g.resolution = 64;
    g.mark_all_dirty();
    g.evaluate();
    // hash every heightmap output of every node, in node order, so any
    // behaviour change anywhere in the chain shows up
    uint64_t h = 1469598103934665603ull;
    int outputs = 0;
    for (const auto &n : g.nodes)
      for (const gpx::Port &port : n->ports) {
        if (port.dir != gpx::PortDir::Out || !port.hmap) continue;
        uint64_t ph = hash_floats(port.hmap->v);
        h ^= ph + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        ++outputs;
      }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    std::string key = f.filename().string();
    lines.push_back(key + "|" + buf);
    if (!update && !recorded.empty()) {
      auto it = recorded.find(key);
      CHECK(it != recorded.end(), "project '" + key + "' has no golden");
      if (it != recorded.end())
        CHECK(it->second == buf,
              "project '" + key + "' CHANGED: golden " + it->second + " now " +
                  buf);
    }
    std::printf("  %-34s %d outputs  %s\n", key.c_str(), outputs, buf);
  }
  if (update || !fs::exists(gp))
    write_lines(gp, lines, "Golden evaluation hashes per project.");
}

// ---------------------------------------------------------- feature manifest
// The manifest is a human-auditable list of shipped capabilities. Machine
// verification: every entry names a test symbol that must still exist in the
// test sources. Delete the test and the manifest fails, which is the point.
struct Feature {
  std::string id, area, desc, test_file, test_symbol;
};

// deliberately tiny hand-rolled reader: the manifest is ours, the format is
// fixed, and this avoids dragging a JSON dependency into this binary
static std::string json_field(const std::string &obj, const char *key) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = obj.find(k);
  if (p == std::string::npos) return "";
  p = obj.find(':', p + k.size());
  if (p == std::string::npos) return "";
  size_t q = obj.find('"', p);
  if (q == std::string::npos) return "";
  size_t e = q + 1;
  std::string out;
  while (e < obj.size() && obj[e] != '"') {
    if (obj[e] == '\\' && e + 1 < obj.size()) ++e;
    out += obj[e++];
  }
  return out;
}

static void check_feature_manifest() {
  std::printf("feature manifest...\n");
  fs::path p = manifest_dir() / "features.json";
  if (!fs::exists(p)) {
    std::printf("  [FAIL] features.json missing\n");
    ++g_failures;
    return;
  }
  std::string text = read_file(p);
  // split on top-level objects inside the "features" array
  std::vector<Feature> feats;
  size_t pos = 0;
  while ((pos = text.find('{', pos)) != std::string::npos) {
    size_t end = text.find('}', pos);
    if (end == std::string::npos) break;
    std::string obj = text.substr(pos, end - pos + 1);
    Feature f;
    f.id = json_field(obj, "id");
    f.area = json_field(obj, "area");
    f.desc = json_field(obj, "description");
    f.test_file = json_field(obj, "test_file");
    f.test_symbol = json_field(obj, "test_symbol");
    if (!f.id.empty()) feats.push_back(f);
    pos = end + 1;
  }
  CHECK(!feats.empty(), "manifest parsed at least one feature");

  std::map<fs::path, std::string> cache;
  std::set<std::string> ids;
  for (const Feature &f : feats) {
    CHECK(ids.insert(f.id).second, "feature id '" + f.id + "' is unique");
    CHECK(!f.desc.empty(), "feature '" + f.id + "' has a description");
    CHECK(!f.test_file.empty() && !f.test_symbol.empty(),
          "feature '" + f.id + "' names an owning test");
    if (f.test_file.empty()) continue;
    fs::path tf = tests_src_dir() / f.test_file;
    auto it = cache.find(tf);
    if (it == cache.end()) {
      cache[tf] = fs::exists(tf) ? read_file(tf) : std::string();
      it = cache.find(tf);
    }
    CHECK(!it->second.empty(),
          "feature '" + f.id + "': test file " + f.test_file + " exists");
    if (!it->second.empty())
      CHECK(it->second.find(f.test_symbol) != std::string::npos,
            "feature '" + f.id + "': owning test '" + f.test_symbol +
                "' still present in " + f.test_file);
  }
  std::printf("  %d features, all owned\n", (int)feats.size());
}

int main(int argc, char **argv) {
  bool update = false;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--update") == 0) update = true;

  std::printf("Geekatplay TerraForge - regression lock%s\n\n",
              update ? " (UPDATING BASELINES)" : "");
  check_node_census(update);
  check_attr_census(update);
  check_golden_projects(update);
  if (!update) check_feature_manifest();

  std::printf("\n%d checks, %s\n", g_checks,
              g_failures ? "FAILED" : "all passed");
  if (update) {
    std::printf("Baselines written. Review the diff before committing.\n");
    return 0;
  }
  return g_failures ? 1 : 0;
}
