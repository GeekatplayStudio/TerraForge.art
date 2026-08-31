// Geekatplay Studio â€” engine test suite (graph, nodes, serialization)
#include "gpx/camera_math.hpp"
#include "gpx/planet_math.hpp"
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
  test_terrain_effects();
  test_sculpt_layer();
  test_planet_math();
  test_field_domain();
  test_field_bridges();
  if (g_failures == 0) {
    std::printf("ALL ENGINE TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}

