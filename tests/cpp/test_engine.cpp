// Geekatplay Studio — engine test suite (graph, nodes, serialization)
#include "gpx/camera_math.hpp"
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
        "Repeat", "MathGradient", "ColorAdjust", "FlatColor", "MaterialOutput",
        "Levels", "GradientMap", "NormalBlend", "TextureTransform",
        "AOFromHeight", "CurvatureFromHeight", "ChannelMix", "MaskToTexture",
        "TextureToMask", "SunLight", "AtmosphereSettings", "CloudLayer",
        "WaterLayer"})
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
    // erosion must carve, not obliterate: the relief has to survive
    CHECK(mx2 - mn2 > (mx - mn) * 0.25f, "erosion preserves relief");
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

static void test_material_graph() {
  std::printf("material graph...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *noise = g.add_node("Noise");
  gpx::Node *tex = g.add_node("TerrainTexture");
  gpx::Node *lv = g.add_node("Levels");
  gpx::Node *gm = g.add_node("GradientMap");
  gpx::Node *ao = g.add_node("AOFromHeight");
  gpx::Node *cv = g.add_node("CurvatureFromHeight");
  gpx::Node *m2t = g.add_node("MaskToTexture");
  gpx::Node *t2m = g.add_node("TextureToMask");
  gpx::Node *xf = g.add_node("TextureTransform");
  gpx::Node *mat = g.add_node("MaterialOutput");
  CHECK(g.add_link(noise->id, "output", tex->id, "input"), "noise->tex");
  CHECK(g.add_link(tex->id, "texture", lv->id, "texture"), "tex->levels");
  CHECK(g.add_link(lv->id, "texture", gm->id, "texture"), "levels->gradient");
  CHECK(g.add_link(gm->id, "texture", xf->id, "texture"), "gradient->xform");
  CHECK(g.add_link(noise->id, "output", ao->id, "height"), "noise->ao");
  CHECK(g.add_link(noise->id, "output", cv->id, "height"), "noise->curv");
  CHECK(g.add_link(noise->id, "output", m2t->id, "input"), "noise->m2t");
  CHECK(g.add_link(xf->id, "texture", t2m->id, "texture"), "xform->t2m");
  CHECK(g.add_link(xf->id, "texture", mat->id, "base color"), "->base color");
  CHECK(g.add_link(ao->id, "texture", mat->id, "ambient occlusion"), "->ao");
  CHECK(g.add_link(m2t->id, "texture", mat->id, "roughness"), "->roughness");
  CHECK(g.evaluate(), "material graph evaluates");
  for (gpx::Node *n : {tex, lv, gm, ao, cv, m2t, xf, mat})
    CHECK(n->error.empty(), (n->type + " no error").c_str());
  gpx::Port *prev = mat->port("preview");
  CHECK(prev && prev->tex && !prev->tex->empty(), "MaterialOutput preview filled");
  gpx::Port *mask = t2m->port("mask");
  CHECK(mask && mask->hmap && finite_map(*mask->hmap), "TextureToMask finite");
}

// Deterministic workflow tests: a saved material must reproduce byte-identical
// channel data after a save/load round trip, and every step of a workflow must
// be reproducible from the same seeds.
static void test_material_library_roundtrip() {
  std::printf("material library roundtrip...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *noise = g.add_node("Noise");
  gpx::Node *tex = g.add_node("TerrainTexture");
  gpx::Node *lv = g.add_node("Levels");
  gpx::Node *nrm = g.add_node("NormalMap");
  gpx::Node *ao = g.add_node("AOFromHeight");
  gpx::Node *mat = g.add_node("MaterialOutput");
  // an unrelated node that must NOT be captured by the material
  gpx::Node *stray = g.add_node("Thermal");
  g.add_link(noise->id, "output", tex->id, "input");
  g.add_link(tex->id, "texture", lv->id, "texture");
  g.add_link(lv->id, "texture", mat->id, "base color");
  g.add_link(noise->id, "output", nrm->id, "input");
  g.add_link(nrm->id, "texture", mat->id, "normal");
  g.add_link(noise->id, "output", ao->id, "height");
  g.add_link(ao->id, "texture", mat->id, "ambient occlusion");
  g.add_link(noise->id, "output", stray->id, "input");
  if (gpx::Attribute *at = mat->attrs.find("name")) at->s = "Test Bark";
  if (gpx::Attribute *at = mat->attrs.find("roughness")) at->f = 0.42f;
  if (gpx::Attribute *at = lv->attrs.find("gamma")) at->f = 1.7f;
  CHECK(g.evaluate(), "source material evaluates");

  std::string doc = gpx::material_to_json(g, mat->id);
  CHECK(doc.find("terraforge-material") != std::string::npos, "format tag");
  CHECK(doc.find("Thermal") == std::string::npos,
        "unrelated downstream node not captured");

  // load into a *fresh* graph: an independent copy, no id collisions
  gpx::Graph g2;
  g2.resolution = 64;
  std::string err;
  uint64_t new_id = gpx::material_from_json(g2, doc, err, 100, 100);
  CHECK(new_id != 0, "material loads");
  CHECK(g2.nodes.size() == 6, "6 nodes restored (stray excluded)");
  gpx::Node *m2 = g2.find_node(new_id);
  CHECK(m2 && m2->type == "MaterialOutput", "output node identified");
  CHECK(m2 && m2->attrs.get_s("name") == "Test Bark", "name preserved");
  CHECK(m2 && std::fabs(m2->attrs.get_f("roughness") - 0.42f) < 1e-6f,
        "surface value preserved");
  CHECK(g2.evaluate(), "loaded material evaluates");

  // deterministic: the channels must be bit-identical to the original
  auto chan = [](gpx::Node *m, const char *port) -> const gpx::TextureRGBA * {
    return m->in_tex(port);
  };
  const gpx::TextureRGBA *a1 = chan(mat, "base color");
  const gpx::TextureRGBA *a2 = chan(m2, "base color");
  CHECK(a1 && a2 && a1->v == a2->v, "base color identical after roundtrip");
  const gpx::TextureRGBA *n1 = chan(mat, "normal");
  const gpx::TextureRGBA *n2 = chan(m2, "normal");
  CHECK(n1 && n2 && n1->v == n2->v, "normal identical after roundtrip");
  const gpx::TextureRGBA *o1 = chan(mat, "ambient occlusion");
  const gpx::TextureRGBA *o2 = chan(m2, "ambient occlusion");
  CHECK(o1 && o2 && o1->v == o2->v, "AO identical after roundtrip");

  // loading twice into the same graph yields two independent materials
  uint64_t third = gpx::material_from_json(g2, doc, err, 400, 400);
  CHECK(third != 0 && third != new_id, "second load is an independent copy");
  CHECK(g2.nodes.size() == 12, "copies do not share nodes");
  CHECK(g2.evaluate(), "graph with two material copies evaluates");
}

// Every workflow step must be reproducible: same graph + same seeds =>
// identical output, and re-running a step after a no-op edit changes nothing.
static void test_workflow_determinism() {
  std::printf("workflow determinism...\n");
  const char *chain[] = {"Noise", "WarpNoise", "Hydraulic", "StreamPower",
                         "Thermal", "TerrainTexture"};
  auto build = [&](gpx::Graph &g) {
    g.resolution = 64;
    gpx::Node *prev = nullptr;
    for (const char *t : chain) {
      gpx::Node *n = g.add_node(t);
      if (!n) continue;
      if (prev) {
        if (n->port("input", gpx::PortDir::In))
          g.add_link(prev->id, "output", n->id, "input");
      }
      if (gpx::Attribute *s = n->attrs.find("seed")) s->seed = 1234;
      if (gpx::Attribute *it = n->attrs.find("iterations")) it->i = 6;
      if (gpx::Attribute *p = n->attrs.find("particles")) p->i = 8;
      prev = n;
    }
    return prev;
  };
  gpx::Graph g1, g2;
  gpx::Node *last1 = build(g1);
  gpx::Node *last2 = build(g2);
  CHECK(g1.evaluate() && g2.evaluate(), "both workflows evaluate");
  // compare every step, not just the final result
  CHECK(g1.nodes.size() == g2.nodes.size(), "same node count");
  for (size_t i = 0; i < g1.nodes.size(); ++i) {
    gpx::Port *p1 = g1.nodes[i]->first_out(gpx::DataType::Heightmap);
    gpx::Port *p2 = g2.nodes[i]->first_out(gpx::DataType::Heightmap);
    if (!p1 || !p2 || !p1->hmap || !p2->hmap) continue;
    CHECK(p1->hmap->v == p2->hmap->v,
          (g1.nodes[i]->type + " step is deterministic").c_str());
  }
  // re-evaluating a clean graph must not alter any step
  std::vector<float> before;
  if (last1) {
    gpx::Port *p = last1->first_out(gpx::DataType::Texture);
    if (p && p->tex) before = p->tex->v;
  }
  g1.evaluate();
  if (last1 && !before.empty()) {
    gpx::Port *p = last1->first_out(gpx::DataType::Texture);
    CHECK(p && p->tex && p->tex->v == before, "clean re-eval changes nothing");
  }
  // forcing a full recompute must reproduce the same result
  g1.mark_all_dirty();
  g1.evaluate();
  if (last1 && !before.empty()) {
    gpx::Port *p = last1->first_out(gpx::DataType::Texture);
    CHECK(p && p->tex && p->tex->v == before, "full recompute reproduces");
  }
}

static void test_camera_math() {
  std::printf("camera math...\n");
  using namespace gpx::cam;
  int nf = 0;
  const SensorFormat *F = sensor_formats(&nf);
  CHECK(nf >= 7, "sensor formats registered");
  CHECK(std::fabs(F[0].width_mm - 36.f) < 1e-4f, "full frame is 36mm wide");

  // a 50mm lens on full frame is the classic ~39.6 deg vertical / 46.8 horizontal
  float fy = fov_y_deg(50.f, 24.f);
  CHECK(std::fabs(fy - 27.0f) < 0.5f, "50mm full frame vertical fov ~27 deg");
  float fx = fov_x_deg(50.f, 36.f);
  CHECK(std::fabs(fx - 39.6f) < 0.5f, "50mm full frame horizontal fov ~39.6 deg");
  // wider lens => wider view, longer lens => narrower
  CHECK(fov_y_deg(24.f, 24.f) > fy, "24mm is wider than 50mm");
  CHECK(fov_y_deg(200.f, 24.f) < fy, "200mm is narrower than 50mm");
  // a smaller sensor crops the same lens
  CHECK(fov_y_deg(50.f, 15.7f) < fov_y_deg(50.f, 24.f), "APS-C crops");

  // exposure: the reference triangle is neutral
  float m = exposure_multiplier(8.f, 1.f / 125.f, 100.f);
  CHECK(std::fabs(m - 1.f) < 0.05f, "f/8 1/125 ISO100 is the neutral exposure");
  // one stop wider open doubles the light
  CHECK(exposure_multiplier(5.6f, 1.f / 125.f, 100.f) > m * 1.8f,
        "opening one stop roughly doubles exposure");
  // doubling ISO doubles exposure; halving shutter halves it
  CHECK(exposure_multiplier(8.f, 1.f / 125.f, 200.f) > m * 1.8f,
        "ISO 200 is one stop brighter");
  CHECK(exposure_multiplier(8.f, 1.f / 250.f, 100.f) < m * 0.6f,
        "1/250 is one stop darker");
  // EV maths
  CHECK(std::fabs(ev100(1.f, 1.f, 100.f)) < 1e-4f, "f/1 1s ISO100 is EV 0");
  CHECK(std::fabs(ev100(2.f, 1.f, 100.f) - 2.f) < 1e-4f, "f/2 1s is EV 2");

  int nfilm = 0;
  const FilmStock *S = film_stocks(&nfilm);
  CHECK(nfilm >= 6, "film stocks registered");
  CHECK(std::fabs(S[0].saturation - 1.f) < 1e-4f, "digital stock is neutral");
  bool has_bw = false;
  for (int i = 0; i < nfilm; ++i)
    if (S[i].saturation <= 0.001f) has_bw = true;
  CHECK(has_bw, "a black and white stock exists");

  CHECK(aperture_radius(50.f, 2.f, 0.001f) >
            aperture_radius(50.f, 8.f, 0.001f),
        "wider aperture gives a larger blur circle");
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
  test_material_graph();
  test_material_library_roundtrip();
  test_workflow_determinism();
  test_camera_math();
  test_ai_spec();
  if (g_failures == 0) {
    std::printf("ALL ENGINE TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
