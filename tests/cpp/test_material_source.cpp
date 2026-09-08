// Geekatplay TerraForge — MaterialSource: importing another object's material.
//
// The node reaches outside the graph's links, so the two things that can go
// wrong are both about order rather than about pixels: reading the source
// before it has been computed, and a graph that will not sort at all because
// the extra edge points at nothing or points backwards. Every test here is
// built to fail if either happens - the graphs are deliberately assembled in
// the wrong order, so an implementation that relies on nodes happening to be
// created in a helpful sequence does not pass.
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
void check_fail(const std::string &msg, int line) {
  std::printf("  [FAIL] %s (line %d)\n", msg.c_str(), line);
  ++g_failures;
}
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) check_fail(msg, __LINE__);                                    \
  } while (0)

// A material worth importing: a real texture on base color and a real
// heightmap on displacement, so both port types are exercised.
struct SourceMaterial {
  gpx::Node *noise = nullptr;
  gpx::Node *tex = nullptr;
  gpx::Node *grad = nullptr;
  gpx::Node *mat = nullptr;
};

SourceMaterial build_material(gpx::Graph &g) {
  SourceMaterial m;
  m.noise = g.add_node("Noise");
  m.tex = g.add_node("TerrainTexture");
  m.grad = g.add_node("GradientMap");
  m.mat = g.add_node("MaterialOutput");
  g.add_link(m.noise->id, "output", m.tex->id, "input");
  g.add_link(m.tex->id, "texture", m.grad->id, "texture");
  g.add_link(m.grad->id, "texture", m.mat->id, "base color");
  g.add_link(m.noise->id, "output", m.mat->id, "displacement");
  return m;
}

void set_source(gpx::Node *n, uint64_t id) {
  if (gpx::Attribute *a = n->attrs.find("source_node"))
    a->s = id ? std::to_string(id) : std::string();
}

int position_of(const std::vector<gpx::Node *> &order, uint64_t id) {
  for (size_t i = 0; i < order.size(); ++i)
    if (order[i]->id == id) return (int)i;
  return -1;
}

const gpx::TextureRGBA *out_tex(gpx::Node *n, const char *port) {
  gpx::Port *p = n->port(port, gpx::PortDir::Out);
  return p ? p->tex.get() : nullptr;
}
const gpx::Heightmap *out_hmap(gpx::Node *n, const char *port) {
  gpx::Port *p = n->port(port, gpx::PortDir::Out);
  return p ? p->hmap.get() : nullptr;
}

bool same_tex(const gpx::TextureRGBA *a, const gpx::TextureRGBA *b) {
  if (!a || !b) return false;
  if (a->w != b->w || a->h != b->h || a->v.size() != b->v.size()) return false;
  return a->v == b->v; // bit-identical: a copy, not a re-creation
}

// ---------------------------------------------------------------------------

// The whole point of the node: what comes out is what the source was given,
// down to the last float, for both a texture channel and the heightmap one.
void test_import_reproduces_the_source() {
  std::printf("material source: reproduces the source...\n");
  gpx::Graph g;
  g.resolution = 32;
  // Added first, on purpose. If evaluation order came from creation order
  // this would read an empty source and every check below would fail.
  gpx::Node *imp = g.add_node("MaterialSource");
  SourceMaterial m = build_material(g);
  set_source(imp, m.mat->id);

  CHECK(g.evaluate(), "graph evaluates");
  CHECK(imp->error.empty(), "importer reports no error: " + imp->error);
  CHECK(same_tex(out_tex(imp, "base color"), m.mat->in_tex("base color")),
        "imported base color is the source's base color");
  const gpx::Heightmap *disp = out_hmap(imp, "displacement");
  const gpx::Heightmap *src_disp = m.mat->in_hmap("displacement");
  CHECK(disp && src_disp && disp->v == src_disp->v,
        "imported displacement is the source's displacement");
  CHECK(disp && !disp->v.empty(), "imported displacement is not empty");
}

// A channel nothing is wired into comes out empty rather than stale or wrong.
void test_unwired_channels_stay_empty() {
  std::printf("material source: unwired channels...\n");
  gpx::Graph g;
  g.resolution = 32;
  SourceMaterial m = build_material(g);
  gpx::Node *imp = g.add_node("MaterialSource");
  set_source(imp, m.mat->id);
  CHECK(g.evaluate(), "graph evaluates");
  const gpx::TextureRGBA *rough = out_tex(imp, "roughness");
  CHECK(rough && rough->empty(), "roughness nobody wired comes out empty");
  const gpx::TextureRGBA *base = out_tex(imp, "base color");
  CHECK(base && !base->empty(), "and base color, which was wired, does not");
}

// The order test proper. Change something upstream of the source and the
// importer must hand out the new material in the same evaluation - not the
// previous one's. This is the failure the declared edge exists to prevent,
// and it is invisible in a single evaluation.
void test_a_change_arrives_in_the_same_pass() {
  std::printf("material source: a change arrives in the same pass...\n");
  gpx::Graph g;
  g.resolution = 32;
  gpx::Node *imp = g.add_node("MaterialSource");
  SourceMaterial m = build_material(g);
  set_source(imp, m.mat->id);
  CHECK(g.evaluate(), "first evaluation");
  const gpx::TextureRGBA *before = out_tex(imp, "base color");
  CHECK(before && !before->empty(), "imported something to begin with");
  const std::vector<float> was = before->v;

  // A different noise: the source material genuinely changes.
  if (gpx::Attribute *s = m.noise->attrs.find("seed")) s->seed = 12345;
  g.mark_dirty(m.noise->id);
  CHECK(imp->dirty, "dirtying the source's input dirties the importer");
  CHECK(g.evaluate(), "second evaluation");

  const gpx::TextureRGBA *after = out_tex(imp, "base color");
  CHECK(after && after->v != was, "the import changed with the material");
  CHECK(same_tex(after, m.mat->in_tex("base color")),
        "and matches the source in the very pass that changed it");
}

// Sorting has to place the source first whichever way round the two were made.
void test_the_source_is_evaluated_first() {
  std::printf("material source: order...\n");
  for (int importer_first = 0; importer_first < 2; ++importer_first) {
    gpx::Graph g;
    g.resolution = 16;
    gpx::Node *imp = nullptr;
    SourceMaterial m;
    if (importer_first) {
      imp = g.add_node("MaterialSource");
      m = build_material(g);
    } else {
      m = build_material(g);
      imp = g.add_node("MaterialSource");
    }
    set_source(imp, m.mat->id);
    const auto order = g.topo_order();
    CHECK(!order.empty(), "graph sorts");
    const int a = position_of(order, m.mat->id), b = position_of(order, imp->id);
    CHECK(a >= 0 && b >= 0 && a < b,
          std::string("source before importer (importer added ") +
              (importer_first ? "first)" : "second)"));
  }
}

// An importer pointed at a node that has been deleted must not stop the graph.
// The tempting implementation counts the edge into the in-degree table and
// then never decrements it, and every node in the project stops evaluating.
void test_a_dangling_source_does_not_stop_the_graph() {
  std::printf("material source: dangling source...\n");
  gpx::Graph g;
  g.resolution = 16;
  SourceMaterial m = build_material(g);
  gpx::Node *imp = g.add_node("MaterialSource");
  const uint64_t gone = m.mat->id;
  set_source(imp, gone);
  g.remove_node(gone);

  const auto order = g.topo_order();
  CHECK(order.size() == g.nodes.size(), "every node still sorts");
  CHECK(g.evaluate(), "and the graph still evaluates");
  const gpx::TextureRGBA *base = out_tex(imp, "base color");
  CHECK(base && base->empty(), "the importer hands out nothing");
}

// Pointing an importer at the material it feeds is a cycle, and has to be
// caught as one rather than silently producing last-pass data forever.
void test_a_circular_import_is_a_cycle() {
  std::printf("material source: circular import...\n");
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *mat = g.add_node("MaterialOutput");
  gpx::Node *imp = g.add_node("MaterialSource");
  CHECK(g.add_link(imp->id, "base color", mat->id, "base color"),
        "importer feeds the material");
  set_source(imp, mat->id); // ...which it also imports
  CHECK(g.topo_order().empty(), "the sort refuses it");
  CHECK(!g.evaluate(), "and evaluation reports the failure");
}

// Naming an object whose material never resolved says so, rather than
// pretending the empty channels are the material.
void test_an_unresolved_object_says_so() {
  std::printf("material source: unresolved object...\n");
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *imp = g.add_node("MaterialSource");
  CHECK(g.evaluate(), "empty importer evaluates");
  CHECK(imp->error.empty(), "an importer nobody has filled in is not an error");
  if (gpx::Attribute *o = imp->attrs.find("object")) o->s = "Terrain";
  g.mark_dirty(imp->id);
  CHECK(g.evaluate(), "named importer evaluates");
  CHECK(imp->error.find("Terrain") != std::string::npos,
        "an object that resolved to no material is reported: " + imp->error);
}

// The source must be a material. Pointing at a noise node is a mistake worth
// naming, not eight empty channels to puzzle over.
void test_a_non_material_source_is_refused() {
  std::printf("material source: non-material source...\n");
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *noise = g.add_node("Noise");
  gpx::Node *imp = g.add_node("MaterialSource");
  set_source(imp, noise->id);
  CHECK(g.evaluate(), "graph evaluates");
  CHECK(!imp->error.empty(), "pointing at a Noise node is an error");
}

// Saving and loading renumbers every node, so an id written into an attribute
// points at whichever node inherited that number - a different, perfectly
// valid node, which is why nothing downstream would ever report it. The graph
// here is built so the numbers genuinely move: a node is deleted before the
// save, and the importer's target is not the node that ends up with its id.
void test_the_reference_survives_a_save_and_load() {
  std::printf("material source: save and load...\n");
  gpx::Graph g;
  g.resolution = 16;
  // Burn some ids, then delete them, so loading cannot reproduce the numbers.
  gpx::Node *junk1 = g.add_node("Noise");
  gpx::Node *junk2 = g.add_node("Noise");
  SourceMaterial m = build_material(g);
  gpx::Node *imp = g.add_node("MaterialSource");
  set_source(imp, m.mat->id);
  const uint64_t was = m.mat->id;
  g.remove_node(junk1->id);
  g.remove_node(junk2->id);

  const std::string text = gpx::graph_to_json(g);
  gpx::Graph loaded;
  std::string err;
  CHECK(gpx::graph_from_json(loaded, text, err), "reloads: " + err);

  gpx::Node *imp2 = nullptr, *mat2 = nullptr;
  for (auto &n : loaded.nodes) {
    if (n->type == "MaterialSource") imp2 = n.get();
    if (n->type == "MaterialOutput") mat2 = n.get();
  }
  CHECK(imp2 && mat2, "both nodes came back");
  if (!imp2 || !mat2) return;
  CHECK(mat2->id != was, "the material really was renumbered (" +
                             std::to_string(was) + " -> " +
                             std::to_string(mat2->id) + ")");
  const gpx::Attribute *src = imp2->attrs.find("source_node");
  CHECK(src && src->s == std::to_string(mat2->id),
        "the reference followed it: " + (src ? src->s : std::string("<none>")));
  CHECK(loaded.evaluate(), "the reloaded graph evaluates");
  CHECK(imp2->error.empty(), "with no error: " + imp2->error);
  CHECK(same_tex(out_tex(imp2, "base color"), mat2->in_tex("base color")),
        "and imports the material it did before the save");
}

// A reference to a node that is not in the file must come back empty, not
// pointed at whatever now holds that number.
void test_a_reference_to_a_deleted_node_comes_back_empty() {
  std::printf("material source: reference to a deleted node...\n");
  gpx::Graph g;
  g.resolution = 16;
  SourceMaterial m = build_material(g);
  gpx::Node *imp = g.add_node("MaterialSource");
  const uint64_t gone = m.mat->id;
  set_source(imp, gone);
  g.remove_node(gone); // saved pointing at nothing

  gpx::Graph loaded;
  std::string err;
  CHECK(gpx::graph_from_json(loaded, gpx::graph_to_json(g), err), "reloads");
  gpx::Node *imp2 = nullptr;
  for (auto &n : loaded.nodes)
    if (n->type == "MaterialSource") imp2 = n.get();
  CHECK(imp2, "the importer came back");
  if (!imp2) return;
  const gpx::Attribute *src = imp2->attrs.find("source_node");
  CHECK(src && src->s.empty(),
        "unbound, rather than pointed at whatever inherited the id: " +
            (src ? src->s : std::string("<none>")));
}

} // namespace

int test_material_source_suite() {
  g_failures = 0;
  test_import_reproduces_the_source();
  test_unwired_channels_stay_empty();
  test_a_change_arrives_in_the_same_pass();
  test_the_source_is_evaluated_first();
  test_a_dangling_source_does_not_stop_the_graph();
  test_a_circular_import_is_a_cycle();
  test_an_unresolved_object_says_so();
  test_a_non_material_source_is_refused();
  test_the_reference_survives_a_save_and_load();
  test_a_reference_to_a_deleted_node_comes_back_empty();
  return g_failures;
}
