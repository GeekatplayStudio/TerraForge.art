// Geekatplay TerraForge - the Terrain menu's style presets.
//
// Vue's "predefined terrain styles", with one difference that matters: a
// style here is not a baked result, it is a small node chain dropped into the
// graph. Pick "Eroded mountain" and the Noise, Hydraulic and Thermal nodes
// that made it are sitting in the editor, wired up and retunable. A preset
// that cannot be taken apart is a dead end.
#include "ai_describe.hpp"
#include "app.hpp"
#include "icons.hpp"
#include "sculpt.hpp"
#include "toolbar_internal.hpp"
#include "undo.hpp"
#include "gpx/node_graph.hpp"
#include <imgui.h>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace studio {

// ---- terrain style presets -------------------------------------------------
// One-click starting points (Vue's "predefined terrain styles"): each builds a
// small node chain so the result stays fully editable — open the graph and
// every stage of the style is right there to retune.
struct StyleNode {
  const char *type;
  std::vector<std::pair<const char *, float>> floats;
  std::vector<std::pair<const char *, int>> ints;
};

static void apply_terrain_style(App &a, const char *name,
                                const std::vector<StyleNode> &chain) {
  undo_push(a, std::string("Terrain style: ") + name);
  std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
  // fresh seed on every click, the way Vue randomizes the fractal origin
  static uint32_t style_serial = 1;
  uint32_t seed = style_serial++ * 2654435761u;

  gpx::Node *prev = nullptr;
  float x = 60, y = 60;
  // place the chain below the existing graph so nothing overlaps
  for (auto &n : a.graph.nodes) y = std::max(y, n->pos_y + 260);
  gpx::Node *first = nullptr;
  for (const StyleNode &sn : chain) {
    gpx::Node *n = a.graph.add_node(sn.type, x, y);
    if (!n) continue;
    if (!first) first = n;
    for (auto &kv : sn.floats)
      if (gpx::Attribute *at = n->attrs.find(kv.first)) at->f = kv.second;
    for (auto &kv : sn.ints)
      if (gpx::Attribute *at = n->attrs.find(kv.first)) {
        at->i = kv.second;
        at->seed = (uint32_t)kv.second;
      }
    if (gpx::Attribute *sd = n->attrs.find("seed")) sd->seed = seed;
    if (prev) {
      gpx::Port *po = prev->first_out(gpx::DataType::Heightmap);
      if (po) a.graph.add_link(prev->id, po->name, n->id, "input");
    }
    prev = n;
    x += 190;
  }
  if (!prev) return;

  // route the style into the TerrainOutput (creating one if needed), so the
  // new terrain shows up immediately
  gpx::Node *out_node = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainOutput") out_node = n.get();
  if (!out_node) out_node = a.graph.add_node("TerrainOutput", x, y);
  if (out_node) {
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == out_node->id && l.to_port == "heightmap") {
        a.graph.remove_link(l.id);
        break;
      }
    gpx::Port *po = prev->first_out(gpx::DataType::Heightmap);
    if (po) a.graph.add_link(prev->id, po->name, out_node->id, "heightmap");
  }
  a.selected_node = prev->id;
  a.view_node = 0; // follow the TerrainOutput again
  a.graph_layout_serial++;
  a.request_eval();
  a.status = std::string("terrain style: ") + name;
}

// A node slipped in between the terrain chain and the Terrain Output: what
// fed the output now feeds the node, and the node feeds the output. The
// chain that made the terrain is untouched, and the new node is one more
// thing in it to retune or delete. One undo step.
void terrain_insert_before_output(App &a, const char *type, const char *what,
                                  const std::vector<std::pair<const char *, float>> &floats,
                                  const std::vector<std::pair<const char *, int>> &ints) {
  undo_push(a, what);
  std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
  gpx::Node *out = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainOutput") out = n.get();
  if (!out) {
    a.status = std::string(what) + ": there is no Terrain Output yet";
    return;
  }
  const gpx::Link *feed = nullptr;
  for (const gpx::Link &l : a.graph.links)
    if (l.to_node == out->id && l.to_port == "heightmap") feed = &l;
  if (!feed) {
    a.status = std::string(what) + ": nothing feeds the Terrain Output yet";
    return;
  }
  const uint64_t from_node = feed->from_node, feed_id = feed->id;
  const std::string from_port = feed->from_port;
  gpx::Node *n = a.graph.add_node(type, out->pos_x - 190.f, out->pos_y + 60.f);
  if (!n) {
    a.status = std::string(what) + ": '" + type + "' is not a node type";
    return;
  }
  for (auto &kv : floats)
    if (gpx::Attribute *at = n->attrs.find(kv.first)) at->f = kv.second;
  for (auto &kv : ints)
    if (gpx::Attribute *at = n->attrs.find(kv.first)) at->i = kv.second;
  a.graph.remove_link(feed_id);
  a.graph.add_link(from_node, from_port, n->id, "input");
  a.graph.add_link(n->id, "output", out->id, "heightmap");
  a.selected_node = n->id;
  a.view_node = 0;
  a.graph_layout_serial++;
  a.request_eval();
  a.status = what;
}

void menu_terrain(App &a) {
  if (!ImGui::BeginMenu("Terrain")) return;

  // Blank first, because it is the one every other entry is an alternative to:
  // flat ground at mid height with a sculpt layer already on it, so the very
  // next thing you can do is paint. Nothing generated, nothing to undo first.
  if (ImGui::MenuItem("Blank terrain")) {
    apply_terrain_style(a, "Blank terrain",
                        {{"Constant", {{"value", 0.5f}}, {}},
                         {"TerrainSculpt", {}, {}}});
    a.show_paint_canvas = true; // the panel that goes with it
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Flat ground at mid height — 50%% grey, no shape of its\n"
                      "own — with a sculpt layer wired on top and the Height\n"
                      "Paint panel open. Paint dark to carve, light to raise;\n"
                      "anything you add downstream still applies.");
  if (ImGui::MenuItem("New terrain...")) new_terrain_request();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The full dialog: size, resolution and a starting shape.");
  ImGui::Separator();

  ImGui::TextDisabled("Style presets (build editable node chains)");
  ImGui::Separator();
  // The realistic chain: the way real ranges form. A fractal for the
  // initial uplift, stream power carving the drainage network the way rivers
  // do over geological time (implicit solver with uplift, so ridges keep
  // rising while valleys deepen), then the hydraulic and thermal pass that
  // gives slopes their angle of repose - and ErosionLayers on the end, so the
  // materials come from the same simulation.
  // Tuned headlessly (tools/chain_preview) against a dozen alternatives: the
  // implicit uplift solver makes plateaus against the fixed borders, and
  // droplets alone leave grain; this order - a swiss-ridge base, the pipe
  // model to settle coherent valleys, explicit stream power to cut the
  // drainage network, then thermal + a light droplet pass with the material
  // masks - reads as a real range at 512 in about two and a half seconds.
  if (ImGui::MenuItem("Realistic mountain range"))
    apply_terrain_style(
        a, "Realistic mountain range",
        {{"Noise", {}, {{"type", 3}, {"octaves", 8}}},
         {"Hydraulic", {}, {{"method", 1}, {"iterations", 200}}},
         {"StreamPower", {{"k_erode", 0.12f}, {"smooth", 0.06f}},
          {{"iterations", 60}, {"method", 0}}},
         {"ErosionLayers", {{"strength", 0.5f}, {"talus", 1.6f}, {"snowline", 0.78f}},
          {{"method", 3}, {"thermal_iters", 80}}}});
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Eroded ridges, then the shallow-water solver settles\n"
                      "the valleys, stream power cuts the drainage network,\n"
                      "and ErosionLayers adds talus and gullies - with the\n"
                      "material masks (rock, scree, soil, grass, snow...) on\n"
                      "its outputs, ready for a MaterialStack.");
  if (ImGui::MenuItem("Mountain"))
    apply_terrain_style(a, "Mountain",
                        {{"Noise", {}, {{"octaves", 10}}}});
  if (ImGui::MenuItem("Ridged peaks"))
    apply_terrain_style(a, "Ridged peaks",
                        {{"Noise", {}, {{"type", 1}, {"octaves", 11}}},
                         {"Peaks", {{"strength", 0.45f}}, {}}});
  if (ImGui::MenuItem("Eroded mountain"))
    apply_terrain_style(a, "Eroded mountain",
                        {{"Noise", {}, {{"octaves", 10}}},
                         {"Hydraulic", {}, {}},
                         {"Thermal", {}, {}}});
  if (ImGui::MenuItem("Canyon"))
    apply_terrain_style(a, "Canyon",
                        {{"Noise", {}, {{"octaves", 9}}},
                         {"Terrace", {{"shape", 6.f}}, {{"levels", 7}}},
                         {"Dissolve", {{"amount", 0.4f}}, {}}});
  if (ImGui::MenuItem("Dunes"))
    apply_terrain_style(a, "Dunes", {{"Dunes", {}, {}}});
  if (ImGui::MenuItem("Iceberg"))
    apply_terrain_style(
        a, "Iceberg",
        {{"Noise", {}, {{"octaves", 8}}},
         {"TerrainClip", {{"softness", 0.04f}}, {{"high_mode", 1}}},
         {"Glaciation", {{"strength", 0.7f}}, {}}});
  if (ImGui::MenuItem("Lunar"))
    apply_terrain_style(a, "Lunar",
                        {{"Noise", {{"gain", 0.42f}}, {{"octaves", 8}}},
                         {"Crater", {}, {{"profile", 1}}},
                         {"Grit", {{"amount", 0.015f}}, {}}});
  ImGui::Separator();
  ImGui::TextDisabled("Each style drops a fresh chain into the graph\n"
                      "and wires it to the Terrain Output — the old\n"
                      "chain stays in the graph, and Ctrl+Z undoes it.");

  // ---- the terrain commands, which until now existed only as icons --------
  ImGui::Separator();
  SculptState &s = sculpt_state();
  if (IconMenuItem(Icon::Brush, "Sculpt mode", s.active)) {
    sculpt_set_active(a, !s.active);
    if (a.workspace != WS_TERRAIN) a.workspace = WS_TERRAIN;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Brush directly on the terrain in the 3D view. Strokes\n"
                      "live in a TerrainSculpt node, so retuning the\n"
                      "procedural chain underneath does not lose them.\n"
                      "[ and ] resize the brush, the wheel does too.");
  if (ImGui::BeginMenu("Brush", s.active)) {
    struct B { Icon icon; SculptTool tool; const char *label, *tip; };
    static const B brushes[] = {
        {Icon::Raise, SculptTool::Raise, "Raise", "Add relief; Alt digs."},
        {Icon::Flatten, SculptTool::Flatten, "Flatten",
         "Pull the surface toward the height under the first click."},
        {Icon::Smooth, SculptTool::Smooth, "Smooth", "Relax bumps and stroke marks."},
        {Icon::Terrace, SculptTool::Terrace, "Terrace", "Cut the slope into steps."},
        {Icon::Noise, SculptTool::Noise, "Noise", "Stamp fractal detail; Alt inverts."},
        {Icon::Erase, SculptTool::Erase, "Erase",
         "Remove sculpted strokes, revealing the procedural terrain."},
        {Icon::Textured, SculptTool::Shade, "Shade",
         "Paint a chosen grey: dark carves, light raises, mid does nothing."}};
    for (const B &b : brushes) {
      if (IconMenuItem(b.icon, b.label, s.tool == b.tool)) s.tool = b.tool;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", b.tip);
    }
    if (s.tool == SculptTool::Shade) {
      ImGui::Separator();
      ImGui::SetNextItemWidth(160);
      ImGui::SliderFloat("Shade", &s.shade, 0.f, 1.f, "%.2f");
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Size", &s.radius, 0.005f, 0.4f, "%.3f");
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Strength", &s.flow, 0.f, 2.f, "%.2f");
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Edge", &s.falloff, 0.2f, 8.f, "%.2f");
    ImGui::EndMenu();
  }
  if (IconMenuItem(Icon::Textured, "Height Paint", a.show_paint_canvas))
    a.show_paint_canvas = !a.show_paint_canvas;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The terrain as a greyscale picture: mid grey does\n"
                      "nothing, darker carves a valley, lighter raises\n"
                      "ground. The same layer and brushes as Sculpt, drawn\n"
                      "flat — the only sane way to draw a river's course.");
  ImGui::Separator();
  // Turning and tiling the terrain that is already there: a node inserted
  // just before the Terrain Output, so the chain that made it stays intact
  // and the new node can be retuned or removed like any other.
  if (ImGui::BeginMenu("Rotate terrain")) {
    for (int q = 1; q <= 3; ++q) {
      char lbl[16];
      std::snprintf(lbl, sizeof lbl, "%d\xC2\xB0", q * 90);
      if (ImGui::MenuItem(lbl))
        terrain_insert_before_output(a, "Transform", "Rotate terrain",
                                     {{"angle", (float)(q * 90)}}, {{"extend", 2}});
    }
    ImGui::Separator();
    ImGui::TextDisabled("Any other angle: the Transform node's Rotate.");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Tile terrain")) {
    for (int t = 2; t <= 4; ++t) {
      char lbl[40];
      std::snprintf(lbl, sizeof lbl, "%d x %d, each tile turned at random", t, t);
      if (ImGui::MenuItem(lbl))
        terrain_insert_before_output(a, "TileRotate", "Tile terrain", {},
                                     {{"across", t}, {"down", t}});
    }
    if (ImGui::MenuItem("2 x 2, turned and mirrored"))
      terrain_insert_before_output(a, "TileRotate", "Tile terrain", {},
                                   {{"across", 2}, {"down", 2}, {"turn", 1}});
    ImGui::Separator();
    ImGui::TextDisabled("Each copy gets a random quarter-turn so no\n"
                        "lattice shows; a feather hides the seams.\n"
                        "The counts and the seed are on the node.");
    ImGui::EndMenu();
  }
  ImGui::Separator();
  if (ImGui::BeginMenu("Resolution")) {
    for (int res : {256, 512, 1024, 2048, 4096}) {
      char lbl[24];
      std::snprintf(lbl, sizeof lbl, "%d x %d", res, res);
      if (ImGui::MenuItem(lbl, nullptr, a.graph.resolution == res)) {
        std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
        a.graph.resolution = res;
        a.graph.mark_all_dirty();
        a.request_eval();
      }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Any value 64..8192 on the tool row above");
    ImGui::EndMenu();
  }
  if (IconMenuItem(Icon::Refresh, "Recompute everything"))
    {
      std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
      a.graph.mark_all_dirty();
      a.request_eval();
    }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Marks every node dirty and evaluates the whole graph.");
  if (IconMenuItem(Icon::Bake, "Bake 4k exports")) {
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    for (auto &n : a.graph.nodes)
      if (auto *e = n->attrs.find("auto_export")) e->b = true;
    a.graph.resolution = 4096;
    a.graph.mark_all_dirty();
    a.request_eval();
    a.status = "baking at 4096; export nodes write when done";
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Re-evaluate at 4096 with every export node enabled.");
  ImGui::Separator();
  if (ImGui::MenuItem("Describe the terrain...")) ai_describe_open(DESCRIBE_TERRAIN);
  ImGui::EndMenu();
}

} // namespace studio
