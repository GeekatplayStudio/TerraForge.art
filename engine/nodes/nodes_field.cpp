// Geekatplay TerraForge — field domain nodes (P0.1).
//
// These are the resolution-independent half of the graph: each one answers
// "what is the value at this point?" rather than "fill this buffer". They are
// the building blocks Vue calls Function nodes and Terragen calls shaders, and
// they are what makes displacement, infinite terrain and per-point material
// control possible.
//
// Two rules every node here follows, because both are load-bearing:
//   * stateless — eval() reads only the node's attributes and the context, so
//     the same point always gives the same answer and evaluation is trivially
//     parallel and re-entrant.
//   * no resolution — nothing here may know how big a buffer is. The moment a
//     node needs neighbours or iteration it belongs in the raster domain.
//
// The 3D noise deliberately shares gpx::planet's implementation so a planet's
// surface and a graph-authored field agree by construction rather than by
// carefully keeping two copies in step.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/metanode.hpp"
#include "gpx/planet_math.hpp"
#include <json.hpp>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>

namespace gpx {

// ------------------------------------------------------------------ inputs
// Vue's five standard graph inputs (manual p769-770) plus time. These are the
// only way data enters a field graph.
#define FIELD_INPUT_NODE(NAME, DESC, TYPE, BODY)                                \
  REGISTER_NODE(                                                                \
      NAME, "Field Input", DESC,                                                \
      [](Node &n) {                                                             \
        n.add_field_out("out", TYPE,                                            \
                        [](const Node &self, const FieldContext &ctx)           \
                            -> FieldValue { (void)self; (void)ctx; BODY });     \
      },                                                                        \
      [](Node &) {})

FIELD_INPUT_NODE(FieldPosition,
                 "Position of the point being evaluated — the root of most graphs",
                 FieldType::Vector,
                 { return FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]); })

FIELD_INPUT_NODE(FieldNormal, "Direction the surface faces at this point",
                 FieldType::Vector,
                 {
                   return FieldValue::vector(ctx.normal[0], ctx.normal[1],
                                             ctx.normal[2]);
                 })

FIELD_INPUT_NODE(FieldAltitude, "Height of this point above the reference plane",
                 FieldType::Number, { return FieldValue(ctx.altitude); })

FIELD_INPUT_NODE(FieldSlope,
                 "Steepness here: 1 flat, 0 vertical, -1 flat facing down",
                 FieldType::Number, { return FieldValue(ctx.slope); })

FIELD_INPUT_NODE(FieldOrientation,
                 "Compass direction the surface faces, as -1 to 1",
                 FieldType::Number, { return FieldValue(ctx.orientation); })

FIELD_INPUT_NODE(FieldTime, "Current time in seconds — the hook for animation",
                 FieldType::Number, { return FieldValue(ctx.time); })

// --------------------------------------------------------------- constants
REGISTER_NODE(
    FieldConstant, "Field Input", "A fixed number, to feed any field input",
    [](Node &n) {
      add_float(n.attrs, "value", "Value", 0.5f, -1000.f, 1000.f);
      n.add_field_out("out", FieldType::Number,
                      [](const Node &self, const FieldContext &) {
                        return FieldValue(self.attrs.get_f("value", 0.5f));
                      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldColorConstant, "Field Input", "A fixed colour, to feed any colour input",
    [](Node &n) {
      add_color(n.attrs, "color", "Colour", 0.6f, 0.55f, 0.5f);
      n.add_field_out("out", FieldType::Color,
                      [](const Node &self, const FieldContext &) {
                        const Attribute *a = self.attrs.find("color");
                        if (!a) return FieldValue::color(1, 1, 1);
                        return FieldValue::color(a->col[0], a->col[1], a->col[2],
                                                 a->col[3]);
                      });
    },
    [](Node &) {})

// -------------------------------------------------------------------- math
REGISTER_NODE(
    FieldMath, "Field Math", "Combines two values: add, subtract, multiply, and the rest",
    [](Node &n) {
      n.add_field_in("a", FieldType::Number, true);
      n.add_field_in("b", FieldType::Number, true);
      add_choice(n.attrs, "op", "Operation",
                 {"Add", "Subtract", "Multiply", "Divide", "Minimum", "Maximum",
                  "Power", "Modulo", "Absolute difference"},
                 0);
      add_float(n.attrs, "a_default", "A (when unconnected)", 0.f, -100.f, 100.f,
                "Defaults");
      add_float(n.attrs, "b_default", "B (when unconnected)", 1.f, -100.f, 100.f,
                "Defaults");
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float a = self.in_number("a", ctx, self.attrs.get_f("a_default", 0.f));
        float b = self.in_number("b", ctx, self.attrs.get_f("b_default", 1.f));
        switch (self.attrs.get_choice("op")) {
          case 0: return FieldValue(a + b);
          case 1: return FieldValue(a - b);
          case 2: return FieldValue(a * b);
          // guarded so a zero divisor cannot poison the whole graph with NaN
          case 3: return FieldValue(std::fabs(b) > 1e-9f ? a / b : 0.f);
          case 4: return FieldValue(std::min(a, b));
          case 5: return FieldValue(std::max(a, b));
          case 6: return FieldValue(std::pow(std::max(a, 0.f), b));
          case 7: return FieldValue(std::fabs(b) > 1e-9f ? std::fmod(a, b) : 0.f);
          default: return FieldValue(std::fabs(a - b));
        }
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldTrig, "Field Math", "Trigonometry: sine, cosine, tangent and their inverses",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_choice(n.attrs, "fn", "Function",
                 {"Sine", "Cosine", "Tangent", "Arc sine", "Arc cosine",
                  "Arc tangent", "Hyperbolic sine", "Hyperbolic cosine",
                  "Hyperbolic tangent"},
                 0);
      add_bool(n.attrs, "degrees", "Work in degrees", false)
          .tooltip = "Interpret the input (and produce the output of the\n"
                     "inverse functions) in degrees rather than radians.";
      add_float(n.attrs, "scale", "Input scale", 1.f, -32.f, 32.f);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float x = self.in_number("in", ctx, 0.f) * self.attrs.get_f("scale", 1.f);
        bool deg = self.attrs.get_b("degrees");
        if (deg) x *= 0.017453293f;
        float r;
        switch (self.attrs.get_choice("fn")) {
          case 0: r = std::sin(x); break;
          case 1: r = std::cos(x); break;
          // tangent is unbounded at the poles; clamp so one bad point cannot
          // blow out everything downstream
          case 2: r = std::clamp(std::tan(x), -1e4f, 1e4f); break;
          case 3: r = std::asin(std::clamp(x, -1.f, 1.f)); break;
          case 4: r = std::acos(std::clamp(x, -1.f, 1.f)); break;
          case 5: r = std::atan(x); break;
          case 6: r = std::sinh(std::clamp(x, -20.f, 20.f)); break;
          case 7: r = std::cosh(std::clamp(x, -20.f, 20.f)); break;
          default: r = std::tanh(x); break;
        }
        // the inverse functions return an angle, so convert it back
        int fn = self.attrs.get_choice("fn");
        if (deg && fn >= 3 && fn <= 5) r *= 57.29578f;
        return FieldValue(r);
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldRemap, "Field Math", "Rescales a value from one range into another",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_range(n.attrs, "from", "Input range", -1.f, 1.f, -100.f, 100.f);
      add_range(n.attrs, "to", "Output range", 0.f, 1.f, -100.f, 100.f);
      add_bool(n.attrs, "clamp", "Clamp to the output range", true);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float x = self.in_number("in", ctx, 0.f);
        float f0, f1, t0, t1;
        self.attrs.get_range("from", f0, f1);
        self.attrs.get_range("to", t0, t1);
        float d = (f1 - f0);
        float t = std::fabs(d) > 1e-9f ? (x - f0) / d : 0.f;
        float r = t0 + t * (t1 - t0);
        if (self.attrs.get_b("clamp", true))
          r = std::clamp(r, std::min(t0, t1), std::max(t0, t1));
        return FieldValue(r);
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldCurve, "Field Math", "Shapes a value with a curve: gain, bias, step or smoothstep",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_choice(n.attrs, "shape", "Shape",
                 {"Gain (gamma)", "Smoothstep", "Step", "Bias", "Invert"}, 0);
      add_float(n.attrs, "amount", "Amount", 1.f, 0.05f, 8.f);
      add_range(n.attrs, "edges", "Edges", 0.f, 1.f, -4.f, 4.f);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float x = self.in_number("in", ctx, 0.f);
        float e0, e1;
        self.attrs.get_range("edges", e0, e1);
        float amt = self.attrs.get_f("amount", 1.f);
        switch (self.attrs.get_choice("shape")) {
          case 0: return FieldValue(std::pow(std::max(x, 0.f), amt));
          case 1: {
            float t = std::fabs(e1 - e0) > 1e-9f ? (x - e0) / (e1 - e0) : 0.f;
            t = std::clamp(t, 0.f, 1.f);
            return FieldValue(t * t * (3.f - 2.f * t));
          }
          case 2: return FieldValue(x >= e0 ? 1.f : 0.f);
          case 3: {
            // Schlick bias: pushes values toward one end without clipping
            float t = std::clamp(x, 0.f, 1.f);
            float b = std::clamp(amt / 8.f, 0.001f, 0.999f);
            return FieldValue(t / (((1.f / b) - 2.f) * (1.f - t) + 1.f));
          }
          default: return FieldValue(1.f - x);
        }
      });
    },
    [](Node &) {})

REGISTER_NODE(
    FieldMix, "Field Math", "Blends between two inputs by a factor",
    [](Node &n) {
      n.add_field_in("a", FieldType::Number, true);
      n.add_field_in("b", FieldType::Number, true);
      n.add_field_in("factor", FieldType::Number, true);
      add_float(n.attrs, "amount", "Blend (when unconnected)", 0.5f, 0.f, 1.f);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float a = self.in_number("a", ctx, 0.f);
        float b = self.in_number("b", ctx, 1.f);
        float t = std::clamp(
            self.in_number("factor", ctx, self.attrs.get_f("amount", 0.5f)),
            0.f, 1.f);
        return FieldValue(a + (b - a) * t);
      });
    },
    [](Node &) {})

// ------------------------------------------------------------------ vector
REGISTER_NODE(
    FieldVectorOp, "Field Math", "Vector maths: length, dot, cross, normalize, distance",
    [](Node &n) {
      n.add_field_in("a", FieldType::Vector, true);
      n.add_field_in("b", FieldType::Vector, true);
      add_choice(n.attrs, "op", "Operation",
                 {"Length", "Dot product", "Distance", "Normalize (X)",
                  "Cross product (X)"},
                 0);
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float a[3], b[3];
        self.in_field("a", ctx, FieldValue::vector(0, 0, 0)).as_vector(a);
        self.in_field("b", ctx, FieldValue::vector(0, 1, 0)).as_vector(b);
        auto len = [](const float *v) {
          return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        };
        switch (self.attrs.get_choice("op")) {
          case 0: return FieldValue(len(a));
          case 1: return FieldValue(a[0]*b[0] + a[1]*b[1] + a[2]*b[2]);
          case 2: {
            float d[3] = {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
            return FieldValue(len(d));
          }
          case 3: {
            float l = len(a);
            return FieldValue(l > 1e-9f ? a[0] / l : 0.f);
          }
          default: return FieldValue(a[1]*b[2] - a[2]*b[1]);
        }
      });
    },
    [](Node &) {})

// ------------------------------------------------------------------- noise
// The workhorse. Shares gpx::planet's 3D implementation so a planet surface and
// a graph-authored field are the same function, not two that happen to look
// alike.
REGISTER_NODE(
    FieldNoise, "Field Noise", "3D coherent noise — the basis of procedural terrain and texture",
    [](Node &n) {
      n.add_field_in("position", FieldType::Vector, true);
      add_choice(n.attrs, "type", "Type",
                 {"Rolling (fBm)", "Ridged", "Billow"}, 0);
      add_seed(n.attrs, "seed", "Seed", 1, "Noise");
      add_float(n.attrs, "frequency", "Feature scale", 3.f, 0.01f, 200.f, "Noise")
          .tooltip = "How many features fit across a unit of space.\n"
                     "Low values give continents, high values give gravel.";
      add_int(n.attrs, "octaves", "Octaves", 6, 1, 12, "Noise")
          .tooltip = "Levels of detail. Capped by the caller's level-of-detail\n"
                     "budget, so distant points cost less automatically.";
      add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 8.f, "Noise");
      add_float(n.attrs, "offset", "Offset", 0.f, -4.f, 4.f, "Noise");
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float p[3];
        self.in_field("position", ctx,
                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
            .as_vector(p);
        float f = self.attrs.get_f("frequency", 3.f);
        // honour the caller's detail budget: this is how one graph serves both
        // a close-up and a horizon pixel without the author doing anything
        int oct = std::clamp(self.attrs.get_i("octaves", 6), 1,
                             std::max(1, (int)ctx.lod));
        float v = planet::pl_fbm(p[0] * f, p[1] * f, p[2] * f,
                                 self.attrs.get_seed("seed"), oct,
                                 self.attrs.get_choice("type"));
        return FieldValue(v * self.attrs.get_f("amplitude", 1.f) +
                          self.attrs.get_f("offset", 0.f));
      });
    },
    [](Node &) {})

// ------------------------------------------------------------------ colour
REGISTER_NODE(
    FieldGradient, "Field Color", "Turns a number into a colour through a gradient",
    [](Node &n) {
      n.add_field_in("in", FieldType::Number, true);
      add_gradient(n.attrs, "gradient", "Gradient",
                   {{0.f, 0.24f, 0.28f, 0.18f, 1.f},
                    {0.5f, 0.45f, 0.40f, 0.32f, 1.f},
                    {1.f, 0.92f, 0.93f, 0.95f, 1.f}});
      add_range(n.attrs, "range", "Input range", 0.f, 1.f, -4.f, 4.f);
      n.add_field_out("out", FieldType::Color, [](const Node &self,
                                                  const FieldContext &ctx) {
        float x = self.in_number("in", ctx, 0.5f);
        float lo, hi;
        self.attrs.get_range("range", lo, hi);
        float t = std::fabs(hi - lo) > 1e-9f ? (x - lo) / (hi - lo) : 0.f;
        t = std::clamp(t, 0.f, 1.f);
        const Attribute *g = self.attrs.find("gradient");
        if (!g || g->stops.empty()) return FieldValue::color(t, t, t);
        const auto &s = g->stops;
        if (t <= s.front().t)
          return FieldValue::color(s.front().r, s.front().g, s.front().b,
                                   s.front().a);
        for (size_t i = 1; i < s.size(); ++i) {
          if (t <= s[i].t) {
            float span = s[i].t - s[i - 1].t;
            float k = span > 1e-9f ? (t - s[i - 1].t) / span : 0.f;
            return FieldValue::color(s[i - 1].r + (s[i].r - s[i - 1].r) * k,
                                     s[i - 1].g + (s[i].g - s[i - 1].g) * k,
                                     s[i - 1].b + (s[i].b - s[i - 1].b) * k,
                                     s[i - 1].a + (s[i].a - s[i - 1].a) * k);
          }
        }
        return FieldValue::color(s.back().r, s.back().g, s.back().b, s.back().a);
      });
    },
    [](Node &) {})

// ------------------------------------------------------------------ bridges
// Where the two domains meet. Everything the raster half already does —
// erosion above all — stays reachable from a field graph, and vice versa.

REGISTER_NODE(
    Rasterize, "Field Bridge",
    "Bakes a field into a heightmap so raster nodes (erosion, blur) can work on it",
    [](Node &n) {
      n.add_field_in("field", FieldType::Number);
      n.add_out("output");
      add_vec2(n.attrs, "center", "Region centre", 0.5f, 0.5f, -100.f, 100.f,
               "Region");
      add_float(n.attrs, "size", "Region size", 1.f, 0.001f, 100.f, "Region")
          .tooltip = "How much of the field's space this buffer covers.\n"
                     "Smaller values zoom in — the field has no resolution of\n"
                     "its own, so this is what decides the detail you capture.";
      add_float(n.attrs, "height", "Sample height", 0.f, -10.f, 10.f, "Region")
          .tooltip = "The Y plane the field is sampled on, for 3D fields.";
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      if (!n.field_connected("field")) {
        n.error = "input 'field' not connected";
        return;
      }
      int w = out.w, h = out.h;
      float cx, cy;
      n.attrs.get_vec2("center", cx, cy);
      float size = n.attrs.get_f("size", 1.f);
      float hy = n.attrs.get_f("height", 0.f);
      // the buffer's resolution decides the detail budget: asking for more
      // octaves than the grid can hold only produces aliasing
      float lod = std::clamp(std::log2((float)std::max(w, 1)) - 1.f, 1.f, 12.f);
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            FieldContext ctx;
            ctx.pos[0] = cx + (x / float(w - 1) - 0.5f) * size;
            ctx.pos[1] = hy;
            ctx.pos[2] = cy + (y / float(h - 1) - 0.5f) * size;
            ctx.altitude = hy;
            ctx.lod = lod;
            // scene time reaches field graphs here, so an animated field
            // graph rasterizes differently per frame
            ctx.time = n.graph ? n.graph->time : 0.f;
            out.at(x, y) = n.in_field("field", ctx).number();
          }
      });
      apply_post(n, out);
    })

REGISTER_NODE(
    Sample, "Field Bridge",
    "Reads a heightmap as a field, so sculpted or eroded terrain can drive a shader",
    [](Node &n) {
      n.add_in("input");
      add_vec2(n.attrs, "center", "Region centre", 0.5f, 0.5f, -100.f, 100.f,
               "Region");
      add_float(n.attrs, "size", "Region size", 1.f, 0.001f, 100.f, "Region");
      add_float(n.attrs, "scale", "Value scale", 1.f, -8.f, 8.f);
      add_bool(n.attrs, "tile", "Repeat outside the region", false)
          .tooltip = "Off: points outside the buffer clamp to its edge.\n"
                     "On: the buffer tiles infinitely.";
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        const Heightmap *in = self.in_hmap("input");
        if (!in || in->empty()) return FieldValue(0.f);
        float cx, cy;
        self.attrs.get_vec2("center", cx, cy);
        float size = self.attrs.get_f("size", 1.f);
        if (std::fabs(size) < 1e-9f) return FieldValue(0.f);
        float u = (ctx.pos[0] - cx) / size + 0.5f;
        float v = (ctx.pos[2] - cy) / size + 0.5f;
        if (self.attrs.get_b("tile")) {
          u = u - std::floor(u);
          v = v - std::floor(v);
        } else {
          u = std::clamp(u, 0.f, 1.f);
          v = std::clamp(v, 0.f, 1.f);
        }
        return FieldValue(in->sample(u, v) * self.attrs.get_f("scale", 1.f));
      });
    },
    // the buffer arrives through the raster evaluation pass; nothing to compute
    [](Node &n) {
      if (!n.in_hmap("input")) n.error = "input not connected";
    })

// ---------------------------------------------------------------- MetaNode
// A whole sub-graph behind one node. Its ports are created when a selection is
// collapsed (see metanode_group), so the setup here only declares the storage;
// compute loads the inner graph, feeds it the boundary inputs, evaluates it and
// copies the boundary outputs back.
REGISTER_NODE(
    MetaNode, "Group", "A sub-graph collapsed into one node — group, name and reuse",
    [](Node &n) {
      add_text(n.attrs, "inner_graph", "Inner graph", "", "Internal")
          .tooltip = "The encapsulated graph, stored with the project.\n"
                     "Edit it by opening the MetaNode, not by hand.";
      add_text(n.attrs, "published", "Published parameters", "", "Internal")
          .tooltip = "Which inner parameters are exposed on this node.";
      add_text(n.attrs, "note", "Note", "", "Description")
          .tooltip = "What this MetaNode is for — it becomes the tooltip when\n"
                     "the node is reused from the library.";
    },
    [](Node &n) {
      const Attribute *ia = n.attrs.find("inner_graph");
      if (!ia || ia->s.empty()) {
        n.error = "empty MetaNode";
        return;
      }
      Graph inner;
      std::string err;
      if (!metanode_open(n, inner, err)) {
        n.error = "inner graph failed to load: " + err;
        return;
      }
      inner.resolution = n.graph ? n.graph->resolution : inner.resolution;
      metanode_apply_published(n, inner);

      // read the boundary description written when the group was formed
      std::vector<std::tuple<std::string, uint64_t, std::string, bool>> bound;
      try {
        nlohmann::json doc = nlohmann::json::parse(ia->s);
        for (const auto &jb : doc.value("boundary", nlohmann::json::array()))
          bound.emplace_back(jb.value("port", ""), jb.value("inner_node", 0ull),
                             jb.value("inner_port", ""),
                             jb.value("dir", "") == "in");
      } catch (const std::exception &e) {
        n.error = e.what();
        return;
      }

      // Loading renumbers node ids, so the boundary's stored ids are mapped
      // onto the live inner nodes through the one shared helper.
      std::map<uint64_t, Node *> by_saved_id = metanode_id_map(n, inner);

      // inputs: copy this node's incoming buffers onto the inner target ports
      // by substituting a Constant-like source is not needed — the inner node
      // reads through a link, so instead we write directly into a cache port
      for (const auto &[pname, inode, iport, is_in] : bound) {
        if (!is_in) continue;
        auto it = by_saved_id.find(inode);
        if (it == by_saved_id.end()) continue;
        const Heightmap *src = n.in_hmap(pname);
        if (!src) continue;
        // give the inner node a standing input by parking the buffer on a port
        Port *p = it->second->port(iport, PortDir::In);
        if (!p) continue;
        p->hmap = std::make_shared<Heightmap>(*src);
      }

      inner.mark_all_dirty();
      inner.evaluate();

      for (const auto &[pname, inode, iport, is_in] : bound) {
        if (is_in) continue;
        auto it = by_saved_id.find(inode);
        if (it == by_saved_id.end()) continue;
        Port *p = it->second->port(iport, PortDir::Out);
        if (!p) continue;
        if (p->hmap) n.out_hmap(pname) = *p->hmap;
        else if (p->tex) n.out_tex(pname) = *p->tex;
      }
    })

} // namespace gpx


