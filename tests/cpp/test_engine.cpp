// Geekatplay Studio â€” engine test suite (graph, nodes, serialization)
#include "gpx/camera_math.hpp"
#include "gpx/planet_math.hpp"
#include "gpx/field_glsl.hpp"
#include "gpx/metanode.hpp"
#include <vector>
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_failures = 0;
// Overloaded rather than a bare printf: passing a std::string to "%s" is
// undefined behaviour, and a test suite that crashes while reporting a failure
// is worse than useless.
static void check_fail(const char *msg, int line) {
  std::printf("  [FAIL] %s (line %d)\n", msg, line);
  ++g_failures;
}
static void check_fail(const std::string &msg, int line) {
  check_fail(msg.c_str(), line);
}
#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (!(cond)) check_fail(msg, __LINE__);                                     \
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

static void test_terrain_effects() {
  std::printf("terrain effects...\n");
  for (const char *t : {"Grit", "Gravel", "Peaks", "Sharpen", "Cracks",
                        "Glaciation", "Dissolve", "TerrainClip", "TerrainSculpt"})
    CHECK(gpx::NodeRegistry::instance().find(t) != nullptr, t);

  // every effect runs, keeps the map finite, and reproduces bit-identically
  for (const char *t : {"Grit", "Gravel", "Peaks", "Sharpen", "Cracks",
                        "Glaciation", "Dissolve", "TerrainClip"}) {
    gpx::Graph g;
    g.resolution = 96;
    gpx::Node *src = g.add_node("Fractal");
    gpx::Node *fx = g.add_node(t);
    CHECK(src && fx, "nodes created");
    if (!src || !fx) continue;
    g.add_link(src->id, "output", fx->id, "input");
    g.evaluate();
    gpx::Heightmap *o1 = out_of(fx);
    CHECK(o1 && finite_map(*o1), t);
    std::vector<float> first = o1->v;
    g.mark_all_dirty();
    g.evaluate();
    gpx::Heightmap *o2 = out_of(fx);
    bool same = o2 && o2->v == first;
    CHECK(same, (std::string(t) + " deterministic").c_str());
  }

  // Dissolve carves and produces a flow map
  {
    gpx::Graph g;
    g.resolution = 96;
    gpx::Node *src = g.add_node("Fractal");
    gpx::Node *fx = g.add_node("Dissolve");
    g.add_link(src->id, "output", fx->id, "input");
    g.evaluate();
    gpx::Heightmap *flow = out_of(fx, "flow_map");
    CHECK(flow && finite_map(*flow), "Dissolve flow map");
    float mn, mx;
    flow->minmax(mn, mx);
    CHECK(mx > mn, "flow map is not flat");
  }

  // TerrainClip flattens above the high mark and outputs the hole mask
  {
    gpx::Graph g;
    g.resolution = 64;
    gpx::Node *src = g.add_node("Fractal");
    gpx::Node *fx = g.add_node("TerrainClip");
    g.add_link(src->id, "output", fx->id, "input");
    if (gpx::Attribute *r = fx->attrs.find("clip")) {
      r->v2[0] = 0.2f;
      r->v2[1] = 0.7f;
    }
    if (gpx::Attribute *m = fx->attrs.find("low_mode")) m->i = 2; // hole
    g.evaluate();
    gpx::Heightmap *out = out_of(fx);
    gpx::Heightmap *mask = out_of(fx, "clip_mask");
    CHECK(out && mask, "clip outputs exist");
    if (out && mask) {
      float mn, mx, imn, imx;
      out->minmax(mn, mx);
      out_of(fx) /* keep */;
      gpx::Heightmap *in = out_of(src);
      in->minmax(imn, imx);
      float d = imx - imn;
      CHECK(mx <= imn + 0.7f * d + 1e-4f, "high clip flattened the peaks");
      float mmin, mmax;
      mask->minmax(mmin, mmax);
      CHECK(mmin < 0.5f && mmax > 0.5f, "clip mask marks the holes");
    }
  }
}

static void test_sculpt_layer() {
  std::printf("sculpt layer...\n");
  gpx::Graph g;
  g.resolution = 96;
  gpx::Node *src = g.add_node("Fractal");
  gpx::Node *sc = g.add_node("TerrainSculpt");
  CHECK(src && sc, "nodes created");
  g.add_link(src->id, "output", sc->id, "input");
  g.evaluate();
  gpx::Heightmap *base = out_of(sc);
  CHECK(base && finite_map(*base), "empty sculpt passes the terrain through");
  gpx::Heightmap *in = out_of(src);
  bool passthrough = base && in && base->v == in->v;
  CHECK(passthrough, "unpainted layer is a no-op");

  // paint a bump into the field and check it lands on the surface
  gpx::Attribute *fa = sc->attrs.find("delta");
  CHECK(fa && fa->fw > 0, "delta field declared");
  fa->field.assign((size_t)fa->fw * fa->fh, 0.f);
  int cx = fa->fw / 2, cy = fa->fh / 2;
  for (int y = -6; y <= 6; ++y)
    for (int x = -6; x <= 6; ++x)
      fa->field[(size_t)(cy + y) * fa->fw + (cx + x)] = 0.5f;
  g.mark_dirty(sc->id);
  g.evaluate();
  gpx::Heightmap *bumped = out_of(sc);
  CHECK(bumped, "sculpted eval");
  if (bumped && in) {
    float before = in->sample(0.5f, 0.5f);
    float after = bumped->sample(0.5f, 0.5f);
    CHECK(after > before + 1e-4f, "the stroke raised the surface");
    // corners untouched
    CHECK(std::fabs(bumped->sample(0.05f, 0.05f) - in->sample(0.05f, 0.05f)) <
              1e-5f,
          "unpainted areas unchanged");
  }
  gpx::Heightmap *smask = out_of(sc, "stroke_mask");
  CHECK(smask && smask->sample(0.5f, 0.5f) > 0.2f, "stroke mask marks the edit");

  // the painted field must survive a save/load round trip bit-exactly enough
  // to reproduce the same surface (16-bit quantization over its own range)
  std::string j = gpx::graph_to_json(g);
  gpx::Graph g2;
  std::string err;
  CHECK(gpx::graph_from_json(g2, j, err), "graph with field round trips");
  gpx::Node *sc2 = nullptr;
  for (auto &n : g2.nodes)
    if (n->type == "TerrainSculpt") sc2 = n.get();
  CHECK(sc2 != nullptr, "sculpt node restored");
  if (sc2) {
    const gpx::Attribute *fb = sc2->attrs.find("delta");
    CHECK(fb && fb->field.size() == fa->field.size(), "field data restored");
    if (fb && fb->field.size() == fa->field.size()) {
      float worst = 0;
      for (size_t i = 0; i < fb->field.size(); ++i)
        worst = std::max(worst, std::fabs(fb->field[i] - fa->field[i]));
      CHECK(worst < 1e-4f, "field survives 16-bit quantization");
    }
    g2.evaluate();
    gpx::Heightmap *b2 = out_of(sc2);
    CHECK(b2 && std::fabs(b2->sample(0.5f, 0.5f) - bumped->sample(0.5f, 0.5f)) <
                    1e-3f,
          "restored sculpt reproduces the surface");
  }
}

static void test_planet_math() {
  std::printf("planet math...\n");
  using namespace gpx::planet;
  Layer L[2];
  L[0].seed = 42;
  L[0].type = 1;
  L[0].frequency = 4.f;
  L[1].seed = 7;
  L[1].type = 0;
  L[1].frequency = 11.f;
  L[1].amplitude = 0.5f;
  L[1].coverage = 0.6f;

  // deterministic: the same direction always gives the same altitude
  float d1[3] = {0.267f, 0.535f, 0.802f};
  float h_a = height(d1, L, 2);
  float h_b = height(d1, L, 2);
  CHECK(h_a == h_b, "planet height is deterministic");
  CHECK(std::isfinite(h_a), "height is finite");
  CHECK(std::fabs(h_a) <= 0.75f, "height stays within the relief budget");

  // different directions give different terrain (the planet is not flat)
  float mn = 1e9f, mx = -1e9f;
  for (int i = 0; i < 400; ++i) {
    float th = i * 0.61803f * 6.2831853f;
    float y = 1.f - 2.f * (i + 0.5f) / 400.f;
    float r = std::sqrt(std::max(0.f, 1.f - y * y));
    float d[3] = {r * std::cos(th), y, r * std::sin(th)};
    float h = height(d, L, 2);
    mn = std::min(mn, h);
    mx = std::max(mx, h);
    CHECK(std::isfinite(h), "sampled height finite");
    if (!std::isfinite(h)) break;
  }
  CHECK(mx - mn > 0.05f, "the surface has real relief");

  // 3D continuity (the reason the function is evaluated in 3D at all):
  // neighbouring directions give neighbouring altitudes â€” no seams
  float step = 0.002f;
  float worst = 0.f;
  for (int i = 0; i < 200; ++i) {
    float th = i * 0.031f;
    float a[3] = {std::cos(th), 0.4f, std::sin(th)};
    float b[3] = {std::cos(th + step), 0.4f, std::sin(th + step)};
    worst = std::max(worst, std::fabs(height(a, L, 2) - height(b, L, 2)));
  }
  CHECK(worst < 0.08f, "surface is continuous (no seams between directions)");

  // a different seed is a different planet
  Layer L2[1] = {L[0]};
  L2[0].seed = 43;
  bool differs = false;
  for (int i = 0; i < 32 && !differs; ++i) {
    float th = i * 0.41f;
    float d[3] = {std::cos(th), 0.2f, std::sin(th)};
    if (std::fabs(height(d, L, 1) - height(d, L2, 1)) > 1e-4f) differs = true;
  }
  CHECK(differs, "changing the seed changes the planet");

  // coverage 0 silences a layer entirely
  Layer L3[1] = {L[0]};
  L3[0].coverage = 0.f;
  CHECK(height(d1, L3, 1) == 0.f, "coverage 0 = no contribution");
}

// ---------------------------------------------------------- field domain
// The second evaluation domain (P0.1): functions evaluated per point, with no
// resolution anywhere in them. These tests pin the properties the whole
// framework rests on.
static void test_field_domain() {
  std::printf("field domain...\n");
  for (const char *t : {"FieldPosition", "FieldNormal", "FieldAltitude",
                        "FieldSlope", "FieldOrientation", "FieldTime",
                        "FieldConstant", "FieldColorConstant", "FieldMath",
                        "FieldTrig", "FieldRemap", "FieldCurve", "FieldMix",
                        "FieldVectorOp", "FieldNoise", "FieldGradient",
                        "Rasterize", "Sample"})
    CHECK(gpx::NodeRegistry::instance().find(t) != nullptr, t);

  // inputs report what the context says
  {
    gpx::Graph g;
    gpx::Node *alt = g.add_node("FieldAltitude");
    gpx::Node *slope = g.add_node("FieldSlope");
    gpx::FieldContext ctx;
    ctx.altitude = 12.5f;
    ctx.slope = 0.25f;
    CHECK(alt->eval_field("out", ctx).number() == 12.5f, "altitude input");
    CHECK(slope->eval_field("out", ctx).number() == 0.25f, "slope input");
  }

  // a field is resolution-independent: the value at a point does not depend on
  // any buffer size, and asking twice gives the same answer
  {
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::FieldContext a = gpx::FieldContext::at(0.31f, 0.f, 0.72f);
    float first = noise->eval_field("out", a).number();
    float again = noise->eval_field("out", a).number();
    CHECK(first == again, "field evaluation is deterministic");
    CHECK(std::isfinite(first), "field value is finite");
    // a nearby point differs, a far point differs more: it is a real field
    gpx::FieldContext b = gpx::FieldContext::at(0.62f, 0.f, 0.11f);
    CHECK(noise->eval_field("out", b).number() != first, "field varies in space");
  }

  // math chains pull through links
  {
    gpx::Graph g;
    gpx::Node *ca = g.add_node("FieldConstant");
    gpx::Node *cb = g.add_node("FieldConstant");
    gpx::Node *m = g.add_node("FieldMath");
    ca->attrs.find("value")->f = 3.f;
    cb->attrs.find("value")->f = 4.f;
    CHECK(g.add_link(ca->id, "out", m->id, "a"), "field link a");
    CHECK(g.add_link(cb->id, "out", m->id, "b"), "field link b");
    gpx::FieldContext ctx;
    m->attrs.find("op")->i = 0; // add
    CHECK(m->eval_field("out", ctx).number() == 7.f, "add");
    m->attrs.find("op")->i = 2; // multiply
    CHECK(m->eval_field("out", ctx).number() == 12.f, "multiply");
    m->attrs.find("op")->i = 3; // divide
    CHECK(std::fabs(m->eval_field("out", ctx).number() - 0.75f) < 1e-6f, "divide");
    // division by zero must not poison the graph with NaN
    cb->attrs.find("value")->f = 0.f;
    CHECK(std::isfinite(m->eval_field("out", ctx).number()), "divide by zero is safe");
  }

  // an unconnected input falls back, so a half-built graph still previews
  {
    gpx::Graph g;
    gpx::Node *m = g.add_node("FieldMath");
    m->attrs.find("a_default")->f = 2.f;
    m->attrs.find("b_default")->f = 5.f;
    m->attrs.find("op")->i = 0;
    gpx::FieldContext ctx;
    CHECK(m->eval_field("out", ctx).number() == 7.f,
          "unconnected inputs use their defaults");
  }

  // type conversion is permissive: a colour read as a number is its luminance
  {
    gpx::FieldValue c = gpx::FieldValue::color(1.f, 1.f, 1.f);
    CHECK(std::fabs(c.number() - 1.f) < 1e-5f, "white reads as 1.0");
    gpx::FieldValue v = gpx::FieldValue::vector(3.f, 4.f, 0.f);
    CHECK(std::fabs(v.number() - 5.f) < 1e-5f, "vector reads as its length");
  }

  // level of detail caps octaves, so a distant point costs less but stays sane
  {
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    noise->attrs.find("octaves")->i = 10;
    gpx::FieldContext near_pt = gpx::FieldContext::at(0.4f, 0.f, 0.4f);
    gpx::FieldContext far_pt = near_pt;
    far_pt.lod = 2.f;
    float hi = noise->eval_field("out", near_pt).number();
    float lo = noise->eval_field("out", far_pt).number();
    CHECK(std::isfinite(lo), "low-detail evaluation is finite");
    CHECK(hi != lo, "detail budget actually changes the result");
  }
}

// The bridges are what keep the two domains one system: erosion must stay
// reachable from a field graph, and an eroded heightfield must be able to
// drive a shader.
static void test_field_bridges() {
  std::printf("field/raster bridges...\n");
  // field -> buffer
  {
    gpx::Graph g;
    g.resolution = 64;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *rast = g.add_node("Rasterize");
    CHECK(g.add_link(noise->id, "out", rast->id, "field"), "field feeds Rasterize");
    g.evaluate();
    gpx::Heightmap *out = out_of(rast);
    CHECK(out && finite_map(*out), "rasterized output is finite");
    float mn, mx;
    out->minmax(mn, mx);
    CHECK(mx > mn, "rasterized field has relief");
  }

  // the whole point: a field can be eroded
  {
    gpx::Graph g;
    g.resolution = 64;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *rast = g.add_node("Rasterize");
    gpx::Node *ero = g.add_node("Hydraulic");
    ero->attrs.find("particles")->i = 8;
    g.add_link(noise->id, "out", rast->id, "field");
    g.add_link(rast->id, "output", ero->id, "input");
    g.evaluate();
    gpx::Heightmap *out = out_of(ero);
    CHECK(out && finite_map(*out), "a field graph can be eroded");
    CHECK(ero->error.empty(), "erosion of a rasterized field has no error");
  }

  // buffer -> field, and back again: the round trip must preserve the shape
  {
    gpx::Graph g;
    g.resolution = 64;
    gpx::Node *src = g.add_node("Noise");
    gpx::Node *samp = g.add_node("Sample");
    gpx::Node *rast = g.add_node("Rasterize");
    g.add_link(src->id, "output", samp->id, "input");
    g.add_link(samp->id, "out", rast->id, "field");
    // keep the values as they are so the comparison is meaningful
    rast->attrs.find("post_remap")->b = false;
    g.evaluate();
    gpx::Heightmap *before = out_of(src);
    gpx::Heightmap *after = out_of(rast);
    CHECK(before && after, "round trip evaluated");
    if (before && after) {
      // sampling is bilinear, so allow a small tolerance away from the edges
      float worst = 0.f;
      for (int y = 4; y < 60; ++y)
        for (int x = 4; x < 60; ++x)
          worst = std::max(worst,
                           std::fabs(before->at(x, y) - after->at(x, y)));
      CHECK(worst < 0.02f,
            "buffer -> field -> buffer preserves the terrain");
    }
  }
}

// ------------------------------------------------------- field -> GLSL
// The transpiler is what lets one authored graph drive both the CPU (tests,
// picking, rasterizing) and the GPU (displacement at camera detail). These
// tests pin its structure; agreement with the CPU result is verified against a
// real GL context in the studio.
static void test_field_glsl() {
  std::printf("field graph -> GLSL...\n");
  gpx::Graph g;
  gpx::Node *pos = g.add_node("FieldPosition");
  gpx::Node *noise = g.add_node("FieldNoise");
  gpx::Node *curve = g.add_node("FieldCurve");
  gpx::Node *math = g.add_node("FieldMath");
  gpx::Node *alt = g.add_node("FieldAltitude");
  CHECK(g.add_link(pos->id, "out", noise->id, "position"), "link position");
  CHECK(g.add_link(noise->id, "out", curve->id, "in"), "link curve");
  CHECK(g.add_link(curve->id, "out", math->id, "a"), "link math a");
  CHECK(g.add_link(alt->id, "out", math->id, "b"), "link math b");

  gpx::GlslProgram p = gpx::field_to_glsl(*math, "out", "gpx_terrain");
  CHECK(p.ok, "graph transpiles: " + p.error);
  if (!p.ok) return;
  CHECK(p.node_count == 5, "every node in the chain was emitted");
  CHECK(p.code.find("vec4 gpx_terrain(vec3 P") != std::string::npos,
        "entry function has the expected signature");
  CHECK(p.code.find("gpxf_fbm") != std::string::npos, "noise call emitted");
  CHECK(p.code.find("alt") != std::string::npos, "altitude input reached");
  CHECK(p.code.find("gpxf_hash") != std::string::npos, "prelude included");
  // every generated variable must be declared before it is used
  CHECK(p.code.find("return v_") != std::string::npos, "returns a value");

  // a node feeding two consumers is emitted once and reused, not duplicated
  {
    gpx::Graph g2;
    gpx::Node *n = g2.add_node("FieldNoise");
    gpx::Node *m = g2.add_node("FieldMath");
    g2.add_link(n->id, "out", m->id, "a");
    g2.add_link(n->id, "out", m->id, "b");
    gpx::GlslProgram q = gpx::field_to_glsl(*m, "out");
    CHECK(q.ok, "diamond graph transpiles");
    CHECK(q.node_count == 2, "shared node emitted once, not twice");
    // count calls inside the generated function only — the prelude also
    // contains the definition of gpxf_fbm
    size_t body = q.code.find("vec4 gpx_field(");
    CHECK(body != std::string::npos, "entry function present");
    int calls = 0;
    for (size_t p = q.code.find("gpxf_fbm(", body); p != std::string::npos;
         p = q.code.find("gpxf_fbm(", p + 1))
      ++calls;
    CHECK(calls == 1, "the shared noise is evaluated once, not per consumer");
  }

  // a Sample node must declare the sampler the host has to bind
  {
    gpx::Graph g3;
    g3.resolution = 32;
    gpx::Node *src = g3.add_node("Noise");
    gpx::Node *s = g3.add_node("Sample");
    g3.add_link(src->id, "output", s->id, "input");
    gpx::GlslProgram q = gpx::field_to_glsl(*s, "out");
    CHECK(q.ok, "Sample transpiles");
    CHECK(q.samplers.size() == 1, "declares one sampler");
    if (!q.samplers.empty())
      CHECK(q.code.find("uniform sampler2D " + q.samplers[0]) != std::string::npos,
            "sampler uniform is declared in the source");
  }

  // an unsupported node must fail loudly rather than emit broken code
  {
    gpx::Graph g4;
    g4.resolution = 32;
    gpx::Node *raster = g4.add_node("Noise"); // raster node, no field output
    gpx::GlslProgram q = gpx::field_to_glsl(*raster, "output");
    CHECK(!q.ok, "a raster node is rejected by the transpiler");
    CHECK(!q.error.empty(), "rejection explains itself");
  }
}

// Every generated variable must be declared before it is used. An emitter that
// streams into the body while resolving its own inputs splices declarations
// into the middle of the line it is writing, which produces code that looks
// plausible and does not compile. This catches that without needing a GPU.
static bool glsl_declared_before_use(const std::string &code, std::string &why) {
  size_t body = code.find("vec4 gpx_");
  if (body == std::string::npos) {
    why = "no entry function";
    return false;
  }
  std::vector<std::string> declared;
  size_t line_start = body;
  while (line_start < code.size()) {
    size_t nl = code.find('\n', line_start);
    if (nl == std::string::npos) nl = code.size();
    std::string line = code.substr(line_start, nl - line_start);
    line_start = nl + 1;

    // the name this line declares, if any: "<type> v_name = ..."
    std::string decl;
    for (const char *kw : {"float ", "vec2 ", "vec3 ", "vec4 "}) {
      size_t k = line.find(kw);
      if (k == std::string::npos) continue;
      size_t s = k + std::strlen(kw);
      size_t e = s;
      while (e < line.size() && (std::isalnum((unsigned char)line[e]) || line[e] == '_'))
        ++e;
      if (e > s && line.compare(s, 2, "v_") == 0) decl = line.substr(s, e - s);
      break;
    }
    // every v_ identifier used on this line must already be declared
    for (size_t i = 0; i + 1 < line.size(); ++i) {
      if (line[i] != 'v' || line[i + 1] != '_') continue;
      if (i > 0 && (std::isalnum((unsigned char)line[i - 1]) || line[i - 1] == '_'))
        continue;
      size_t e = i;
      while (e < line.size() && (std::isalnum((unsigned char)line[e]) || line[e] == '_'))
        ++e;
      std::string name = line.substr(i, e - i);
      i = e - 1;
      if (name == decl) continue;
      bool found = false;
      for (const std::string &d : declared)
        if (d == name) { found = true; break; }
      if (!found) {
        why = "'" + name + "' used before it is declared, in: " + line;
        return false;
      }
    }
    if (!decl.empty()) declared.push_back(decl);
  }
  return true;
}

// ------------------------------------------------------- displacement (P1)
// The three nodes that evaluate their input somewhere other than the point
// they were asked about. Their correctness is checkable analytically, which is
// what these tests do rather than eyeballing a picture.
static void test_displacement() {
  std::printf("displacement, redirect and computed normals...\n");
  auto ctx_at = [](float x, float y, float z) {
    gpx::FieldContext c = gpx::FieldContext::at(x, y, z);
    c.lod = 8.f;
    return c;
  };

  // ---- Redirect really moves the evaluation point ------------------------
  {
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *red = g.add_node("FieldRedirect");
    gpx::Node *cst = g.add_node("FieldConstant"); // a constant vector offset
    cst->attrs.find("value")->f = 0.25f;
    g.add_link(noise->id, "out", red->id, "input");
    g.add_link(cst->id, "out", red->id, "redirect");
    // a Number feeding a Vector input broadcasts, so the offset is (.25,.25,.25)
    red->attrs.find("strength")->f = 1.f;

    gpx::FieldValue moved = red->eval_field("out", ctx_at(1.f, 0.f, 2.f));
    gpx::FieldValue direct = noise->eval_field("out", ctx_at(1.25f, 0.25f, 2.25f));
    CHECK(std::fabs(moved.number() - direct.number()) < 1e-6f,
          "redirect evaluates its input at the offset position");

    // zero offset must be exactly a pass-through, or redirect is not safe to
    // leave in a graph while it is being tuned
    cst->attrs.find("value")->f = 0.f;
    gpx::FieldValue same = red->eval_field("out", ctx_at(1.f, 0.f, 2.f));
    gpx::FieldValue plain = noise->eval_field("out", ctx_at(1.f, 0.f, 2.f));
    CHECK(same.number() == plain.number(), "a zero redirect changes nothing");
  }

  // ---- Redirect in 'replace' mode ----------------------------------------
  {
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *red = g.add_node("FieldRedirect");
    gpx::Node *cst = g.add_node("FieldConstant");
    cst->attrs.find("value")->f = 0.5f;
    red->attrs.find("mode")->i = 1; // replace
    g.add_link(noise->id, "out", red->id, "input");
    g.add_link(cst->id, "out", red->id, "redirect");
    // the answer must be the same wherever it is asked from
    float a = red->eval_field("out", ctx_at(1.f, 0.f, 2.f)).number();
    float b = red->eval_field("out", ctx_at(-9.f, 3.f, 40.f)).number();
    CHECK(std::fabs(a - b) < 1e-6f,
          "replace mode ignores the incoming position entirely");
  }

  // ---- Displace: depth, outwards-only, and the two outputs agreeing ------
  {
    gpx::Graph g;
    gpx::Node *cst = g.add_node("FieldConstant");
    gpx::Node *dis = g.add_node("FieldDisplace");
    g.add_link(cst->id, "out", dis->id, "amount");
    dis->attrs.find("dir_mode")->i = 1; // straight up
    dis->attrs.find("depth")->f = 10.f;

    cst->attrs.find("value")->f = 2.f;
    gpx::FieldContext c = ctx_at(0.f, 5.f, 0.f);
    CHECK(std::fabs(dis->eval_field("out", c).number() - 25.f) < 1e-4f,
          "displaced height is altitude + amount * depth");
    float off[3];
    dis->eval_field("offset", c).as_vector(off);
    CHECK(std::fabs(off[1] - 20.f) < 1e-4f, "offset output carries the same move");
    CHECK(std::fabs(off[0]) < 1e-6f && std::fabs(off[2]) < 1e-6f,
          "straight up displaces only in Y");

    // relative depth multiplies by the reference size
    dis->attrs.find("depth_mode")->i = 1;
    dis->attrs.find("relative_size")->f = 0.5f;
    CHECK(std::fabs(dis->eval_field("out", c).number() - 15.f) < 1e-4f,
          "relative depth scales by the reference size");
    dis->attrs.find("depth_mode")->i = 0;

    // outwards only discards the negative half
    cst->attrs.find("value")->f = -2.f;
    CHECK(std::fabs(dis->eval_field("out", c).number() - -15.f) < 1e-4f,
          "negative displacement dents inward by default");
    dis->attrs.find("outwards_only")->b = true;
    CHECK(std::fabs(dis->eval_field("out", c).number() - 5.f) < 1e-4f,
          "outwards-only clamps the dent away, leaving the surface untouched");
  }

  // ---- Displace: the quality boost actually buys detail -------------------
  {
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *dis = g.add_node("FieldDisplace");
    g.add_link(noise->id, "out", dis->id, "amount");
    noise->attrs.find("octaves")->i = 10;
    dis->attrs.find("dir_mode")->i = 1;

    gpx::FieldContext c = ctx_at(0.3f, 0.f, 0.7f);
    c.lod = 2.f; // a distant point: the noise is capped to two octaves
    float plain = dis->eval_field("out", c).number();
    dis->attrs.find("quality")->i = 4;
    float boosted = dis->eval_field("out", c).number();
    CHECK(plain != boosted,
          "quality boost raises the detail budget for this displacement");
  }

  // ---- Displace: smoothing softens, and is a no-op at zero ---------------
  {
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *dis = g.add_node("FieldDisplace");
    g.add_link(noise->id, "out", dis->id, "amount");
    noise->attrs.find("frequency")->f = 40.f; // fine detail, so smoothing shows
    dis->attrs.find("dir_mode")->i = 1;
    gpx::FieldContext c = ctx_at(0.3f, 0.f, 0.7f);

    float sharp = dis->eval_field("out", c).number();
    dis->attrs.find("smoothing")->f = 0.f;
    CHECK(dis->eval_field("out", c).number() == sharp,
          "zero smoothing is exactly a no-op");
    dis->attrs.find("smoothing")->f = 1.f;
    dis->attrs.find("smooth_radius")->f = 0.05f;
    CHECK(dis->eval_field("out", c).number() != sharp,
          "smoothing changes the result once it is switched on");
  }

  // ---- ComputeNormal against an analytic slope ---------------------------
  {
    // A field that is exactly h = 3x has a known normal everywhere, so this
    // checks the arithmetic rather than merely that something came out.
    // TexCoord's first component is the X coordinate, which gives a clean
    // linear ramp; a Vector would read as its length instead.
    gpx::Graph g;
    gpx::Node *tcx = g.add_node("FieldTexCoord");
    gpx::Node *math = g.add_node("FieldMath");
    math->attrs.find("op")->i = 2; // multiply
    math->attrs.find("b_default")->f = 3.f;
    gpx::Node *cn = g.add_node("FieldComputeNormal");
    g.add_link(tcx->id, "out", math->id, "a");
    g.add_link(math->id, "out", cn->id, "height");

    cn->attrs.find("epsilon")->f = 0.01f;
    cn->attrs.find("strength")->f = 1.f;
    gpx::FieldContext c = ctx_at(1.f, 0.f, 1.f);
    float nvec[3];
    cn->eval_field("normal", c).as_vector(nvec);
    // gradient 3 in x, 0 in z  ->  normal proportional to (-3, 1, 0)
    float len = std::sqrt(9.f + 1.f);
    CHECK(std::fabs(nvec[0] - (-3.f / len)) < 1e-3f, "normal X matches the slope");
    CHECK(std::fabs(nvec[1] - (1.f / len)) < 1e-3f, "normal Y matches the slope");
    CHECK(std::fabs(nvec[2]) < 1e-3f, "no gradient in Z gives no Z component");
    float unit = std::sqrt(nvec[0]*nvec[0] + nvec[1]*nvec[1] + nvec[2]*nvec[2]);
    CHECK(std::fabs(unit - 1.f) < 1e-4f, "the normal is unit length");

    // the slope output must describe the same surface as the normal
    float slope = cn->eval_field("slope", c).number();
    CHECK(std::fabs(slope - nvec[1]) < 1e-4f,
          "slope output agrees with the normal it came from");
  }

  // ---- ComputeNormal on flat ground --------------------------------------
  {
    gpx::Graph g;
    gpx::Node *cst = g.add_node("FieldConstant");
    gpx::Node *cn = g.add_node("FieldComputeNormal");
    g.add_link(cst->id, "out", cn->id, "height");
    float nvec[3];
    cn->eval_field("normal", ctx_at(4.f, 0.f, -2.f)).as_vector(nvec);
    CHECK(std::fabs(nvec[0]) < 1e-6f && std::fabs(nvec[1] - 1.f) < 1e-6f &&
              std::fabs(nvec[2]) < 1e-6f,
          "flat ground points straight up");
    CHECK(std::fabs(cn->eval_field("slope", ctx_at(0, 0, 0)).number() - 1.f) < 1e-6f,
          "flat ground has slope 1");
  }

  // ---- Zone confines a field to a region ---------------------------------
  {
    gpx::Graph g;
    gpx::Node *in = g.add_node("FieldConstant");
    gpx::Node *out = g.add_node("FieldConstant");
    gpx::Node *z = g.add_node("FieldZone");
    in->attrs.find("value")->f = 1.f;
    out->attrs.find("value")->f = 0.f;
    g.add_link(in->id, "out", z->id, "inside");
    g.add_link(out->id, "out", z->id, "outside");
    z->attrs.find("size")->f = 10.f;
    z->attrs.find("fade")->f = 0.5f; // inner radius 5

    CHECK(std::fabs(z->eval_field("out", ctx_at(0, 0, 0)).number() - 1.f) < 1e-6f,
          "the centre of the zone is fully inside");
    CHECK(std::fabs(z->eval_field("out", ctx_at(100, 0, 0)).number()) < 1e-6f,
          "far outside the zone the other field wins");
    float edge = z->eval_field("out", ctx_at(7.5f, 0, 0)).number();
    CHECK(edge > 0.f && edge < 1.f, "the fade band blends between the two");
    CHECK(std::fabs(z->eval_field("mask", ctx_at(0, 0, 0)).number() - 1.f) < 1e-6f,
          "the mask output is the region on its own (Vue's Extract)");
    // 'flat' means a column: height must not matter
    float high = z->eval_field("out", ctx_at(0, 1000, 0)).number();
    CHECK(std::fabs(high - 1.f) < 1e-6f, "a flat zone ignores altitude");
  }

  // ---- TexCoord --------------------------------------------------------
  {
    gpx::Graph g;
    gpx::Node *tc = g.add_node("FieldTexCoord");
    gpx::FieldValue v = tc->eval_field("out", ctx_at(2.f, 5.f, 3.f));
    CHECK(v.type == gpx::FieldType::TexCoord, "produces texture coordinates");
    CHECK(std::fabs(v.v[0] - 2.f) < 1e-6f && std::fabs(v.v[1] - 3.f) < 1e-6f,
          "top-down projection reads X and Z");
    tc->attrs.find("plane")->i = 1; // front: X and Y
    v = tc->eval_field("out", ctx_at(2.f, 5.f, 3.f));
    CHECK(std::fabs(v.v[1] - 5.f) < 1e-6f, "front projection reads Y");
  }

  // ---- all five transpile, and the multi-output nodes emit differently ----
  {
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *red = g.add_node("FieldRedirect");
    gpx::Node *dis = g.add_node("FieldDisplace");
    gpx::Node *cn = g.add_node("FieldComputeNormal");
    gpx::Node *z = g.add_node("FieldZone");
    gpx::Node *tc = g.add_node("FieldTexCoord");
    g.add_link(noise->id, "out", red->id, "input");
    g.add_link(red->id, "out", dis->id, "amount");
    g.add_link(dis->id, "out", cn->id, "height");
    g.add_link(cn->id, "slope", z->id, "inside");

    for (gpx::Node *n : {red, dis, cn, z, tc}) {
      gpx::GlslProgram p = gpx::field_to_glsl(*n, "", "gpx_f");
      CHECK(p.ok, std::string(n->type) + " transpiles: " + p.error);
      std::string why;
      CHECK(glsl_declared_before_use(p.code, why),
            std::string(n->type) + " emits valid ordering: " + why);
    }
    // ComputeNormal resolves four samples while writing one line, which is
    // exactly how declarations get spliced into the middle of a statement.
    for (const char *port : {"normal", "slope"}) {
      gpx::GlslProgram p = gpx::field_to_glsl(*cn, port, "gpx_f");
      std::string why;
      CHECK(p.ok && glsl_declared_before_use(p.code, why),
            std::string("ComputeNormal.") + port + " emits valid ordering: " + why);
    }

    // A node's two outputs are different values, not two views of one.
    gpx::GlslProgram a = gpx::field_to_glsl(*cn, "normal", "gpx_f");
    gpx::GlslProgram b = gpx::field_to_glsl(*cn, "slope", "gpx_f");
    CHECK(a.ok && b.ok, "both ComputeNormal outputs transpile");
    CHECK(a.code != b.code, "normal and slope emit different code");
    gpx::GlslProgram c = gpx::field_to_glsl(*dis, "out", "gpx_f");
    gpx::GlslProgram d = gpx::field_to_glsl(*dis, "offset", "gpx_f");
    CHECK(c.ok && d.ok, "both Displace outputs transpile");
    CHECK(c.code != d.code, "height and offset emit different code");
  }

  // ---- type conversion must mean the same thing on both sides ------------
  {
    // FieldValue is deliberately permissive: a number used as a vector
    // broadcasts, a vector used as a number is its length, a colour is its
    // luminance. The GPU has to agree, or a graph silently renders differently
    // there — which is precisely the bug the GPU check caught.
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise"); // Number
    gpx::Node *red = g.add_node("FieldRedirect");
    g.add_link(noise->id, "out", red->id, "redirect"); // Number -> Vector
    gpx::GlslProgram p = gpx::field_to_glsl(*red, "out", "gpx_f");
    CHECK(p.ok, "scalar into a vector input transpiles");
    CHECK(p.code.find(".xyz") == std::string::npos,
          "a scalar source is broadcast, not read as xyz with two zeros");

    // and on the CPU, the same wiring broadcasts
    float v[3];
    gpx::FieldContext c = ctx_at(0.4f, 0.f, 0.6f);
    noise->eval_field("out", c).as_vector(v);
    CHECK(v[0] == v[1] && v[1] == v[2], "a number broadcasts to all three axes");

    // vector into a number input reads as length on both sides
    gpx::Graph g2;
    gpx::Node *pos = g2.add_node("FieldPosition"); // Vector
    gpx::Node *math = g2.add_node("FieldMath");
    g2.add_link(pos->id, "out", math->id, "a"); // Vector -> Number
    gpx::GlslProgram q = gpx::field_to_glsl(*math, "out", "gpx_f");
    CHECK(q.ok, "vector into a number input transpiles");
    CHECK(q.code.find("length(") != std::string::npos,
          "a vector read as a number is its length, matching FieldValue");
  }

  // ---- the scoped cache: a redirect re-emits, a plain graph does not ------
  {
    // Under a redirect the same noise is a different value, so it must be
    // emitted twice; without one it must still be emitted once. This is the
    // property that makes warping correct on the GPU rather than silently
    // reusing the un-redirected value.
    gpx::Graph g;
    gpx::Node *noise = g.add_node("FieldNoise");
    gpx::Node *red = g.add_node("FieldRedirect");
    gpx::Node *math = g.add_node("FieldMath");
    g.add_link(noise->id, "out", red->id, "input");
    g.add_link(noise->id, "out", red->id, "redirect");
    g.add_link(red->id, "out", math->id, "a");
    g.add_link(noise->id, "out", math->id, "b");

    gpx::GlslProgram p = gpx::field_to_glsl(*math, "out", "gpx_f");
    CHECK(p.ok, "redirected graph transpiles: " + p.error);
    size_t body = p.code.find("vec4 gpx_f(");
    int calls = 0;
    for (size_t i = p.code.find("gpxf_fbm(", body); i != std::string::npos;
         i = p.code.find("gpxf_fbm(", i + 1))
      ++calls;
    // once at the original point (shared by the redirect vector and the
    // math input), once again at the redirected point
    CHECK(calls == 2, "the redirected subtree is re-emitted at the new point");
  }
}

// --------------------------------------------------------- analysis (P1)
// Hydrological analysis has answers you can work out on paper, so these tests
// check the arithmetic rather than that a picture appeared.
static void test_analysis() {
  std::printf("flow, wetness and resampling...\n");
  const int W = 32;

  // Park a buffer straight onto a node's input port. The graph prefers a
  // port's own buffer over a link, which is how MetaNodes feed their boundary.
  auto feed = [](gpx::Node *n, const char *port, const gpx::Heightmap &h) {
    gpx::Port *p = n->port(port, gpx::PortDir::In);
    if (p) p->hmap = std::make_shared<gpx::Heightmap>(h);
  };

  // A pure slope in X: every cell's steepest descent is its left neighbour
  // (the diagonals are the same height but further away), so each row drains
  // straight to the left edge and the leftmost cell collects the whole row.
  gpx::Heightmap ramp(W, W);
  for (int y = 0; y < W; ++y)
    for (int x = 0; x < W; ++x) ramp.at(x, y) = x * 0.01f;

  {
    gpx::Graph g;
    g.resolution = W;
    gpx::Node *fa = g.add_node("FlowAccumulation");
    fa->attrs.find("log_scale")->b = false;
    fa->attrs.find("post_remap")->b = false; // raw counts, so we can check them
    feed(fa, "input", ramp);
    fa->dirty = true;
    g.evaluate();

    gpx::Heightmap *out = out_of(fa);
    CHECK(out != nullptr, "flow accumulation produced output");
    if (out) {
      CHECK(std::fabs(out->at(0, W / 2) - (float)W) < 1e-3f,
            "the outlet of a row collects exactly one cell per column");
      CHECK(std::fabs(out->at(W - 1, W / 2) - 1.f) < 1e-3f,
            "the ridge cell contributes only itself");
      // accumulation must rise monotonically downhill along a row
      bool rises = true;
      for (int x = 1; x < W; ++x)
        if (out->at(W - 1 - x, W / 2) < out->at(W - x, W / 2)) rises = false;
      CHECK(rises, "accumulation grows as water moves downhill");
    }
  }

  {   // wetness: finite everywhere, including on ground that is exactly flat
    gpx::Graph g;
    g.resolution = W;
    gpx::Node *wi = g.add_node("WetnessIndex");
    gpx::Heightmap flat(W, W);
    for (float &v : flat.v) v = 0.5f;
    feed(wi, "input", flat);
    wi->dirty = true;
    g.evaluate();
    gpx::Heightmap *out = out_of(wi);
    CHECK(out && finite_map(*out),
          "perfectly flat ground stays finite (no divide by zero)");
  }

  {   // wetness rises downhill, where the contributing area is larger
    gpx::Graph g;
    g.resolution = W;
    gpx::Node *wi = g.add_node("WetnessIndex");
    wi->attrs.find("post_remap")->b = false;
    feed(wi, "input", ramp);
    wi->dirty = true;
    g.evaluate();
    gpx::Heightmap *out = out_of(wi);
    CHECK(out != nullptr, "wetness produced output");
    if (out) {
      CHECK(finite_map(*out), "wetness is finite everywhere");
      CHECK(out->at(1, W / 2) > out->at(W - 2, W / 2),
            "the foot of a slope is wetter than its top");
    }
  }

  {   // resampling coarser must lose detail, not gain it
    gpx::Graph g;
    g.resolution = W;
    gpx::Node *noise = g.add_node("Noise");
    noise->attrs.find("octaves")->i = 8;
    gpx::Node *rs = g.add_node("Resample");
    rs->attrs.find("post_remap")->b = false;
    g.add_link(noise->id, "output", rs->id, "input");
    g.mark_all_dirty();
    g.evaluate();

    gpx::Heightmap *src = out_of(noise);
    gpx::Heightmap *out = out_of(rs);
    CHECK(src && out, "resample produced output");
    if (src && out) {
      // roughness as mean absolute difference between neighbours
      auto rough = [](const gpx::Heightmap &h) {
        double s = 0;
        int n = 0;
        for (int y = 0; y < h.h; ++y)
          for (int x = 1; x < h.w; ++x) {
            s += std::fabs(h.at(x, y) - h.at(x - 1, y));
            ++n;
          }
        return n ? s / n : 0.0;
      };
      CHECK(rough(*out) < rough(*src),
            "half sampling is smoother than the source it came from");
      CHECK(finite_map(*out), "resampled terrain is finite");
    }
  }
}

// -------------------------------------------------- field materials (P2)
// Environment-sensitive distribution: where a material belongs. The point of
// these tests is that the answers are the ones you would give out loud —
// "snow above the treeline", "rock on the steep bits" — not merely that a
// number came out.
static void test_field_materials() {
  std::printf("environment-sensitive materials...\n");
  auto at_alt = [](float a, float slope = 1.f, float orient = 0.f) {
    gpx::FieldContext c;
    c.altitude = a;
    c.slope = slope;
    c.orientation = orient;
    c.lod = 8.f;
    return c;
  };

  {   // snow above a line, with a soft edge
    gpx::Graph g;
    gpx::Node *d = g.add_node("FieldDistribution");
    d->attrs.find("altitude")->v2[0] = 100.f; // band 100..1000
    d->attrs.find("altitude")->v2[1] = 1000.f;
    d->attrs.find("altitude_fuzz")->f = 10.f;

    CHECK(d->eval_field("out", at_alt(500.f)).number() == 1.f,
          "well inside the band the material is fully present");
    CHECK(d->eval_field("out", at_alt(0.f)).number() == 0.f,
          "well below the band it is absent");
    CHECK(d->eval_field("out", at_alt(5000.f)).number() == 0.f,
          "well above the band it is absent");
    float edge = d->eval_field("out", at_alt(100.f)).number();
    CHECK(edge > 0.f && edge < 1.f, "the band edge is a fade, not a step");
    CHECK(std::fabs(edge - 0.5f) < 1e-4f,
          "exactly on the boundary it is half present");

    // and the fade is monotonic through the transition, or it would band
    bool rises = true;
    float prev = -1.f;
    for (int i = 0; i <= 20; ++i) {
      float v = d->eval_field("out", at_alt(85.f + i * 1.5f)).number();
      if (v < prev - 1e-6f) rises = false;
      prev = v;
    }
    CHECK(rises, "the fade rises monotonically into the band");
  }

  {   // a zero fade is a hard edge, and must not divide by zero
    gpx::Graph g;
    gpx::Node *d = g.add_node("FieldDistribution");
    d->attrs.find("altitude")->v2[0] = 0.f;
    d->attrs.find("altitude")->v2[1] = 10.f;
    d->attrs.find("altitude_fuzz")->f = 0.f;
    CHECK(d->eval_field("out", at_alt(5.f)).number() == 1.f, "inside is 1");
    CHECK(d->eval_field("out", at_alt(10.001f)).number() == 0.f,
          "just outside is 0 with no fade");
    CHECK(std::isfinite(d->eval_field("out", at_alt(0.f)).number()),
          "a zero fade stays finite (smoothstep with equal edges would not)");
  }

  {   // criteria multiply: rock is steep AND low
    gpx::Graph g;
    gpx::Node *d = g.add_node("FieldDistribution");
    d->attrs.find("altitude")->v2[0] = 0.f;
    d->attrs.find("altitude")->v2[1] = 100.f;
    d->attrs.find("altitude_fuzz")->f = 0.f;
    d->attrs.find("use_slope")->b = true;
    d->attrs.find("slope")->v2[0] = 0.f;   // steep only
    d->attrs.find("slope")->v2[1] = 0.4f;
    d->attrs.find("slope_fuzz")->f = 0.f;

    CHECK(d->eval_field("out", at_alt(50.f, 0.2f)).number() == 1.f,
          "low and steep: the material belongs");
    CHECK(d->eval_field("out", at_alt(50.f, 0.9f)).number() == 0.f,
          "low but flat: rejected by the steepness criterion alone");
    CHECK(d->eval_field("out", at_alt(500.f, 0.2f)).number() == 0.f,
          "steep but high: rejected by the altitude criterion alone");

    d->attrs.find("invert")->b = true;
    CHECK(d->eval_field("out", at_alt(50.f, 0.2f)).number() == 0.f,
          "invert flips the result");
  }

  {   // a criterion can be driven by a field, which is how it reads a
      // computed slope from downstream of a displacement
    gpx::Graph g;
    gpx::Node *cst = g.add_node("FieldConstant");
    cst->attrs.find("value")->f = 700.f;
    gpx::Node *d = g.add_node("FieldDistribution");
    d->attrs.find("altitude")->v2[0] = 500.f;
    d->attrs.find("altitude")->v2[1] = 900.f;
    d->attrs.find("altitude_fuzz")->f = 0.f;
    g.add_link(cst->id, "out", d->id, "altitude");
    // the context says 0, the connected field says 700: the field must win
    CHECK(d->eval_field("out", at_alt(0.f)).number() == 1.f,
          "a connected field overrides the context value");
  }

  {   // colour mixing
    gpx::Graph g;
    gpx::Node *ca = g.add_node("FieldColorConstant");
    gpx::Node *cb = g.add_node("FieldColorConstant");
    gpx::Node *mx = g.add_node("FieldColorMix");
    ca->attrs.find("color")->col[0] = 1.f;
    ca->attrs.find("color")->col[1] = 0.f;
    ca->attrs.find("color")->col[2] = 0.f;
    cb->attrs.find("color")->col[0] = 0.f;
    cb->attrs.find("color")->col[1] = 1.f;
    cb->attrs.find("color")->col[2] = 0.f;
    g.add_link(ca->id, "out", mx->id, "a");
    g.add_link(cb->id, "out", mx->id, "b");
    mx->attrs.find("amount")->f = 1.f;

    gpx::FieldContext c = at_alt(0.f);
    gpx::FieldValue v = mx->eval_field("out", c);
    CHECK(v.type == gpx::FieldType::Color, "produces a colour");
    CHECK(std::fabs(v.v[0]) < 1e-6f && std::fabs(v.v[1] - 1.f) < 1e-6f,
          "a full mix is entirely B");
    mx->attrs.find("amount")->f = 0.f;
    v = mx->eval_field("out", c);
    CHECK(std::fabs(v.v[0] - 1.f) < 1e-6f,
          "a zero factor leaves A untouched, whatever the mode");
    // and that holds for every mode, which is what makes the factor mean one
    // thing rather than seven
    for (int mode = 0; mode < 7; ++mode) {
      mx->attrs.find("mode")->i = mode;
      v = mx->eval_field("out", c);
      CHECK(std::fabs(v.v[0] - 1.f) < 1e-6f && std::fabs(v.v[1]) < 1e-6f,
            "mode " + std::to_string(mode) + " is a no-op at factor zero");
    }
    mx->attrs.find("mode")->i = 2; // multiply
    mx->attrs.find("amount")->f = 1.f;
    v = mx->eval_field("out", c);
    CHECK(std::fabs(v.v[0]) < 1e-6f && std::fabs(v.v[1]) < 1e-6f,
          "red times green is black");
  }

  {   // Both transpile, and the emitted code is well-formed.
      //
      // Every input is deliberately connected. An unconnected input resolves
      // to a literal without emitting anything, so a bare node cannot exhibit
      // the ordering bug at all — testing one would pass while the shader the
      // user actually gets does not compile. That is exactly what happened.
    gpx::Graph g;
    gpx::Node *n1 = g.add_node("FieldNoise");
    gpx::Node *n2 = g.add_node("FieldNoise");
    n2->attrs.find("seed")->seed = 5;

    gpx::Node *d = g.add_node("FieldDistribution");
    d->attrs.find("use_slope")->b = true;
    d->attrs.find("use_orientation")->b = true;
    g.add_link(n1->id, "out", d->id, "altitude");
    g.add_link(n2->id, "out", d->id, "slope");
    g.add_link(n1->id, "out", d->id, "orientation");

    gpx::Node *ga = g.add_node("FieldGradient");
    gpx::Node *cc = g.add_node("FieldColorConstant");
    gpx::Node *mx = g.add_node("FieldColorMix");
    g.add_link(n1->id, "out", ga->id, "in");
    g.add_link(ga->id, "out", mx->id, "a");
    g.add_link(cc->id, "out", mx->id, "b");
    g.add_link(n2->id, "out", mx->id, "factor");

    // First prove the checker can fail, or passing it means nothing. This is
    // the shape the bug produced: a declaration spliced into the middle of
    // the statement that was being written.
    {
      std::string why;
      CHECK(!glsl_declared_before_use(
                "vec4 gpx_f(){\n  vec4 v_ca_0 =   vec4 v_n_1 = P.x;\n"
                "v_n_1;\n  return v_ca_0;\n}\n",
                why),
            "the ordering checker rejects a spliced declaration");
      CHECK(glsl_declared_before_use(
                "vec4 gpx_f(){\n  vec4 v_n_1 = vec4(P, 0.0);\n"
                "  vec4 v_ca_0 = v_n_1;\n  return v_ca_0;\n}\n",
                why),
            "and accepts the same code written in order");
    }

    for (gpx::Node *n : {d, mx}) {
      gpx::GlslProgram p = gpx::field_to_glsl(*n, "out", "gpx_f");
      CHECK(p.ok, std::string(n->type) + " transpiles: " + p.error);
      std::string why;
      CHECK(glsl_declared_before_use(p.code, why),
            std::string(n->type) + " emits valid ordering: " + why);
    }
    gpx::GlslProgram p = gpx::field_to_glsl(*d, "out", "gpx_f");
    CHECK(p.code.find("gpxf_band") != std::string::npos,
          "distribution uses the shared band helper, not smoothstep");

    // a colour source must keep its alpha rather than having 1.0 forced on it
    gpx::GlslProgram q = gpx::field_to_glsl(*mx, "out", "gpx_f");
    CHECK(q.ok && q.code.find("vec4(vec3(") == std::string::npos,
          "a colour input is taken whole, not rebuilt from a luminance");
  }
}

// ------------------------------------------------------------------ bypass
// "The network is processed as if the node did not even exist" (Terragen p15).
// The graph implements this during link resolution, so it holds for every node
// in both domains without per-node code.
static void test_bypass() {
  std::printf("node bypass...\n");
  // a run of bypassed nodes is walked through, not just one
  {
    gpx::Graph g;
    g.resolution = 48;
    gpx::Node *src = g.add_node("Noise");
    gpx::Node *a = g.add_node("Smooth");
    gpx::Node *b = g.add_node("Terrace");
    gpx::Node *c = g.add_node("Plateau");
    gpx::Node *sink = g.add_node("Thru");
    g.add_link(src->id, "output", a->id, "input");
    g.add_link(a->id, "output", b->id, "input");
    g.add_link(b->id, "output", c->id, "input");
    g.add_link(c->id, "output", sink->id, "input");
    g.evaluate();
    std::vector<float> full = out_of(sink)->v;

    a->enabled = b->enabled = c->enabled = false;
    g.mark_all_dirty();
    g.evaluate();
    const gpx::Heightmap *seen = sink->in_hmap("input");
    CHECK(seen != nullptr, "three bypassed nodes still resolve");
    CHECK(seen && seen->v == out_of(src)->v,
          "a chain of bypassed nodes reads straight back to the source");
    CHECK(seen && seen->v != full, "bypassing actually changed the result");

    // and re-enabling restores exactly what was there before
    a->enabled = b->enabled = c->enabled = true;
    g.mark_all_dirty();
    g.evaluate();
    CHECK(out_of(sink)->v == full, "re-enabling restores the original result");
  }

  // a bypassed node with nothing feeding it yields nothing, rather than
  // silently handing on stale output from before it was disabled
  {
    gpx::Graph g;
    g.resolution = 32;
    gpx::Node *sm = g.add_node("Smooth");
    gpx::Node *sink = g.add_node("Thru");
    g.add_link(sm->id, "output", sink->id, "input");
    sm->enabled = false;
    g.mark_all_dirty();
    g.evaluate();
    CHECK(sink->in_hmap("input") == nullptr,
          "a bypassed node with no input resolves to nothing");
  }

  // the field domain resolves through bypasses too
  {
    gpx::Graph g;
    gpx::Node *c = g.add_node("FieldConstant");
    gpx::Node *curve = g.add_node("FieldCurve");
    gpx::Node *math = g.add_node("FieldMath");
    c->attrs.find("value")->f = 0.25f;
    curve->attrs.find("shape")->i = 4; // invert: 1 - x
    g.add_link(c->id, "out", curve->id, "in");
    g.add_link(curve->id, "out", math->id, "a");
    math->attrs.find("op")->i = 0;              // add
    math->attrs.find("b_default")->f = 0.f;
    gpx::FieldContext ctx;
    CHECK(std::fabs(math->eval_field("out", ctx).number() - 0.75f) < 1e-6f,
          "field chain evaluates through the curve");
    curve->enabled = false;
    CHECK(std::fabs(math->eval_field("out", ctx).number() - 0.25f) < 1e-6f,
          "bypassing a field node reads through to its input");
  }

  // bypass survives save/load, and older files without the flag load enabled
  {
    gpx::Graph g;
    g.resolution = 32;
    gpx::Node *n1 = g.add_node("Noise");
    gpx::Node *sm = g.add_node("Smooth");
    g.add_link(n1->id, "output", sm->id, "input");
    sm->enabled = false;
    std::string json = gpx::graph_to_json(g);
    CHECK(json.find("\"enabled\"") != std::string::npos,
          "a bypassed node records the flag");
    gpx::Graph g2;
    std::string err;
    CHECK(gpx::graph_from_json(g2, json, err), "reloads");
    gpx::Node *sm2 = nullptr;
    for (auto &n : g2.nodes)
      if (n->type == "Smooth") sm2 = n.get();
    CHECK(sm2 && !sm2->enabled, "bypass survives the round trip");

    // a project written before bypass existed has no flag and must load enabled
    gpx::Graph g3;
    std::string legacy =
        R"({"resolution":32,"nodes":[{"id":1,"type":"Noise","pos":[0,0],)"
        R"("attrs":{}}],"links":[]})";
    CHECK(gpx::graph_from_json(g3, legacy, err), "legacy project loads");
    CHECK(!g3.nodes.empty() && g3.nodes[0]->enabled,
          "a node with no recorded flag defaults to enabled");
  }
}

// --------------------------------------------------------------- MetaNodes
// The guarantee that makes grouping safe: collapsing part of a graph must not
// change what it computes, and expanding it again must give back exactly what
// was there.
static void test_metanodes() {
  std::printf("MetaNodes...\n");
  auto build = [](gpx::Graph &g, uint64_t ids[3]) {
    g.resolution = 48;
    gpx::Node *src = g.add_node("Noise", 0, 0);
    gpx::Node *a = g.add_node("Smooth", 200, 0);
    gpx::Node *b = g.add_node("Terrace", 400, 0);
    gpx::Node *sink = g.add_node("Thru", 600, 0);
    g.add_link(src->id, "output", a->id, "input");
    g.add_link(a->id, "output", b->id, "input");
    g.add_link(b->id, "output", sink->id, "input");
    ids[0] = src->id;
    ids[1] = a->id;
    ids[2] = b->id;
    return sink;
  };

  gpx::Graph g;
  uint64_t ids[3];
  gpx::Node *sink = build(g, ids);
  g.evaluate();
  std::vector<float> before = out_of(sink)->v;
  CHECK(!before.empty(), "reference graph evaluated");

  // collapse the two filters into a MetaNode
  std::string err;
  gpx::Node *meta = gpx::metanode_group(g, {ids[1], ids[2]}, err);
  CHECK(meta != nullptr, "grouping succeeded: " + err);
  if (!meta) return;
  CHECK(meta->type == "MetaNode", "a MetaNode was created");
  CHECK(g.find_node(ids[1]) == nullptr, "inner nodes left the outer graph");
  int ins = 0, outs = 0;
  for (const gpx::Port &p : meta->ports)
    (p.dir == gpx::PortDir::In ? ins : outs)++;
  CHECK(ins == 1, "one boundary input");
  CHECK(outs == 1, "one boundary output");

  g.mark_all_dirty();
  g.evaluate();
  gpx::Heightmap *after = sink->in_hmap("input")
                              ? const_cast<gpx::Heightmap *>(sink->in_hmap("input"))
                              : nullptr;
  CHECK(after != nullptr, "the MetaNode feeds the sink");
  if (after)
    CHECK(after->v == before,
          "a MetaNode computes exactly what the nodes it replaced computed");

  // and expanding it restores the original graph and result
  std::vector<uint64_t> restored = gpx::metanode_ungroup(g, meta->id, err);
  CHECK(restored.size() == 2, "ungrouping restored both nodes: " + err);
  g.mark_all_dirty();
  g.evaluate();
  CHECK(out_of(sink)->v == before, "ungrouping restores the original result");
  bool has_smooth = false, has_terrace = false;
  for (const auto &n : g.nodes) {
    if (n->type == "Smooth") has_smooth = true;
    if (n->type == "Terrace") has_terrace = true;
  }
  CHECK(has_smooth && has_terrace, "the inner node types came back");
  for (const auto &n : g.nodes)
    CHECK(n->type != "MetaNode", "the MetaNode itself is gone");
}

// Publishing lifts an inner parameter onto the MetaNode, so a group can expose
// a small interface instead of everything it contains.
static void test_metanode_published() {
  std::printf("MetaNode published parameters...\n");
  gpx::Graph g;
  g.resolution = 48;
  gpx::Node *src = g.add_node("Noise", 0, 0);
  gpx::Node *ter = g.add_node("Terrace", 200, 0);
  gpx::Node *sink = g.add_node("Thru", 400, 0);
  g.add_link(src->id, "output", ter->id, "input");
  g.add_link(ter->id, "output", sink->id, "input");
  uint64_t inner_id = ter->id;

  std::string err;
  gpx::Node *meta = gpx::metanode_group(g, {inner_id}, err);
  CHECK(meta != nullptr, "grouped: " + err);
  if (!meta) return;

  CHECK(gpx::metanode_publish(*meta, inner_id, "levels", "Step count"),
        "published an inner parameter");
  CHECK(!gpx::metanode_publish(*meta, inner_id, "levels", "again"),
        "publishing the same parameter twice is refused");
  auto pubs = gpx::metanode_published(*meta);
  CHECK(pubs.size() == 1, "one published parameter recorded");
  CHECK(pubs[0].label == "Step count", "the published label is kept");

  // the mirrored attribute exists on the MetaNode with the inner type/range
  std::string mirror = "pub_" + std::to_string(inner_id) + "_levels";
  gpx::Attribute *m = meta->attrs.find(mirror);
  CHECK(m != nullptr, "a real widget is mirrored onto the MetaNode");
  if (!m) return;
  CHECK(m->type == gpx::AttrType::Int, "the mirror keeps the inner type");
  CHECK(m->label == "Step count", "the mirror uses the published label");

  // changing the published value must change what the MetaNode computes
  g.mark_all_dirty();
  g.evaluate();
  std::vector<float> at_default = out_of(sink)->v;
  m->i = m->imin + 1;
  g.mark_dirty(meta->id);
  g.evaluate();
  CHECK(out_of(sink)->v != at_default,
        "editing a published parameter changes the MetaNode's result");

  // and it survives a save/load round trip with the graph
  std::string json = gpx::graph_to_json(g);
  gpx::Graph g2;
  CHECK(gpx::graph_from_json(g2, json, err), "graph with a MetaNode reloads");
  gpx::Node *meta2 = nullptr;
  for (auto &n : g2.nodes)
    if (n->type == "MetaNode") meta2 = n.get();
  CHECK(meta2 != nullptr, "the MetaNode survived");
  if (meta2) {
    CHECK(gpx::metanode_published(*meta2).size() == 1,
          "published parameters survived the round trip");
    gpx::Graph inner;
    CHECK(gpx::metanode_open(*meta2, inner, err),
          "the inner graph survived: " + err);
    CHECK(!inner.nodes.empty(), "inner graph has its nodes");
  }

  CHECK(gpx::metanode_unpublish(*meta, inner_id, "levels"), "unpublished");
  CHECK(meta->attrs.find(mirror) == nullptr, "the mirror widget was removed");
}

// ------------------------------------------------------------ universal blend
// Terragen puts blend controls on most nodes: any node that transforms a
// heightmap can have its effect confined. The graph provides it, so it works on
// every such node without the node author doing anything.
static void test_universal_blend() {
  std::printf("universal blend...\n");
  // the port exists on filters, and not on nodes that already have their own
  {
    gpx::Graph g;
    gpx::Node *ter = g.add_node("Terrace");
    bool has_blend = false, has_mask = false;
    for (const gpx::Port &p : ter->ports)
      if (p.dir == gpx::PortDir::In) {
        if (p.name == "blend") has_blend = true;
        if (p.name == "mask") has_mask = true;
      }
    CHECK(has_mask, "Terrace has its own mask input");
    CHECK(!has_blend, "a node with its own mask does not get a duplicate blend");

    gpx::Node *plateau = g.add_node("WarpNoise");
    bool p_blend = false;
    for (const gpx::Port &p : plateau->ports)
      if (p.dir == gpx::PortDir::In && p.name == "blend") p_blend = true;
    CHECK(p_blend, "a filter without its own mask gains a blend input");
    CHECK(plateau->attrs.find("blend_invert") != nullptr,
          "and the matching invert control");
  }

  // an unconnected blend changes nothing at all
  {
    gpx::Graph g;
    g.resolution = 48;
    gpx::Node *src = g.add_node("Noise");
    gpx::Node *f = g.add_node("WarpNoise");
    g.add_link(src->id, "output", f->id, "input");
    g.evaluate();
    std::vector<float> unblended = out_of(f)->v;
    CHECK(!unblended.empty(), "filter evaluated");
    g.mark_all_dirty();
    g.evaluate();
    CHECK(out_of(f)->v == unblended,
          "an unused blend port leaves the result untouched");
  }

  // with a mask connected, the effect is confined to it
  {
    gpx::Graph g;
    g.resolution = 48;
    gpx::Node *src = g.add_node("Noise");
    gpx::Node *f = g.add_node("WarpNoise");
    gpx::Node *maskgen = g.add_node("Shape"); // a smooth spatial gradient
    g.add_link(src->id, "output", f->id, "input");
    g.evaluate();
    std::vector<float> full = out_of(f)->v;
    std::vector<float> input = out_of(src)->v;

    g.add_link(maskgen->id, "output", f->id, "blend");
    g.mark_all_dirty();
    g.evaluate();
    std::vector<float> blended = out_of(f)->v;
    CHECK(blended.size() == full.size(), "blended result is the same size");
    CHECK(blended != full, "connecting a blend mask changed the result");

    // every blended sample must lie between the untouched input and the full
    // effect — that is what "confined" means
    bool between = true;
    int moved = 0;
    for (size_t i = 0; i < blended.size(); ++i) {
      float lo = std::min(input[i], full[i]), hi = std::max(input[i], full[i]);
      if (blended[i] < lo - 1e-4f || blended[i] > hi + 1e-4f) between = false;
      if (std::fabs(blended[i] - input[i]) > 1e-5f) ++moved;
    }
    CHECK(between, "blending stays between the input and the full effect");
    CHECK(moved > 0, "the effect still applies somewhere");
    CHECK(moved < (int)blended.size(), "and is held back somewhere else");

    gpx::Attribute *inv = f->attrs.find("blend_invert");
    CHECK(inv != nullptr, "the invert control exists");
    if (!inv) return;
    inv->b = true;
    g.mark_all_dirty();
    g.evaluate();
    CHECK(out_of(f)->v != blended, "inverting the blend changes where it lands");
  }
}

// --------------------------------------------------------------- animation
// The timeline is P7, but the hook has to exist on every parameter now:
// retrofitting it after terrain, materials, lighting, atmosphere, clouds and
// render would mean touching every node a second time.
static void test_animation_hooks() {
  std::printf("animation hooks...\n");
  // track behaviour
  {
    gpx::Track t;
    CHECK(t.empty(), "a new track is empty");
    CHECK(t.sample(0.f) == 0.f, "an empty track samples to zero");
    CHECK(t.set_key(0.f, 10.f), "first key added");
    CHECK(t.sample(5.f) == 10.f, "one key holds its value everywhere");
    CHECK(t.set_key(2.f, 20.f), "second key added");
    CHECK(!t.set_key(2.f, 30.f), "re-keying the same time replaces, not adds");
    CHECK(t.keys.size() == 2, "still two keys");
    // ends hold rather than extrapolate
    CHECK(t.sample(-5.f) == 10.f, "before the first key holds");
    CHECK(t.sample(99.f) == 30.f, "after the last key holds");
    t.interp = gpx::Interp::Linear;
    CHECK(std::fabs(t.sample(1.f) - 20.f) < 1e-4f, "linear interpolates");
    t.interp = gpx::Interp::Constant;
    CHECK(t.sample(1.f) == 10.f, "constant steps");
    t.interp = gpx::Interp::Smooth;
    float mid = t.sample(1.f);
    CHECK(mid > 10.f && mid < 30.f, "smooth stays between its keys");
    CHECK(t.has_key_at(2.f), "key lookup works");
    CHECK(t.remove_key(2.f), "key removed");
    CHECK(t.keys.size() == 1, "one key left");

    // keys added out of order are kept sorted, so sampling stays correct
    gpx::Track u;
    u.set_key(3.f, 30.f);
    u.set_key(1.f, 10.f);
    u.set_key(2.f, 20.f);
    u.interp = gpx::Interp::Linear;
    CHECK(u.keys[0].time == 1.f && u.keys[2].time == 3.f, "keys sorted");
    CHECK(std::fabs(u.sample(1.5f) - 15.f) < 1e-4f, "sampling out-of-order keys");
  }

  // an animated attribute drives the graph as scene time moves
  {
    gpx::Graph g;
    g.resolution = 48;
    gpx::Node *noise = g.add_node("Noise");
    gpx::Attribute *oct = noise->attrs.find("octaves");
    CHECK(oct != nullptr, "the node has the attribute we animate");
    if (!oct) return;
    oct->anim.interp = gpx::Interp::Linear;
    oct->anim.set_key(0.f, 3.f);
    oct->anim.set_key(10.f, 9.f);
    CHECK(oct->animated(), "the attribute reports itself as animated");

    g.time = 0.f;
    g.mark_all_dirty();
    g.evaluate();
    CHECK(oct->i == 3, "at t=0 the animated value is applied");
    std::vector<float> at0 = out_of(noise)->v;

    g.time = 10.f;
    g.evaluate();
    CHECK(oct->i == 9, "at t=10 the value has moved");
    CHECK(out_of(noise)->v != at0, "and the terrain changed with it");

    // scrubbing back reproduces the earlier frame exactly — an animation that
    // is not repeatable is not an animation
    g.time = 0.f;
    g.evaluate();
    CHECK(out_of(noise)->v == at0, "scrubbing back reproduces the frame");
  }

  // an un-animated graph is untouched by time, so nothing pays for this
  {
    gpx::Graph g;
    g.resolution = 48;
    gpx::Node *n = g.add_node("Noise");
    g.evaluate();
    std::vector<float> a = out_of(n)->v;
    g.time = 42.f;
    g.evaluate();
    CHECK(out_of(n)->v == a, "time does not disturb an un-animated graph");
  }

  // tracks survive save/load with the value they belong to
  {
    gpx::Graph g;
    g.resolution = 32;
    gpx::Node *n = g.add_node("Noise");
    gpx::Attribute *oct = n->attrs.find("octaves");
    oct->anim.interp = gpx::Interp::Constant;
    oct->anim.set_key(1.f, 4.f);
    oct->anim.set_key(4.f, 8.f);
    std::string json = gpx::graph_to_json(g);
    CHECK(json.find("anim") != std::string::npos, "the track is written out");
    gpx::Graph g2;
    std::string err;
    CHECK(gpx::graph_from_json(g2, json, err), "reloads");
    gpx::Attribute *o2 = g2.nodes.empty() ? nullptr
                                          : g2.nodes[0]->attrs.find("octaves");
    CHECK(o2 && o2->anim.keys.size() == 2, "both keys came back");
    if (o2) {
      CHECK(o2->anim.interp == gpx::Interp::Constant, "interpolation kept");
      CHECK(std::fabs(o2->anim.keys[1].value - 8.f) < 1e-4f, "values kept");
    }
  }

  // the field domain sees scene time, which is how an animated field graph
  // rasterizes differently per frame
  {
    gpx::Graph g;
    g.resolution = 32;
    gpx::Node *tnode = g.add_node("FieldTime");
    gpx::Node *rast = g.add_node("Rasterize");
    rast->attrs.find("post_remap")->b = false;
    g.add_link(tnode->id, "out", rast->id, "field");
    g.time = 2.f;
    g.mark_all_dirty();
    g.evaluate();
    gpx::Heightmap *out = out_of(rast);
    CHECK(out && std::fabs(out->v[0] - 2.f) < 1e-4f,
          "scene time reaches the field domain");
    g.time = 7.f;
    g.mark_all_dirty();
    g.evaluate();
    CHECK(out_of(rast) && std::fabs(out_of(rast)->v[0] - 7.f) < 1e-4f,
          "and follows the timeline");
  }
}

int main() {
  // unbuffered: if a test crashes, the last line printed tells us where
  std::setvbuf(stdout, nullptr, _IONBF, 0);
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
  test_terrain_effects();
  test_sculpt_layer();
  test_planet_math();
  test_field_domain();
  test_field_bridges();
  test_field_glsl();
  test_displacement();
  test_analysis();
  test_field_materials();
  test_bypass();
  test_metanodes();
  test_metanode_published();
  test_universal_blend();
  test_animation_hooks();
  if (g_failures == 0) {
    std::printf("ALL ENGINE TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}








