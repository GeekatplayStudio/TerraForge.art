// Geekatplay TerraForge — the Material Editor's layer surgery, without a window.
//
// Adding, deleting and reordering layers is graph rewiring, and the failure
// mode is a material that quietly stops reaching the renderer: no crash, no
// error, just a surface that went black. A screenshot will not catch that
// reliably, so the rewiring is checked here instead.
//
// Linked into undo_tests, which already runs a slice of the studio with no GL
// context.
#include "material_stack_ops.hpp"
#include "gpx/node_graph.hpp"
#include <cstdio>
#include <string>

namespace material_stack_tests {

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                   \
      ++g_fail;                                                                \
    }                                                                          \
  } while (0)

using namespace studio;

// Does the material actually receive a colour from this node?
static bool feeds_material(gpx::Graph &g, gpx::Node *mat, gpx::Node *src) {
  gpx::Link *l = layer_incoming(g, mat->id, "base color");
  return l && src && l->from_node == src->id;
}

static void test_first_layer_adopts_the_existing_material() {
  std::printf("  material layers: first Add keeps what was there...\n");
  gpx::Graph g;
  gpx::Node *mat = g.add_node("MaterialOutput");
  gpx::Node *col = g.add_node("FlatColor");
  g.add_link(col->id, "texture", mat->id, "base color");

  auto layers = collect_layers(g, mat);
  CHECK(layers.empty(), "a plain material has no layer stack");

  gpx::Node *l1 = add_material_layer(g, mat, layers);
  CHECK(l1 && l1->type == "MaterialLayer", "a layer is created");
  CHECK(feeds_material(g, mat, l1), "the layer now drives the material");
  gpx::Link *own = layer_incoming(g, l1->id, "albedo");
  CHECK(own && own->from_node == col->id,
        "the colour that was driving the material became the layer's own "
        "albedo, so nothing was thrown away");
  CHECK(collect_layers(g, mat).size() == 1, "the stack has one layer");
}

static void test_stack_grows_downward_and_reads_top_first() {
  std::printf("  material layers: order and chaining...\n");
  gpx::Graph g;
  gpx::Node *mat = g.add_node("MaterialOutput");
  auto ls = collect_layers(g, mat);
  gpx::Node *a = add_material_layer(g, mat, ls);
  if (a->attrs.find("name")) a->attrs.find("name")->s = "bottom";
  ls = collect_layers(g, mat);
  gpx::Node *b = add_material_layer(g, mat, ls);
  if (b->attrs.find("name")) b->attrs.find("name")->s = "middle";
  ls = collect_layers(g, mat);
  gpx::Node *c = add_material_layer(g, mat, ls);
  if (c->attrs.find("name")) c->attrs.find("name")->s = "top";

  ls = collect_layers(g, mat);
  CHECK(ls.size() == 3, "three layers in the stack");
  CHECK(ls[0] == c && ls[1] == b && ls[2] == a,
        "the stack reads top first, the way an image editor shows it");
  CHECK(feeds_material(g, mat, c), "only the top layer touches the material");
  gpx::Link *bel = layer_incoming(g, c->id, "below albedo");
  CHECK(bel && bel->from_node == b->id, "each layer sits on the one below it");
  for (const char *p : {"below normal", "below rough"})
    CHECK(layer_incoming(g, c->id, p), "normal and roughness chain too");
}

static void test_delete_closes_the_gap() {
  std::printf("  material layers: delete...\n");
  gpx::Graph g;
  gpx::Node *mat = g.add_node("MaterialOutput");
  auto ls = collect_layers(g, mat);
  gpx::Node *a = add_material_layer(g, mat, ls);
  ls = collect_layers(g, mat);
  gpx::Node *b = add_material_layer(g, mat, ls);
  ls = collect_layers(g, mat);
  gpx::Node *c = add_material_layer(g, mat, ls);

  // remove the middle one
  delete_material_layer(g, b, mat, collect_layers(g, mat));
  ls = collect_layers(g, mat);
  CHECK(ls.size() == 2, "two layers left");
  CHECK(ls[0] == c && ls[1] == a, "the top and bottom survived, in order");
  gpx::Link *bel = layer_incoming(g, c->id, "below albedo");
  CHECK(bel && bel->from_node == a->id,
        "the gap closed: the top now sits directly on the bottom");
  CHECK(feeds_material(g, mat, c), "the material is still driven");

  // remove the top one
  delete_material_layer(g, c, mat, collect_layers(g, mat));
  ls = collect_layers(g, mat);
  CHECK(ls.size() == 1 && ls[0] == a, "one layer left");
  CHECK(feeds_material(g, mat, a),
        "deleting the top hands the material to what was underneath, rather "
        "than leaving it with no colour at all");

  // and the last one
  delete_material_layer(g, a, mat, collect_layers(g, mat));
  CHECK(collect_layers(g, mat).empty(), "the stack is empty");
  CHECK(!layer_incoming(g, mat->id, "base color"),
        "with no layers the material has no colour input, which is honest");
}

static void test_reorder_moves_the_settings_with_the_layer() {
  std::printf("  material layers: reorder...\n");
  gpx::Graph g;
  gpx::Node *mat = g.add_node("MaterialOutput");
  gpx::Node *red = g.add_node("FlatColor");
  gpx::Node *grn = g.add_node("FlatColor");

  auto ls = collect_layers(g, mat);
  gpx::Node *lo = add_material_layer(g, mat, ls);
  ls = collect_layers(g, mat);
  gpx::Node *hi = add_material_layer(g, mat, ls);

  lo->attrs.find("name")->s = "rock";
  lo->attrs.find("opacity")->f = 0.25f;
  lo->attrs.find("use_slope")->b = true;
  g.add_link(red->id, "texture", lo->id, "albedo");

  hi->attrs.find("name")->s = "snow";
  hi->attrs.find("opacity")->f = 0.75f;
  hi->attrs.find("use_altitude")->b = true;
  g.add_link(grn->id, "texture", hi->id, "albedo");

  swap_material_layers(g, hi, lo);

  ls = collect_layers(g, mat);
  CHECK(ls.size() == 2, "still two layers after a swap");
  CHECK(feeds_material(g, mat, ls[0]), "the material is still driven");
  CHECK(ls[0]->attrs.get_s("name") == "rock",
        "the layer that was underneath is now on top");
  CHECK(ls[1]->attrs.get_s("name") == "snow", "and the other one moved down");
  CHECK(ls[0]->attrs.get_b("use_slope", false) &&
            !ls[0]->attrs.get_b("use_altitude", false),
        "its environment constraints travelled with it");
  CHECK(std::abs(ls[0]->attrs.get_f("opacity", 1.f) - 0.25f) < 1e-6f,
        "so did its opacity");
  gpx::Link *own = layer_incoming(g, ls[0]->id, "albedo");
  CHECK(own && own->from_node == red->id, "and so did its own colour input");

  // the chain wiring itself must be untouched: still exactly one link into
  // each "below" port, no orphans left over
  int below = 0;
  for (gpx::Link &l : g.links)
    if (l.to_port.rfind("below", 0) == 0) ++below;
  CHECK(below == 3, "the chain still has its three below-links, no duplicates");
}

static void test_a_foreign_node_is_not_mistaken_for_a_layer() {
  std::printf("  material layers: a plain material stays plain...\n");
  gpx::Graph g;
  gpx::Node *mat = g.add_node("MaterialOutput");
  gpx::Node *st = g.add_node("MaterialStack");
  gpx::Node *col = g.add_node("FlatColor");
  g.add_link(col->id, "texture", st->id, "albedo 1");
  g.add_link(st->id, "albedo", mat->id, "base color");
  CHECK(collect_layers(g, mat).empty(),
        "a MaterialStack driving the colour is not a layer chain");
}

static void test_presence_summary_says_what_constrains_the_layer() {
  std::printf("  material layers: presence summary...\n");
  gpx::Graph g;
  gpx::Node *mat = g.add_node("MaterialOutput");
  gpx::Node *l = add_material_layer(g, mat, collect_layers(g, mat));
  CHECK(layer_presence_summary(l) == "everywhere",
        "an unconstrained layer says so");
  l->attrs.find("use_slope")->b = true;
  l->attrs.find("slope")->v2[0] = 30.f;
  l->attrs.find("slope")->v2[1] = 75.f;
  std::string s = layer_presence_summary(l);
  CHECK(s.find("slope") != std::string::npos &&
            s.find("30") != std::string::npos,
        "a slope-constrained layer names its band");
  CHECK(layer_display_name(l, 0) == "Layer 1", "a layer has a readable name");
}

int run_all() {
  test_first_layer_adopts_the_existing_material();
  test_stack_grows_downward_and_reads_top_first();
  test_delete_closes_the_gap();
  test_reorder_moves_the_settings_with_the_layer();
  test_a_foreign_node_is_not_mistaken_for_a_layer();
  test_presence_summary_says_what_constrains_the_layer();
  std::printf("  material layer surgery: %d checks, %d failures\n", g_checks,
              g_fail);
  return g_fail;
}

} // namespace material_stack_tests

int test_material_stack_ops_run() { return material_stack_tests::run_all(); }
