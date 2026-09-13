// Geekatplay TerraForge - the rock operations.
//
// Everything the interface can do, scripting can do. A rock is made by adding
// a Rock node and letting scene_rocks.cpp build it, so the op is mostly a
// matter of turning words into a recipe.
#include "ai_assist.hpp"
#include "app.hpp"
#include "gpx/rock.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace studio {

using nlohmann::json;

namespace {

void setf(gpx::Node &n, const char *key, float v) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Float) a->f = std::clamp(v, a->fmin, a->fmax);
}

// A free spot for a new node, right of everything already there.
void free_spot(gpx::Graph &g, float &x, float &y) {
  x = 120.f;
  y = 120.f;
  for (auto &n : g.nodes) x = std::max(x, n->pos_x + 240.f);
}

// One stone. Caller holds the graph lock.
gpx::Node *make_rock(App &a, gpx::RockType t, uint32_t seed, float size_m,
                     float x_m, float z_m, float heading, const std::string &name) {
  float x = 0.f, y = 0.f;
  free_spot(a.graph, x, y);
  gpx::Node *n = a.graph.add_node("Rock", x, y);
  if (!n) return nullptr;
  if (gpx::Attribute *at = n->attrs.find("type")) at->i = (int)t;
  if (gpx::Attribute *at = n->attrs.find("seed")) at->seed = seed ? seed : 1u;
  if (gpx::Attribute *at = n->attrs.find("object")) at->s = name;
  setf(*n, "size_m", size_m);
  setf(*n, "x_m", x_m);
  setf(*n, "z_m", z_m);
  setf(*n, "heading", heading);
  a.graph.mark_dirty(n->id);
  return n;
}

uint32_t roll(uint32_t &s) {
  s = s * 1664525u + 1013904223u;
  return s ? s : 1u;
}

} // namespace

int ai_rock_op(App &a, const std::string &op, const json &act, std::string &err) {
  if (op == "rock_kinds") {
    json out = json::array();
    for (int i = 0; i < (int)gpx::RockType::Count; ++i) {
      const gpx::RockType t = (gpx::RockType)i;
      out.push_back({{"kind", gpx::rock_type_name(t)}, {"made_by", gpx::rock_type_note(t)}});
    }
    a.api_reply = json{{"kinds", out}}.dump();
    a.status = std::to_string((int)gpx::RockType::Count) + " kinds of rock";
    return 1;
  }

  if (op != "add_rock") return -1;

  // ---- what kind -----------------------------------------------------------
  const std::string words = act.value("words", act.value("kind", act.value("prompt", std::string())));
  gpx::RockType type = gpx::RockType::Angular;
  bool any_kind = false;
  if (!words.empty()) {
    const gpx::RockType t = gpx::rock_type_from_words(words);
    if (t != gpx::RockType::Count) type = t;
    else {
      err = "add_rock: no kind of rock matches '" + words +
            "'. {\"op\":\"rock_kinds\"} lists them.";
      return 0;
    }
  } else {
    // no kind named: a mixture, which is what a real slope is
    any_kind = true;
  }

  const int count = std::clamp(act.value("count", 1), 1, 500);
  const float size_m = std::max(act.value("size_m", 1.f), 0.01f);
  // Rocks of one kind are not one size: a scree slope is mostly small stones
  // with a few large ones, so the spread is multiplicative rather than linear.
  const float spread = std::clamp(act.value("size_variation", 0.45f), 0.f, 0.95f);
  const float area_m = std::max(act.value("area_m", count > 1 ? 30.f : 0.f), 0.f);
  const float tile = std::max(render_settings().terrain_size_m, 1.f);
  // where: the middle of the tile unless told otherwise
  const float cx = act.value("x_m", tile * 0.5f);
  const float cz = act.value("z_m", tile * 0.5f);
  uint32_t seed = (uint32_t)act.value("seed", 0);
  if (!seed) seed = (uint32_t)(a.graph.nodes.size() * 2654435761u + 12345u);

  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(1500))) {
    err = "add_rock: the graph is busy, try again";
    return 0;
  }
  undo_push(a, count > 1 ? "Add rocks" : "Add a rock");

  int made = 0;
  for (int i = 0; i < count; ++i) {
    const gpx::RockType t =
        any_kind ? (gpx::RockType)(roll(seed) % (uint32_t)gpx::RockType::Count) : type;
    // the size, log-spread about what was asked for
    const float u = (float)roll(seed) / 4294967296.f * 2.f - 1.f;
    const float sz = size_m * std::exp(u * spread);
    float px = cx, pz = cz;
    if (area_m > 0.f) {
      // an even scatter over a disc: sqrt on the radius, or they crowd the
      // middle the way a naive random radius always does
      const float r = area_m * 0.5f * std::sqrt((float)roll(seed) / 4294967296.f);
      const float ang = (float)roll(seed) / 4294967296.f * 6.28318530718f;
      px += r * std::cos(ang);
      pz += r * std::sin(ang);
    }
    const float head = (float)roll(seed) / 4294967296.f * 360.f;
    const std::string nm = act.value("name", std::string());
    if (make_rock(a, t, roll(seed), sz, px, pz, head,
                  count > 1 || nm.empty() ? std::string() : nm))
      ++made;
  }
  if (!made) {
    err = "add_rock: could not add the node";
    return 0;
  }
  a.graph_layout_serial++;
  a.request_eval();
  a.status = "added " + std::to_string(made) + (made == 1 ? " rock" : " rocks") +
             (any_kind ? " of every kind" : std::string(" - ") + gpx::rock_type_name(type));
  return 1;
}

} // namespace studio
