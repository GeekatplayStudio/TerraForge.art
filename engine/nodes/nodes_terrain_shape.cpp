// Geekatplay TerraForge - TerrainShape: what the piece of ground actually is.
//
// A terrain tile was always the whole square, and the only thing that could
// be done about its border was `post_zero_edges`: one slider that faded the
// rectangle to its lowest value over a fixed smoothstep. That makes a square
// island and nothing else, and it conflates two different questions - how far
// in the fade reaches, and how hard it pulls - into a single number.
//
// This node answers them separately. The outline is a named shape (or a mask
// you supply), its rim can wander instead of being a perfect curve, and the
// edge is three controls rather than one: how far in it reaches, the curve it
// takes from the middle out to the sides, and how far down it goes by the
// time it gets there. See gpx/shapes.hpp for what each one means.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/shapes.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

shape::Params read_shape(const Node &n) {
  shape::Params p;
  p.kind = std::clamp(n.attrs.get_choice("shape"), 0, (int)shape::KIND_COUNT - 1);
  n.attrs.get_vec2("center", p.cx, p.cy);
  float w = 0.f, h = 0.f;
  n.attrs.get_vec2("size", w, h);
  p.rx = std::max(w, 1e-4f) * 0.5f;
  p.ry = std::max(h, 1e-4f) * 0.5f;
  p.corner = n.attrs.get_f("corner", 0.3f);
  p.rotation = n.attrs.get_f("rotation", 0.f);
  p.edge_amount = n.attrs.get_f("edge_amount", 0.f);
  p.edge_scale = n.attrs.get_f("edge_scale", 5.f);
  p.edge_octaves = n.attrs.get_i("edge_octaves", 4);
  p.seed = n.attrs.get_seed("seed");
  p.extent = n.attrs.get_f("extent", 0.3f);
  p.gradient = n.attrs.get_f("gradient", 1.f);
  p.intensity = n.attrs.get_f("intensity", 1.f);
  return p;
}

} // namespace

REGISTER_NODE(
    TerrainShape, "Shape",
    "The outline of the ground - rectangle, round, or your own mask - with a wandering rim and an edge that gives way over a distance you set",
    [](Node &n) {
      n.add_in("input");
      // The escape hatch: any mask can be the outline, which is how a
      // coastline traced from a real map gets in here.
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("mask");

      add_choice(n.attrs, "shape", "Shape",
                 {"Rectangle", "Rounded rectangle", "Round", "Diamond",
                  "From mask"},
                 2, "Shape")
          .tooltip = "Round is an ellipse when the width and height differ.\n"
                     "From mask takes the outline from the mask input and\n"
                     "only the edge treatment from here. With a named shape,\n"
                     "a connected mask carves it: a fractal there gives a\n"
                     "ragged coast, a slope mask keeps only the flats.";
      add_vec2(n.attrs, "size", "Size", 0.84f, 0.84f, 0.01f, 2.f, "Shape")
          .tooltip = "Width and height across, as a fraction of the tile. 1\n"
                     "touches the borders; less leaves ground around it.";
      add_vec2(n.attrs, "center", "Centre", 0.5f, 0.5f, -0.5f, 1.5f, "Shape")
          .tooltip = "Where the shape sits on the tile. Outside 0..1 pushes\n"
                     "it off the edge, which is how you get a coast rather\n"
                     "than an island.";
      add_float(n.attrs, "rotation", "Rotation", 0.f, -180.f, 180.f, "Shape")
          .tooltip = "Turns the shape. Meaningless for a circle, and the whole\n"
                     "point for a stretched one.";
      add_float(n.attrs, "corner", "Corner rounding", 0.3f, 0.f, 1.f, "Shape")
          .tooltip = "Rounded rectangle only: how much of the half-size the\n"
                     "corners round off. 1 is a full stadium.";

      add_float(n.attrs, "edge_amount", "Edge wander", 0.25f, 0.f, 1.f, "Rim")
          .tooltip = "How far the rim departs from the perfect curve, as a\n"
                     "fraction of the radius. 0 is a drawing-board outline,\n"
                     "which is the one thing no real coast has.";
      add_float(n.attrs, "edge_scale", "Edge detail", 5.f, 0.2f, 64.f, "Rim")
          .tooltip = "How many bays and headlands there are around the rim.";
      add_int(n.attrs, "edge_octaves", "Edge roughness", 4, 1, 10, "Rim")
          .tooltip = "How much finer detail rides on the large bays.";
      add_seed(n.attrs, "seed", "Seed", 0, "Rim");

      // The three questions the old single slider ran together.
      add_float(n.attrs, "extent", "Blend extent", 0.3f, 0.001f, 1.f, "Edge blend")
          .tooltip = "How far in from the rim the blend reaches, as a\n"
                     "fraction of the shape's own radius - so it means the\n"
                     "same on a big island and a small one. 1 blends from\n"
                     "the very centre.";
      add_float(n.attrs, "gradient", "Blend gradient", 1.f, 0.05f, 8.f,
                "Edge blend")
          .tooltip = "The curve from the centre out to the sides. Below 1\n"
                     "the ground stays high and drops away near the rim -\n"
                     "a plateau with a cliff. Above 1 it starts falling\n"
                     "from well inside - a beach. 1 is the plain S-curve.";
      add_float(n.attrs, "intensity", "Blend intensity", 1.f, 0.f, 1.f,
                "Edge blend")
          .tooltip = "How far down the edge actually goes. 1 takes it all\n"
                     "the way to the base level; less leaves the rim\n"
                     "standing proud of it.";

      add_choice(n.attrs, "base", "Base level",
                 {"Lowest in the terrain", "Zero", "Set below"}, 0, "Outside")
          .tooltip = "What the ground outside the shape falls to.";
      add_float(n.attrs, "base_level", "Level", 0.f, -1.f, 2.f, "Outside")
          .tooltip = "Used when Base level is 'Set below'.";
      add_bool(n.attrs, "keep_relief", "Keep relief outside", false, "Outside")
          .tooltip = "On, the terrain outside keeps its shape and is only\n"
                     "pulled down toward the base - an island on a seabed\n"
                     "that still has hills. Off, outside is flat.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &msk = n.out_hmap("mask");
      out = *in;
      msk = *in;
      const shape::Params p = read_shape(n);
      const Heightmap *shape_mask = n.in_hmap("mask");
      if (p.kind == shape::Mask && !shape_mask) {
        n.error = "shape is 'From mask' but the mask input is not connected";
        return;
      }
      float mn, mx;
      in->minmax(mn, mx);
      const int mode = n.attrs.get_choice("base");
      const float base = mode == 0   ? mn
                         : mode == 1 ? 0.f
                                     : n.attrs.get_f("base_level", 0.f);
      const bool keep = n.attrs.get_b("keep_relief", false);
      const int w = out.w, h = out.h;
      const float inv_w = w > 1 ? 1.f / (w - 1) : 1.f;
      const float inv_h = h > 1 ? 1.f / (h - 1) : 1.f;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            const float u = x * inv_w, v = y * inv_h;
            float a;
            if (p.kind == shape::Mask) {
              // The supplied mask is the region; the edge controls still
              // apply to it, read as how far inside the point is.
              const float m = shape_mask->at(
                  std::clamp(x * shape_mask->w / std::max(w, 1), 0,
                             shape_mask->w - 1),
                  std::clamp(y * shape_mask->h / std::max(h, 1), 0,
                             shape_mask->h - 1));
              a = 1.f - std::clamp(p.intensity, 0.f, 1.f) *
                            (1.f - shape::falloff(p, m));
            } else {
              a = shape::presence(p, u, v);
              // A mask connected beside a named shape used to be ignored,
              // which read as "plugging a fractal in does nothing". It
              // carves the shape now: where the mask is 0 the shape is
              // gone, where it is 1 the shape decides.
              if (shape_mask) {
                const float m = shape_mask->at(
                    std::clamp(x * shape_mask->w / std::max(w, 1), 0, shape_mask->w - 1),
                    std::clamp(y * shape_mask->h / std::max(h, 1), 0, shape_mask->h - 1));
                a *= std::clamp(m, 0.f, 1.f);
              }
            }
            const size_t i = (size_t)y * w + x;
            msk.v[i] = a;
            const float target = keep ? base + (in->v[i] - mn) * (1.f - a) : base;
            out.v[i] = target + (in->v[i] - target) * a;
          }
      });
    })

} // namespace gpx
