// Geekatplay TerraForge - do the sliders do anything?
//
// Step 5 of the node audit programme asks that every node be exercised in a
// working workflow. Doing that by hand for 227 nodes is a fortnight, and the
// interesting failure is narrow enough to ask mechanically: a parameter that
// is drawn, documented, tooltipped, saved and loaded, and that changes
// nothing at all when you move it.
//
// That failure is silent by construction. The node evaluates, the output is
// finite, the contract battery is green, the manual describes the control,
// and the person moving the slider concludes their terrain is wrong rather
// than that the slider is. It is the exact defect a test suite built around
// "does it crash" cannot see.
//
// So: build each node fed by real upstream nodes, evaluate it, move one
// parameter, evaluate again, and see whether anything moved. A choice is
// tried on every one of its options, because the second branch of a switch
// nobody uses is where this hides.
//
// This reports; it does not fail - the same discipline as tools/node_audit.
// A parameter can legitimately do nothing in the default configuration (a
// blend amount with no mask connected, a mode whose branch needs an input
// this harness does not provide), so the output is a list to read, not a
// gate to switch on. The findings that survive a look get an entry in
// KNOWN_INERT with the reason, and what is left is the report.
//
//     cmake --build build --target param_audit && build/param_audit
#include "gpx/node_graph.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace gpx;

namespace {

// Small, because this runs thousands of evaluations - but not too small. At
// 48 a radius of 0.01 and a radius of 0.037 are both "one pixel", and every
// radius control in the catalogue reported itself dead.
constexpr int RES = 128;

// A number that stands for everything the node produced. Not a hash - the
// difference has to be reportable, because "it changed" and "it changed by
// 1e-9" are different answers and only one of them means the slider works.
double fingerprint(Node *n) {
  double s = 0;
  int k = 1;
  for (const Port &p : n->ports) {
    if (p.dir != PortDir::Out) continue;
    if (p.hmap)
      for (float v : p.hmap->v) s += std::isfinite(v) ? v * (k++ % 97) : 0.0;
    if (p.tex)
      for (float v : p.tex->v) s += std::isfinite(v) ? v * (k++ % 89) : 0.0;
    if (p.pts && p.pts->size()) {
      // Every channel, not just position. Half of an EcosystemLayer's
      // controls write to scale, yaw, tilt, tint or phase, and a fingerprint
      // that reads only x and y reports all of them dead.
      const PointCloud &c = *p.pts;
      auto add = [&](const std::vector<float> &v, double weight) {
        for (float f : v) s += std::isfinite(f) ? f * weight : 0.0;
      };
      add(c.x, 3.0);
      add(c.y, 5.0);
      add(c.v, 7.0);
      add(c.sx, 11.0);
      add(c.sy, 13.0);
      add(c.sz, 17.0);
      add(c.yaw, 19.0);
      add(c.tilt, 23.0);
      add(c.tint, 29.0);
      add(c.phase, 31.0);
      add(c.radius, 37.0);
      add(c.offset, 41.0);
      for (uint16_t sp : c.species) s += sp * 43.0;
      s += (double)c.size() * 1e6;
    }
  }
  return s;
}

// Feed every input from a plausible source, the same way the contract battery
// does. A node judged on its unconnected behaviour would report every
// parameter as inert.
Node *build(Graph &g, const std::string &type) {
  Node *n = g.add_node(type, 400, 0);
  if (!n) return nullptr;
  float fy = 0;
  uint32_t feed_seed = 1;
  // Each feeder gets its own seed. Two inputs fed identical terrain make a
  // Switch look like it has no switch and a Compare like it has no
  // tolerance: whichever way the node chooses, the answer is the same.
  auto sow = [&](Node *s) {
    if (s)
      if (Attribute *a = s->attrs.find("seed")) a->seed = feed_seed * 2654435761u;
    ++feed_seed;
    return s;
  };
  std::vector<Port> ports = n->ports; // add_node may reallocate
  for (const Port &p : ports) {
    if (p.dir != PortDir::In) continue;
    if (p.type == DataType::Heightmap) {
      if (Node *s = sow(g.add_node("Noise", 0, fy)))
        g.add_link(s->id, "output", n->id, p.name);
    } else if (p.type == DataType::Texture) {
      Node *s = sow(g.add_node("Noise", 0, fy));
      Node *c = g.add_node("MaskToTexture", 180, fy);
      if (s && c) {
        g.add_link(s->id, "output", c->id, "input");
        g.add_link(c->id, "texture", n->id, p.name);
      }
    } else if (p.type == DataType::Points) {
      if (Node *s = sow(g.add_node("ScatterPoints", 0, fy)))
        g.add_link(s->id, "points", n->id, p.name);
    } else if (p.type == DataType::Field) {
      if (Node *s = g.add_node("FieldNoise", 0, fy))
        for (const Port &q : s->ports)
          if (q.dir == PortDir::Out && q.type == DataType::Field) {
            g.add_link(s->id, q.name, n->id, p.name);
            break;
          }
    }
    fy += 120;
  }
  return g.find_node(n->id);
}

// Reads a file, so the harness cannot exercise it honestly.
bool needs_a_file(const std::string &t) {
  return t == "HeightmapFile" || t == "ImageTexture" || t == "PointsFromCsv" ||
         t == "ImportObject" || t == "PBRMaterial" || t == "ExportMesh" ||
         t == "ExportTexture" || t == "ExportHeightmap" || t == "ExportPoints" ||
         t == "RenderOutput" || t == "MetaNode" || t == "TextureFile" ||
         t == "MeshFile" || t == "TerrainImprint";
}

// Parameters that legitimately move nothing here, with the reason. This list
// is the difference between a report somebody acts on and one they mute.
const std::map<std::string, const char *> KNOWN_INERT = {
    {"*:seed", "a node with no randomness in it keeps a seed for the family's "
               "sake; moving it is meant to do nothing"},
    {"*:name", "a label, not a parameter"},
    {"*:plan", "the placeholder text on a [Planned] node"},
    {"*:alias", "a handle for scripts to address the node by"},
};

// Nodes whose parameters are read somewhere this harness cannot see. Not an
// excuse - a statement of what the tool's reach is. MaterialOutput's ninety
// shading controls are read by material_params_from() when the renderer
// uploads its uniforms, and no amount of graph evaluation will move a
// heightmap because the index of refraction changed.
const std::map<std::string, const char *> READ_ELSEWHERE = {
    {"MaterialOutput",
     "read by material_params_from() at render time, not by the graph"},
    {"MaterialStack",
     "the layer blending rules are applied by the studio's material stack, "
     "not during graph evaluation"},
};

const char *known_inert(const std::string &type, const std::string &key) {
  auto it = KNOWN_INERT.find(type + ":" + key);
  if (it != KNOWN_INERT.end()) return it->second;
  it = KNOWN_INERT.find("*:" + key);
  return it != KNOWN_INERT.end() ? it->second : nullptr;
}

// What might be switching a parameter off. Not just the choice boxes: the
// enable toggles ("By altitude", "By slope") are bools, and a species count
// is an int that decides whether sp4_presence means anything at all. Missing
// those was the difference between 244 findings and a handful.
int gate_variants(const Attribute &a) {
  switch (a.type) {
    case AttrType::Bool: return 1;
    case AttrType::Choice: return (int)a.labels.size();
    case AttrType::Int: return a.imax > a.imin ? 1 : 0;
    // An amount sitting at zero is the commonest gate of all: "decay
    // influence 0" is what makes decay reach and decay falloff do nothing,
    // and neither of them is broken.
    case AttrType::Float:
      return (std::fabs(a.f - a.fmin) < 1e-6f && a.fmax > a.fmin) ? 1 : 0;
    default: return 0;
  }
}

bool set_gate(Attribute &a, int opt, std::string &label) {
  switch (a.type) {
    case AttrType::Bool:
      a.b = !a.b;
      label = a.b ? "on" : "off";
      return true;
    case AttrType::Choice:
      if (opt == a.i || opt >= (int)a.labels.size()) return false;
      a.i = opt;
      label = "'" + a.labels[(size_t)opt] + "'";
      return true;
    case AttrType::Int:
      if (a.i == a.imax) return false;
      a.i = a.imax;
      label = std::to_string(a.imax);
      return true;
    case AttrType::Float: {
      const float v = a.fmin + 0.5f * (a.fmax - a.fmin);
      if (std::fabs(v - a.f) < 1e-9f) return false;
      a.f = v;
      label = std::to_string(v);
      return true;
    }
    default:
      return false;
  }
}

// Move one parameter somewhere else in its own range. Returns false when
// there is nowhere else to go - a choice with one option, a float whose range
// is a point - which is a different finding and reported as one.
bool perturb(Attribute &a, int variant, std::string &what) {
  auto far_from = [](float v, float lo, float hi) {
    const float a1 = lo + 0.73f * (hi - lo), a2 = lo + 0.19f * (hi - lo);
    return std::fabs(a1 - v) > std::fabs(a2 - v) ? a1 : a2;
  };
  switch (a.type) {
    case AttrType::Float: {
      if (a.fmax - a.fmin < 1e-9f) return false;
      const float v = far_from(a.f, a.fmin, a.fmax);
      what = std::to_string(a.f) + " -> " + std::to_string(v);
      a.f = v;
      return true;
    }
    case AttrType::Int: {
      if (a.imax <= a.imin) return false;
      const int v = a.i == a.imax ? a.imin : a.imax;
      what = std::to_string(a.i) + " -> " + std::to_string(v);
      a.i = v;
      return true;
    }
    case AttrType::Bool:
      what = a.b ? "on -> off" : "off -> on";
      a.b = !a.b;
      return true;
    case AttrType::Choice: {
      // every option, one per variant, because the branch nobody reaches is
      // exactly where a dead switch case lives
      const int n = (int)a.labels.size();
      if (n < 2) return false;
      int v = variant % n;
      if (v == a.idefault) v = (v + 1) % n;
      if (v == a.i) return false;
      what = "'" + a.labels[(size_t)a.i] + "' -> '" + a.labels[(size_t)v] + "'";
      a.i = v;
      return true;
    }
    case AttrType::Seed:
      what = "reseeded";
      a.seed += 0x9E3779B9u;
      return true;
    case AttrType::Range:
    case AttrType::Vec2: {
      if (a.v2max - a.v2min < 1e-9f) return false;
      const float lo = a.v2min + 0.31f * (a.v2max - a.v2min);
      const float hi = a.v2min + 0.62f * (a.v2max - a.v2min);
      if (std::fabs(a.v2[0] - lo) < 1e-6f && std::fabs(a.v2[1] - hi) < 1e-6f)
        return false;
      what = "range moved";
      a.v2[0] = lo;
      a.v2[1] = hi;
      return true;
    }
    case AttrType::Color:
      what = "colour changed";
      a.col[0] = 1.f - a.col[0];
      a.col[1] = a.col[1] * 0.5f + 0.25f;
      a.col[2] = 1.f - a.col[2];
      return true;
    default:
      return false; // Gradient, Filename, Text, Field: not moved by a number
  }
}

} // namespace

int main(int argc, char **argv) {
  const bool verbose = argc > 1 && !std::strcmp(argv[1], "-v");
  std::map<std::string, std::vector<std::string>> inert;
  std::map<std::string, std::vector<std::string>> conditionals;
  std::vector<std::string> unusable;
  int nodes = 0, params = 0, live = 0, excused = 0;

  for (const NodeDef *d : NodeRegistry::instance().all()) {
    if (needs_a_file(d->type)) continue;
    if (d->description.rfind("[Planned]", 0) == 0) continue;
    if (auto it = READ_ELSEWHERE.find(d->type); it != READ_ELSEWHERE.end()) {
      unusable.push_back(d->type + " (" + d->category + ") - " + it->second);
      continue;
    }

    // how many attributes, how many options each choice has, and which of
    // them could be gating another
    std::vector<int> variants, gates;
    {
      Graph g;
      g.resolution = RES;
      Node *n = build(g, d->type);
      if (!n) continue;
      for (const Attribute &a : n->attrs.items) {
        variants.push_back(a.type == AttrType::Choice ? (int)a.labels.size()
                                                      : 1);
        gates.push_back(gate_variants(a));
      }
    }
    if (variants.empty()) continue;
    ++nodes;

    // the untouched result
    double base = 0;
    {
      Graph g;
      g.resolution = RES;
      Node *n = build(g, d->type);
      if (!n) continue;
      g.evaluate();
      base = fingerprint(n);
      // A node that produces no buffer at all configures the scene rather
      // than computing something - a sun, a camera, a cloud layer, a render
      // setting. Its parameters land in the scene, which this harness has no
      // window onto, so every one of them would be reported dead. Saying so
      // is the honest answer; reporting them is not.
      bool produced = false;
      for (const Port &p : n->ports) {
        if (p.dir != PortDir::Out) continue;
        produced = produced || (p.hmap && !p.hmap->empty()) ||
                   (p.tex && !p.tex->empty()) || (p.pts && p.pts->size());
      }
      if (!produced) {
        --nodes;
        unusable.push_back(d->type + " (" + d->category +
                           ") - writes into the scene, not into a buffer");
        continue;
      }
    }

    for (size_t ai = 0; ai < variants.size(); ++ai) {
      bool moved_anything = false, ever_applied = false;
      std::string key, detail;
      for (int v = 0; v < variants[ai] && !moved_anything; ++v) {
        Graph g;
        g.resolution = RES;
        Node *n = build(g, d->type);
        if (!n || ai >= n->attrs.items.size()) break;
        Attribute &a = n->attrs.items[ai];
        key = a.key;
        std::string what;
        if (!perturb(a, v, what)) continue;
        ever_applied = true;
        detail = what;
        g.mark_all_dirty();
        g.evaluate();
        if (fingerprint(n) != base) moved_anything = true;
      }
      if (!ever_applied) continue;
      ++params;
      if (moved_anything) {
        ++live;
        continue;
      }
      if (const char *why = known_inert(d->type, key)) {
        ++excused;
        if (verbose)
          std::printf("  (excused) %s.%s - %s\n", d->type.c_str(), key.c_str(),
                      why);
        continue;
      }
      // Before calling it dead: most parameters that look dead are simply
      // switched off. A softness that only applies to the smooth blend, a
      // sea level that only applies in the relative altitude mode - each is
      // inert in the default configuration and perfectly alive one step
      // away. So the mode switches on the node are tried, one at a time,
      // before anything is reported. Without this the tool named 601
      // parameters and was right about a handful, which is how a report
      // teaches people to close it.
      std::string conditional;
      for (size_t ci = 0; ci < gates.size() && conditional.empty(); ++ci) {
        if (ci == ai) continue;
        for (int opt = 0; opt < gates[ci]; ++opt) {
          Graph g;
          g.resolution = RES;
          Node *n = build(g, d->type);
          if (!n || ai >= n->attrs.items.size()) break;
          std::string label;
          if (!set_gate(n->attrs.items[ci], opt, label)) continue;
          const std::string sw_key = n->attrs.items[ci].key;
          g.mark_all_dirty();
          g.evaluate();
          const double under = fingerprint(n);
          std::string what;
          if (!perturb(n->attrs.items[ai], 0, what)) continue;
          g.mark_all_dirty();
          g.evaluate();
          if (fingerprint(n) != under) {
            conditional = sw_key + "=" + label;
            break;
          }
        }
      }
      if (!conditional.empty()) {
        ++live;
        conditionals[d->type].push_back(key + "  (only with " + conditional +
                                        ")");
        continue;
      }
      inert[d->type].push_back(key + "  (" + detail + ")");
    }
  }

  int total_inert = 0, total_cond = 0;
  for (auto &[t, ks] : inert) total_inert += (int)ks.size();
  for (auto &[t, ks] : conditionals) total_cond += (int)ks.size();
  std::printf("Parameter audit - %d nodes, %d parameters moved\n"
              "  %d changed the output (%d of them only under a mode "
              "switch)\n  %d changed nothing, %d excused by name, "
              "%d nodes not exercised\n\n",
              nodes, params, live, total_cond, total_inert, excused,
              (int)unusable.size());

  if (inert.empty()) {
    std::printf("every parameter that could be moved changed something\n");
  } else {
    std::printf("Moved with no effect on the output:\n");
    for (auto &[type, keys] : inert) {
      std::printf("  %s\n", type.c_str());
      for (const std::string &k : keys) std::printf("      %s\n", k.c_str());
    }
  }

  if (verbose && total_cond) {
    // Not findings. Printed because "this slider needs that switch first" is
    // exactly the sentence a manual should carry and usually does not.
    std::printf("\nAlive, but only in a particular mode:\n");
    for (auto &[type, keys] : conditionals) {
      std::printf("  %s\n", type.c_str());
      for (const std::string &k : keys) std::printf("      %s\n", k.c_str());
    }
  }
  if (verbose && !unusable.empty()) {
    std::printf("\nNot exercised - nothing here can see what they change:\n");
    for (const std::string &s : unusable) std::printf("  %s\n", s.c_str());
  }
  return 0;
}
