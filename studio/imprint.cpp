// Geekatplay TerraForge - objects standing on the terrain. See the header.
//
// The footprint itself (the convex hull of the base, the margin, the blend
// distance) is computed in imprint_footprint.cpp, which has no App and so
// is tested on its own. This file is the studio's side: the node in the
// chain, the lock that holds the base on the surface, and the footprints
// text the node reads.
#include "imprint.hpp"
#include "app.hpp"
#include "gizmo.hpp"
#include "imprint_footprint.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <cmath>
#include <mutex>
#include <string>

namespace studio {

const gpx::Heightmap *app_natural_ground(); // app_upload.cpp

int imprint_ground_of(const SceneObject &o) {
  if (o.type != SceneObject::Mesh) return -1;
  const SceneState &sc = scene();
  if (o.parent < 0 || o.parent >= (int)sc.objects.size()) return -1;
  return sc.objects[o.parent].type == SceneObject::Terrain ? o.parent : -1;
}

int imprint_place_on_terrain(App &a, int object) {
  SceneState &sc = scene();
  if (object < 0 || object >= (int)sc.objects.size()) return -1;
  int terrain = -1;
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[i].type == SceneObject::Terrain) { terrain = i; break; }
  if (terrain < 0 || terrain == object) return -1;
  undo_push(a, "Place on terrain");
  int idx = scene_move_object(object, terrain + 1, terrain);
  if (idx >= 0 && idx < (int)sc.objects.size()) {
    sc.objects[idx].ground_lock = true;
    sc.objects[idx].ground_offset = 0.f;
    sc.selected = idx;
    a.scene_selection_serial++;
  }
  a.status = "placed on the terrain: the ground will mould to it";
  return idx;
}

namespace {

// The TerrainImprint node, spliced before TerrainOutput's heightmap the way
// the sculpt layer is. Caller holds graph_mtx.
gpx::Node *find_or_make_node(App &a) {
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainImprint") return n.get();
  if (a.graph.nodes.empty()) return nullptr;
  gpx::Node *out_node = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainOutput") out_node = n.get();
  gpx::Node *feed = nullptr;
  std::string feed_port = "output";
  if (out_node)
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == out_node->id && l.to_port == "heightmap") {
        feed = a.graph.find_node(l.from_node);
        feed_port = l.from_port;
      }
  if (!feed) {
    for (auto &cand : a.graph.nodes)
      if (cand->first_out(gpx::DataType::Heightmap) && cand->type != "TerrainOutput")
        feed = cand.get();
    if (feed) feed_port = feed->first_out(gpx::DataType::Heightmap)->name;
  }
  if (!feed) return nullptr;
  gpx::Node *im = a.graph.add_node("TerrainImprint", feed->pos_x + 190, feed->pos_y - 60);
  if (!im) return nullptr;
  a.graph.add_link(feed->id, feed_port, im->id, "input");
  if (out_node) {
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == out_node->id && l.to_port == "heightmap" && l.from_node == feed->id) {
        a.graph.remove_link(l.id);
        break;
      }
    a.graph.add_link(im->id, "output", out_node->id, "heightmap");
  }
  a.graph_layout_serial++;
  a.status = "TerrainImprint node added: the ground moulds to placed objects";
  return im;
}

// Every grounded object's line, as the node reads them.
std::string footprints_text(const SceneState &sc, float hs, bool &any) {
  std::string text;
  any = false;
  for (const SceneObject &o : sc.objects) {
    if (imprint_ground_of(o) < 0 || !o.enabled) continue;
    Footprint f = imprint_footprint(o, hs);
    if (!f.valid()) continue;
    any = true;
    text += imprint_footprint_line(f, o.ground_sink, o.ground_margin,
                                    o.ground_blend, o.ground_lift,
                                    o.ground_dig);
  }
  return text;
}

} // namespace

unsigned long long imprint_node(App &a) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) return 0;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainImprint") return n->id;
  return 0;
}

void app_service_imprint(App &a) {
  SceneState &sc = scene();
  const float hs = std::max(render_settings().height_scale, 1e-6f);
  bool any = false;
  std::string text = footprints_text(sc, hs, any);
  static std::string last;
  if (text == last && !any) return;

  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) return; // next frame, then
  gpx::Node *node = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainImprint") node = n.get();
  if (!node && any) node = find_or_make_node(a);

  // The lock: a grounded object's base rides on the natural ground - the
  // node's input, so the mould it makes cannot lift it. Dragging it up or
  // down in Y sets how far above or below the ground it sits.
  // the natural ground: the placed surface before the imprint (what the
  // viewport shows), else the node's own input
  const gpx::Heightmap *ground = app_natural_ground();
  if (!ground && node) ground = node->in_hmap("input");
  if (ground && !ground->v.empty()) {
    int drag_obj = -1, drag_axis = -1;
    const bool dragging = gizmo_dragging(drag_obj, drag_axis);
    bool moved = false;
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
      SceneObject &o = sc.objects[i];
      if (imprint_ground_of(o) < 0 || !o.ground_lock) continue;
      Footprint f = imprint_footprint(o, hs);
      if (!f.valid()) continue;
      // the ground under the footprint: its highest point, so no corner of
      // the base is ever below the natural surface unless the user sinks it
      float h = -1e30f;
      const size_t n = f.xz.size() / 2;
      for (size_t k = 0; k < n; ++k)
        h = std::max(h, ground->sample(std::clamp(f.xz[k * 2], 0.f, 1.f), std::clamp(f.xz[k * 2 + 1], 0.f, 1.f)));
      h = std::max(h, ground->sample(std::clamp(f.cx, 0.f, 1.f), std::clamp(f.cz, 0.f, 1.f)));
      const float base_rel = f.base - o.pos[1]; // the base below the pivot, heightmap units
      const float rest = h - base_rel;          // pos.y that puts the base on the ground
      if (dragging && drag_obj == i && drag_axis == 1) {
        o.ground_offset = o.pos[1] - rest; // the user is choosing the offset
      } else if (std::fabs(o.pos[1] - (rest + o.ground_offset)) > 1e-6f) {
        o.pos[1] = rest + o.ground_offset;
        moved = true;
      }
    }
    // the bases may have moved: rebuild the text so the node sees them
    if (moved) text = footprints_text(sc, hs, any);
  }
  if (text == last) return;
  last = text;
  if (!node) return;
  if (gpx::Attribute *at = node->attrs.find("footprints")) {
    if (at->s != text) {
      at->s = text;
      a.graph.mark_dirty(node->id);
      a.request_eval();
    }
  }
}

} // namespace studio
