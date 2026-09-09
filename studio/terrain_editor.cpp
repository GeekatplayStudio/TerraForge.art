// Geekatplay TerraForge — Vue's Terrain Editor as graph operations
// (see terrain_editor.hpp).
#include "terrain_editor.hpp"
#include "app.hpp"
#include "undo.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cctype>
#include <mutex>

namespace studio {

namespace {

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

gpx::Node *output_node(App &a) {
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainOutput") return n.get();
  return nullptr;
}

// The link feeding the output's heightmap, or null.
const gpx::Link *output_feed(App &a, gpx::Node *out) {
  for (const gpx::Link &l : a.graph.links)
    if (l.to_node == out->id && l.to_port == "heightmap") return &l;
  return nullptr;
}

// Every effect the editor can drop in, with the node it is and how Vue's
// Rock hardness (0 soft .. 1 hard) reaches its parameters.
struct Effect {
  const char *name, *type, *label;
  bool erosion;
  std::vector<std::pair<const char *, float>> floats; // fixed values
  std::vector<std::pair<const char *, int>> ints;
  const char *hard_key;  // parameter the hardness drives, or null
  float hard_lo, hard_hi; // its value at hardness 0 and 1
};
const std::vector<Effect> &effects() {
  static const std::vector<Effect> E = {
      // erosion (Vue's eight)
      {"diffusive", "Smooth", "Diffusive", true, {}, {}, "radius", 4.f, 1.f},
      {"thermal", "Thermal", "Thermal", true, {}, {{"iterations", 60}}, "angle_deg", 22.f, 42.f},
      {"glaciation", "Glaciation", "Glaciation", true, {}, {}, "hardness", 0.f, 1.f},
      {"wind", "Wind", "Wind", true, {}, {}, "strength", 0.8f, 0.2f},
      {"dissolve", "Dissolve", "Dissolve", true, {}, {}, "hardness", 0.f, 1.f},
      {"alluvium", "SedimentDeposit", "Alluvium", true, {}, {}, "amount", 0.6f, 0.2f},
      {"fluvial", "StreamPower", "Fluvial", true, {}, {{"iterations", 40}}, "k_erode", 0.2f, 0.05f},
      {"river valley", "Rivers", "River valley", true, {}, {}, "valley_width", 1.4f, 0.6f},
      // global effects (Vue's twelve)
      {"grit", "Grit", "Grit", false, {}, {}, nullptr, 0, 0},
      {"gravel", "Gravel", "Gravel", false, {}, {}, nullptr, 0, 0},
      {"pebbles", "Pebbles", "Pebbles", false, {}, {}, nullptr, 0, 0},
      {"stones", "FakeStones", "Stones", false, {}, {}, nullptr, 0, 0},
      {"peaks", "Peaks", "Peaks", false, {{"strength", 0.35f}}, {}, nullptr, 0, 0},
      {"fir trees", "FirTrees", "Fir trees", false, {}, {}, nullptr, 0, 0},
      {"plateaus", "Plateau", "Plateaus", false, {{"level", 0.7f}, {"softness", 0.15f}}, {}, nullptr, 0, 0},
      {"terraces", "Terrace", "Terraces", false, {{"shape", 2.f}}, {{"levels", 8}}, nullptr, 0, 0},
      {"stairs", "Terrace", "Stairs", false, {{"shape", 8.f}, {"mix", 1.f}}, {{"levels", 12}}, nullptr, 0, 0},
      {"craters", "Crater", "Craters", false, {}, {{"count", 12}}, nullptr, 0, 0},
      {"sharpen", "Sharpen", "Sharpen", false, {}, {}, nullptr, 0, 0},
      {"cracks", "Cracks", "Cracks", false, {}, {}, nullptr, 0, 0},
  };
  return E;
}

const Effect *find_effect(const std::string &name) {
  const std::string n = lower(name);
  for (const Effect &e : effects())
    if (n == e.name || n == lower(e.type)) return &e;
  return nullptr;
}

// A node the editor put in the chain: any effect type, the clip, the sculpt
// layer, Invert, the import blend.
bool is_editor_node(const std::string &type) {
  if (type == "TerrainClip" || type == "Invert" || type == "Smooth") return true;
  for (const Effect &e : effects())
    if (type == e.type) return true;
  return false;
}

} // namespace

// ------------------------------------------------------------- effects
const std::vector<std::string> &terrain_editor_effect_names(bool erosion) {
  static std::vector<std::string> ero, glob;
  if (ero.empty())
    for (const Effect &e : effects()) (e.erosion ? ero : glob).push_back(e.label);
  return erosion ? ero : glob;
}

bool terrain_editor_effect(App &a, const std::string &effect, float hardness, std::string &err) {
  const Effect *e = find_effect(effect);
  if (!e) {
    err = "terrain_effect: no effect called '" + effect + "'";
    return false;
  }
  std::vector<std::pair<const char *, float>> floats = e->floats;
  if (e->hard_key) {
    const float h = std::clamp(hardness, 0.f, 1.f);
    floats.push_back({e->hard_key, e->hard_lo + (e->hard_hi - e->hard_lo) * h});
  }
  const std::string what = std::string("Terrain effect: ") + e->label;
  terrain_insert_before_output(a, e->type, what.c_str(), floats, e->ints);
  if (a.status.rfind(what, 0) == 0 && a.status.size() > what.size()) {
    err = a.status; // the helper's own explanation ("no Terrain Output yet")
    return false;
  }
  return true;
}

int terrain_editor_effect_count(App &a) {
  std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
  gpx::Node *out = output_node(a);
  int n = 0;
  const gpx::Link *l = out ? output_feed(a, out) : nullptr;
  while (l) {
    gpx::Node *from = a.graph.find_node(l->from_node);
    if (!from || !is_editor_node(from->type)) break;
    ++n;
    const gpx::Link *next = nullptr;
    for (const gpx::Link &k : a.graph.links)
      if (k.to_node == from->id && k.to_port == "input") next = &k;
    l = next;
  }
  return n;
}

// ------------------------------------------------------------ clipping
TerrainClipState terrain_editor_clip_get(App &a) {
  TerrainClipState s;
  std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainClip") {
      s.present = true;
      n->attrs.get_range("clip", s.low, s.high);
      s.low_mode = n->attrs.get_choice("low_mode");
      s.high_mode = n->attrs.get_choice("high_mode");
      s.softness = n->attrs.get_f("softness", 0.f);
      break;
    }
  return s;
}

bool terrain_editor_clip_set(App &a, const TerrainClipState &s, std::string &err) {
  {
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    bool have = false;
    for (auto &n : a.graph.nodes)
      if (n->type == "TerrainClip") have = true;
    if (!have) {
      if (!output_node(a)) {
        err = "terrain_clip: there is no Terrain Output yet";
        return false;
      }
    }
    if (have) {
      for (auto &n : a.graph.nodes)
        if (n->type == "TerrainClip") {
          if (gpx::Attribute *at = n->attrs.find("clip")) {
            at->v2[0] = std::min(s.low, s.high);
            at->v2[1] = std::max(s.low, s.high);
          }
          if (gpx::Attribute *at = n->attrs.find("low_mode")) at->i = s.low_mode;
          if (gpx::Attribute *at = n->attrs.find("high_mode")) at->i = s.high_mode;
          if (gpx::Attribute *at = n->attrs.find("softness")) at->f = s.softness;
          a.graph.mark_dirty(n->id);
        }
      a.request_eval();
      return true;
    }
  }
  terrain_insert_before_output(a, "TerrainClip", "Terrain clipping",
                               {{"softness", s.softness}},
                               {{"low_mode", s.low_mode}, {"high_mode", s.high_mode}});
  std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainClip")
      if (gpx::Attribute *at = n->attrs.find("clip")) {
        at->v2[0] = std::min(s.low, s.high);
        at->v2[1] = std::max(s.low, s.high);
        a.graph.mark_dirty(n->id);
      }
  a.request_eval();
  return true;
}

// Remove one node from the chain, feeding what fed it to what it fed.
static bool unsplice(App &a, gpx::Node *n) {
  uint64_t from_node = 0;
  std::string from_port;
  for (const gpx::Link &l : a.graph.links)
    if (l.to_node == n->id && l.to_port == "input") {
      from_node = l.from_node;
      from_port = l.from_port;
    }
  std::vector<std::pair<uint64_t, std::string>> outs;
  for (const gpx::Link &l : a.graph.links)
    if (l.from_node == n->id) outs.push_back({l.to_node, l.to_port});
  a.graph.remove_node(n->id);
  if (from_node)
    for (auto &o : outs) a.graph.add_link(from_node, from_port, o.first, o.second);
  return true;
}

bool terrain_editor_clip_clear(App &a, std::string &err) {
  undo_push(a, "Remove terrain clipping");
  std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainClip") {
      unsplice(a, n.get());
      a.graph_layout_serial++;
      a.request_eval();
      return true;
    }
  err = "terrain_clip: there is no clipping to remove";
  return false;
}

// -------------------------------------------------------------- global
bool terrain_editor_global(App &a, const std::string &action_in, std::string &err) {
  const std::string action = lower(action_in);
  if (action == "invert") {
    terrain_insert_before_output(a, "Invert", "Invert terrain", {}, {});
    return true;
  }
  if (action == "smooth_all" || action == "retopologize") {
    terrain_insert_before_output(a, "Smooth", "Smooth the whole terrain", {{"radius", 2.f}}, {});
    return true;
  }
  if (action == "zero_edges") {
    undo_push(a, "Zero edges");
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    gpx::Node *out = output_node(a);
    if (!out) {
      err = "zero_edges: there is no Terrain Output yet";
      return false;
    }
    if (gpx::Attribute *at = out->attrs.find("zero_edges")) {
      at->f = at->f > 0.f ? 0.f : 0.12f;
      a.graph.mark_dirty(out->id);
      a.status = at->f > 0.f ? "zero edges on" : "zero edges off";
    }
    a.request_eval();
    return true;
  }
  if (action == "halve" || action == "double") {
    undo_push(a, action == "halve" ? "Halve resolution" : "Double resolution");
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    int r = a.graph.resolution;
    r = action == "halve" ? std::max(64, r / 2) : std::min(8192, r * 2);
    a.graph.resolution = r;
    a.graph.mark_all_dirty();
    a.request_eval();
    a.status = "resolution " + std::to_string(r);
    return true;
  }
  if (action == "reset_sculpt") {
    undo_push(a, "Reset sculpting");
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    bool any = false;
    for (auto &n : a.graph.nodes)
      if (n->type == "TerrainSculpt")
        if (gpx::Attribute *fa = n->attrs.find("delta")) {
          std::fill(fa->field.begin(), fa->field.end(), 0.f);
          a.graph.mark_dirty(n->id);
          any = true;
        }
    if (!any) {
      err = "reset_sculpt: nothing has been sculpted";
      return false;
    }
    a.request_eval();
    return true;
  }
  if (action == "remove_effects") {
    undo_push(a, "Remove terrain effects");
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    gpx::Node *out = output_node(a);
    int removed = 0;
    while (out) {
      const gpx::Link *l = output_feed(a, out);
      gpx::Node *from = l ? a.graph.find_node(l->from_node) : nullptr;
      if (!from || !is_editor_node(from->type)) break;
      unsplice(a, from);
      ++removed;
    }
    if (!removed) {
      err = "remove_effects: no effects in front of the Terrain Output";
      return false;
    }
    a.graph_layout_serial++;
    a.request_eval();
    a.status = std::to_string(removed) + " effect node(s) removed";
    return true;
  }
  err = "terrain_global: unknown action '" + action_in +
        "' (invert, zero_edges, smooth_all, halve, double, reset_sculpt, remove_effects)";
  return false;
}

// --------------------------------------------------------- import picture
bool terrain_editor_import_picture(App &a, const std::string &path, const std::string &mode_in,
                                   float proportion, std::string &err) {
  static const char *modes[] = {"blend", "add", "subtract", "multiply", "min", "max"};
  int mode = -1;
  const std::string m = lower(mode_in);
  for (int i = 0; i < 6; ++i)
    if (m == modes[i]) mode = i;
  if (mode < 0) {
    err = "import_picture: mode is blend, add, subtract, multiply, min or max";
    return false;
  }
  undo_push(a, "Import terrain picture");
  std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
  gpx::Node *out = output_node(a);
  if (!out) {
    err = "import_picture: there is no Terrain Output yet";
    return false;
  }
  const gpx::Link *feed = output_feed(a, out);
  gpx::Node *file = a.graph.add_node("HeightmapFile", out->pos_x - 400.f, out->pos_y + 220.f);
  if (!file) {
    err = "import_picture: HeightmapFile node unavailable";
    return false;
  }
  if (gpx::Attribute *at = file->attrs.find("path")) at->s = path;
  // no chain yet: the picture is the terrain
  if (!feed) {
    a.graph.add_link(file->id, "output", out->id, "heightmap");
  } else {
    gpx::Node *blend = a.graph.add_node("Blend", out->pos_x - 190.f, out->pos_y + 120.f);
    if (!blend) {
      err = "import_picture: Blend node unavailable";
      return false;
    }
    if (gpx::Attribute *at = blend->attrs.find("mode")) at->i = mode; // Mix, Add, Subtract, Multiply, Min, Max
    if (gpx::Attribute *at = blend->attrs.find("factor")) at->f = std::clamp(proportion, 0.f, 1.f);
    const uint64_t from_node = feed->from_node, feed_id = feed->id;
    const std::string from_port = feed->from_port;
    a.graph.remove_link(feed_id);
    a.graph.add_link(from_node, from_port, blend->id, "input A");
    a.graph.add_link(file->id, "output", blend->id, "input B");
    a.graph.add_link(blend->id, "output", out->id, "heightmap");
    a.selected_node = blend->id;
  }
  a.view_node = 0;
  a.graph_layout_serial++;
  a.request_eval();
  a.status = "picture imported: " + path;
  return true;
}

} // namespace studio
