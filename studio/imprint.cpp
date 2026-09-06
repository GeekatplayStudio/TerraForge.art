// Geekatplay TerraForge - objects standing on the terrain. See the header.
#include "imprint.hpp"
#include "app.hpp"
#include "gizmo.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

namespace studio {

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

// The footprint of a mesh on the tile, in tile fractions: the bounds'
// half-extents scaled, centred on the object, turned by its heading.
struct Print {
  float cx, cz, rx, rz, rot, base_rel; // base_rel: base below pos.y, heightmap units
};

Print print_of(const SceneObject &o) {
  const float hs = std::max(render_settings().height_scale, 1e-5f);
  const float sx = o.scale * o.scl[0], sy = o.scale * o.scl[1], sz = o.scale * o.scl[2];
  const float bcx = 0.5f * (o.bmin[0] + o.bmax[0]) * sx;
  const float bcz = 0.5f * (o.bmin[2] + o.bmax[2]) * sz;
  const float r = o.yaw * 0.017453292519943295f;
  Print p;
  // a touch of margin, so the flat patch reaches just past the walls
  p.rx = 0.5f * (o.bmax[0] - o.bmin[0]) * std::fabs(sx) * 1.06f;
  p.rz = 0.5f * (o.bmax[2] - o.bmin[2]) * std::fabs(sz) * 1.06f;
  p.cx = o.pos[0] + bcx * std::cos(r) + bcz * std::sin(r);
  p.cz = o.pos[2] - bcx * std::sin(r) + bcz * std::cos(r);
  p.rot = o.yaw;
  p.base_rel = o.bmin[1] * sy / hs;
  return p;
}

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
  // the footprints, as the node reads them
  std::string text;
  bool any = false;
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (imprint_ground_of(o) < 0 || !o.enabled) continue;
    any = true;
    Print p = print_of(o);
    char line[160];
    std::snprintf(line, sizeof line, "%.6f %.6f %.6f %.6f %.3f %.6f\n", p.cx, p.cz, p.rx, p.rz,
                  p.rot, o.pos[1] + p.base_rel);
    text += line;
  }
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
  const gpx::Heightmap *ground = node ? node->in_hmap("input") : nullptr;
  if (ground && !ground->v.empty()) {
    int drag_obj = -1, drag_axis = -1;
    const bool dragging = gizmo_dragging(drag_obj, drag_axis);
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
      SceneObject &o = sc.objects[i];
      if (imprint_ground_of(o) < 0 || !o.ground_lock) continue;
      Print p = print_of(o);
      const float h = ground->sample(std::clamp(p.cx, 0.f, 1.f), std::clamp(p.cz, 0.f, 1.f));
      const float rest = h - p.base_rel; // pos.y that puts the base on the ground
      if (dragging && drag_obj == i && drag_axis == 1) {
        o.ground_offset = o.pos[1] - rest; // the user is choosing the offset
      } else if (std::fabs(o.pos[1] - (rest + o.ground_offset)) > 1e-6f) {
        o.pos[1] = rest + o.ground_offset;
      }
    }
    // the bases may have moved: rebuild the text so the node sees them
    text.clear();
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
      const SceneObject &o = sc.objects[i];
      if (imprint_ground_of(o) < 0 || !o.enabled) continue;
      Print p = print_of(o);
      char line[160];
      std::snprintf(line, sizeof line, "%.6f %.6f %.6f %.6f %.3f %.6f\n", p.cx, p.cz, p.rx, p.rz,
                    p.rot, o.pos[1] + p.base_rel);
      text += line;
    }
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
