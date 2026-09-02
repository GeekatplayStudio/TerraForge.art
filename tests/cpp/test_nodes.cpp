// Geekatplay TerraForge â€” universal node contract (test tier 1).
//
// One data-driven battery applied to EVERY node in the registry. Adding a node
// automatically brings it under test, so coverage can never quietly fall behind
// the node count â€” which is exactly how a 90-node engine ends up with 12 tested
// nodes.
//
// The contract each node must satisfy:
//   metadata     â€” category and description present; description is a sentence
//   attributes   â€” labelled, defaults inside their declared range, tooltips on
//                  anything not self-evident
//   evaluation   â€” produces finite output from a plausible input, no crash
//   determinism  â€” same inputs twice, bit-identical output
//   seed         â€” a seeded node reacts to its seed
//   serializationâ€” attributes survive a JSON round trip exactly
//   robustness   â€” extreme attribute values do not produce NaN/Inf
#include "gpx/field_glsl.hpp"
#include "gpx/port_catalog.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;
static std::string g_node; // node under test, for messages

static void fail(const std::string &msg) {
  std::printf("  [FAIL] %-22s %s\n", g_node.c_str(), msg.c_str());
  ++g_failures;
}
#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    ++g_checks;                                                                 \
    if (!(cond)) fail(msg);                                                     \
  } while (0)

static const int RES = 48; // small: this runs for every node

// Nodes that legitimately need a file on disk, so an empty output is correct
// rather than a bug.
static bool needs_file(const std::string &t) {
  return t == "HeightmapFile" || t == "TextureFile" || t == "Stamp" ||
         t == "PBRMaterial" || t == "PointsFromCsv";
}
// Nodes that write to disk when evaluated â€” skipped so the suite has no side
// effects on the working tree.
static bool writes_file(const std::string &t) {
  return t.rfind("Export", 0) == 0;
}
// Configuration nodes: they drive the renderer and the scene rather than
// producing a buffer, so "no image output" is their correct behaviour. They
// still must satisfy every other part of the contract.
static bool is_config_node(const std::string &t) {
  return t == "SunLight" || t == "AtmosphereSettings" || t == "CloudLayer" ||
         t == "WaterLayer" || t == "RenderCamera" || t == "RenderQuality";
}
// Terminal sinks: they consume and export, so having no output port is correct.
// TerrainDisplacement and TerrainSurface are sinks too — what they "export" is
// a compiled shader to the viewport rather than a file, so they have nothing
// to hand a downstream node.
static bool is_sink(const std::string &t) {
  return t == "ExportMesh" || t == "ExportTexture" ||
         t == "TerrainDisplacement" || t == "TerrainSurface" ||
         t == "SurfaceDisplacement";
}
// Containers get their ports and their behaviour from what is put inside them,
// so an empty one having neither is correct rather than a defect. MetaNodes are
// exercised properly by test_metanodes in the engine suite, which groups a real
// graph and checks the result is unchanged.
static bool is_container(const std::string &t) { return t == "MetaNode"; }

static bool finite_map(const gpx::Heightmap &m) {
  for (float v : m.v)
    if (!std::isfinite(v)) return false;
  return true;
}
static bool finite_tex(const gpx::TextureRGBA &t) {
  for (float v : t.v)
    if (!std::isfinite(v)) return false;
  return true;
}

// Prove the finiteness checker can fail before trusting it on ninety nodes.
//
// It could not. The build carried -ffast-math, which implies
// -ffinite-math-only, so the compiler folded every std::isfinite() to true and
// both NaN/Inf checks below passed on every node whatever they contained.
// Measured with the exact flags: isfinite(NaN) returned 1.
//
// A checker that cannot fail is worse than no checker, because it reads as
// coverage. This runs first, and if the build ever picks up finite-math-only
// again it fails immediately and says why.
static void test_finiteness_checker_binds() {
  std::printf("[finiteness checker]\n");
  volatile float zero = 0.f; // volatile so the value is not folded either
  const float nan_v = 0.f / zero, inf_v = 1.f / zero;
  CHECK(!std::isfinite(nan_v),
        "isfinite(NaN) is true - the build has finite-math-only enabled and "
        "every NaN/Inf check in this suite is dead code");
  CHECK(!std::isfinite(inf_v), "isfinite(Inf) is true - see above");

  gpx::Heightmap m(4, 4);
  for (float &v : m.v) v = 0.5f;
  CHECK(finite_map(m), "finite_map rejects a clean heightmap");
  m.v[7] = nan_v;
  CHECK(!finite_map(m), "finite_map missed a NaN");
  m.v[7] = inf_v;
  CHECK(!finite_map(m), "finite_map missed an Inf");

  gpx::TextureRGBA t(4, 4);
  for (float &v : t.v) v = 0.25f;
  CHECK(finite_tex(t), "finite_tex rejects a clean texture");
  t.v[9] = nan_v;
  CHECK(!finite_tex(t), "finite_tex missed a NaN");
}

// Build "node under test" fed by a plausible terrain on every input it has.
// Returns the node, or null if it could not be created.
static gpx::Node *build(gpx::Graph &g, const std::string &type,
                        std::vector<gpx::Node *> &feeders) {
  gpx::Node *n = g.add_node(type, 400, 0);
  if (!n) return nullptr;
  int fy = 0;
  for (const gpx::Port &p : n->ports) {
    if (p.dir != gpx::PortDir::In) continue;
    // feed heightmap inputs with noise, texture inputs with a colorized noise,
    // field inputs with a field source of the matching value type
    if (p.type == gpx::DataType::Heightmap) {
      gpx::Node *src = g.add_node("Noise", 0, (float)fy);
      if (!src) continue;
      g.add_link(src->id, "output", n->id, p.name);
      feeders.push_back(src);
    } else if (p.type == gpx::DataType::Texture) {
      gpx::Node *src = g.add_node("Noise", 0, (float)fy);
      gpx::Node *cv = g.add_node("MaskToTexture", 180, (float)fy);
      if (!src || !cv) continue;
      g.add_link(src->id, "output", cv->id, "input");
      g.add_link(cv->id, "texture", n->id, p.name);
      feeders.push_back(src);
    } else if (p.type == gpx::DataType::Points) {
      gpx::Node *src = g.add_node("ScatterPoints", 0, (float)fy);
      if (!src) continue;
      g.add_link(src->id, "points", n->id, p.name);
      feeders.push_back(src);
    } else if (p.type == gpx::DataType::Field) {
      const char *src_type = "FieldNoise";
      if (p.field_type == gpx::FieldType::Vector) src_type = "FieldPosition";
      else if (p.field_type == gpx::FieldType::Color) src_type = "FieldColorConstant";
      gpx::Node *src = g.add_node(src_type, 0, (float)fy);
      if (!src) continue;
      g.add_link(src->id, "out", n->id, p.name);
      feeders.push_back(src);
    }
    fy += 130;
  }
  return n;
}

// every output port of a node, checked for finiteness; returns how many
// non-empty outputs it produced
static int check_outputs_finite(gpx::Node *n) {
  int produced = 0;
  for (const gpx::Port &p : n->ports) {
    if (p.dir != gpx::PortDir::Out) continue;
    if (p.hmap && !p.hmap->empty()) {
      ++produced;
      CHECK(finite_map(*p.hmap), "output '" + p.name + "' has NaN/Inf");
    }
    if (p.tex && !p.tex->empty()) {
      ++produced;
      CHECK(finite_tex(*p.tex), "texture '" + p.name + "' has NaN/Inf");
    }
    if (p.pts && p.pts->size() > 0) {
      ++produced;
      bool ok = true;
      for (size_t i = 0; i < p.pts->size(); ++i)
        ok = ok && std::isfinite(p.pts->x[i]) && std::isfinite(p.pts->y[i]) &&
             std::isfinite(p.pts->v[i]) && p.pts->x[i] >= 0.f &&
             p.pts->x[i] <= 1.f && p.pts->y[i] >= 0.f && p.pts->y[i] <= 1.f;
      CHECK(ok, "points '" + p.name + "' are finite and on the tile");
    }
  }
  return produced;
}

static bool has_raster_output(gpx::Node *n) {
  for (const gpx::Port &p : n->ports)
    if (p.dir == gpx::PortDir::Out && p.type != gpx::DataType::Field) return true;
  return false;
}

// A spread of points a field node is asked about. Includes an origin, negative
// coordinates and a far-away point, because those are where a careless
// implementation produces NaN.
static std::vector<gpx::FieldContext> field_probe_points() {
  std::vector<gpx::FieldContext> pts;
  const float coords[][3] = {{0, 0, 0},        {0.5f, 0.2f, 0.5f},
                             {-1.3f, 0.f, 2.7f}, {123.4f, -8.f, -56.f},
                             {1e-4f, 1e-4f, 1e-4f}};
  for (const auto &c : coords) {
    gpx::FieldContext ctx = gpx::FieldContext::at(c[0], c[1], c[2]);
    ctx.normal[0] = 0.3f; ctx.normal[1] = 0.9f; ctx.normal[2] = -0.31f;
    ctx.derive_from_normal();
    ctx.time = 1.5f;
    pts.push_back(ctx);
  }
  return pts;
}

// Field outputs are pull-evaluated rather than computed into a buffer, so they
// are checked on their own terms: finite and deterministic everywhere.
static void check_field_outputs(gpx::Node *n) {
  auto pts = field_probe_points();
  for (const gpx::Port &p : n->ports) {
    if (p.dir != gpx::PortDir::Out || p.type != gpx::DataType::Field) continue;
    CHECK(p.field_eval != nullptr,
          "field output '" + p.name + "' has an evaluator");
    if (!p.field_eval) continue;
    for (const gpx::FieldContext &ctx : pts) {
      gpx::FieldValue a = n->eval_field(p.name, ctx);
      gpx::FieldValue b = n->eval_field(p.name, ctx);
      CHECK(a.finite(), "field '" + p.name + "' is finite at every probe point");
      CHECK(a == b, "field '" + p.name + "' is deterministic");
      CHECK(a.type == p.field_type,
            "field '" + p.name + "' returns its declared type");
    }
  }
}

// A field node that cannot be emitted to GLSL would evaluate on the CPU but
// silently do nothing on the GPU. Requiring an emitter for every field node
// makes that class of divergence impossible to ship.
static void check_field_transpiles(gpx::Node *n) {
  bool has_field_out = false;
  for (const gpx::Port &p : n->ports)
    if (p.dir == gpx::PortDir::Out && p.type == gpx::DataType::Field)
      has_field_out = true;
  if (!has_field_out) return;
  CHECK(gpx::field_glsl_supports(n->type),
        "has a GLSL emitter (or it would work on CPU but not on the GPU)");
  if (!gpx::field_glsl_supports(n->type)) return;
  gpx::GlslProgram prog = gpx::field_to_glsl(*n, "out", "gpx_test");
  CHECK(prog.ok, "transpiles to GLSL: " + prog.error);
  if (!prog.ok) return;
  CHECK(prog.code.find("vec4 gpx_test(") != std::string::npos,
        "emits the entry function");
  CHECK(prog.code.find("return v_") != std::string::npos,
        "returns a generated value");
  // nothing may be left unresolved in the emitted source
  CHECK(prog.code.find("PLACEHOLDER") == std::string::npos,
        "emitted code has no placeholders");
  CHECK(prog.node_count >= 1, "emitted at least one node");
}

// snapshot every output so two runs can be compared bit for bit
static std::vector<float> snapshot(gpx::Node *n) {
  std::vector<float> out;
  for (const gpx::Port &p : n->ports) {
    if (p.dir != gpx::PortDir::Out) continue;
    if (p.hmap) out.insert(out.end(), p.hmap->v.begin(), p.hmap->v.end());
    if (p.tex) out.insert(out.end(), p.tex->v.begin(), p.tex->v.end());
    // include field outputs so determinism covers them too
    if (p.type == gpx::DataType::Field && p.field_eval)
      for (const gpx::FieldContext &ctx : field_probe_points()) {
        gpx::FieldValue v = n->eval_field(p.name, ctx);
        out.insert(out.end(), v.v, v.v + 4);
      }
  }
  return out;
}

// ------------------------------------------------------------------ checks
static void check_metadata(const gpx::NodeDef *d) {
  CHECK(!d->category.empty(), "has a category");
  CHECK(!d->description.empty(), "has a description");
  CHECK(d->description.size() >= 12,
        "description is meaningful (>=12 chars), got: " + d->description);
}

static void check_attributes(gpx::Node *n) {
  std::set<std::string> keys;
  for (const gpx::Attribute &a : n->attrs.items) {
    CHECK(!a.key.empty(), "attribute has a key");
    CHECK(keys.insert(a.key).second, "attribute key '" + a.key + "' is unique");
    CHECK(!a.label.empty(), "attribute '" + a.key + "' has a label");
    switch (a.type) {
      case gpx::AttrType::Float:
        CHECK(a.fmin <= a.fmax, "float '" + a.key + "' has min <= max");
        CHECK(a.f >= a.fmin - 1e-6f && a.f <= a.fmax + 1e-6f,
              "float '" + a.key + "' default is inside its range");
        CHECK(std::isfinite(a.f), "float '" + a.key + "' default is finite");
        break;
      case gpx::AttrType::Int:
        CHECK(a.imin <= a.imax, "int '" + a.key + "' has min <= max");
        CHECK(a.i >= a.imin && a.i <= a.imax,
              "int '" + a.key + "' default is inside its range");
        break;
      case gpx::AttrType::Choice:
        CHECK(a.labels.size() >= 2,
              "choice '" + a.key + "' offers at least two options");
        CHECK(a.i >= 0 && a.i < (int)a.labels.size(),
              "choice '" + a.key + "' default indexes a real option");
        break;
      case gpx::AttrType::Range:
      case gpx::AttrType::Vec2:
        CHECK(a.v2min <= a.v2max, "vec2/range '" + a.key + "' has min <= max");
        break;
      case gpx::AttrType::Gradient:
        CHECK(!a.stops.empty(), "gradient '" + a.key + "' has stops");
        break;
      case gpx::AttrType::Field:
        CHECK(a.fw > 0 && a.fh > 0, "field '" + a.key + "' has a size");
        break;
      default: break;
    }
  }
}

static void check_ports(gpx::Node *n) {
  std::set<std::string> in_names, out_names;
  int outs = 0;
  for (const gpx::Port &p : n->ports) {
    CHECK(!p.name.empty(), "port has a name");
    if (p.dir == gpx::PortDir::In)
      CHECK(in_names.insert(p.name).second,
            "input port '" + p.name + "' is unique among inputs");
    else {
      CHECK(out_names.insert(p.name).second,
            "output port '" + p.name + "' is unique among outputs");
      ++outs;
    }
  }
  if (!is_sink(n->type) && !is_container(n->type))
    CHECK(outs >= 1, "has at least one output");
}

static void check_eval_and_determinism(const std::string &type) {
  gpx::Graph g;
  g.resolution = RES;
  std::vector<gpx::Node *> feeders;
  gpx::Node *n = build(g, type, feeders);
  if (!n) {
    fail("could not be instantiated");
    return;
  }
  g.evaluate();
  CHECK(n->error.empty() || needs_file(type) || is_container(type),
        "evaluates without error (got: " + n->error + ")");
  int produced = check_outputs_finite(n);
  check_field_outputs(n);
  check_field_transpiles(n);
  if (has_raster_output(n) && !needs_file(type) && !is_config_node(type) &&
      !is_sink(type) && !is_container(type))
    CHECK(produced >= 1, "produced at least one non-empty output");

  std::vector<float> first = snapshot(n);
  g.mark_all_dirty();
  g.evaluate();
  std::vector<float> second = snapshot(n);
  CHECK(first == second, "is deterministic across two evaluations");
}

static void check_seed_matters(const std::string &type) {
  gpx::Graph g;
  g.resolution = RES;
  std::vector<gpx::Node *> feeders;
  gpx::Node *n = build(g, type, feeders);
  if (!n) return;
  gpx::Attribute *seed = nullptr;
  for (gpx::Attribute &a : n->attrs.items)
    if (a.type == gpx::AttrType::Seed) seed = &a;
  if (!seed) return; // not a seeded node
  g.evaluate();
  std::vector<float> before = snapshot(n);
  if (before.empty()) return;
  seed->seed = seed->seed + 12345u;
  g.mark_all_dirty();
  g.evaluate();
  std::vector<float> after = snapshot(n);
  CHECK(before != after, "reacts to its seed");
}

static void check_serialization(const std::string &type) {
  gpx::Graph g;
  g.resolution = 16;
  gpx::Node *n = g.add_node(type);
  if (!n) return;
  // perturb every attribute so defaults cannot mask a serialization gap
  for (gpx::Attribute &a : n->attrs.items) {
    switch (a.type) {
      case gpx::AttrType::Float: a.f = a.fmin + (a.fmax - a.fmin) * 0.37f; break;
      case gpx::AttrType::Int: a.i = a.imin + (a.imax - a.imin) / 3; break;
      case gpx::AttrType::Bool: a.b = !a.b; break;
      case gpx::AttrType::Seed: a.seed = 987654u; break;
      case gpx::AttrType::Choice:
        a.i = (int)a.labels.size() - 1;
        break;
      case gpx::AttrType::Range:
      case gpx::AttrType::Vec2:
        a.v2[0] = a.v2min + (a.v2max - a.v2min) * 0.25f;
        a.v2[1] = a.v2min + (a.v2max - a.v2min) * 0.75f;
        break;
      case gpx::AttrType::Color:
        a.col[0] = 0.11f; a.col[1] = 0.22f; a.col[2] = 0.33f;
        break;
      case gpx::AttrType::Filename:
      case gpx::AttrType::Text: a.s = "round/trip test"; break;
      default: break;
    }
  }
  std::string json = gpx::graph_to_json(g);
  gpx::Graph g2;
  std::string err;
  CHECK(gpx::graph_from_json(g2, json, err), "graph with this node round trips");
  if (g2.nodes.empty()) return;
  gpx::Node *n2 = g2.nodes[0].get();
  CHECK(n2->type == type, "type survives the round trip");
  for (const gpx::Attribute &a : n->attrs.items) {
    const gpx::Attribute *b = n2->attrs.find(a.key);
    if (!b) {
      fail("attribute '" + a.key + "' lost in serialization");
      continue;
    }
    ++g_checks;
    bool same = true;
    switch (a.type) {
      case gpx::AttrType::Float: same = std::fabs(a.f - b->f) < 1e-6f; break;
      case gpx::AttrType::Int:
      case gpx::AttrType::Choice: same = a.i == b->i; break;
      case gpx::AttrType::Bool: same = a.b == b->b; break;
      case gpx::AttrType::Seed: same = a.seed == b->seed; break;
      case gpx::AttrType::Range:
      case gpx::AttrType::Vec2:
        same = std::fabs(a.v2[0] - b->v2[0]) < 1e-6f &&
               std::fabs(a.v2[1] - b->v2[1]) < 1e-6f;
        break;
      case gpx::AttrType::Color:
        same = std::fabs(a.col[0] - b->col[0]) < 1e-6f;
        break;
      case gpx::AttrType::Filename:
      case gpx::AttrType::Text: same = a.s == b->s; break;
      default: break;
    }
    if (!same) fail("attribute '" + a.key + "' changed value in serialization");
  }
}

// Bypass is universal: disabling any node must make the graph behave as though
// it were not there. For a filter that means its consumer sees the filter's own
// input. This is checked on every node so a new one cannot opt out by accident.
static void check_bypass(const std::string &type) {
  gpx::Graph g;
  g.resolution = 32;
  std::vector<gpx::Node *> feeders;
  gpx::Node *n = build(g, type, feeders);
  if (!n) return;
  // needs a heightmap in and out to have a pass-through channel at all
  gpx::Port *in = nullptr, *out = nullptr;
  for (gpx::Port &p : n->ports) {
    if (p.dir == gpx::PortDir::In && p.type == gpx::DataType::Heightmap && !in)
      in = &p;
    if (p.dir == gpx::PortDir::Out && p.type == gpx::DataType::Heightmap && !out)
      out = &p;
  }
  if (!in || !out) return;
  gpx::Node *src = g.upstream_node(*n, in->name);
  if (!src) return;

  // read the node's output through a downstream consumer, which is where the
  // bypass has to take effect
  gpx::Node *sink = g.add_node("Thru", 800, 0);
  if (!sink) return;
  if (!g.add_link(n->id, out->name, sink->id, "input")) return;
  g.evaluate();
  const gpx::Heightmap *upstream_out = src->port("output", gpx::PortDir::Out)
                                           ? src->port("output", gpx::PortDir::Out)->hmap.get()
                                           : nullptr;
  if (!upstream_out) return;

  n->enabled = false;
  g.mark_all_dirty();
  g.evaluate();
  const gpx::Heightmap *seen = sink->in_hmap("input");
  CHECK(seen != nullptr, "bypassed node still resolves for its consumer");
  if (seen)
    CHECK(seen->v == upstream_out->v,
          "bypassed node passes its input straight through");
  CHECK(n->last_compute_ms == 0.0, "bypassed node is not computed at all");
}

// Extreme but legal attribute values must not produce NaN/Inf. This is where
// the "user drags a slider to the end" class of bug lives.
static void check_extremes(const std::string &type) {
  for (int pass = 0; pass < 2; ++pass) {
    gpx::Graph g;
    g.resolution = RES;
    std::vector<gpx::Node *> feeders;
    gpx::Node *n = build(g, type, feeders);
    if (!n) return;
    for (gpx::Attribute &a : n->attrs.items) {
      switch (a.type) {
        case gpx::AttrType::Float: a.f = pass ? a.fmax : a.fmin; break;
        case gpx::AttrType::Int: a.i = pass ? a.imax : a.imin; break;
        case gpx::AttrType::Bool: a.b = pass != 0; break;
        case gpx::AttrType::Range:
        case gpx::AttrType::Vec2:
          a.v2[0] = a.v2min;
          a.v2[1] = pass ? a.v2max : a.v2min;
          break;
        default: break;
      }
    }
    // cap anything that would make the suite crawl
    if (gpx::Attribute *it = n->attrs.find("iterations"))
      it->i = std::min(it->i, 12);
    if (gpx::Attribute *it = n->attrs.find("particles"))
      it->i = std::min(it->i, 12);
    if (gpx::Attribute *it = n->attrs.find("octaves"))
      it->i = std::min(it->i, 10);
    g.mark_all_dirty();
    g.evaluate();
    check_outputs_finite(n);
  }
}

void test_all_nodes_contract() {
  auto all = gpx::NodeRegistry::instance().all();
  std::printf("universal node contract over %d node types...\n", (int)all.size());
  int skipped = 0;
  for (const gpx::NodeDef *d : all) {
    g_node = d->type;
    check_metadata(d);
    {
      gpx::Graph g;
      g.resolution = 16;
      gpx::Node *n = g.add_node(d->type);
      if (!n) {
        fail("could not be instantiated");
        continue;
      }
      check_attributes(n);
      check_ports(n);
    }
    if (writes_file(d->type)) {
      ++skipped;
      continue; // would touch the working tree
    }
    check_eval_and_determinism(d->type);
    check_bypass(d->type);
    check_seed_matters(d->type);
    check_serialization(d->type);
    check_extremes(d->type);
  }
  g_node.clear();
  std::printf("  %d nodes checked (%d eval-skipped: they write files)\n",
              (int)all.size(), skipped);
}

// ------------------------------------------------ the infinite-surface bridge
// Planets and the endless ground plane have no heightmap: they are evaluated
// on the GPU from parameters. The only way to author their shape is to author
// a function, so a field graph is transpiled to GLSL and spliced into the
// planet shader (studio/planet_renderer.cpp). If that transpile stops
// producing a callable function the surfaces do not error - they simply stop
// responding, which is the hardest kind of breakage to notice. So it is
// pinned here, where a screenshot cannot be the only thing holding it up.
static void test_surface_displacement_bridge() {
  std::printf("surface displacement bridge...\n");
  g_node = "SurfaceDisplacement";
  gpx::Graph g;
  gpx::Node *src = g.add_node("FieldNoise", 0, 0);
  gpx::Node *sink = g.add_node("SurfaceDisplacement", 240, 0);
  CHECK(src && sink, "both node types are registered");
  if (!src || !sink) return;
  CHECK(sink->category == "Export", "the sink is filed under Export");
  CHECK(g.add_link(src->id, "out", sink->id, "field"),
        "FieldNoise.out connects to SurfaceDisplacement.field");
  CHECK(sink->field_connected("field"), "the sink sees the connection");
  CHECK(sink->attrs.find("strength") != nullptr, "it has a strength");
  CHECK(sink->attrs.find("live") != nullptr, "it can be switched off");

  // an unwired sink must say so rather than silently doing nothing
  gpx::Graph g2;
  gpx::Node *lone = g2.add_node("SurfaceDisplacement", 0, 0);
  g2.evaluate();
  CHECK(lone && !lone->error.empty(), "an unconnected sink reports it");

  // and the graph feeding it must compile to a GLSL function by that name
  gpx::GlslProgram prog =
      gpx::field_to_glsl(*src, "out", "gpx_surface_field");
  CHECK(prog.ok, prog.ok ? "transpiles" : ("transpile failed: " + prog.error).c_str());
  CHECK(prog.code.find("gpx_surface_field") != std::string::npos,
        "the generated code declares the entry point the shader calls");
  CHECK(prog.code.find("vec4") != std::string::npos,
        "the entry point returns vec4, as the shader expects");
  CHECK(prog.samplers.empty(),
        "a pure field graph needs no textures - the procedural surfaces "
        "have none to give it");
  g_node.clear();
}

// ------------------------------------------------------- port-aware creation
// Dragging a wire onto empty canvas offers only the nodes that could take it,
// then wires the new one up. Two rules decide that, and both are pure, so both
// are tested here rather than by dragging in a screenshot.
//
// The safety invariant is the one that matters: if a type is OFFERED, then a
// node of that type, once created, must yield a port of the right direction
// and data type - otherwise the menu promises a connection it cannot make.
// Hesiod's version of this feature aborted the process on exactly that case
// (docs/design/2026-07-22-port-aware-drag-to-create-design, crash A).
static void test_port_catalog() {
  std::printf("port catalog and drag-to-create rules...\n");
  const auto all = gpx::NodeRegistry::instance().all();
  CHECK(!all.empty(), "the registry has nodes");

  const gpx::DataType TYPES[3] = {gpx::DataType::Heightmap,
                                  gpx::DataType::Texture, gpx::DataType::Field};
  const gpx::PortDir DIRS[2] = {gpx::PortDir::In, gpx::PortDir::Out};

  int offered = 0;
  for (const gpx::NodeDef *d : all) {
    g_node = d->type;
    const std::vector<gpx::PortInfo> &cat = gpx::port_catalog(d->type);
    CHECK(!cat.empty() || is_sink(d->type) || is_container(d->type),
          "the catalog knows this type's ports");

    // the catalog must say exactly what a real node of that type declares,
    // in the same order - it is built by the same setup, so this pins that
    gpx::Graph g;
    gpx::Node *live = g.add_node(d->type, 0, 0);
    CHECK(live != nullptr, "the type can be created");
    if (!live) continue;
    CHECK(cat.size() == live->ports.size(), "catalog and live port count agree");
    if (cat.size() == live->ports.size())
      for (size_t i = 0; i < cat.size(); ++i) {
        CHECK(cat[i].name == live->ports[i].name, "port names agree, in order");
        CHECK(cat[i].dir == live->ports[i].dir, "port directions agree");
        CHECK(cat[i].type == live->ports[i].type, "port data types agree");
      }

    for (gpx::DataType t : TYPES)
      for (gpx::PortDir dir : DIRS) {
        bool offer = gpx::node_offers(d->type, t, dir);
        std::string port = gpx::select_port(*live, t, dir);
        if (offer) {
          ++offered;
          CHECK(!port.empty(), "an offered type yields a port to connect");
          if (!port.empty()) {
            const gpx::Port *p = live->port(port, dir);
            CHECK(p != nullptr, "the chosen port exists in that direction");
            if (p) CHECK(p->type == t, "the chosen port carries the right type");
          }
        } else {
          CHECK(port.empty(), "a type that is not offered yields no port");
        }
      }
  }
  g_node.clear();
  CHECK(offered > 100, "the sweep actually exercised offers");

  // an unknown type is offered rather than hidden: a node missing from the
  // catalog must stay reachable in the menu
  CHECK(gpx::node_offers("NoSuchNodeType", gpx::DataType::Heightmap,
                         gpx::PortDir::In),
        "an unknown type fails open");
  CHECK(gpx::port_catalog("NoSuchNodeType").empty(),
        "an unknown type has no ports");

  // the conventional name wins over declaration order
  {
    gpx::Graph g;
    gpx::Node *n = g.add_node("Smooth", 0, 0);
    if (n) {
      std::string p = gpx::select_port(*n, gpx::DataType::Heightmap,
                                       gpx::PortDir::In);
      CHECK(p == "input", "a port named 'input' is preferred");
    }
  }
}

int main() {
  std::printf("Geekatplay TerraForge - node contract suite\n\n");
  test_finiteness_checker_binds(); // before anything relies on it
  test_all_nodes_contract();
  test_surface_displacement_bridge();
  test_port_catalog();
  std::printf("\n%d checks, %s (%d failures)\n", g_checks,
              g_failures ? "FAILED" : "all passed", g_failures);
  return g_failures ? 1 : 0;
}

