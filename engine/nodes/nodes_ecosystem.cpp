// Geekatplay TerraForge - EcosystemLayer: a material layer that populates.
//
// Vue's EcoSystem material (manual p1087-1109), as a layer in the material
// stack: it shades nothing itself, but where its presence rule says the
// layer is, it places instances - a rate per hectare, thinned by slope,
// altitude, orientation and the objects standing on the ground, gathered
// into clumps, and in a stack, attracted to or repelled from the instances
// of the ecosystem layer beneath it. Stack an ecosystem of grass on top of
// an ecosystem of trees, set repulsion, and the grass keeps out from under
// the canopy; stack pebbles over boulders with negative repulsion and the
// pebbles collect at the boulders' feet.
//
// Every dial is in metres because the graph knows the tile's width
// (`size_m`, kept in step by the studio), so a population authored on one
// terrain means the same thing on another.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/points.hpp"
#include "gpx/scatter.hpp"
#include "scatter_attrs.hpp"
#include <algorithm>

namespace gpx {

REGISTER_NODE(
    EcosystemLayer, "Material",
    "Ecosystem layer: a population placed by the layer's presence, reacting to the layer below",
    [](Node &n) {
      // a member of the material stack: the layers below pass through
      // untouched, so an ecosystem sits between colour layers like any
      // other layer and the Material Studio's Up/Down/Remove work on it
      n.add_in("below albedo", DataType::Texture, true);
      n.add_in("below normal", DataType::Texture, true);
      n.add_in("below rough", DataType::Texture, true);
      n.add_in("mask", DataType::Heightmap, true);
      n.add_in("terrain", DataType::Heightmap, true);
      n.add_in("below", DataType::Points, true);
      n.add_in("objects", DataType::Heightmap, true);
      n.add_in("driver", DataType::Heightmap, true);
      n.add_out("albedo", DataType::Texture);
      n.add_out("normal", DataType::Texture);
      n.add_out("roughness", DataType::Texture);
      n.add_out("presence", DataType::Heightmap);
      n.add_out("points", DataType::Points);
      n.add_out("density", DataType::Heightmap);

      add_text(n.attrs, "name", "Name", "Ecosystem", "Layer")
          .tooltip = "What this population is called in the stack and the\n"
                     "Objects tree.";
      add_bool(n.attrs, "enabled", "Populate", true, "Layer")
          .tooltip = "Turns the layer off without removing it or losing its\n"
                     "settings.";
      add_bool(n.attrs, "invert_mask", "Invert presence", false, "Layer")
          .tooltip = "Uses the mask the other way round: the layer appears where\n"
                     "the mask is dark.";
      eco::declare_density(n);
      eco::declare_presence(n);
      eco::declare_interaction(n);
      eco::declare_transform(n);
    },
    [](Node &n) {
      // the stack passes straight through: this layer shades nothing itself
      struct Pass { const char *in, *out; };
      for (const Pass &p : {Pass{"below albedo", "albedo"}, Pass{"below normal", "normal"},
                            Pass{"below rough", "roughness"}}) {
        TextureRGBA &out = n.out_tex(p.out);
        if (const TextureRGBA *in = n.in_tex(p.in); in && !in->empty()) out = *in;
        else out = TextureRGBA();
      }
      const Heightmap *pres = n.in_hmap("mask");
      const Heightmap *terrain = n.in_hmap("terrain");
      const Heightmap *objects = n.in_hmap("objects");
      const Heightmap *driver = n.in_hmap("driver");
      const PointCloud *below = n.in_points("below");
      Heightmap &pout = n.out_hmap("presence");
      if (pres && !pres->empty()) pout = *pres;
      PointCloud &pts = n.out_points("points");
      pts.clear();
      if (!n.attrs.get_b("enabled", true)) return;

      // Populated around the camera instead of over the tile: the studio
      // realises the cells the camera can see (studio/eco_dynamic.cpp) and
      // the node holds only the rule. It emits nothing here on purpose -
      // there is no tile for a planet's surface to be a picture of.
      if (n.attrs.get_b("unbounded", false)) {
        n.out_hmap("density") = Heightmap();
        return;
      }
      const float size_m = std::max(n.attrs.get_f("size_m", 5000.f), 1.f);
      float rate = 1.f;
      scatter::CandidateParams cp = eco::read_candidates(n, rate);
      scatter::Presence pr = eco::read_presence(n, pres, terrain, objects);
      scatter::Interaction it = eco::read_interaction(n, below, size_m);
      scatter::Transform tr = eco::read_transform(n, driver, size_m);

      scatter::candidates(cp, pts);
      scatter::filter(pts, pr, rate);
      // the transform first: overlap avoidance needs each instance's
      // footprint, which its size decides
      scatter::transform(pts, tr);
      scatter::interact(pts, it);

      // the density map: the presence rule as a raster, so the Material
      // Studio can show where the population goes before it is placed
      Heightmap &den = n.out_hmap("density");
      const int res = 256;
      den = Heightmap(res, res);
      parallel_rows(res, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < res; ++x)
            den.at(x, y) = scatter::presence_at(pr, (x + 0.5f) / res, (y + 0.5f) / res);
      });
    })

} // namespace gpx
