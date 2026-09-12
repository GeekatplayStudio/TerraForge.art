// Geekatplay TerraForge - the dials of a population, declared once.
//
// EcosystemLayer carries all of them; the Points-family nodes (ScatterArea,
// PointsInteract, PointsTransform) each carry one stage's worth, so a graph
// can be built from the pieces. Declaring and reading them here keeps the
// two spellings identical: an attribute renamed in one place is renamed in
// the other, and a macro recorded against the layer replays on the nodes.
#pragma once
#include "gpx/node_graph.hpp"
#include "gpx/scatter.hpp"
#include <algorithm>

namespace gpx::eco {

// ---------------------------------------------------------------- presence
inline void declare_presence(Node &n) {
  add_float(n.attrs, "threshold", "Presence threshold", 0.05f, 0.f, 1.f, "Density")
      .tooltip = "Presence below this places nothing at all.";
  add_float(n.attrs, "slope_influence", "Slope influence", 0.5f, 0.f, 1.f, "Density")
      .tooltip = "1: instances thin out on steep ground. 0: the same density\n"
                 "whatever the slope.";
  add_bool(n.attrs, "use_altitude", "By altitude", false, "Altitude")
      .tooltip = "Limits the layer to a band of heights.";
  add_choice(n.attrs, "altitude_mode", "Range of altitudes",
             {"By terrain", "Absolute", "Relative to sea"}, 0, "Altitude")
      .tooltip = "Whether the altitude band is read against the terrain's\n"
                 "own range, in absolute height units, or from sea level.";
  add_range(n.attrs, "altitude", "Altitude band", 0.f, 1.f, 0.f, 1.f, "Altitude")
      .tooltip = "As a fraction of the terrain's own height range.";
  add_float(n.attrs, "sea_level", "Sea level", 0.f, 0.f, 1.f, "Altitude")
      .tooltip = "The height that counts as sea level, when the altitude\n"
                 "band is measured from it rather than from the terrain's\n"
                 "own range.";
  add_float(n.attrs, "altitude_fuzz", "Fade", 0.08f, 0.f, 0.5f, "Altitude")
      .tooltip = "How gradually the layer gives out at the edges of its\n"
                 "altitude band. Zero draws a contour line across the\n"
                 "hillside.";
  add_bool(n.attrs, "use_slope", "By slope", false, "Slope")
      .tooltip = "Limits the layer to a band of steepness.";
  add_range(n.attrs, "slope", "Slope band", 0.f, 30.f, 0.f, 90.f, "Slope")
      .tooltip = "Degrees from horizontal. 0 is flat, 90 is a cliff.";
  add_float(n.attrs, "slope_fuzz", "Fade", 6.f, 0.f, 45.f, "Slope")
      .tooltip = "How gradually the layer gives out at the edges of its\n"
                 "slope band.";
  add_bool(n.attrs, "use_orientation", "By orientation", false, "Orientation")
      .tooltip = "Limits the layer to slopes facing a particular way.";
  add_float(n.attrs, "orientation", "Faces", 0.f, 0.f, 360.f, "Orientation")
      .tooltip = "Compass direction the surface looks towards. 0 is north.";
  add_float(n.attrs, "orient_width", "Arc", 60.f, 5.f, 180.f, "Orientation")
      .tooltip = "How wide an arc of facings counts as the favoured\n"
                 "direction. Narrow puts moss on the north face alone; wide\n"
                 "covers most of the hill.";
  add_float(n.attrs, "orient_fuzz", "Fade", 20.f, 0.f, 90.f, "Orientation")
      .tooltip = "How gradually the layer gives out as a slope turns away\n"
                 "from the favoured direction.";
  add_float(n.attrs, "height_scale", "Terrain height scale", 1.f, 0.001f, 100.f, "Slope", true)
      .tooltip = "World height of a heightmap unit as a fraction of the tile's\n"
                 "width; the studio keeps this in step with the project so a\n"
                 "slope in degrees is the slope the viewport shows.";
  add_float(n.attrs, "decay_influence", "Decay near objects", 0.f, 0.f, 1.f, "Objects")
      .tooltip = "Thins the population around the objects standing on the\n"
                 "terrain (the 'objects' input: 0 at an object, 1 far away).\n"
                 "1 leaves a void right at them.";
  add_float(n.attrs, "decay_reach", "Reach", 0.05f, 0.001f, 0.5f, "Objects")
      .tooltip = "How far from the objects the decay extends, as a fraction\n"
                 "of the terrain.";
  add_float(n.attrs, "decay_falloff", "Falloff", 0.f, -1.f, 1.f, "Objects")
      .tooltip = "0 linear. Positive: the void is larger and more sudden.\n"
                 "Negative: gentler.";
}

inline scatter::Presence read_presence(const Node &n, const Heightmap *mask,
                                       const Heightmap *terrain,
                                       const Heightmap *distance) {
  scatter::Presence p;
  p.mask = mask && !mask->empty() ? mask : nullptr;
  p.terrain = terrain && !terrain->empty() ? terrain : nullptr;
  p.distance = distance && !distance->empty() ? distance : nullptr;
  p.invert_mask = n.attrs.get_b("invert_mask", false);
  p.threshold = n.attrs.get_f("threshold", 0.05f);
  p.slope_influence = n.attrs.get_f("slope_influence", 0.5f);
  p.use_altitude = n.attrs.get_b("use_altitude", false);
  p.altitude_mode = n.attrs.get_choice("altitude_mode");
  n.attrs.get_range("altitude", p.alt_lo, p.alt_hi);
  p.alt_fuzz = n.attrs.get_f("altitude_fuzz", 0.08f);
  p.sea = n.attrs.get_f("sea_level", 0.f);
  p.use_slope = n.attrs.get_b("use_slope", false);
  n.attrs.get_range("slope", p.slope_lo, p.slope_hi);
  p.slope_fuzz = n.attrs.get_f("slope_fuzz", 6.f);
  p.height_scale = n.attrs.get_f("height_scale", 1.f);
  p.use_orientation = n.attrs.get_b("use_orientation", false);
  p.orient = n.attrs.get_f("orientation", 0.f);
  p.orient_width = n.attrs.get_f("orient_width", 60.f);
  p.orient_fuzz = n.attrs.get_f("orient_fuzz", 20.f);
  p.decay_influence = n.attrs.get_f("decay_influence", 0.f);
  p.decay_reach = n.attrs.get_f("decay_reach", 0.05f);
  p.decay_falloff = n.attrs.get_f("decay_falloff", 0.f);
  return p;
}

// ------------------------------------------------------------- interaction
inline void declare_interaction(Node &n) {
  add_float(n.attrs, "affinity", "Affinity with layer below", 0.f, -1.f, 1.f, "Interaction")
      .tooltip = "Positive: instances gather around the instances of the\n"
                 "layer below (primroses around the trees) and thin out\n"
                 "elsewhere. Negative: everywhere except near them.";
  add_float(n.attrs, "affinity_radius_m", "Affinity radius (m)", 25.f, 0.1f, 2000.f, "Interaction", true)
      .tooltip = "How far this population reaches to gather around the one\n"
                 "below it, in metres.";
  add_float(n.attrs, "repulsion", "Repulsion from layer below", 0.f, -1.f, 1.f, "Interaction")
      .tooltip = "Sudden. Positive: a void around each instance below (no\n"
                 "grass under the canopy). Negative: only inside that void\n"
                 "(small stones at the foot of the boulder). Use both: near\n"
                 "the trees but not under them.";
  add_float(n.attrs, "repulsion_radius_m", "Repulsion radius (m)", 8.f, 0.1f, 2000.f, "Interaction", true)
      .tooltip = "How far this population is pushed back from the one below\n"
                 "it, in metres - the bare ring around the base of a tree.";
  add_bool(n.attrs, "avoid_overlap", "Avoid overlapping instances", true, "Interaction")
      .tooltip = "No two instances closer than their footprints allow.";
}

inline scatter::Interaction read_interaction(const Node &n, const PointCloud *below,
                                             float size_m) {
  scatter::Interaction it;
  it.below = below && below->size() ? below : nullptr;
  const float inv = 1.f / std::max(size_m, 1.f);
  it.affinity = n.attrs.get_f("affinity", 0.f);
  it.affinity_radius = n.attrs.get_f("affinity_radius_m", 25.f) * inv;
  it.repulsion = n.attrs.get_f("repulsion", 0.f);
  it.repulsion_radius = n.attrs.get_f("repulsion_radius_m", 8.f) * inv;
  it.avoid_overlap = n.attrs.get_b("avoid_overlap", true);
  return it;
}

// --------------------------------------------------------------- transform
inline void declare_transform(Node &n) {
  // What this population stands for, in the ecology's own vocabulary
  // (gpx/ecology.hpp): "boulder", "conifer", "moss". It is what lets a thing
  // added later be placed correctly without anybody restating the rules - a
  // new moss model is assigned to the moss group and lands wherever moss
  // belongs, in every biome that has moss in it, including ones written
  // before that model existed.
  add_text(n.attrs, "eco_group", "Ecology group", "", "Population")
      .tooltip = "The group this population stands for: boulder, conifer,\n"
                 "moss, grass... The rules about where a thing goes are\n"
                 "written about groups, not about models, so anything\n"
                 "assigned to a group is placed by those rules.";
  add_int(n.attrs, "species", "Species", 1, 1, 8, "Population")
      .tooltip = "How many kinds of object this layer places. Each scene\n"
                 "object bound to the layer picks the species it stands for.";
  for (int s = 1; s <= 8; ++s) {
    std::string k = "sp" + std::to_string(s);
    add_float(n.attrs, k + "_presence", "Species " + std::to_string(s) + " presence", 1.f, 0.f, 1.f, "Population")
        .tooltip = "Relative to the other species: raising every presence\n"
                   "places no more instances.";
    add_float(n.attrs, k + "_scale", "Species " + std::to_string(s) + " scale", 1.f, 0.05f, 10.f, "Population", true)
        .tooltip = "This species' size, as a multiple of the layer's own\n"
                   "overall scaling. A population of one mesh at several\n"
                   "sizes reads as a stand of different ages; every copy\n"
                   "identical reads as instancing.";
    // Each kind varies in its own way. A stand of boulders is all sizes; the
    // saplings under it are much of a muchness. One figure for the whole
    // layer cannot say both, so each kind carries its own - and 0 takes the
    // layer's, which is what every population had before this.
    add_float(n.attrs, k + "_variation", "Species " + std::to_string(s) + " size variation", 0.f, 0.f, 1.f,
              "Population")
        .tooltip = "How much this kind varies in size about its own scale: 0\n"
                   "takes the layer's overall variation, 1 ranges from half\n"
                   "to twice. Rocks vary far more than nursery trees do.";
    add_float(n.attrs, k + "_lean", "Species " + std::to_string(s) + " lean to slope", -1.f, -1.f, 1.f,
              "Population")
        .tooltip = "How far this kind tips with the ground it stands on: 0\n"
                   "grows straight up whatever the slope, 1 lies flat along\n"
                   "it. A boulder sits on the hillside; a tree stands up out\n"
                   "of it. Below 0 takes the layer's own setting.";
  }
  add_float(n.attrs, "scale", "Overall scaling", 1.f, 0.05f, 10.f, "Scaling", true)
      .tooltip = "The size of one copy, as a multiple of the mesh's own\n"
                 "size.";
  add_float(n.attrs, "variation", "Size variation", 0.3f, 0.f, 1.f, "Scaling")
      .tooltip = "1: instances range from half to twice the size.";
  add_float(n.attrs, "keep_proportions", "Keep proportions", 1.f, 0.f, 1.f, "Scaling")
      .tooltip = "1: the three axes scale together. 0: each on its own.";
  add_float(n.attrs, "direction", "Direction from surface", 0.f, 0.f, 1.f, "Scaling")
      .tooltip = "0: instances grow vertically whatever the slope.\n"
                 "1: perpendicular to the ground (rocks); trees want 0.";
  add_choice(n.attrs, "rotation", "Rotation", {"Up axis", "None", "Driven"}, 0, "Scaling")
      .tooltip = "How far a copy may be turned about its up axis. Full\n"
                 "rotation is right for anything without a front; less keeps\n"
                 "a set of objects aligned.";
  add_float(n.attrs, "rotation_max", "Maximum angle", 1.f, 0.f, 1.f, "Scaling")
      .tooltip = "As a fraction of a half turn either way.";
  add_float(n.attrs, "offset_m", "Offset from surface (m)", 0.f, -50.f, 50.f, "Scaling")
      .tooltip = "Negative buries the instance.";
  add_float(n.attrs, "footprint_m", "Footprint radius (m)", 2.f, 0.01f, 500.f, "Scaling", true)
      .tooltip = "The ground one instance claims at scale 1; what overlap\n"
                 "avoidance and the layer above measure against.";
  add_float(n.attrs, "shrink", "Shrink at low density", 0.f, -1.f, 1.f, "Scaling")
      .tooltip = "Lone instances are smaller (negative: larger), as at the\n"
                 "edge of a wood.";
  add_float(n.attrs, "shrink_radius_m", "Low-density radius (m)", 30.f, 0.1f, 2000.f, "Scaling", true)
      .tooltip = "How close to the population below a copy must be before it\n"
                 "is made smaller, in metres. This is what puts stunted\n"
                 "growth under a canopy rather than an abrupt edge.";
  add_float(n.attrs, "lean", "Lean out at low density", 0.f, 0.f, 1.f, "Scaling")
      .tooltip = "Lone instances lean into the slope, as plants reaching\n"
                 "for light.";
  add_float(n.attrs, "color_variation", "Color variation", 0.3f, 0.f, 1.f, "Color")
      .tooltip = "How much copies differ in brightness from one another.\n"
                 "Identical tint across a whole population is the second\n"
                 "clearest sign of instancing, after identical size.";
  add_float(n.attrs, "phase_range", "Time offset range (s)", 1.f, 0.f, 10.f, "Animation")
      .tooltip = "Each instance's wind phase is shifted by up to this, so a\n"
                 "field sways as a crowd, not a marching army.";
}

inline scatter::Transform read_transform(const Node &n, const Heightmap *driver,
                                         float size_m) {
  scatter::Transform t;
  const float inv = 1.f / std::max(size_m, 1.f);
  t.species_count = n.attrs.get_i("species", 1);
  for (int s = 0; s < 8; ++s) {
    std::string k = "sp" + std::to_string(s + 1);
    t.weights[s] = n.attrs.get_f(k + "_presence", 1.f);
    t.species_scale[s] = n.attrs.get_f(k + "_scale", 1.f);
    t.species_variation[s] = n.attrs.get_f(k + "_variation", 0.f);
    t.species_lean[s] = n.attrs.get_f(k + "_lean", -1.f);
  }
  t.driver = driver && !driver->empty() ? driver : nullptr;
  t.scale = n.attrs.get_f("scale", 1.f);
  t.variation = n.attrs.get_f("variation", 0.3f);
  t.keep_proportions = n.attrs.get_f("keep_proportions", 1.f);
  t.direction = n.attrs.get_f("direction", 0.f);
  t.rotation = n.attrs.get_choice("rotation");
  t.rotation_max = n.attrs.get_f("rotation_max", 1.f);
  t.offset = n.attrs.get_f("offset_m", 0.f) * inv;
  t.radius = n.attrs.get_f("footprint_m", 2.f) * inv;
  t.shrink = n.attrs.get_f("shrink", 0.f);
  t.shrink_radius = n.attrs.get_f("shrink_radius_m", 30.f) * inv;
  t.lean = n.attrs.get_f("lean", 0.f);
  t.color_variation = n.attrs.get_f("color_variation", 0.3f);
  t.phase_range = n.attrs.get_f("phase_range", 1.f);
  return t;
}

// ----------------------------------------------------------------- density
inline void declare_density(Node &n) {
  add_float(n.attrs, "density", "Density (per hectare)", 20.f, 0.01f, 100000.f, "Density", true)
      .tooltip = "Instances per hectare (100 m x 100 m) at full presence.\n"
                 "A rate, not a count: the same setting fills a 1 km tile\n"
                 "and a 20 km one to the same look.";
  add_float(n.attrs, "spacing_m", "Minimum spacing (m)", 5.f, 0.05f, 1000.f, "Density", true)
      .tooltip = "The lattice the candidates stand on. Changing the density\n"
                 "never moves an instance; changing this reseeds them all.";
  add_choice(n.attrs, "placement", "Placement", {"Jittered", "Random", "Regular"}, 0, "Density")
      .tooltip = "How the candidate positions are laid out before anything\n"
                 "is rejected. A jittered lattice covers ground evenly;\n"
                 "purely random leaves clumps and bald patches, which is\n"
                 "sometimes what you want.";
  add_float(n.attrs, "clump_amount", "Clumping", 0.f, 0.f, 1.f, "Density")
      .tooltip = "Groups instances together as species do in nature.";
  add_float(n.attrs, "clump_size_m", "Clump size (m)", 60.f, 0.5f, 5000.f, "Density", true)
      .tooltip = "How far across one clump of plants is, in metres.\n"
                 "Vegetation gathers where the ground suits it, and a\n"
                 "population spread perfectly evenly is the clearest sign of\n"
                 "a generated one.";
  add_seed(n.attrs, "seed", "Seed", 0, "Density");
  add_bool(n.attrs, "unbounded", "Populate around the camera", false, "Density")
      .tooltip = "Off: the population covers the terrain tile, computed once.\n"
                 "On: it covers the ground wherever the camera goes - a planet\n"
                 "or an infinite terrain has no tile to cover - generated in\n"
                 "cells on demand and thrown away behind you. What a cell\n"
                 "holds never depends on where it was seen from.";
  add_float(n.attrs, "population_m", "Populate within (m)", 2000.f, 10.f, 200000.f,
            "Density", true)
      .tooltip = "How far from the camera the ground is populated when\n"
                 "'Populate around the camera' is on.";
  add_float(n.attrs, "size_m", "Terrain size (m)", 5000.f, 1.f, 1000000.f, "Density", true)
      .tooltip = "The tile's width; the studio keeps this in step with the\n"
                 "project so the rate above means what it says.";
}

// The candidate lattice and the accepted fraction that together give the
// asked-for rate. `target` is what full presence everywhere would place.
inline scatter::CandidateParams read_candidates(const Node &n, float &rate,
                                                size_t max_instances = 400000) {
  scatter::CandidateParams c;
  const float size_m = std::max(n.attrs.get_f("size_m", 5000.f), 1.f);
  const float inv = 1.f / size_m;
  c.seed = n.attrs.get_seed("seed");
  c.spacing = n.attrs.get_f("spacing_m", 5.f) * inv;
  c.mode = n.attrs.get_choice("placement");
  c.clump_amount = n.attrs.get_f("clump_amount", 0.f);
  c.clump_size = n.attrs.get_f("clump_size_m", 60.f) * inv;
  const double hectares = (double)size_m * size_m / 10000.0;
  double target = std::min((double)n.attrs.get_f("density", 20.f) * hectares, (double)max_instances);
  const int ln = scatter::lattice_n(c);
  const double cells = (double)ln * ln;
  // twice the candidates the target needs, so a mask that halves the
  // presence still reaches the rate where it is present; capped per cell
  c.per_cell = std::clamp((int)std::ceil(target * 2.0 / cells), 1, 64);
  rate = (float)std::min(target / (cells * c.per_cell), 1.0);
  return c;
}

} // namespace gpx::eco
