// Geekatplay TerraForge - the distribution pipeline as Points nodes, one
// stage each, for a graph built from pieces rather than from the
// EcosystemLayer that bundles them: ScatterArea places by rate and
// presence, PointsInteract reacts to another cloud and resolves overlap,
// PointsTransform decides species, size, turn and lean. Chain them and you
// have the layer; swap one for something of your own and you have a
// population the layer cannot make.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/points.hpp"
#include "gpx/scatter.hpp"
#include "scatter_attrs.hpp"
#include <algorithm>

namespace gpx {

REGISTER_NODE(
    ScatterArea, "Points",
    "Scatter by density per hectare and the presence of the ground",
    [](Node &n) {
      n.add_in("presence", DataType::Heightmap, true);
      n.add_in("terrain", DataType::Heightmap, true);
      n.add_in("objects", DataType::Heightmap, true);
      n.add_out("points", DataType::Points);
      add_bool(n.attrs, "invert_mask", "Invert presence", false, "Density");
      eco::declare_density(n);
      eco::declare_presence(n);
    },
    [](Node &n) {
      PointCloud &pts = n.out_points("points");
      pts.clear();
      float rate = 1.f;
      scatter::CandidateParams cp = eco::read_candidates(n, rate);
      scatter::Presence pr = eco::read_presence(n, n.in_hmap("presence"), n.in_hmap("terrain"),
                                                n.in_hmap("objects"));
      scatter::candidates(cp, pts);
      scatter::filter(pts, pr, rate);
    })

REGISTER_NODE(
    PointsInteract, "Points",
    "Attract to, repel from another cloud; keep instances from overlapping",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_in("below", DataType::Points, true);
      n.add_out("points", DataType::Points);
      eco::declare_interaction(n);
      add_float(n.attrs, "size_m", "Terrain size (m)", 5000.f, 1.f, 1000000.f, "Interaction", true);
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      PointCloud &out = n.out_points("points");
      out.clear();
      if (!in) return;
      out = *in;
      out.ensure_attrs();
      const float size_m = n.attrs.get_f("size_m", 5000.f);
      scatter::Interaction it = eco::read_interaction(n, n.in_points("below"), size_m);
      // a cloud that never met a transform has no footprint; give it the
      // radius a footprint of 2 m at this size would, so overlap means something
      bool any = false;
      for (float r : out.radius) if (r > 0.f) { any = true; break; }
      if (!any) for (float &r : out.radius) r = 2.f / std::max(size_m, 1.f);
      scatter::interact(out, it);
    })

REGISTER_NODE(
    PointsTransform, "Points",
    "Species, size, rotation, lean and tint per instance",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_in("driver", DataType::Heightmap, true);
      n.add_out("points", DataType::Points);
      eco::declare_transform(n);
      add_float(n.attrs, "size_m", "Terrain size (m)", 5000.f, 1.f, 1000000.f, "Scaling", true);
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      PointCloud &out = n.out_points("points");
      out.clear();
      if (!in) return;
      out = *in;
      scatter::Transform tr = eco::read_transform(n, n.in_hmap("driver"), n.attrs.get_f("size_m", 5000.f));
      scatter::transform(out, tr);
    })

} // namespace gpx
