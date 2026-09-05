// Geekatplay TerraForge - the Material Editor's graph surgery, on a bare
// graph: channel modes (Vue p704), the hierarchy (p690), randomize (p692),
// snapshot restore (p693) and the visibility switch (p691).
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include "material_channel_ops.hpp"
#include <cstdio>
#include <string>

using namespace studio;

static int s_failures = 0;
static void ok(bool c, const char *what) {
  if (!c) {
    std::printf("FAIL: %s\n", what);
    ++s_failures;
  }
}

int test_material_channel_ops_run() {
  std::printf("material channel ops...\n");
  gpx::Graph g;
  gpx::Node *mat = g.add_node("MaterialOutput");
  ok(channel_mode_of(g, mat, "base color") == CH_NONE, "a fresh material has no colour source");

  // modes connect the right node, and switching replaces it
  gpx::Node *pic = channel_set_mode(g, mat, "base color", CH_PICTURE, "C:/tex/rock.png");
  ok(pic && pic->type == "TextureFile", "Mapped picture adds a TextureFile");
  ok(pic && pic->attrs.get_s("path") == "C:/tex/rock.png", "with the chosen file");
  ok(channel_mode_of(g, mat, "base color") == CH_PICTURE, "and the channel reads as a picture");
  gpx::Node *same = channel_set_mode(g, mat, "base color", CH_PICTURE, "C:/tex/moss.png");
  ok(same == pic && pic->attrs.get_s("path") == "C:/tex/moss.png",
     "another picture keeps the node and changes its file");
  gpx::Node *fc = channel_set_mode(g, mat, "base color", CH_PROCEDURAL);
  ok(fc && fc->type == "FractalColor", "Procedural adds a FractalColor");
  ok(g.upstream_node(*mat, "base color") == fc, "which now feeds the colour");
  ok(channel_set_mode(g, mat, "base color", CH_PROCEDURAL) == fc, "asking again changes nothing");
  gpx::Node *grain = channel_set_mode(g, mat, "base color", CH_GRAIN);
  ok(grain && grain->type == "NaturalGrain", "Natural grain adds a NaturalGrain");
  ok(channel_set_mode(g, mat, "base color", CH_NONE) == nullptr, "None disconnects");
  ok(channel_mode_of(g, mat, "base color") == CH_NONE, "and the channel is empty again");
  ok(g.find_node(grain->id) != nullptr, "the node is left in the graph, not deleted");
  ok(channel_mode_of(g, mat, "roughness") == CH_NONE, "other channels were never touched");

  // hierarchy: a layered material lists its layers top first, a mix lists
  // the two materials it mixes
  channel_set_mode(g, mat, "base color", CH_PROCEDURAL);
  std::vector<MatHierItem> h = material_hierarchy(g, mat);
  ok(h.size() == 1 && h[0].kind == 0, "a simple material is one line");
  gpx::Node *stack = g.add_node("MaterialStack");
  gpx::Node *m1 = g.add_node("FlatColor"), *m2 = g.add_node("NaturalGrain");
  for (const gpx::Link &l : g.links)
    if (l.to_node == mat->id && l.to_port == "base color") { g.remove_link(l.id); break; }
  g.add_link(m1->id, "texture", stack->id, "albedo 1");
  g.add_link(m2->id, "texture", stack->id, "albedo 2");
  g.add_link(stack->id, "albedo", mat->id, "base color");
  h = material_hierarchy(g, mat);
  ok(h.size() == 4, "a mixed material lists the mix and both materials");
  ok(h.size() == 4 && h[1].kind == 2 && h[2].depth == 2 && h[3].node == m2->id,
     "mix at depth 1, the two materials at depth 2");
  ok(channel_mode_of(g, mat, "base color") == CH_OTHER, "a mix is not a leaf mode");

  // randomize: every seed upstream changes, nothing else does
  uint32_t before = m2->attrs.find("seed")->seed;
  int n = material_randomize(g, mat, 7u);
  ok(n >= 1, "randomize reseeds at least one node");
  ok(m2->attrs.find("seed")->seed != before, "the grain's seed changed");
  ok(m1->attrs.get_f("r", -1.f) == 0.5f, "a colour is not a seed and did not change");

  // visibility switch: three states on a layer
  gpx::Node *layer = g.add_node("MaterialLayer");
  ok(hier_visibility(layer) == 0, "a new layer is visible");
  hier_set_visibility(layer, 1);
  ok(hier_visibility(layer) == 1 && !layer->enabled, "invisible bypasses the layer");
  hier_set_visibility(layer, 2);
  ok(hier_visibility(layer) == 2 && layer->enabled && layer->attrs.get_b("highlight"),
     "highlight re-enables it and shows it flat");
  hier_set_visibility(layer, 0);
  ok(hier_visibility(layer) == 0, "and back to normal");

  // snapshot: store the mixed state, change the material, restore; the
  // MaterialOutput keeps its id and the old sources are gone
  std::string snap = gpx::material_to_json(g, mat->id);
  ok(!snap.empty(), "a material serialises");
  channel_set_mode(g, mat, "base color", CH_PICTURE, "C:/tex/other.png");
  ok(channel_mode_of(g, mat, "base color") == CH_PICTURE, "the material changed");
  gpx::Node *stray = g.upstream_node(*mat, "base color");
  uint64_t stray_id = stray ? stray->id : 0;
  std::string err;
  ok(material_replace_from_json(g, mat->id, snap, err), err.empty() ? "restore succeeds" : err.c_str());
  ok(g.find_node(mat->id) == mat, "the material node is the same node");
  h = material_hierarchy(g, mat);
  ok(h.size() == 4 && h[1].kind == 2, "the mix is back");
  ok(g.find_node(stray_id) == nullptr, "the replaced picture node was removed");
  int outputs = 0;
  for (auto &node : g.nodes)
    if (node->type == "MaterialOutput") ++outputs;
  ok(outputs == 1, "restoring did not leave a second MaterialOutput behind");
  ok(!material_replace_from_json(g, 999999, snap, err), "restoring into a missing material fails cleanly");
  return s_failures;
}
