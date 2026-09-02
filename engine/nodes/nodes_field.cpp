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

// --------------------------------------------------------------- cellular
// Worley noise: scatter a point in every lattice cell, and ask how far away
// the nearest ones are. It is the second basis pattern of procedural terrain
// after fBm, and the one that produces everything fBm cannot - cracked mud,
// basalt columns, scree fields, tectonic plates, crater fields.
//
// The four outputs are four landscapes from the same cells. Note which way up
// each one is, because it is easy to get backwards: F1 puts a pit at every
// cell point, F2-F1 puts a dome there and its zeros along the seams, and the
// cell value paints each cell one flat height, which is how you get plates.
REGISTER_NODE(
    FieldVoronoi, "Field Noise",
    "Cellular (Worley) noise - cracks, plates, scree and crater fields",
    [](Node &n) {
      n.add_field_in("position", FieldType::Vector, true);
      add_seed(n.attrs, "seed", "Seed", 1, "Cells");
      add_float(n.attrs, "frequency", "Cell size", 6.f, 0.01f, 200.f, "Cells")
          .tooltip = "How many cells fit across a unit of space.\n"
                     "Low values give continent-sized plates, high values\n"
                     "give gravel.";
      add_float(n.attrs, "jitter", "Jitter", 1.f, 0.f, 1.f, "Cells")
          .tooltip = "How far each cell's point may wander from its centre.\n"
                     "0 is a perfect grid; 1 is fully irregular.";
      add_int(n.attrs, "octaves", "Octaves", 1, 1, 6, "Cells")
          .tooltip = "Stacks the cells at rising frequency and falling\n"
                     "weight, the way fBm stacks noise: continents of\n"
                     "plates with gravel in the cracks. 1 is the plain\n"
                     "pattern.";
      add_choice(n.attrs, "metric", "Cell shape",
                 {"Round (Euclidean)", "Diamond (Manhattan)",
                  "Square (Chebyshev)"},
                 0, "Cells")
          .tooltip = "The distance the cells are measured with, which is\n"
                     "what decides their silhouette.";
      add_choice(n.attrs, "output", "Pattern",
                 {"Distance to nearest (F1)", "Distance to second (F2)",
                  "Distance to the seam (F2 - F1)", "Flat cell value"},
                 0, "Pattern")
          .tooltip =
          "F1 is zero at each cell's own point and rises outward: cell\n"
          "centres become pits and the seams between them become ridges.\n"
          "Crater fields, dimpled rock.\n\n"
          "F2 is the same one cell further out - rounder, smoother swells.\n\n"
          "F2 - F1 is zero exactly on the seam between two cells and\n"
          "highest at the centre: domes with sharp creases between them.\n"
          "Invert it and the seams become the cracks.\n\n"
          "Flat cell value gives each cell one random height - plates,\n"
          "terraces, tectonic blocks.";
      add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 8.f, "Pattern");
      add_float(n.attrs, "offset", "Offset", 0.f, -4.f, 4.f, "Pattern");
      add_bool(n.attrs, "invert", "Invert", false, "Pattern")
          .tooltip = "Turns pits into domes, and walls into channels.";
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float p[3];
        self.in_field("position", ctx,
                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
            .as_vector(p);
        const float base_f = self.attrs.get_f("frequency", 6.f);
        const uint32_t seed = self.attrs.get_seed("seed");
        const float jitter = self.attrs.get_f("jitter", 1.f);
        const int metric = self.attrs.get_choice("metric");
        const int mode = self.attrs.get_choice("output");
        const int oct = std::clamp(self.attrs.get_i("octaves", 1), 1, 6);
        // fBm of cells: each octave doubles the frequency and halves the
        // weight, with its own seed so the layers do not line up. Mirrored
        // in the GLSL emitter, which unrolls the same sum.
        float v = 0.f, amp = 1.f, norm = 0.f, freq = base_f;
        for (int o = 0; o < oct; ++o) {
          float f1, f2, id;
          planet::pl_cell(p[0] * freq, p[1] * freq, p[2] * freq,
                          seed + (uint32_t)o * 101u, jitter, metric, f1, f2,
                          id);
          float term;
          switch (mode) {
            case 1: term = f2; break;
            case 2: term = f2 - f1; break;
            case 3: term = id; break;
            default: term = f1; break;
          }
          v += term * amp;
          norm += amp;
          amp *= 0.5f;
          freq *= 2.03f;
        }
        v = norm > 0.f ? v / norm : 0.f;
        v = v * self.attrs.get_f("amplitude", 1.f) +
            self.attrs.get_f("offset", 0.f);
        if (self.attrs.get_b("invert", false)) v = 1.f - v;
        return FieldValue(v);
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


