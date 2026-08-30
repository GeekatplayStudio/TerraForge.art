// Geekatplay Studio — engine test suite (graph, nodes, serialization)
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static int g_failures = 0;
#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                    \
      g_failures++;                                                             \
    }                                                                           \
  } while (0)

static bool finite_map(const gpx::Heightmap &m) {
  for (float v : m.v)
    if (!std::isfinite(v)) return false;
  return !m.empty();
}

static gpx::Heightmap *out_of(gpx::Node *n, const char *port = "output") {
  gpx::Port *p = n->port(port);
  return p && p->hmap ? p->hmap.get() : nullptr;
}

static void test_registry() {
  std::printf("registry...\n");
  auto all = gpx::NodeRegistry::instance().all();
  CHECK(all.size() >= 25, "at least 25 node types registered");
  for (const char *t :
       {"Noise", "Fractal", "Shape", "Hydraulic", "Thermal", "StreamPower", "Wind",
        "SedimentDeposit", "Blend", "Smooth", "Terrace", "SelectSlope", "Threshold",
        "Compare", "Switch", "Thru", "TerrainTexture", "Splatmap", "SplatMaterial",
        "TextureBlend", "AlbedoToPBR", "NormalMap", "ExportHeightmap",
        "PowerFractal", "FakeStones", "Stratify", "Shear", "Craggy", "Crater",
        "Dunes", "Snow", "Rivers", "Coast", "Stamp", "HeightmapFile", "Select",
        "Repeat", "MathGradient", "ColorAdjust", "FlatColor"})
    CHECK(gpx::NodeRegistry::instance().find(t) != nullptr, t);
}

static void test_graph_eval() {
  std::printf("graph evaluation...\n");
  gpx::Graph g;
  g.resolution = 128;
  gpx::Node *noise = g.add_node("Noise");
  gpx::Node *smooth = g.add_node("Smooth");
  CHECK(noise && smooth, "nodes created");
  CHECK(g.add_link(noise->id, "output", smooth->id, "input"), "link created");
  CHECK(g.evaluate(), "evaluate succeeds");
  gpx::Heightmap *out = out_of(smooth);
  CHECK(out && finite_map(*out), "smooth output finite");
  CHECK(out->w == 128, "output at graph resolution");
  // dirty tracking: nothing recomputes on second pass
  CHECK(!noise->dirty && !smooth->dirty, "clean after eval");
  double ms = smooth->last_compute_ms;
  g.evaluate();
  CHECK(smooth->last_compute_ms == ms, "no recompute when clean");
  // attribute change recomputes downstream only
  g.mark_dirty(smooth->id);
  CHECK(smooth->dirty && !noise->dirty, "dirty propagates downstream only");
}

static void test_cycle_rejection() {
  std::printf("cycle rejection...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *a = g.add_node("Smooth");
  gpx::Node *b = g.add_node("Smooth");
  CHECK(g.add_link(a->id, "output", b->id, "input"), "forward link ok");
  CHECK(!g.add_link(b->id, "output", a->id, "input"), "cycle rejected");
  CHECK(g.links.size() == 1, "cycle link not kept");
}

static void test_determinism() {
  std::printf("noise determinism...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *n1 = g.add_node("Noise");
  g.evaluate();
  gpx::Heightmap first = *out_of(n1);
  g.mark_all_dirty();
  g.evaluate();
  gpx::Heightmap second = *out_of(n1);
  CHECK(first.v == second.v, "same seed reproduces exactly");
  n1->attrs.find("seed")->seed = 999;
  g.mark_all_dirty();
  g.evaluate();
  CHECK(out_of(n1)->v != first.v, "different seed differs");
}

static void test_erosion() {
  std::printf("erosion sanity...\n");
  for (int method = 0; method < 2; ++method) {
    gpx::Graph g;
    g.resolution = 128;
    gpx::Node *noise = g.add_node("Noise");
    gpx::Node *ero = g.add_node("Hydraulic");
    ero->attrs.find("method")->i = method;
    ero->attrs.find("particles")->i = 20;
    ero->attrs.find("iterations")->i = 30;
    g.add_link(noise->id, "output", ero->id, "input");
    CHECK(g.evaluate(), "erosion evaluates");
    gpx::Heightmap *in = out_of(noise), *out = out_of(ero);
    CHECK(out && finite_map(*out), "erosion output finite");
    CHECK(in->v != out->v, "erosion changed the terrain");
    // range preservation: normalized workflow keeps output within sane bounds
    float mn, mx, mn2, mx2;
    in->minmax(mn, mx);
    out->minmax(mn2, mx2);
    CHECK(mx2 - mn2 < (mx - mn) * 2.f + 1.f, "erosion amplitude bounded");
  }
  // stream power both methods
  for (int method = 0; method < 2; ++method) {
    gpx::Graph g;
    g.resolution = 96;
    gpx::Node *noise = g.add_node("Noise");
    gpx::Node *sp = g.add_node("StreamPower");
    sp->attrs.find("method")->i = method;
    sp->attrs.find("iterations")->i = 8;
    g.add_link(noise->id, "output", sp->id, "input");
    CHECK(g.evaluate(), "stream power evaluates");
    CHECK(finite_map(*out_of(sp)), "stream power finite");
    gpx::Port *fp = sp->port("flow_map");
    CHECK(fp && fp->hmap && finite_map(*fp->hmap), "flow map produced");
  }
}

static void test_masks_and_logic() {
  std::printf("masks & logic...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *noise = g.add_node("Noise");
  gpx::Node *sel = g.add_node("SelectSlope");
  gpx::Node *thr = g.add_node("Threshold");
  g.add_link(noise->id, "output", sel->id, "input");
  g.add_link(noise->id, "output", thr->id, "input");
  g.evaluate();
  for (gpx::Node *n : {sel, thr}) {
    gpx::Heightmap *m = out_of(n, "mask");
    CHECK(m && finite_map(*m), "mask finite");
    float mn, mx;
    m->minmax(mn, mx);
    CHECK(mn >= -1e-5f && mx <= 1.f + 1e-5f, "mask in [0,1]");
  }
}

static void test_materials() {
  std::printf("materials...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *noise = g.add_node("Noise");
  gpx::Node *tex = g.add_node("TerrainTexture");
  gpx::Node *m1 = g.add_node("Threshold");
  gpx::Node *splat = g.add_node("Splatmap");
  gpx::Node *comp = g.add_node("SplatMaterial");
  gpx::Node *pbr = g.add_node("AlbedoToPBR");
  g.add_link(noise->id, "output", tex->id, "input");
  g.add_link(noise->id, "output", m1->id, "input");
  g.add_link(m1->id, "mask", splat->id, "mask R");
  g.add_link(splat->id, "splat", comp->id, "splat");
  g.add_link(tex->id, "texture", comp->id, "layer R");
  g.add_link(comp->id, "texture", pbr->id, "albedo");
  CHECK(g.evaluate(), "material chain evaluates");
  auto tex_ok = [&](gpx::Node *n, const char *port) {
    gpx::Port *p = n->port(port);
    if (!p || !p->tex || p->tex->empty()) return false;
    for (float v : p->tex->v)
      if (!std::isfinite(v)) return false;
    return true;
  };
  CHECK(tex_ok(tex, "texture"), "terrain texture ok");
  CHECK(tex_ok(splat, "splat"), "splatmap ok");
  CHECK(tex_ok(comp, "texture"), "splat composite ok");
  CHECK(tex_ok(pbr, "normal"), "pbr normal ok");
  CHECK(tex_ok(pbr, "roughness"), "pbr roughness ok");
  for (gpx::Node *n : {tex, m1, splat, comp, pbr})
    CHECK(n->error.empty(), "no node errors in material chain");
}

static void test_serialization() {
  std::printf("serialization roundtrip...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *noise = g.add_node("Noise", 10, 20);
  gpx::Node *ero = g.add_node("Hydraulic", 300, 40);
  noise->attrs.find("octaves")->i = 5;
  noise->attrs.find("seed")->seed = 4242;
  g.add_link(noise->id, "output", ero->id, "input");
  std::string json = gpx::graph_to_json(g);
  gpx::Graph g2;
  std::string err;
  CHECK(gpx::graph_from_json(g2, json, err), "load ok");
  CHECK(g2.nodes.size() == 2, "node count preserved");
  CHECK(g2.links.size() == 1, "link preserved");
  gpx::Node *n2 = g2.nodes[0]->type == "Noise" ? g2.nodes[0].get() : g2.nodes[1].get();
  CHECK(n2->attrs.find("octaves")->i == 5, "int attr preserved");
  CHECK(n2->attrs.find("seed")->seed == 4242u, "seed preserved");
  CHECK(g2.evaluate(), "loaded graph evaluates");
}

static void test_surface_nodes() {
  std::printf("surface & hydro nodes...\n");
  gpx::Graph g;
  g.resolution = 96;
  gpx::Node *noise = g.add_node("Noise");
  const char *chain[] = {"PowerFractal", "FakeStones", "Stratify", "Shear",
                         "Craggy",       "Snow",       "Rivers",   "Coast",
                         "Dunes",        "Crater",     "Terrace"};
  for (const char *t : chain) {
    gpx::Node *node = g.add_node(t);
    CHECK(node != nullptr, t);
    if (!node) continue;
    // wire noise into the first heightmap input if present
    for (auto &p : node->ports)
      if (p.dir == gpx::PortDir::In && p.type == gpx::DataType::Heightmap &&
          !p.optional) {
        g.add_link(noise->id, "output", node->id, p.name);
        break;
      }
  }
  CHECK(g.evaluate(), "surface graph evaluates");
  for (auto &node : g.nodes) {
    if (node->type == "Noise") continue;
    gpx::Port *out = node->first_out(gpx::DataType::Heightmap);
    bool ok = out && out->hmap && finite_map(*out->hmap);
    CHECK(ok, (node->type + " output finite").c_str());
    CHECK(node->error.empty() || node->type == "Dunes" || node->type == "Crater",
          (node->type + " no error").c_str());
  }
}

static void test_ai_spec() {
  std::printf("AI spec builder...\n");
  const char *spec = R"({
    "nodes": [
      {"id":"base","type":"Noise","attrs":{"type":"Ridged","octaves":10,"bogus":1},"pos":[0,100]},
      {"id":"ero","type":"Hydraulic","attrs":{"particles":30},"pos":[260,100]},
      {"id":"tex","type":"TerrainTexture","attrs":{"snow_line":0.8},"pos":[520,100]},
      {"id":"bad","type":"NoSuchNode","attrs":{}}
    ],
    "links": [
      ["base","output","ero","input"],
      ["ero","output","tex","input"],
      ["bad","output","tex","flow"]
    ],
    "environment": {"sun_azimuth": 220, "fog_type": 2}
  })";
  gpx::Graph g;
  g.resolution = 64;
  std::string err, env;
  CHECK(gpx::graph_from_ai_spec(g, spec, err, &env), "spec builds");
  CHECK(g.nodes.size() == 3, "unknown node skipped, 3 built");
  CHECK(g.links.size() == 2, "link to unknown node dropped");
  CHECK(!env.empty(), "environment returned");
  gpx::Node *noise = nullptr;
  for (auto &n : g.nodes)
    if (n->type == "Noise") noise = n.get();
  CHECK(noise && noise->attrs.find("type")->i == 1, "choice set by label string");
  CHECK(noise && noise->attrs.find("octaves")->i == 10, "int attr set");
  CHECK(g.evaluate(), "AI-built graph evaluates");
  // fenced response also parses
  std::string fenced = std::string("```json\n") + spec + "\n```";
  gpx::Graph g2;
  g2.resolution = 32;
  CHECK(gpx::graph_from_ai_spec(g2, fenced, err, nullptr), "markdown fences tolerated");
  // catalog exists and mentions key nodes
  std::string cat = gpx::registry_catalog_for_ai();
  CHECK(cat.find("Hydraulic") != std::string::npos, "catalog lists Hydraulic");
  CHECK(cat.find("snow_line") != std::string::npos, "catalog lists attrs");
}

int main() {
  std::printf("=== Geekatplay Studio engine tests ===\n");
  test_registry();
  test_graph_eval();
  test_cycle_rejection();
  test_determinism();
  test_erosion();
  test_masks_and_logic();
  test_materials();
  test_serialization();
  test_surface_nodes();
  test_ai_spec();
  if (g_failures == 0) {
    std::printf("ALL ENGINE TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
