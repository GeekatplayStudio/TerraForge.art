// Geekatplay Studio â€” engine test suite (graph, nodes, serialization)
#include "gpx/camera_math.hpp"
#include "gpx/hydrology.hpp"
#include "gpx/planet_math.hpp"
#include "gpx/field_glsl.hpp"
#include "gpx/metanode.hpp"
#include <vector>
#include "gpx/node_graph.hpp"
#include "gpx/parallel.hpp"
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
// AGENTS.md engine rule 1 says "every run, on every thread count". Nothing
// tested the second half, and it was false: the droplet solver dealt particles
// out as per_round/T, seeded its RNG from the worker id, and let each worker
// read its own accumulator, so the partition decided which particles existed,
// where they started, and what they saw. Measured at 256 with one seed,
// workers 1/2/3/4/8 produced five different terrains.
//
// The suite could not see it because it runs at whatever the machine has.
// gpx::set_worker_count() exists so this test can vary it.
static void test_thread_count_determinism() {
  std::printf("thread-count determinism...\n");
  // Every solver that splits work across workers. A per-cell filter cannot be
  // partition-dependent; these accumulate, which is where the hazard lives.
  struct Case { const char *type; const char *note; };
  const Case cases[] = {
      {"Hydraulic", "droplet/pipe hydraulic erosion"},
      {"Thermal", "thermal talus"},
      {"StreamPower", "stream power incision"},
      {"SedimentDeposit", "sediment deposition"},
      {"Wind", "aeolian transport"},
  };
  const unsigned counts[] = {1, 2, 3, 5, 8};

  for (const Case &c : cases) {
    std::vector<float> reference;
    bool have_ref = false;
    for (unsigned T : counts) {
      gpx::set_worker_count(T);
      gpx::Graph g;
      g.resolution = 128;
      gpx::Node *src = g.add_node("Noise");
      if (!src) continue;
      src->attrs.find("seed")->seed = 4242;
      gpx::Node *n = g.add_node(c.type);
      if (!n) break;
      g.add_link(src->id, "output", n->id, "input");
      if (gpx::Attribute *s = n->attrs.find("seed")) s->seed = 99;
      CHECK(g.evaluate(), std::string(c.type) + " evaluates");
      gpx::Port *out = n->port("output", gpx::PortDir::Out);
      if (!out || !out->hmap) break;
      if (!have_ref) {
        reference = out->hmap->v;
        have_ref = true;
      } else {
        CHECK(out->hmap->v == reference,
              std::string(c.note) + " differs at " + std::to_string(T) +
                  " workers - its result depends on the machine's core count");
      }
    }
  }
  gpx::set_worker_count(0); // back to the default for the rest of the suite
}

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

  // Colour and gradient used to be silently ignored, so the assistant, the
  // Python API and MCP could all ask for a colour on a material node and be
  // quietly refused. Nothing failed; the colour simply stayed as it was.
  {
    const char *cspec = R"({
      "nodes": [
        {"id":"c","type":"FieldColorConstant","attrs":{"color":[0.2,0.4,0.6]}},
        {"id":"k","type":"FieldColorConstant","attrs":{"color":0.25}},
        {"id":"g","type":"FieldGradient",
         "attrs":{"gradient":[[1.0,1,0,0],[0.0,0,0,1]]}}
      ], "links": []
    })";
    gpx::Graph gc;
    gc.resolution = 32;
    std::string e2;
    CHECK(gpx::graph_from_ai_spec(gc, cspec, e2, nullptr), "colour spec builds");
    std::vector<gpx::Node *> cols;
    gpx::Node *gr = nullptr;
    for (auto &n : gc.nodes) {
      if (n->type == "FieldColorConstant") cols.push_back(n.get());
      if (n->type == "FieldGradient") gr = n.get();
    }
    CHECK(cols.size() == 2 && gr != nullptr, "colour nodes built");
    if (cols.size() == 2) {
      const gpx::Attribute *a = cols[0]->attrs.find("color");
      const gpx::Attribute *b = cols[1]->attrs.find("color");
      CHECK(a && std::fabs(a->col[0] - 0.2f) < 1e-6f &&
                std::fabs(a->col[2] - 0.6f) < 1e-6f,
            "an rgb triple sets a colour attribute");
      CHECK(a && a->col[3] == 1.f, "an rgb triple is opaque");
      CHECK(b && std::fabs(b->col[1] - 0.25f) < 1e-6f,
            "a single number is taken as a grey");
    }
    if (gr) {
      const gpx::Attribute *ga = gr->attrs.find("gradient");
      CHECK(ga && ga->stops.size() == 2, "a gradient's stops are set");
      CHECK(ga && !ga->stops.empty() && ga->stops[0].t == 0.f,
            "stops given out of order are sorted, or the ramp draws nonsense");
    }
  }
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

  // Two generated functions can share one shader stage, which needs the
  // prelude present exactly once. A second copy of gpxf_hash / gpxf_fbm is a
  // link error, and sharing a stage is how the terrain shader carries a
  // displacement and a surface colour at the same time.
  {
    gpx::Graph g5;
    gpx::Node *n5 = g5.add_node("FieldNoise");
    gpx::GlslProgram p5 = gpx::field_to_glsl(*n5, "out", "gpx_a");
    CHECK(p5.ok, "program transpiles");
    CHECK(p5.code.find("float gpxf_fbm") != std::string::npos,
          "a program carries the prelude by default");

    std::string stripped = gpx::field_glsl_strip_prelude(p5.code);
    CHECK(stripped.find("float gpxf_fbm") == std::string::npos,
          "stripping removes the prelude's definitions");
    CHECK(stripped.find("vec4 gpx_a(") != std::string::npos,
          "but keeps the generated function itself");
    CHECK(stripped.find("gpxf_fbm(") != std::string::npos,
          "which still calls into the prelude the stage emits once");
    CHECK(gpx::field_glsl_strip_prelude(stripped) == stripped,
          "stripping twice is harmless");

    std::string stage = std::string(gpx::field_glsl_prelude()) + stripped +
                        gpx::field_glsl_strip_prelude(
                            gpx::field_to_glsl(*n5, "out", "gpx_b").code);
    int defs = 0;
    for (size_t i = stage.find("float gpxf_fbm"); i != std::string::npos;
         i = stage.find("float gpxf_fbm", i + 1))
      ++defs;
    CHECK(defs == 1, "two functions in one stage define the prelude once");
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

// ------------------------------------------------------ memory ceiling
// The graph holds one buffer per output port for its whole life. Under a
// budget it releases the ones nothing further in the pass will read, which
// bounds peak memory. The property that matters is that this changes *when*
// things are computed and never *what* they compute.
static void test_buffer_budget() {
  std::printf("[buffer budget]\n");

  // A chain long enough that peak memory is much less than total memory.
  auto build = [](gpx::Graph &g, int links_n) {
    g.resolution = 128;
    gpx::Node *prev = g.add_node("Noise");
    prev->attrs.find("seed")->seed = 4242;
    for (int i = 0; i < links_n; ++i) {
      gpx::Node *f = g.add_node("Smooth");
      g.add_link(prev->id, "output", f->id, "input");
      prev = f;
    }
    return prev;
  };

  gpx::Graph plain;
  gpx::Node *tail_plain = build(plain, 8);
  CHECK(plain.evaluate(), "unbudgeted graph did not evaluate");
  const gpx::Heightmap *ref = out_of(tail_plain);
  CHECK(ref && !ref->empty(), "unbudgeted graph produced nothing");
  std::vector<float> expect = ref->v;
  size_t full_bytes = plain.buffer_bytes();
  CHECK(full_bytes > 0, "buffer_bytes reports nothing for a live graph");

  gpx::Graph tight;
  gpx::Node *tail_tight = build(tight, 8);
  // Room for a handful of buffers, not for all nine.
  tight.buffer_budget = full_bytes / 3;
  CHECK(tight.evaluate(), "budgeted graph did not evaluate");

  const gpx::Heightmap *got = out_of(tail_tight);
  CHECK(got && got->v.size() == expect.size(),
        "budgeted graph produced a different sized output");
  if (got && got->v.size() == expect.size()) {
    bool same = true;
    for (size_t i = 0; i < expect.size(); ++i)
      if (expect[i] != got[0].v[i]) { same = false; break; }
    CHECK(same, "a memory budget changed the result — it must only change "
                "when buffers live, never what they contain");
  }

  // It has to have actually done something, or the equality above is vacuous.
  CHECK(tight.released_bytes > 0, "the budget released nothing");
  CHECK(tight.buffer_bytes() < full_bytes,
        "the budgeted graph holds as much as the unbudgeted one");

  // The terminal node is nobody's input, so its result must survive: that is
  // the answer the caller asked for.
  CHECK(out_of(tail_tight) != nullptr, "the graph's own output was released");

  // Re-evaluating must rebuild whatever was released, and land in the same
  // place. This is the guarantee that makes releasing safe at all.
  CHECK(tight.evaluate(), "re-evaluation after release failed");
  const gpx::Heightmap *again = out_of(tail_tight);
  CHECK(again && again->v.size() == expect.size(), "rebuild produced nothing");
  if (again && again->v.size() == expect.size()) {
    bool same = true;
    for (size_t i = 0; i < expect.size(); ++i)
      if (expect[i] != again->v[i]) { same = false; break; }
    CHECK(same, "rebuilding a released buffer changed the result");
  }

  // A protected node keeps its buffer however tight the budget is.
  gpx::Graph guarded;
  gpx::Node *tail_guard = build(guarded, 8);
  gpx::Node *first = guarded.nodes.front().get();
  guarded.buffer_budget = 1; // release everything releasable
  guarded.protected_nodes = {first->id};
  CHECK(guarded.evaluate(), "protected graph did not evaluate");
  gpx::Port *fp = first->port("output", gpx::PortDir::Out);
  CHECK(fp && fp->hmap && !fp->hmap->empty(),
        "a protected node's buffer was released");
  CHECK(out_of(tail_guard) != nullptr, "the output was released under a "
                                       "budget of one byte");

  // Zero means unlimited, and must be bit-identical to having no budget code
  // at all.
  gpx::Graph off;
  gpx::Node *tail_off = build(off, 8);
  off.buffer_budget = 0;
  CHECK(off.evaluate(), "unlimited graph did not evaluate");
  CHECK(off.released_bytes == 0, "a zero budget released something");
  CHECK(off.buffer_bytes() == full_bytes,
        "a zero budget did not behave like no budget");
  const gpx::Heightmap *unl = out_of(tail_off);
  CHECK(unl && unl->v == expect, "a zero budget changed the result");
}

// --------------------------------------------------------------- cellular
// Worley noise has properties that a wrong implementation still *looks*
// plausible while violating - a truncated neighbourhood gives a pattern that
// reads as cells until you notice the seams jump, and a bad bit slice gives a
// different but perfectly cell-like pattern. So the invariants are asserted
// rather than eyeballed.
static void test_cellular() {
  std::printf("cellular (Worley) noise...\n");
  using gpx::planet::pl_cell;
  float f1, f2, id;

  // f2 is never nearer than f1, and neither is negative
  {
    bool ok = true;
    for (int i = 0; i < 4000; ++i) {
      float x = (float)((i * 37) % 211) * 0.043f - 4.f;
      float y = (float)((i * 53) % 197) * 0.051f - 4.f;
      float z = (float)((i * 71) % 173) * 0.037f - 4.f;
      pl_cell(x, y, z, 7, 1.f, i % 3, f1, f2, id);
      if (!(f2 >= f1) || f1 < 0.f || !std::isfinite(f1) || !std::isfinite(f2))
        ok = false;
      if (id < 0.f || id > 1.f) ok = false;
    }
    CHECK(ok, "f2 >= f1 >= 0, all finite, id in 0..1");
  }

  // no jitter puts every feature point at its cell centre, so the centre of a
  // cell is exactly on one and exactly one unit from its six neighbours
  {
    pl_cell(3.5f, 2.5f, -1.5f, 11, 0.f, 0, f1, f2, id);
    CHECK(std::fabs(f1) < 1e-6f, "no jitter: the cell centre is the point");
    CHECK(std::fabs(f2 - 1.f) < 1e-6f,
          "no jitter: the next point is one cell away");
  }

  // same input, same answer - this is a terrain generator
  {
    float a1, a2, ai, b1, b2, bi;
    pl_cell(1.234f, -5.678f, 0.9f, 42, 0.8f, 0, a1, a2, ai);
    pl_cell(1.234f, -5.678f, 0.9f, 42, 0.8f, 0, b1, b2, bi);
    CHECK(a1 == b1 && a2 == b2 && ai == bi, "deterministic");
    float c1, c2, ci;
    pl_cell(1.234f, -5.678f, 0.9f, 43, 0.8f, 0, c1, c2, ci);
    CHECK(a1 != c1 || ai != ci, "a different seed is a different pattern");
  }

  // the nearest-cell value is flat across a cell and changes between cells:
  // that is what makes the "plates" output a plate rather than a gradient
  {
    float mid_id;
    pl_cell(6.5f, 6.5f, 6.5f, 3, 0.f, 0, f1, f2, mid_id);
    bool flat = true;
    for (int i = 0; i < 20; ++i) {
      float o = -0.35f + 0.035f * (float)i;
      pl_cell(6.5f + o, 6.5f + o * 0.5f, 6.5f, 3, 0.f, 0, f1, f2, id);
      if (id != mid_id) flat = false;
    }
    CHECK(flat, "the cell value is constant inside its cell");
    pl_cell(7.5f, 6.5f, 6.5f, 3, 0.f, 0, f1, f2, id);
    CHECK(id != mid_id, "and different in the next cell");
  }

  // Continuity is the one that catches a truncated search. f1 is a distance
  // to the nearest of a fixed set of points, so it is 1-Lipschitz: stepping
  // by h can never change it by more than h. Search only the 8 nearest cells
  // instead of all 27 and this fails at the seams.
  {
    const float h = 0.002f;
    float worst = 0.f;
    for (int i = 0; i < 3000; ++i) {
      float x = (float)((i * 13) % 401) * 0.021f - 4.f;
      float y = (float)((i * 29) % 307) * 0.027f - 4.f;
      float z = (float)((i * 47) % 251) * 0.019f - 4.f;
      float a1, a2, ai, b1, b2, bi;
      pl_cell(x, y, z, 9, 1.f, 0, a1, a2, ai);
      pl_cell(x + h, y, z, 9, 1.f, 0, b1, b2, bi);
      float d = std::fabs(b1 - a1);
      if (d > worst) worst = d;
    }
    CHECK(worst <= h * 1.001f + 1e-6f,
          "f1 is 1-Lipschitz: the whole 3x3x3 neighbourhood is searched");
  }

  // the three metrics really are three shapes
  {
    float e1, m1, c1;
    pl_cell(0.2f, 0.7f, 0.4f, 5, 1.f, 0, e1, f2, id);
    pl_cell(0.2f, 0.7f, 0.4f, 5, 1.f, 1, m1, f2, id);
    pl_cell(0.2f, 0.7f, 0.4f, 5, 1.f, 2, c1, f2, id);
    CHECK(c1 <= e1 + 1e-6f && e1 <= m1 + 1e-6f,
          "Chebyshev <= Euclidean <= Manhattan, as the norms require");
  }

  // and the node wired to it agrees with the maths
  {
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldVoronoi", 0, 0);
    CHECK(n != nullptr, "FieldVoronoi is registered");
    if (n) {
      n->attrs.find("frequency")->f = 4.f;
      n->attrs.find("jitter")->f = 0.6f;
      n->attrs.find("output")->i = 0; // F1
      n->attrs.find("amplitude")->f = 1.f;
      n->attrs.find("offset")->f = 0.f;
      gpx::FieldContext ctx;
      ctx.pos[0] = 0.31f; ctx.pos[1] = 0.42f; ctx.pos[2] = 0.53f;
      float got = n->eval_field("out", ctx).v[0];
      pl_cell(0.31f * 4.f, 0.42f * 4.f, 0.53f * 4.f,
              n->attrs.get_seed("seed"), 0.6f, 0, f1, f2, id);
      CHECK(std::fabs(got - f1) < 1e-6f, "the node reports F1 as computed");
    }
  }
}

// ------------------------------------------------------------ depression fill
// Priority-Flood has three properties that pin it completely, and a wrong
// implementation breaks at least one while still producing a picture that
// looks like a filled terrain.
static void test_depression_fill() {
  std::printf("depression filling and flow routing...\n");
  const int N = 64;

  // a bowl carved into a tilted plane: one closed basin, one outlet
  gpx::Heightmap dem(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      float base = 0.6f - 0.004f * (float)y; // gentle slope toward +y
      float dx = (float)x - 32.f, dy = (float)y - 32.f;
      float r = std::sqrt(dx * dx + dy * dy);
      if (r < 12.f) base -= 0.25f * (1.f - r / 12.f); // the bowl
      dem.v[(size_t)y * N + x] = base;
    }

  std::vector<float> filled = gpx::fill_depressions(dem, 0.f);
  CHECK(filled.size() == dem.v.size(), "one filled height per cell");

  // 1. it never digs. Filling is filling.
  {
    bool ok = true;
    for (size_t i = 0; i < filled.size(); ++i)
      if (filled[i] < dem.v[i] - 1e-6f) ok = false;
    CHECK(ok, "the filled surface is never below the original");
  }

  // 2. the border is untouched: water leaves there, so nothing pools against it
  {
    bool ok = true;
    for (int x = 0; x < N; ++x) {
      if (std::fabs(filled[x] - dem.v[x]) > 1e-6f) ok = false;
      size_t b = (size_t)(N - 1) * N + x;
      if (std::fabs(filled[b] - dem.v[b]) > 1e-6f) ok = false;
    }
    CHECK(ok, "border cells keep their own height");
  }

  // 3. the basin really did fill, and to one level - that is what a lake is
  {
    float centre = filled[(size_t)32 * N + 32];
    CHECK(centre > dem.v[(size_t)32 * N + 32] + 0.05f, "the bowl filled");
    bool level = true;
    int flooded = 0;
    for (int y = 26; y <= 38; ++y)
      for (int x = 26; x <= 38; ++x) {
        size_t i = (size_t)y * N + x;
        if (filled[i] > dem.v[i] + 1e-6f) {
          ++flooded;
          if (std::fabs(filled[i] - centre) > 1e-4f) level = false;
        }
      }
    CHECK(flooded > 50, "a real lake, not a few cells");
    CHECK(level, "one water level across the whole lake");
  }

  // 4. no pits left: every non-border cell has a neighbour no higher than it.
  //    This is the property flow routing depends on, and the reason for the
  //    whole exercise.
  {
    const int DX[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int DY[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    int pits_before = 0, pits_after = 0;
    for (int y = 1; y < N - 1; ++y)
      for (int x = 1; x < N - 1; ++x) {
        bool low_b = true, low_a = true;
        for (int k = 0; k < 8; ++k) {
          size_t ni = (size_t)(y + DY[k]) * N + (x + DX[k]);
          if (dem.v[ni] <= dem.v[(size_t)y * N + x]) low_b = false;
          if (filled[ni] <= filled[(size_t)y * N + x]) low_a = false;
        }
        if (low_b) ++pits_before;
        if (low_a) ++pits_after;
      }
    CHECK(pits_before > 0, "the test terrain really has a pit to fill");
    CHECK(pits_after == 0, "no cell is left with nowhere to drain");
  }

  // 5. deterministic, including with the epsilon tilt whose result depends on
  //    the traversal order
  {
    std::vector<float> a = gpx::fill_depressions(dem, 1e-5f);
    std::vector<float> b = gpx::fill_depressions(dem, 1e-5f);
    CHECK(a == b, "bit-identical across runs, epsilon and all");
  }

  // 6. and the node reports the lake the fill implies
  {
    gpx::Graph g;
    gpx::Node *src = g.add_node("Constant", 0, 0);
    gpx::Node *fb = g.add_node("FillBasins", 200, 0);
    CHECK(fb != nullptr, "FillBasins is registered");
    (void)src;
    if (fb) {
      fb->attrs.find("normalize_depth")->b = false;
      gpx::Port *pin = fb->port("input", gpx::PortDir::In);
      CHECK(pin != nullptr, "it takes a heightmap");
      // drive it directly: the node reads its input port's buffer
      if (pin) {
        auto hm = std::make_shared<gpx::Heightmap>(dem);
        pin->hmap = hm;
        const gpx::NodeDef *def = gpx::NodeRegistry::instance().find("FillBasins");
        if (def && def->compute) {
          def->compute(*fb);
          // read through the port, not out_hmap(): that one is the writer's
          // accessor and clears the buffer it hands back
          const gpx::Port *dp = fb->port("depth", gpx::PortDir::Out);
          const gpx::Port *mp = fb->port("mask", gpx::PortDir::Out);
          CHECK(dp && dp->hmap && mp && mp->hmap, "depth and mask are produced");
          if (dp && dp->hmap && mp && mp->hmap) {
            const gpx::Heightmap &d = *dp->hmap;
            const gpx::Heightmap &m = *mp->hmap;
            size_t c = (size_t)32 * N + 32;
            CHECK(d.v.size() == dem.v.size(), "sized from the input, not the graph");
            CHECK(d.v[c] > 0.05f, "depth at the lake centre matches the fill");
            CHECK(m.v[c] == 1.f, "the mask marks the lake");
            CHECK(m.v[0] == 0.f, "and not the dry border");
          }
        }
      }
    }
  }

  // 7. the routing fix binds: with pits filled, water reaching the basin
  //    carries on past it, so cells downslope of the lake see more of it
  {
    gpx::Graph g;
    gpx::Node *fa = g.add_node("FlowAccumulation", 0, 0);
    CHECK(fa != nullptr, "FlowAccumulation is registered");
    if (fa) {
      gpx::Port *pin = fa->port("input", gpx::PortDir::In);
      auto hm = std::make_shared<gpx::Heightmap>(dem);
      if (pin) pin->hmap = hm;
      const gpx::NodeDef *def =
          gpx::NodeRegistry::instance().find("FlowAccumulation");
      auto total_below = [&](bool fill) {
        fa->attrs.find("fill_pits")->b = fill;
        fa->attrs.find("log_scale")->b = false;
        def->compute(*fa);
        const gpx::Port *op = fa->port("output", gpx::PortDir::Out);
        double sum = 0;
        if (op && op->hmap && op->hmap->w == N)
          for (int y = 48; y < N - 1; ++y)
            for (int x = 20; x < 44; ++x) sum += op->hmap->v[(size_t)y * N + x];
        return sum;
      };
      double unfilled = total_below(false);
      double refilled = total_below(true);
      CHECK(refilled > unfilled * 1.05,
            "filling pits carries water past the basin instead of losing it");
    }
  }
}

// -------------------------------------------------- distance field & filters
// The distance transform claims to be EXACT Euclidean distance in linear
// time, which is a strong claim with a cheap test: brute force on a small
// grid must agree to float precision, at every cell, for arbitrary shapes.
// An approximate transform (chamfer, two-pass city block) fails this on the
// diagonals, which is precisely where terrain falloffs show the artefact as
// square-ish halos around round lakes.
static void test_distance_and_filters() {
  std::printf("distance field, median, equalize...\n");

  auto run_node = [](const char *type, const gpx::Heightmap &input,
                     auto setup) -> gpx::Heightmap {
    gpx::Graph g;
    gpx::Node *n = g.add_node(type, 0, 0);
    CHECK(n != nullptr, "node registered");
    if (!n) return gpx::Heightmap(1, 1);
    setup(*n);
    gpx::Port *pin = n->port("input", gpx::PortDir::In);
    auto hm = std::make_shared<gpx::Heightmap>(input);
    if (pin) pin->hmap = hm;
    const gpx::NodeDef *def = gpx::NodeRegistry::instance().find(type);
    def->compute(*n);
    for (const gpx::Port &po : n->ports)
      if (po.dir == gpx::PortDir::Out && po.hmap) return *po.hmap;
    return gpx::Heightmap(1, 1);
  };

  // --- exactness against brute force -------------------------------------
  {
    const int N = 28;
    gpx::Heightmap mask(N, N);
    // an arbitrary scatter of shape cells, deterministic
    std::vector<std::pair<int, int>> pts;
    for (int i = 0; i < 9; ++i) {
      int x = (i * 37 + 11) % N, y = (i * 53 + 5) % N;
      pts.push_back({x, y});
      mask.v[(size_t)y * N + x] = 1.f;
    }
    gpx::Heightmap d = run_node("DistanceField", mask, [](gpx::Node &n) {
      n.attrs.find("mode")->i = 1;          // raw distance
      n.attrs.find("reach")->f = 1.f;       // reach = the whole tile width
      n.attrs.find("post_remap")->b = false;     // and no post-processing
    });
    CHECK(d.w == N && d.h == N, "output sized from the input");
    float worst = 0.f;
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) {
        float best = 1e9f;
        for (auto &pt : pts) {
          float dx = (float)(x - pt.first), dy = (float)(y - pt.second);
          best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        // the node divides by reach = 1.0 * width
        float got = d.v[(size_t)y * N + x] * (float)N;
        worst = std::max(worst, std::fabs(got - best));
      }
    CHECK(worst < 1e-3f, "exact Euclidean distance, every cell, any shape");
  }

  // --- the fade mode is a ready-made falloff ------------------------------
  {
    const int N = 32;
    gpx::Heightmap mask(N, N);
    mask.v[(size_t)16 * N + 16] = 1.f;
    gpx::Heightmap d = run_node("DistanceField", mask, [](gpx::Node &n) {
      n.attrs.find("mode")->i = 0;
      n.attrs.find("reach")->f = 0.25f;
      n.attrs.find("post_remap")->b = false;
    });
    CHECK(d.v[(size_t)16 * N + 16] == 1.f, "1 exactly on the shape");
    CHECK(d.v[0] == 0.f, "0 beyond the reach");
    float near = d.v[(size_t)16 * N + 18], far = d.v[(size_t)16 * N + 22];
    CHECK(near > far && far > 0.f, "and falling off monotonically between");
  }

  // --- median: kills a spike, leaves a cliff ------------------------------
  {
    const int N = 24;
    gpx::Heightmap in(N, N);
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x)
        in.v[(size_t)y * N + x] = x < N / 2 ? 0.2f : 0.8f; // a hard cliff
    in.v[(size_t)5 * N + 5] = 9.f; // and one absurd needle
    gpx::Heightmap out = run_node("Median", in, [](gpx::Node &n) {
      n.attrs.find("post_remap")->b = false;
    });
    CHECK(out.v[(size_t)5 * N + 5] == 0.2f, "the needle is gone");
    CHECK(out.v[(size_t)10 * N + (N / 2 - 1)] == 0.2f &&
              out.v[(size_t)10 * N + (N / 2)] == 0.8f,
          "the cliff edge did not move or soften");
  }

  // --- equalize: uses the whole range, and full strength means uniform ----
  {
    const int N = 64;
    gpx::Heightmap in(N, N);
    // squashed distribution: everything crammed into 0.4..0.5, plus anchors
    for (size_t i = 0; i < in.v.size(); ++i)
      in.v[i] = 0.4f + 0.1f * (float)(i % 97) / 97.f;
    in.v[0] = 0.f;
    in.v[1] = 1.f;
    gpx::Heightmap out = run_node("Equalize", in, [](gpx::Node &n) {
      n.attrs.find("post_remap")->b = false;
    });
    // after equalisation the values should spread: the middle of the sorted
    // data should sit near the middle of the range instead of near 0.45
    std::vector<float> sorted(out.v);
    std::sort(sorted.begin(), sorted.end());
    float median_v = sorted[sorted.size() / 2];
    CHECK(median_v > 0.35f && median_v < 0.65f,
          "the crammed distribution spread back across the range");
    // a flat input must pass through unchanged (span is zero)
    gpx::Heightmap flat(16, 16);
    for (float &v : flat.v) v = 0.31f;
    gpx::Heightmap fo = run_node("Equalize", flat, [](gpx::Node &n) {
      n.attrs.find("post_remap")->b = false;
    });
    CHECK(fo.v[10] == 0.31f, "a flat input passes through untouched");
  }
}

// ------------------------------------------------------------ curve & shapes
static void test_curve_and_shapes() {
  std::printf("tone curve and analytic shapes...\n");

  // the raster Shape node's analytic modes (waves, step, band, paraboloid)
  {
    auto shape = [&](int type, float freq) {
      gpx::Graph g;
      g.resolution = 64;
      gpx::Node *n = g.add_node("Shape", 0, 0);
      n->attrs.find("type")->i = type;
      n->attrs.find("frequency")->f = freq;
      n->attrs.find("post_remap")->b = false;
      gpx::NodeRegistry::instance().find("Shape")->compute(*n);
      return *n->port("output", gpx::PortDir::Out)->hmap;
    };
    // wave sine: peaks 4 times across the tile at frequency 4
    auto ws = shape(6, 4.f);
    // crossings land exactly on samples (value 0.5), so track the last
    // strictly-signed sample rather than multiplying neighbors
    int crossings = 0, last = 0;
    for (int x = 0; x < 64; ++x) {
      float d = ws.at(x, 32) - 0.5f;
      int s = d > 1e-6f ? 1 : (d < -1e-6f ? -1 : 0);
      if (s != 0) {
        if (last != 0 && s != last) ++crossings;
        last = s;
      }
    }
    CHECK(crossings >= 7 && crossings <= 9,
          "sine wave crosses its midline twice per period");
    // step: monotonic along the axis, 0 one side and 1 the other
    auto st = shape(9, 4.f);
    CHECK(st.at(2, 32) < 0.05f && st.at(61, 32) > 0.95f,
          "step is 0 before the line and 1 after");
    // band: peaks at the center line, falls off both sides
    auto bd = shape(10, 4.f);
    CHECK(bd.at(32, 32) > 0.95f && bd.at(2, 32) < 0.05f,
          "band is 1 on the line and 0 far away");
    // paraboloid: dome with the apex at the center
    auto pb = shape(11, 4.f);
    CHECK(pb.at(32, 32) > 0.95f && pb.at(0, 0) == 0.f,
          "paraboloid peaks at the center and clamps at the rim");
  }

  // the Curve node: an inverting curve (white to black) must flip the
  // terrain's shape exactly, and identity stops must change nothing
  {
    gpx::Graph g;
    gpx::Node *n = g.add_node("Curve", 0, 0);
    CHECK(n != nullptr, "Curve is registered");
    if (n) {
      const int N = 32;
      gpx::Heightmap in(N, N);
      for (size_t i = 0; i < in.v.size(); ++i)
        in.v[i] = 0.1f + 0.8f * (float)(i % 61) / 61.f;
      gpx::Port *pin = n->port("input", gpx::PortDir::In);
      auto hm = std::make_shared<gpx::Heightmap>(in);
      if (pin) pin->hmap = hm;
      n->attrs.find("post_remap")->b = false;
      const gpx::NodeDef *def = gpx::NodeRegistry::instance().find("Curve");

      // identity first
      def->compute(*n);
      const gpx::Port *op = n->port("output", gpx::PortDir::Out);
      CHECK(op && op->hmap, "output produced");
      if (op && op->hmap) {
        float worst = 0.f;
        for (size_t i = 0; i < in.v.size(); ++i)
          worst = std::max(worst, std::fabs(op->hmap->v[i] - in.v[i]));
        CHECK(worst < 1e-6f, "the default curve is the identity");
      }

      // then invert: swap the stops' brightness
      gpx::Attribute *cv = n->attrs.find("curve");
      cv->stops = {{0.f, 1.f, 1.f, 1.f, 1.f}, {1.f, 0.f, 0.f, 0.f, 1.f}};
      def->compute(*n);
      if (op && op->hmap) {
        float mn, mx;
        in.minmax(mn, mx);
        float worst = 0.f;
        for (size_t i = 0; i < in.v.size(); ++i) {
          float expect = mn + (mx - (in.v[i]))* 1.f; // mn + (1-t)*span
          worst = std::max(worst, std::fabs(op->hmap->v[i] - expect));
        }
        CHECK(worst < 1e-5f, "an inverted curve mirrors the heights exactly");
      }
    }
  }

  // FieldShape: the modes' defining properties, not their pictures
  {
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldShape", 0, 0);
    CHECK(n != nullptr, "FieldShape is registered");
    if (n) {
      gpx::FieldContext ctx;
      auto at = [&](float x, float z) {
        ctx.pos[0] = x; ctx.pos[1] = 0.f; ctx.pos[2] = z;
        return n->eval_field("out", ctx).v[0];
      };
      // gaussian bump: 1 at the centre, falling with distance
      n->attrs.find("mode")->i = 4;
      CHECK(std::fabs(at(0.5f, 0.5f) - 1.f) < 1e-6f, "bump peaks at its centre");
      CHECK(at(0.6f, 0.5f) > at(0.8f, 0.5f), "and falls off with distance");
      // step: direction 0 means the +X side is 1
      n->attrs.find("mode")->i = 7;
      CHECK(at(0.9f, 0.5f) == 1.f && at(0.1f, 0.5f) == 0.f,
            "step splits along its line");
      // sine at frequency 1: a full period across one unit
      n->attrs.find("mode")->i = 0;
      n->attrs.find("frequency")->f = 1.f;
      n->attrs.find("phase")->f = 0.f;
      float a = at(0.5f, 0.5f), b = at(1.0f, 0.5f), c = at(1.5f, 0.5f);
      CHECK(std::fabs(a - c) < 1e-5f, "one unit is one full period");
      CHECK(std::fabs((a + b) - 1.f) < 1e-5f,
            "half a period lands on the opposite phase");
    }
  }
}

// ------------------------------------------------------------------- paths
static void test_terrain_metrics() {
  std::printf("terrain metrics...\n");
  const int N = 64;
  // rough left half, mirror-smooth right half
  gpx::Heightmap t(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x)
      t.at(x, y) = x < 32 ? ((x * 7 + y * 13) % 5) * 0.1f : 0.2f;
  auto run = [&](int metric) {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node("TerrainMetrics", 0, 0);
    n->attrs.find("metric")->i = metric;
    n->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(t);
    gpx::NodeRegistry::instance().find("TerrainMetrics")->compute(*n);
    return *n->port("mask", gpx::PortDir::Out)->hmap;
  };
  auto rug = run(0);
  CHECK(rug.at(10, 32) > rug.at(54, 32) + 0.2f,
        "rugosity reads high on rough ground, low on smooth");
  auto tri = run(1);
  CHECK(tri.at(10, 32) > tri.at(54, 32) + 0.2f, "TRI agrees");
  // shape index: a dome scores high, a cup low
  gpx::Heightmap bowl(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      float dx = (x - 32) / 16.f, dy = (y - 32) / 16.f;
      bowl.at(x, y) = dx * dx + dy * dy;
    }
  {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node("TerrainMetrics", 0, 0);
    n->attrs.find("metric")->i = 2;
    n->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(bowl);
    gpx::NodeRegistry::instance().find("TerrainMetrics")->compute(*n);
    const gpx::Heightmap &si = *n->port("mask", gpx::PortDir::Out)->hmap;
    // remapped 0..1 over the tile; the cup's centre must sit at the low end
    CHECK(si.at(32, 32) < 0.3f, "a cup reads at the low end of shape index");
  }
}

static void test_basalt() {
  std::printf("basalt columns...\n");
  gpx::Graph g;
  g.resolution = 128;
  gpx::Node *n = g.add_node("BasaltField", 0, 0);
  n->attrs.find("post_remap")->b = false;
  gpx::NodeRegistry::instance().find("BasaltField")->compute(*n);
  const gpx::Heightmap &out = *n->port("output", gpx::PortDir::Out)->hmap;
  const gpx::Heightmap &cr = *n->port("cracks", gpx::PortDir::Out)->hmap;
  // tiered flats: many cells share exactly the same level away from cracks
  std::map<float, int> levels;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x)
      if (cr.at(x, y) < 0.01f) levels[out.at(x, y)]++;
  int big = 0;
  for (auto &kv : levels)
    if (kv.second > 100) ++big;
  CHECK(big >= 3 && big <= 30, "the field is a handful of flat tiers");
  float cmx = *std::max_element(cr.v.begin(), cr.v.end());
  CHECK(cmx == 1.f, "crack lines reach full strength on the seams");
  bool finite = true;
  for (float v : out.v) finite = finite && std::isfinite(v);
  CHECK(finite, "basalt is finite");
}

static void test_quilt() {
  std::printf("quilting...\n");
  gpx::Graph g;
  g.resolution = 128;
  gpx::Node *src = g.add_node("Noise", 0, 0);
  gpx::Node *q = g.add_node("Quilt", 0, 0);
  g.add_link(src->id, "output", q->id, "input");
  g.evaluate();
  const gpx::Heightmap &in = *src->port("output", gpx::PortDir::Out)->hmap;
  const gpx::Heightmap &out = *q->port("output", gpx::PortDir::Out)->hmap;
  CHECK(out.w == 128, "quilt fills the tile");
  // same statistics family: the quilt's range must sit inside the input's
  float imn, imx, omn, omx;
  in.minmax(imn, imx);
  out.minmax(omn, omx);
  CHECK(omn >= imn - 1e-4f && omx <= imx + 1e-4f,
        "every quilted value comes from the exemplar's range");
  // but a different arrangement, not a copy
  size_t diff = 0;
  for (size_t i = 0; i < out.v.size(); ++i) diff += out.v[i] != in.v[i];
  CHECK(diff > out.v.size() / 2, "the quilt rearranges the surface");
  bool finite = true;
  for (float v : out.v) finite = finite && std::isfinite(v);
  CHECK(finite, "the quilt is finite");
  gpx::Heightmap keep = out;
  q->dirty = true;
  g.evaluate();
  CHECK(q->port("output", gpx::PortDir::Out)->hmap->v == keep.v,
        "quilting is bit-identical across computes");
}

static void test_dla() {
  std::printf("diffusion-limited aggregation...\n");
  gpx::Graph g;
  g.resolution = 128;
  gpx::Node *n = g.add_node("DiffusionLimited", 0, 0);
  n->attrs.find("particles")->i = 600;
  n->attrs.find("smooth_radius")->f = 0.f;
  gpx::NodeRegistry::instance().find("DiffusionLimited")->compute(*n);
  const gpx::Heightmap &m = *n->port("mask", gpx::PortDir::Out)->hmap;
  size_t stuck = 0;
  for (float v : m.v) stuck += v > 0.5f;
  CHECK(stuck > 300, "most particles aggregate");
  CHECK(m.at(64, 64) == 1.f, "the seed cell is part of the cluster");
  // sparse and branching, not a blob: the cluster's bounding box is much
  // larger than a solid disc of the same cell count would need
  int xmin = 128, xmax = 0, ymin = 128, ymax = 0;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x)
      if (m.at(x, y) > 0.5f) {
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
      }
  float bbox_area = (float)(xmax - xmin) * (ymax - ymin);
  CHECK(bbox_area > (float)stuck * 2.f, "the aggregate is sparse, not solid");
  gpx::Heightmap keep = m;
  gpx::NodeRegistry::instance().find("DiffusionLimited")->compute(*n);
  CHECK(n->port("mask", gpx::PortDir::Out)->hmap->v == keep.v,
        "the growth is bit-identical across computes");
}

static void test_points_io() {
  std::printf("points CSV roundtrip...\n");
  const char *file = "test_points_roundtrip.csv";
  gpx::Graph g;
  g.resolution = 32;
  gpx::Node *ex = g.add_node("ExportPoints", 0, 0);
  gpx::Node *im = g.add_node("PointsFromCsv", 0, 0);
  auto pts = std::make_shared<gpx::PointCloud>();
  pts->add(0.25f, 0.75f, 1.5f);
  pts->add(0.5f, 0.5f, 0.25f);
  pts->add(0.875f, 0.125f, 3.f);
  ex->port("points", gpx::PortDir::In)->pts = pts;
  ex->attrs.find("path")->s = file;
  ex->attrs.find("auto_export")->b = true;
  gpx::NodeRegistry::instance().find("ExportPoints")->compute(*ex);
  im->attrs.find("path")->s = file;
  gpx::NodeRegistry::instance().find("PointsFromCsv")->compute(*im);
  const gpx::PointCloud &back = *im->port("points", gpx::PortDir::Out)->pts;
  CHECK(back.size() == 3, "all points come back");
  bool same = back.size() == 3;
  for (size_t i = 0; i < back.size() && same; ++i)
    same = std::fabs(back.x[i] - pts->x[i]) < 1e-4f &&
           std::fabs(back.y[i] - pts->y[i]) < 1e-4f &&
           std::fabs(back.v[i] - pts->v[i]) < 1e-4f;
  CHECK(same, "coordinates and values survive the roundtrip");
  std::remove(file);
}

static void test_landform() {
  std::printf("landforms...\n");
  const int N = 64;
  auto run = [&](int type) {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node("Landform", 0, 0);
    n->attrs.find("type")->i = type;
    n->attrs.find("post_remap")->b = false;
    gpx::NodeRegistry::instance().find("Landform")->compute(*n);
    return *n->port("output", gpx::PortDir::Out)->hmap;
  };
  auto island = run(0);
  CHECK(island.at(32, 32) > island.at(2, 2) + 0.2f,
        "the island rises from the sea");
  auto caldera = run(2);
  CHECK(caldera.at(32 + 12, 32) > caldera.at(32, 32) + 0.1f,
        "the caldera rim stands above the crater floor");
  auto rift = run(3);
  // the trench runs along the direction axis (x at angle 0), so sample
  // across it, not along it
  CHECK(rift.at(32, 32) < rift.at(32, 4) - 0.2f,
        "the rift floor sits below the plateau");
  auto mesa = run(4);
  CHECK(std::fabs(mesa.at(32, 32) - mesa.at(36, 36)) < 0.08f,
        "the mesa top is level");
  CHECK(mesa.at(32, 32) > mesa.at(2, 2) + 0.4f, "the mesa stands tall");
  bool finite = true;
  for (auto *hm : {&island, &caldera, &rift, &mesa})
    for (float v : hm->v) finite = finite && std::isfinite(v);
  CHECK(finite, "landforms are finite");
}

static void test_path_nodes() {
  std::printf("path family...\n");
  const int N = 64;
  gpx::Graph g;
  g.resolution = N;
  gpx::Node *src = g.add_node("PointsToPath", 0, 0);
  gpx::Node *rs = g.add_node("PathResample", 0, 0);
  gpx::Node *fr = g.add_node("PathFractalize", 0, 0);
  gpx::Node *sd = g.add_node("PathSDF", 0, 0);
  CHECK(src && rs && fr && sd, "path nodes all register");

  // an unordered zigzag cloud: the tour must visit near neighbors in order
  auto cloud = std::make_shared<gpx::PointCloud>();
  cloud->add(0.9f, 0.5f, 0.f);
  cloud->add(0.1f, 0.5f, 0.f);
  cloud->add(0.5f, 0.5f, 0.f);
  cloud->add(0.3f, 0.5f, 0.f);
  cloud->add(0.7f, 0.5f, 0.f);
  src->port("points", gpx::PortDir::In)->pts = cloud;
  gpx::NodeRegistry::instance().find("PointsToPath")->compute(*src);
  const gpx::PointCloud &tour = *src->port("path", gpx::PortDir::Out)->pts;
  CHECK(tour.size() == 5, "the tour visits every point");
  bool ordered = true;
  for (size_t i = 1; i < tour.size(); ++i)
    ordered = ordered && tour.x[i] > tour.x[i - 1];
  CHECK(ordered, "collinear points come out in line order");

  // resample: even spacing along the straight line
  g.add_link(src->id, "path", rs->id, "path");
  rs->attrs.find("spacing")->f = 0.1f;
  gpx::NodeRegistry::instance().find("PathResample")->compute(*rs);
  const gpx::PointCloud &ev = *rs->port("path", gpx::PortDir::Out)->pts;
  CHECK(ev.size() >= 8, "resampling a 0.8-long line at 0.1 yields the steps");
  bool even = true;
  for (size_t i = 1; i < ev.size(); ++i) {
    float dx = ev.x[i] - ev.x[i - 1], dy = ev.y[i] - ev.y[i - 1];
    even = even && std::fabs(std::sqrt(dx * dx + dy * dy) - 0.1f) < 0.01f;
  }
  CHECK(even, "the spacing is uniform");

  // fractalize adds points and stays deterministic
  g.add_link(src->id, "path", fr->id, "path");
  gpx::NodeRegistry::instance().find("PathFractalize")->compute(*fr);
  const gpx::PointCloud &fz = *fr->port("path", gpx::PortDir::Out)->pts;
  CHECK(fz.size() > tour.size() * 8, "four subdivisions multiply the points");
  gpx::PointCloud keep = fz;
  gpx::NodeRegistry::instance().find("PathFractalize")->compute(*fr);
  CHECK(fr->port("path", gpx::PortDir::Out)->pts->x == keep.x,
        "fractalize is bit-identical across computes");

  // PathSpline passes exactly through every control point
  {
    gpx::Graph g2;
    g2.resolution = N;
    gpx::Node *sp = g2.add_node("PathSpline", 0, 0);
    auto ctrl = std::make_shared<gpx::PointCloud>();
    ctrl->add(0.1f, 0.1f, 0.f);
    ctrl->add(0.5f, 0.8f, 0.f);
    ctrl->add(0.9f, 0.2f, 0.f);
    sp->port("path", gpx::PortDir::In)->pts = ctrl;
    gpx::NodeRegistry::instance().find("PathSpline")->compute(*sp);
    const gpx::PointCloud &cur = *sp->port("path", gpx::PortDir::Out)->pts;
    CHECK(cur.size() == 17, "two segments at 8 samples plus the endpoint");
    for (size_t c = 0; c < ctrl->size(); ++c) {
      bool hit = false;
      for (size_t i = 0; i < cur.size(); ++i)
        hit = hit || (std::fabs(cur.x[i] - ctrl->x[c]) < 1e-6f &&
                      std::fabs(cur.y[i] - ctrl->y[c]) < 1e-6f);
      CHECK(hit, "the spline passes through each control point");
    }
  }

  // PathFind: with a wall across the middle and a gap in it, the route
  // must pass through the gap rather than climb the wall
  {
    gpx::Heightmap ter(N, N);
    for (int y = 0; y < N; ++y)
      for (int x = 30; x < 34; ++x) ter.at(x, y) = 1.f; // a tall wall
    for (int y = 8; y < 12; ++y)
      for (int x = 30; x < 34; ++x) ter.at(x, y) = 0.f; // the gap
    gpx::Graph g2;
    g2.resolution = N;
    gpx::Node *pf = g2.add_node("PathFind", 0, 0);
    pf->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(ter);
    gpx::NodeRegistry::instance().find("PathFind")->compute(*pf);
    const gpx::Heightmap &pm = *pf->port("path_mask", gpx::PortDir::Out)->hmap;
    const gpx::PointCloud &route = *pf->port("path", gpx::PortDir::Out)->pts;
    CHECK(route.size() > 10, "a route was found");
    bool through_gap = false, over_wall = false;
    for (int y = 0; y < N; ++y)
      if (pm.at(32, y) > 0.5f) {
        if (y >= 8 && y < 12) through_gap = true;
        else over_wall = true;
      }
    CHECK(through_gap && !over_wall, "the route detours through the gap");
    CHECK(pf->error.empty(), "no routing error");
  }

  // SDF: zero on the line, grows away, mask is the inverse band
  g.add_link(src->id, "path", sd->id, "path");
  gpx::NodeRegistry::instance().find("PathSDF")->compute(*sd);
  const gpx::Heightmap &dist = *sd->port("distance", gpx::PortDir::Out)->hmap;
  const gpx::Heightmap &m = *sd->port("mask", gpx::PortDir::Out)->hmap;
  CHECK(dist.at(32, 32) == 0.f, "distance is zero on the path");
  CHECK(dist.at(32, 8) > 0.5f, "distance grows away from the path");
  CHECK(m.at(32, 32) == 1.f && m.at(32, 8) < 0.5f, "the mask is the band");

  // wavelet noise: finite, varying, deterministic, and genuinely band
  // limited - the tile's mean is near zero because the low band is removed
  {
    gpx::Graph g;
    g.resolution = 64;
    gpx::Node *n = g.add_node("WaveletNoise", 0, 0);
    n->attrs.find("post_remap")->b = false;
    gpx::NodeRegistry::instance().find("WaveletNoise")->compute(*n);
    const gpx::Heightmap &a = *n->port("output", gpx::PortDir::Out)->hmap;
    double mean = 0;
    float lo = 1e9f, hi = -1e9f;
    bool fin = true;
    for (float v : a.v) {
      fin = fin && std::isfinite(v);
      mean += v;
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    mean /= a.v.size();
    CHECK(fin && hi - lo > 0.1f, "wavelet noise is finite and varies");
    CHECK(std::fabs(mean - 0.5) < 0.05, "the low band is gone: mean sits at the midline");
    gpx::Heightmap keep = a;
    gpx::NodeRegistry::instance().find("WaveletNoise")->compute(*n);
    CHECK(n->port("output", gpx::PortDir::Out)->hmap->v == keep.v,
          "wavelet noise is bit-identical across computes");
  }

  // Gabor: with anisotropy 1 and orientation 0 the streaks run along x, so
  // the surface varies less along x than across it
  {
    gpx::Graph g;
    g.resolution = 64;
    gpx::Node *n = g.add_node("GaborNoise", 0, 0);
    n->attrs.find("orientation")->f = 0.f;
    n->attrs.find("anisotropy")->f = 1.f;
    n->attrs.find("post_remap")->b = false;
    gpx::NodeRegistry::instance().find("GaborNoise")->compute(*n);
    const gpx::Heightmap &a = *n->port("output", gpx::PortDir::Out)->hmap;
    double along = 0, across = 0;
    for (int y = 1; y < 63; ++y)
      for (int x = 1; x < 63; ++x) {
        along += std::fabs(a.at(x + 1, y) - a.at(x, y));
        across += std::fabs(a.at(x, y + 1) - a.at(x, y));
      }
    // the cosine oscillates ALONG its orientation, so the ridges (iso-lines)
    // run perpendicular: variation along x is high, along y low
    CHECK(across < along * 0.7, "anisotropic gabor streaks across its axis");
    bool finite = true;
    for (float v : a.v) finite = finite && std::isfinite(v);
    CHECK(finite, "gabor is finite");

    // phasor flavors: finite, varying, and deterministic
    for (int flavor : {1, 2, 3}) {
      n->attrs.find("flavor")->i = flavor;
      gpx::NodeRegistry::instance().find("GaborNoise")->compute(*n);
      const gpx::Heightmap &ph = *n->port("output", gpx::PortDir::Out)->hmap;
      float lo = 1e9f, hi = -1e9f;
      bool fin = true;
      for (float v : ph.v) {
        fin = fin && std::isfinite(v);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      CHECK(fin && hi - lo > 0.3f, "phasor flavor is finite and varies");
    }
  }
}

static void test_selectors() {
  std::printf("selectors...\n");
  const int N = 64;
  gpx::Heightmap ramp(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) ramp.at(x, y) = x / (float)(N - 1);

  auto run = [&](const char *type, std::function<void(gpx::Node &)> wire) {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node(type, 0, 0);
    wire(*n);
    gpx::NodeRegistry::instance().find(type)->compute(*n);
    return *n->port("mask", gpx::PortDir::Out)->hmap;
  };

  // midrange peaks at the center height and falls off both ways
  auto mid = run("SelectMidrange", [&](gpx::Node &n) {
    n.attrs.find("invert")->b = false;
    n.port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(ramp);
  });
  CHECK(mid.at(32, 32) > mid.at(4, 32) && mid.at(32, 32) > mid.at(60, 32),
        "midrange peaks in the middle of the range");

  // transitions: two ramps crossing select the crossing band
  gpx::Heightmap ramp2(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) ramp2.at(x, y) = 1.f - x / (float)(N - 1);
  auto tr = run("SelectTransitions", [&](gpx::Node &n) {
    n.port("input A", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(ramp);
    n.port("input B", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(ramp2);
  });
  // the exact crossing falls between samples, so the nearest cell reads the
  // smoothstep of a one-cell offset rather than a full 1
  CHECK(tr.at(32, 32) > 0.6f, "the crossing line is selected");
  CHECK(tr.at(4, 32) == 0.f && tr.at(60, 32) == 0.f,
        "away from the crossing nothing is selected");

  // border band around a disc mask
  gpx::Heightmap disc(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x)
      disc.at(x, y) =
          ((x - 32) * (x - 32) + (y - 32) * (y - 32) < 144) ? 1.f : 0.f;
  auto bd = run("SelectBorder", [&](gpx::Node &n) {
    n.attrs.find("reach")->f = 0.08f;
    n.port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(disc);
  });
  CHECK(bd.at(32 + 12, 32) > 0.5f, "the rim is selected");
  CHECK(bd.at(32, 32) == 0.f, "the disc's center is not");
  CHECK(bd.at(2, 2) == 0.f, "far outside is not");
}

static void test_noise_variants() {
  std::printf("fBm variants...\n");
  for (int type : {9, 10, 11, 12}) { // IQ, Jordan, Pingpong, Voronoise
    gpx::Graph g;
    g.resolution = 64;
    gpx::Node *n = g.add_node("Noise", 0, 0);
    n->attrs.find("type")->i = type;
    n->attrs.find("post_remap")->b = false;
    gpx::NodeRegistry::instance().find("Noise")->compute(*n);
    const gpx::Heightmap &a = *n->port("output", gpx::PortDir::Out)->hmap;
    float lo = 1e9f, hi = -1e9f;
    bool finite = true;
    for (float v : a.v) {
      finite = finite && std::isfinite(v);
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    CHECK(finite, "variant output is finite");
    CHECK(hi - lo > 0.05f, "variant output actually varies");
    gpx::Heightmap keep = a;
    gpx::NodeRegistry::instance().find("Noise")->compute(*n);
    CHECK(n->port("output", gpx::PortDir::Out)->hmap->v == keep.v,
          "variant is bit-identical across computes");
  }
}

static void test_local_filters() {
  std::printf("locality filters...\n");
  const int N = 64;
  auto run = [&](const char *type, const gpx::Heightmap &in,
                 std::function<void(gpx::Node &)> tune, const char *outp) {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node(type, 0, 0);
    tune(*n);
    n->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(in);
    gpx::NodeRegistry::instance().find(type)->compute(*n);
    return *n->port(outp, gpx::PortDir::Out)->hmap;
  };

  // Detrend flattens a pure ramp to (nearly) its mean everywhere
  gpx::Heightmap ramp(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) ramp.at(x, y) = x / (float)N;
  auto dt = run("Detrend", ramp, [](gpx::Node &) {}, "output");
  float lo = 1e9f, hi = -1e9f;
  for (float v : dt.v) { lo = std::min(lo, v); hi = std::max(hi, v); }
  CHECK(hi - lo < 0.02f, "detrending a ramp leaves a flat surface");

  // Kuwahara preserves a hard step better than a box blur would
  gpx::Heightmap step(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 32; x < N; ++x) step.at(x, y) = 1.f;
  auto kw = run("Kuwahara", step, [](gpx::Node &n) {
    n.attrs.find("radius")->i = 4;
  }, "output");
  CHECK(kw.at(30, 32) < 0.05f && kw.at(34, 32) > 0.95f,
        "kuwahara keeps the step edge sharp");

  // SmoothFill up only raises, never lowers
  auto sf = run("SmoothFill", step, [](gpx::Node &n) {
    n.attrs.find("radius")->i = 8;
  }, "output");
  bool never_lower = true;
  for (size_t i = 0; i < sf.v.size(); ++i)
    never_lower = never_lower && sf.v[i] >= step.v[i] - 1e-6f;
  CHECK(never_lower, "fill-up never cuts into the terrain");

  // RelativeElevation of a bump: high at the top, low in the moat, 0.5 far out
  gpx::Heightmap bump(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      float dx = (x - 32) / 8.f, dy = (y - 32) / 8.f;
      bump.at(x, y) = std::exp(-(dx * dx + dy * dy));
    }
  auto re = run("RelativeElevation", bump, [](gpx::Node &n) {
    n.attrs.find("radius")->i = 12;
  }, "mask");
  CHECK(re.at(32, 32) > 0.9f, "the peak reads high relative to its area");
  CHECK(std::fabs(re.at(4, 4) - 0.5f) < 0.05f, "flat ground reads neutral");

  // MeanShift flattens a gentle two-level blend into plateaus but keeps
  // the levels apart
  {
    gpx::Heightmap soft(N, N);
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x)
        soft.at(x, y) = std::clamp((x - 28) / 8.f, 0.f, 1.f);
    auto ms = run("MeanShift", soft, [](gpx::Node &n) {
      n.attrs.find("iterations")->i = 5;
    }, "output");
    CHECK(ms.at(4, 32) < 0.05f && ms.at(60, 32) > 0.95f,
          "the two plateaus survive mode seeking");
  }

  // SetBorders pins the rim and leaves the middle alone
  {
    gpx::Heightmap flat(N, N, 0.8f);
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node("SetBorders", 0, 0);
    n->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(flat);
    gpx::NodeRegistry::instance().find("SetBorders")->compute(*n);
    const gpx::Heightmap &sb = *n->port("output", gpx::PortDir::Out)->hmap;
    CHECK(sb.at(0, 32) == 0.f, "the border sits at the level");
    CHECK(sb.at(32, 32) == 0.8f, "the interior is untouched");
  }

  // SelectBlobs lights a bump of its size and ignores broad structure
  {
    gpx::Heightmap b(N, N);
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) {
        float dx = (x - 32) / 3.f, dy = (y - 32) / 3.f;
        b.at(x, y) = std::exp(-(dx * dx + dy * dy)) + 0.3f;
      }
    auto bl = run("SelectBlobs", b, [](gpx::Node &n) {
      n.attrs.find("size")->f = 0.1f;
    }, "mask");
    CHECK(bl.at(32, 32) > 0.5f, "the bump is found");
    CHECK(bl.at(4, 4) < 0.1f, "flat ground is not");
  }

  // DetailEqualizer at unit gains must reconstruct the input exactly
  {
    gpx::Heightmap noisy(N, N);
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x)
        noisy.at(x, y) = ((x * 13 + y * 7) % 11) * 0.09f + x / (float)N;
    auto eq = run("DetailEqualizer", noisy, [](gpx::Node &) {}, "output");
    float worst = 0;
    for (size_t i = 0; i < eq.v.size(); ++i)
      worst = std::max(worst, std::fabs(eq.v[i] - noisy.v[i]));
    CHECK(worst < 1e-4f, "unit gains reconstruct the input exactly");
    // killing the fine band leaves a smoother surface
    auto smooth2 = run("DetailEqualizer", noisy, [](gpx::Node &n) {
      n.attrs.find("fine")->f = 0.f;
    }, "output");
    double var_in = 0, var_out = 0;
    for (int y = 1; y < N; ++y)
      for (int x = 1; x < N; ++x) {
        var_in += std::fabs(noisy.at(x, y) - noisy.at(x - 1, y));
        var_out += std::fabs(smooth2.at(x, y) - smooth2.at(x - 1, y));
      }
    CHECK(var_out < var_in * 0.6, "zeroing the fine band calms the surface");
  }

  // Convolve: sharpen preserves a flat field, sobel X reads a ramp's slope
  {
    gpx::Heightmap flat(N, N, 0.6f);
    auto sh2 = run("Convolve", flat, [](gpx::Node &) {}, "output");
    CHECK(std::fabs(sh2.at(32, 32) - 0.6f) < 1e-5f,
          "sharpen leaves a flat field flat");
    gpx::Heightmap rmp(N, N);
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) rmp.at(x, y) = x / (float)N;
    auto sx = run("Convolve", rmp, [](gpx::Node &n) {
      n.attrs.find("kernel")->i = 3;
    }, "output");
    CHECK(std::fabs(sx.at(32, 32) - 8.f / N) < 1e-4f,
          "sobel X reads the ramp's slope");
    auto cu = run("Convolve", rmp, [](gpx::Node &n) {
      n.attrs.find("kernel")->i = 5; // custom, default text = sharpen
    }, "output");
    CHECK(std::fabs(cu.at(32, 32) - rmp.at(32, 32)) < 1e-5f,
          "the typed kernel parses and applies");
  }

  // KMeans with two flat levels and k=2 must recover the split exactly
  {
    gpx::Heightmap two(N, N);
    for (int y = 0; y < N; ++y)
      for (int x = 32; x < N; ++x) two.at(x, y) = 1.f;
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node("KMeans", 0, 0);
    n->attrs.find("k")->i = 2;
    n->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(two);
    gpx::NodeRegistry::instance().find("KMeans")->compute(*n);
    const gpx::Heightmap &mlow = *n->port("mask A", gpx::PortDir::Out)->hmap;
    // interior cells (the boundary column carries slope, its own feature)
    CHECK(mlow.at(4, 32) == 1.f && mlow.at(60, 32) == 0.f,
          "mask A is the low zone");
    gpx::Heightmap keep = *n->port("clusters", gpx::PortDir::Out)->hmap;
    gpx::NodeRegistry::instance().find("KMeans")->compute(*n);
    CHECK(n->port("clusters", gpx::PortDir::Out)->hmap->v == keep.v,
          "clustering is bit-identical across computes");
  }

  // MakeTileable: opposite edges must meet (wrap continuity)
  {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node("Noise", 0, 0);
    gpx::Node *t = g.add_node("MakeTileable", 0, 0);
    g.add_link(n->id, "output", t->id, "input");
    g.evaluate();
    const gpx::Heightmap &a = *t->port("output", gpx::PortDir::Out)->hmap;
    float worst = 0;
    for (int y = 0; y < a.h; ++y)
      worst = std::max(worst, std::fabs(a.at(0, y) - a.at(a.w - 1, y)));
    for (int x = 0; x < a.w; ++x)
      worst = std::max(worst, std::fabs(a.at(x, 0) - a.at(x, a.h - 1)));
    // one texel apart on a wrapped surface: the seam can be no worse than a
    // step the signal takes anywhere in the interior (ridge creases included)
    float gmax = 0;
    for (int y = 1; y < a.h - 2; ++y)
      for (int x = 1; x < a.w - 2; ++x) {
        gmax = std::max(gmax, std::fabs(a.at(x + 1, y) - a.at(x, y)));
        gmax = std::max(gmax, std::fabs(a.at(x, y + 1) - a.at(x, y)));
      }
    CHECK(worst <= gmax + 1e-6f, "the wrap seam is no rougher than the interior");
    // and the raw input's seam must have been genuinely worse than a texel step
    const gpx::Heightmap &raw = *n->port("output", gpx::PortDir::Out)->hmap;
    float raw_seam = 0;
    for (int y = 0; y < raw.h; ++y)
      raw_seam = std::max(raw_seam,
                          std::fabs(raw.at(0, y) - raw.at(raw.w - 1, y)));
    CHECK(worst < raw_seam, "tiling reduced the seam");
  }

  // DirectionalBlur along x smears a dot into a horizontal streak
  gpx::Heightmap dot(N, N);
  dot.at(32, 32) = 1.f;
  auto db = run("DirectionalBlur", dot, [](gpx::Node &n) {
    n.attrs.find("length")->f = 0.1f;
  }, "output");
  CHECK(db.at(36, 32) > 0.f && db.at(32, 36) == 0.f,
        "the streak follows the direction and not the perpendicular");
}

static void test_flood() {
  std::printf("standing water...\n");
  const int N = 64;
  gpx::Heightmap in(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) in.at(x, y) = x / (float)N; // ramp to the east
  // an enclosed pit high on the ramp: deep, but walled off from the sea
  for (int y = 30; y < 34; ++y)
    for (int x = 50; x < 54; ++x) in.at(x, y) = 0.05f;

  auto run = [&](int mode) {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node("Flood", 0, 0);
    n->attrs.find("level")->f = 0.3f;
    n->attrs.find("mode")->i = mode;
    n->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(in);
    gpx::NodeRegistry::instance().find("Flood")->compute(*n);
    return std::pair<gpx::Heightmap, gpx::Heightmap>(
        *n->port("water_mask", gpx::PortDir::Out)->hmap,
        *n->port("output", gpx::PortDir::Out)->hmap);
  };

  auto [edge_mask, edge_out] = run(1);
  CHECK(edge_mask.at(2, 32) == 1.f, "the low west side floods from the edge");
  CHECK(edge_mask.at(52, 32) == 0.f, "the walled pit stays dry");
  CHECK(edge_mask.at(62, 32) == 0.f, "high ground stays dry");
  CHECK(edge_out.at(2, 32) > in.at(2, 32), "the surface rises to the level");

  auto [any_mask, any_out] = run(0);
  (void)any_out;
  CHECK(any_mask.at(52, 32) == 1.f, "everywhere-below mode floods the pit too");

  // point-seeded: a spring inside the walled pit floods only the pit
  {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *sc = g.add_node("ScatterPoints", 0, 0);
    gpx::Node *fd = g.add_node("Flood", 0, 0);
    fd->attrs.find("level")->f = 0.3f;
    fd->attrs.find("mode")->i = 2;
    fd->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(in);
    auto pts = std::make_shared<gpx::PointCloud>();
    pts->add(52.f / N, 32.f / N, 1.f);
    sc->port("points", gpx::PortDir::Out)->pts = pts;
    g.add_link(sc->id, "points", fd->id, "sources");
    gpx::NodeRegistry::instance().find("Flood")->compute(*fd);
    const gpx::Heightmap &m = *fd->port("water_mask", gpx::PortDir::Out)->hmap;
    CHECK(m.at(52, 32) == 1.f, "the spring floods its own basin");
    CHECK(m.at(2, 32) == 0.f, "unconnected low ground stays dry");
  }

  // white noise: deterministic, spans 0..1, mean near a half
  {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *wn = g.add_node("WhiteNoise", 0, 0);
    wn->attrs.find("post_remap")->b = false;
    gpx::NodeRegistry::instance().find("WhiteNoise")->compute(*wn);
    const gpx::Heightmap &a = *wn->port("output", gpx::PortDir::Out)->hmap;
    double mean = 0;
    for (float v : a.v) mean += v;
    mean /= a.v.size();
    CHECK(mean > 0.45 && mean < 0.55, "white noise is centered");
    gpx::Heightmap keep = a;
    gpx::NodeRegistry::instance().find("WhiteNoise")->compute(*wn);
    CHECK(wn->port("output", gpx::PortDir::Out)->hmap->v == keep.v,
          "white noise is bit-identical across computes");
  }
}

static void test_morphology() {
  std::printf("morphology...\n");
  const int N = 64;
  auto run = [&](const char *type, const gpx::Heightmap &in,
                 std::function<void(gpx::Node &)> tune) {
    gpx::Graph g;
    g.resolution = N;
    gpx::Node *n = g.add_node(type, 0, 0);
    tune(*n);
    n->port("input", gpx::PortDir::In)->hmap =
        std::make_shared<gpx::Heightmap>(in);
    gpx::NodeRegistry::instance().find(type)->compute(*n);
    const gpx::Port *op = n->ports[1].dir == gpx::PortDir::Out
                              ? &n->ports[1]
                              : n->port(n->ports.back().name, gpx::PortDir::Out);
    for (const gpx::Port &p : n->ports)
      if (p.dir == gpx::PortDir::Out && p.hmap) return *p.hmap;
    (void)op;
    return gpx::Heightmap(1, 1);
  };

  // a single lit pixel dilated with a square element becomes a square
  gpx::Heightmap dot(N, N);
  dot.v[(size_t)(N / 2) * N + N / 2] = 1.f;
  auto d = run("Morphology", dot, [](gpx::Node &n) {
    n.attrs.find("op")->i = 0;
    n.attrs.find("radius")->i = 3;
    n.attrs.find("shape")->i = 0;
  });
  float sum = 0;
  for (float v : d.v) sum += v;
  CHECK(sum == 49.f, "square dilation of a point covers (2r+1)^2 cells");

  // closing fills a small hole in a solid block
  gpx::Heightmap block(N, N, 1.f);
  block.v[(size_t)(N / 2) * N + N / 2] = 0.f;
  auto c = run("Morphology", block, [](gpx::Node &n) {
    n.attrs.find("op")->i = 3;
    n.attrs.find("radius")->i = 2;
    n.attrs.find("shape")->i = 0;
  });
  CHECK(c.v[(size_t)(N / 2) * N + N / 2] == 1.f, "closing fills the hole");

  // AreaRemove: a big blob survives, a lone pixel does not
  gpx::Heightmap blobs(N, N);
  for (int y = 10; y < 20; ++y)
    for (int x = 10; x < 20; ++x) blobs.v[(size_t)y * N + x] = 1.f;
  blobs.v[(size_t)40 * N + 40] = 1.f;
  auto ar = run("AreaRemove", blobs, [](gpx::Node &n) {
    n.attrs.find("min_area")->f = 0.005f; // 20 cells at 64x64
  });
  CHECK(ar.v[(size_t)15 * N + 15] == 1.f, "the big blob is kept");
  CHECK(ar.v[(size_t)40 * N + 40] == 0.f, "the lone pixel is dropped");

  // SkeletonDistance: 1 on a bar's centerline, 0 at its edge
  {
    gpx::Heightmap bar2(N, N);
    for (int y = 24; y < 40; ++y)
      for (int x = 8; x < 56; ++x) bar2.at(x, y) = 1.f;
    auto sd = run("SkeletonDistance", bar2, [](gpx::Node &) {});
    // the discrete skeleton may sit one cell off the exact middle
    CHECK(sd.at(32, 32) > 0.8f, "the centerline reads near 1");
    CHECK(sd.at(32, 24) < 0.2f, "the shape's edge reads near 0");
    CHECK(sd.at(2, 2) == 0.f, "outside the shape reads 0");
  }

  // Skeleton: a thick bar thins but stays connected end to end
  gpx::Heightmap bar(N, N);
  for (int y = 28; y < 36; ++y)
    for (int x = 8; x < 56; ++x) bar.v[(size_t)y * N + x] = 1.f;
  auto sk = run("Skeleton", bar, [](gpx::Node &) {});
  float total = 0;
  for (float v : sk.v) total += v;
  CHECK(total > 30.f && total < 8.f * 48.f * 0.5f,
        "the skeleton is much thinner than the bar but not empty");
  // every column the bar spans still has skeleton in it (connectivity proxy)
  bool covered = true;
  // ends erode by about the half-thickness, so test the interior span
  for (int x = 16; x < 48; ++x) {
    bool any = false;
    for (int y = 0; y < N; ++y) any = any || sk.v[(size_t)y * N + x] > 0.5f;
    covered = covered && any;
  }
  CHECK(covered, "the skeleton spans the bar's full length");
}

static void test_points_domain() {
  std::printf("point-cloud domain...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *sc = g.add_node("ScatterPoints", 0, 0);
  gpx::Node *rx = g.add_node("PointsRelax", 0, 0);
  gpx::Node *fl = g.add_node("PointsFilter", 0, 0);
  gpx::Node *st = g.add_node("PointsToMask", 0, 0);
  gpx::Node *sd = g.add_node("PointsSDF", 0, 0);
  CHECK(sc && rx && fl && st && sd, "points nodes all register");
  sc->attrs.find("count")->i = 200;
  auto compute = [](gpx::Node *n) {
    gpx::NodeRegistry::instance().find(n->type)->compute(*n);
  };

  // scatter: deterministic, in range, correct count
  compute(sc);
  const gpx::Port *sp = sc->port("points", gpx::PortDir::Out);
  CHECK(sp && sp->pts && sp->pts->size() == 200, "scatter makes 200 points");
  for (size_t i = 0; i < sp->pts->size(); ++i)
    CHECK(sp->pts->x[i] >= 0.f && sp->pts->x[i] <= 1.f &&
              sp->pts->y[i] >= 0.f && sp->pts->y[i] <= 1.f,
          "points stay on the tile");
  gpx::PointCloud first = *sp->pts;
  compute(sc);
  CHECK(sp->pts->x == first.x && sp->pts->y == first.y &&
            sp->pts->v == first.v,
        "scatter is bit-identical across computes");

  // spaced mode honours min distance
  sc->attrs.find("mode")->i = 2;
  sc->attrs.find("min_dist")->f = 0.05f;
  compute(sc);
  {
    const gpx::PointCloud &p = *sp->pts;
    float worst = 1e9f;
    for (size_t a = 0; a < p.size(); ++a)
      for (size_t b = a + 1; b < p.size(); ++b) {
        float dx = p.x[a] - p.x[b], dy = p.y[a] - p.y[b];
        worst = std::min(worst, dx * dx + dy * dy);
      }
    CHECK(p.size() > 20, "spaced mode still yields points");
    CHECK(worst >= 0.05f * 0.05f - 1e-6f, "spaced points keep min distance");
  }
  sc->attrs.find("mode")->i = 0;
  compute(sc);

  // relax spreads points: worst pair distance should not shrink
  g.add_link(sc->id, "points", rx->id, "points");
  auto min_d2 = [](const gpx::PointCloud &p) {
    float worst = 1e9f;
    for (size_t a = 0; a < p.size(); ++a)
      for (size_t b = a + 1; b < p.size(); ++b) {
        float dx = p.x[a] - p.x[b], dy = p.y[a] - p.y[b];
        worst = std::min(worst, dx * dx + dy * dy);
      }
    return worst;
  };
  float before = min_d2(*sp->pts);
  compute(rx);
  const gpx::Port *rp = rx->port("points", gpx::PortDir::Out);
  CHECK(rp && rp->pts && rp->pts->size() == sp->pts->size(),
        "relax keeps every point");
  CHECK(min_d2(*rp->pts) >= before, "relaxation never worsens the tightest pair");

  // filter: a half mask keeps roughly half
  auto mask = std::make_shared<gpx::Heightmap>(64, 64);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) mask->v[(size_t)y * 64 + x] = x < 32 ? 0.f : 1.f;
  g.add_link(sc->id, "points", fl->id, "points");
  fl->port("mask", gpx::PortDir::In)->hmap = mask;
  fl->attrs.find("band")->v2[0] = 0.75f;
  fl->attrs.find("band")->v2[1] = 1.f;
  compute(fl);
  const gpx::Port *fp = fl->port("points", gpx::PortDir::Out);
  CHECK(fp && fp->pts && fp->pts->size() > 0 &&
            fp->pts->size() < sp->pts->size(),
        "mask filter removes some points");
  for (size_t i = 0; i < fp->pts->size(); ++i)
    CHECK(fp->pts->x[i] >= 0.5f - 1.f / 64, "filtered points obey the mask");

  // stamp: mask peaks at a point, zero far away
  g.add_link(sc->id, "points", st->id, "points");
  compute(st);
  const gpx::Port *mp = st->port("mask", gpx::PortDir::Out);
  CHECK(mp && mp->hmap && mp->hmap->w == 64, "stamp rasterizes at graph res");
  float mx = *std::max_element(mp->hmap->v.begin(), mp->hmap->v.end());
  CHECK(mx > 0.9f, "stamped kernels reach amplitude");

  // value distributions: power law skews small, uniform does not
  {
    sc->attrs.find("value_dist")->i = 1;
    sc->attrs.find("dist_shape")->f = 3.f;
    compute(sc);
    double mean = 0;
    for (float v : sp->pts->v) mean += v;
    mean /= sp->pts->size();
    CHECK(mean < 0.35, "power-law values skew toward small");
    sc->attrs.find("value_dist")->i = 0;
    compute(sc);
  }

  // merge, shuffle, set-values
  {
    auto A = std::make_shared<gpx::PointCloud>();
    A->add(0.2f, 0.2f, 1.f);
    A->add(0.8f, 0.8f, 1.f);
    auto B = std::make_shared<gpx::PointCloud>();
    B->add(0.21f, 0.2f, 2.f); // within 0.05 of an A point
    B->add(0.5f, 0.5f, 2.f);
    gpx::Node *mg = g.add_node("PointsMerge", 0, 0);
    mg->port("points A", gpx::PortDir::In)->pts = A;
    mg->port("points B", gpx::PortDir::In)->pts = B;
    mg->attrs.find("min_dist")->f = 0.05f;
    compute(mg);
    const gpx::PointCloud &m2 = *mg->port("points", gpx::PortDir::Out)->pts;
    CHECK(m2.size() == 3, "merge keeps A and drops the crowding B point");

    gpx::Node *sh = g.add_node("PointsShuffle", 0, 0);
    g.add_link(sc->id, "points", sh->id, "points");
    compute(sh);
    const gpx::PointCloud &shp = *sh->port("points", gpx::PortDir::Out)->pts;
    CHECK(shp.size() == sp->pts->size() && shp.x != sp->pts->x,
          "shuffle keeps every point but changes the order");

    gpx::Node *sv = g.add_node("PointsSetValues", 0, 0);
    auto ramp = std::make_shared<gpx::Heightmap>(64, 64);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) ramp->v[(size_t)y * 64 + x] = x / 63.f;
    g.add_link(sc->id, "points", sv->id, "points");
    sv->port("source", gpx::PortDir::In)->hmap = ramp;
    compute(sv);
    const gpx::PointCloud &svp = *sv->port("points", gpx::PortDir::Out)->pts;
    bool follows = svp.size() > 0;
    for (size_t i = 0; i < svp.size(); ++i)
      follows = follows && std::fabs(svp.v[i] - svp.x[i]) < 0.06f;
    CHECK(follows, "values follow the sampled ramp");
  }

  // SDF: zero at a point cell, grows away from it
  g.add_link(sc->id, "points", sd->id, "points");
  compute(sd);
  const gpx::Port *dp = sd->port("distance", gpx::PortDir::Out);
  CHECK(dp && dp->hmap, "sdf output exists");
  float dmn = *std::min_element(dp->hmap->v.begin(), dp->hmap->v.end());
  float dmx = *std::max_element(dp->hmap->v.begin(), dp->hmap->v.end());
  CHECK(dmn == 0.f && dmx > 0.2f, "distance is 0 at seeds and grows outward");
}

static void test_path_carve() {
  std::printf("path carving...\n");
  const int N = 64;
  gpx::Heightmap in(N, N);
  for (float &v : in.v) v = 0.5f; // flat, so every change is the path's doing

  auto run = [&](const char *pts, float depth, int profile,
                 float width) -> std::pair<gpx::Heightmap, gpx::Heightmap> {
    gpx::Graph g;
    gpx::Node *n = g.add_node("PathCarve", 0, 0);
    n->attrs.find("points")->s = pts;
    n->attrs.find("depth")->f = depth;
    n->attrs.find("profile")->i = profile;
    n->attrs.find("width")->f = width;
    n->attrs.find("smooth")->i = 0;
    n->attrs.find("post_remap")->b = false;
    gpx::Port *pin = n->port("input", gpx::PortDir::In);
    auto hm = std::make_shared<gpx::Heightmap>(in);
    if (pin) pin->hmap = hm;
    gpx::NodeRegistry::instance().find("PathCarve")->compute(*n);
    const gpx::Port *op = n->port("output", gpx::PortDir::Out);
    const gpx::Port *mp = n->port("path_mask", gpx::PortDir::Out);
    return {op && op->hmap ? *op->hmap : gpx::Heightmap(1, 1),
            mp && mp->hmap ? *mp->hmap : gpx::Heightmap(1, 1)};
  };

  // a straight horizontal cut through the middle
  {
    auto [out, mask] = run("0.1,0.5  0.9,0.5", 0.1f, 0, 0.08f);
    CHECK(out.w == N, "output produced");
    const size_t mid = (size_t)(N / 2) * N + N / 2;
    CHECK(out.v[mid] < 0.41f && out.v[mid] > 0.39f,
          "the centre of the cut reaches full depth");
    CHECK(mask.v[mid] > 0.95f, "the path mask is 1 on the line");
    // far from the line, untouched
    const size_t far = (size_t)5 * N + N / 2;
    CHECK(out.v[far] == 0.5f, "beyond the width nothing moves");
    CHECK(mask.v[far] == 0.f, "and the mask is empty there");
    // falloff: closer to the line is deeper
    const size_t near1 = (size_t)(N / 2 - 2) * N + N / 2;
    const size_t near2 = (size_t)(N / 2 - 4) * N + N / 2;
    CHECK(out.v[near1] < out.v[near2], "the profile falls off with distance");
  }

  // a wall is the same shape upward
  {
    auto [out, mask] = run("0.1,0.5  0.9,0.5", -0.1f, 0, 0.08f);
    (void)mask;
    const size_t mid = (size_t)(N / 2) * N + N / 2;
    CHECK(out.v[mid] > 0.59f, "negative depth raises a wall");
  }

  // a diagonal cut is continuous - no gaps where the rasterisation steps
  {
    auto [out, mask] = run("0.1,0.1  0.9,0.9", 0.1f, 1, 0.05f);
    (void)mask;
    int shallow = 0;
    for (int i = 12; i < N - 12; ++i) {
      // walk the diagonal; every cell on it must be carved deep
      const size_t idx = (size_t)i * N + i;
      if (out.v[idx] > 0.45f) ++shallow;
    }
    CHECK(shallow == 0, "a diagonal path carves without gaps");
  }

  // deterministic
  {
    auto [a1, m1] = run("0.2,0.8  0.5,0.4  0.8,0.7", 0.08f, 2, 0.06f);
    auto [a2, m2] = run("0.2,0.8  0.5,0.4  0.8,0.7", 0.08f, 2, 0.06f);
    CHECK(a1.v == a2.v && m1.v == m2.v, "bit-identical across runs");
  }

  // two points minimum, said plainly
  {
    gpx::Graph g;
    gpx::Node *n = g.add_node("PathCarve", 0, 0);
    n->attrs.find("points")->s = "0.5,0.5";
    gpx::Port *pin = n->port("input", gpx::PortDir::In);
    auto hm = std::make_shared<gpx::Heightmap>(in);
    if (pin) pin->hmap = hm;
    gpx::NodeRegistry::instance().find("PathCarve")->compute(*n);
    CHECK(!n->error.empty(), "one point reports an error instead of guessing");
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
  test_thread_count_determinism();
  test_camera_math();
  test_ai_spec();
  test_terrain_effects();
  test_sculpt_layer();
  test_planet_math();
  test_cellular();
  test_depression_fill();
  test_distance_and_filters();
  test_curve_and_shapes();
  test_path_carve();
  test_points_domain();
  test_morphology();
  test_flood();
  test_local_filters();
  test_noise_variants();
  test_selectors();
  test_path_nodes();
  test_landform();
  test_points_io();
  test_dla();
  test_quilt();
  test_basalt();
  test_terrain_metrics();
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
  test_buffer_budget();
  if (g_failures == 0) {
    std::printf("ALL ENGINE TESTS PASSED\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}








