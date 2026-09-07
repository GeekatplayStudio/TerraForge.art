// Geekatplay TerraForge - Lake: a body of water put somewhere, rather than a
// level applied everywhere.
//
// We already had `Flood` ("everywhere below this height") and `FillBasins`
// ("fill every hollow to its outlet"). Neither can answer "a lake, there,
// about that big", which is the thing an author actually wants, and which
// Terragen answers with its Lake object: a water disc with a Centre, a Max
// radius and a Water level, attached to a planet (docs.planetside.co.uk,
// Lake - Transform tab).
//
// Terragen's is always round and always flat, and stops there: the shore is
// wherever the disc happens to cut the ground. That is the part worth going
// past, and the part the request asked for. Here the rim can wander, the lake
// can conform to the ground it sits in rather than clipping through it, the
// bed can be carved so the water is not a sheet laid on a hillside, and the
// waterline comes out as its own mask so a beach can be shaded along it.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/shapes.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

// The tile's width in metres, so a radius is authored in metres and survives
// a change of terrain size. The studio keeps `size_m` in step with the
// project, as it does for every other metric node.
float tile_m(const Node &n) {
  return std::max(n.attrs.get_f("size_m", 5000.f), 1e-3f);
}

shape::Params read_shape(const Node &n) {
  shape::Params p;
  p.kind = std::clamp(n.attrs.get_choice("shape"), 0, (int)shape::KIND_COUNT - 1);
  n.attrs.get_vec2("center", p.cx, p.cy);
  // Terragen calls it Max radius, and so do we: the rim wanders inside it.
  const float r = std::max(n.attrs.get_f("radius_m", 400.f), 1e-3f) / tile_m(n);
  p.rx = r;
  p.ry = r * std::max(n.attrs.get_f("aspect", 1.f), 1e-3f);
  p.rotation = n.attrs.get_f("rotation", 0.f);
  p.corner = 0.3f;
  p.edge_amount = n.attrs.get_f("edge_amount", 0.3f);
  p.edge_scale = n.attrs.get_f("edge_scale", 4.f);
  p.edge_octaves = n.attrs.get_i("edge_octaves", 4);
  p.seed = n.attrs.get_seed("seed");
  // The shore band is the blend: how far in from the rim the water gives way.
  p.extent = std::max(n.attrs.get_f("shore", 0.12f), 1e-4f);
  p.gradient = n.attrs.get_f("shore_gradient", 1.f);
  p.intensity = 1.f;
  return p;
}

} // namespace

REGISTER_NODE(
    Lake, "Hydrology",
    "A body of water at a place: centre, max radius and water level, with a wandering shore, a carved bed and a beach mask",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");     // the terrain, with the bed carved and banked
      n.add_out("water");      // the water surface, terrain where it is dry
      n.add_out("depth");      // how deep the water is
      n.add_out("mask");       // where there is water
      n.add_out("shore");      // the waterline band, for a beach material

      // ---- Terragen's own three, under its own names
      add_float(n.attrs, "level", "Water level", 0.32f, 0.f, 1.f, "Lake")
          .tooltip = "The height of the water surface, as a fraction of the\n"
                     "terrain's own range. Terragen states this as a height\n"
                     "above the planet; here it follows the terrain so a\n"
                     "lake stays put when the relief is re-scaled.";
      add_vec2(n.attrs, "center", "Centre", 0.5f, 0.5f, -0.5f, 1.5f, "Lake")
          .tooltip = "Where the lake sits on the tile.";
      add_float(n.attrs, "radius_m", "Max radius (m)", 400.f, 0.5f, 200000.f,
                "Lake", true)
          .tooltip = "The furthest the water can reach from the centre, in\n"
                     "metres. The wandering rim moves inside this, never\n"
                     "past it - which is exactly what Terragen's Max radius\n"
                     "means.";
      add_float(n.attrs, "aspect", "Stretch", 1.f, 0.05f, 20.f, "Lake")
          .tooltip = "1 is round, which is the only shape Terragen's Lake\n"
                     "can be. Higher stretches it one way, so a lake can\n"
                     "lie along a valley.";
      add_float(n.attrs, "rotation", "Rotation", 0.f, -180.f, 180.f, "Lake");
      add_choice(n.attrs, "shape", "Outline",
                 {"Rectangle", "Rounded rectangle", "Round", "Diamond",
                  "From mask"},
                 2, "Lake")
          .tooltip = "From mask takes the outline from the mask input, so a\n"
                     "lake can be traced from a real one.";

      // ---- the rim, which is the part Terragen leaves as a circle
      add_float(n.attrs, "edge_amount", "Shore wander", 0.3f, 0.f, 1.f, "Shore")
          .tooltip = "How far the waterline departs from the perfect curve,\n"
                     "as a fraction of the radius. This is what makes bays\n"
                     "and spits; 0 gives the drawing-board circle.";
      add_float(n.attrs, "edge_scale", "Shore detail", 4.f, 0.2f, 64.f, "Shore")
          .tooltip = "How many bays and headlands around the shore.";
      add_int(n.attrs, "edge_octaves", "Shore roughness", 4, 1, 10, "Shore")
          .tooltip = "How much finer detail rides on the large bays.";
      add_float(n.attrs, "shore", "Shore width", 0.12f, 0.001f, 1.f, "Shore")
          .tooltip = "How far in from the rim the water shallows, as a\n"
                     "fraction of the lake's radius. This is the band the\n"
                     "'shore' output marks.";
      add_float(n.attrs, "shore_gradient", "Shore gradient", 1.f, 0.05f, 8.f,
                "Shore")
          .tooltip = "The profile from the middle out to the shore. Below 1\n"
                     "the water stays deep and shallows abruptly - a tarn\n"
                     "in a rock basin. Above 1 it shallows from far out -\n"
                     "a wide beach.";
      add_seed(n.attrs, "seed", "Seed", 0, "Shore");

      // ---- the bed, which is what stops the water being a sheet on a hill
      add_bool(n.attrs, "conform", "Follow the ground", true, "Bed")
          .tooltip = "On, the lake only fills where the ground is already\n"
                     "below the water level, so it settles into the valley\n"
                     "it is in. Off, it is a flat disc that ignores the\n"
                     "terrain, which is what Terragen's Lake object is.";
      add_float(n.attrs, "carve", "Carve the bed", 0.35f, 0.f, 1.f, "Bed")
          .tooltip = "Pulls the ground under the lake down below the water,\n"
                     "deepest in the middle. 0 leaves the terrain alone and\n"
                     "the water may be a film over it.";
      add_float(n.attrs, "bank", "Bank the shore", 0.5f, 0.f, 1.f, "Bed")
          .tooltip = "Levels the ground just outside the waterline toward\n"
                     "the water, so the lake meets a shore rather than a\n"
                     "wall. This is the thing that reads as a lake.";
      add_float(n.attrs, "size_m", "Terrain size (m)", 5000.f, 1.f, 1000000.f,
                "Lake", true)
          .tooltip = "The tile's width; the studio keeps this in step with\n"
                     "the project so the radius above means metres.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &water = n.out_hmap("water");
      Heightmap &depth = n.out_hmap("depth");
      Heightmap &msk = n.out_hmap("mask");
      Heightmap &shore = n.out_hmap("shore");
      out = *in;
      water = *in;
      depth = *in;
      msk = *in;
      shore = *in;

      const shape::Params p = read_shape(n);
      const Heightmap *shape_mask = n.in_hmap("mask");
      if (p.kind == shape::Mask && !shape_mask) {
        n.error = "outline is 'From mask' but the mask input is not connected";
        return;
      }
      float mn, mx;
      in->minmax(mn, mx);
      const float span = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      const float level = mn + n.attrs.get_f("level", 0.32f) * span;
      const bool conform = n.attrs.get_b("conform", true);
      const float carve = n.attrs.get_f("carve", 0.35f) * span;
      const float bank = std::clamp(n.attrs.get_f("bank", 0.5f), 0.f, 1.f);
      const int w = out.w, h = out.h;
      const float inv_w = w > 1 ? 1.f / (w - 1) : 1.f;
      const float inv_h = h > 1 ? 1.f / (h - 1) : 1.f;

      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            const float u = x * inv_w, v = y * inv_h;
            // how far inside the outline this point is, 1 in the middle of
            // the lake and 0 on the rim
            float a;
            if (p.kind == shape::Mask) {
              const float m = shape_mask->at(
                  std::clamp(x * shape_mask->w / std::max(w, 1), 0,
                             shape_mask->w - 1),
                  std::clamp(y * shape_mask->h / std::max(h, 1), 0,
                             shape_mask->h - 1));
              a = shape::falloff(p, m);
            } else {
              a = shape::falloff(p, shape::inside(p, u, v));
            }
            const float ground = in->v[i];
            // The bed: pulled down under the lake, deepest where the water
            // is furthest from its shore, so the bottom is a bowl and not a
            // step. Banking pulls the ground just outside the rim toward
            // the water, which is what turns a cut-off disc into a shore.
            float g = ground - carve * a;
            if (bank > 0.f && a > 0.f && a < 1.f)
              g += (level - g) * bank * (1.f - a) * a * 2.f;
            // Where the water actually is. Conforming means the ground has
            // to be under the surface as well as inside the outline, which
            // is what settles a lake into the valley it is in rather than
            // laying it across a hillside.
            const float wet = conform ? (g < level ? a : 0.f) : a;
            const float d = wet > 0.f ? std::max(level - g, 0.f) * wet : 0.f;
            out.v[i] = g;
            water.v[i] = wet > 0.f ? level : g;
            depth.v[i] = d;
            // The mask is where the water *surface* is, which is not the same
            // as where it is deep. A flat disc still has a surface over the
            // rock that pokes through it - that is what makes it a disc - so
            // keying the mask on depth would have made "follow the ground"
            // and "ignore it" produce the same picture.
            msk.v[i] = wet;
            // The waterline: strongest at the rim and gone in the middle,
            // which is where a beach is. Gated on there being water at all
            // rather than scaled by how much - `wet` already falls to zero
            // at the rim to soften the water's edge, so multiplying by it
            // would have capped this band at a quarter and put its peak in
            // the wrong place.
            shore.v[i] = wet > 0.f ? std::clamp(1.f - a, 0.f, 1.f) : 0.f;
          }
      });
    })

} // namespace gpx
