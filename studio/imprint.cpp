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
    // The sunk depth joins the dead band. Without this the node saw a base
    // below the ground and dug the ground down to it, so "sink into the
    // ground" moved the ground instead of the object - which is the exact
    // opposite of what it says.
    // Sinking must not move the ground, in either direction. Above the
    // base the sunk depth joins the dead band; below it, whatever of the
    // natural unevenness the sink has not yet covered is left alone too.
    const float sunk = std::max(o.ground_sunk, 0.f);
    text += imprint_footprint_line(f, o.ground_sink + sunk, o.ground_margin,
                                    o.ground_blend, o.ground_lift,
                                    o.ground_dig,
                                    std::max(o.ground_uneven - sunk, 0.f));
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
    bool moved = false;
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
      SceneObject &o = sc.objects[i];
      if (imprint_ground_of(o) < 0 || !o.ground_lock) continue;
      Footprint f = imprint_footprint(o, hs);
      if (!f.valid()) continue;
      // The ground under the footprint, sampled at its corners and centre.
      //
      // Resting on the *highest* of those is what leaves a gap: on any
      // slope the base sits at the height of its highest corner and hangs
      // over everything else. `ground_settle` says where between the lowest
      // and the highest the base actually sits - 0 sinks it in until it
      // touches everywhere, 1 keeps it clear of the ground entirely.
      float hi = -1e30f, lo = 1e30f;
      const size_t n = f.xz.size() / 2;
      auto note = [&](float u, float v) {
        const float g = ground->sample(std::clamp(u, 0.f, 1.f),
                                       std::clamp(v, 0.f, 1.f));
        hi = std::max(hi, g);
        lo = std::min(lo, g);
      };
      for (size_t k = 0; k < n; ++k) note(f.xz[k * 2], f.xz[k * 2 + 1]);
      note(f.cx, f.cz);
      if (std::fabs(o.ground_uneven - (hi - lo)) > 1e-6f) moved = true;
      o.ground_uneven = hi - lo;
      const float h = hi - std::max(o.ground_sunk, 0.f);
      const float base_rel = f.base - o.pos[1]; // the base below the pivot
      const float rest = h - base_rel;          // pos.y that seats the base

      // The height is the user's to set, from anywhere.
      //
      // This used to write o.pos[1] every frame and read it back only while
      // a gizmo was being dragged on the Y axis, so typing an altitude in
      // the Transform panel, or setting one over the API, or keyframing one,
      // was silently overwritten on the next frame. The lock was not holding
      // the object to the surface, it was holding it away from the user.
      //
      // Now anything that moves the object is taken as a new altitude:
      // whatever changed pos[1] since the last pass wins, and becomes the
      // offset from the surface. What the lock still does - the thing it is
      // for - is carry that offset along as the object moves across the
      // ground, or as the ground itself changes underneath it.
      const GroundLockStep step = ground_lock_step(
          o.pos[1], o.ground_last_y, o.ground_seen_y, rest, o.ground_offset);
      o.ground_offset = step.offset;
      if (std::fabs(o.pos[1] - step.y) > 1e-6f) {
        o.pos[1] = step.y;
        moved = true;
      }
      o.ground_last_y = o.pos[1];
      o.ground_seen_y = true;
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
