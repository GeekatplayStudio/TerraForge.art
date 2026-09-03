// Geekatplay TerraForge — Vue-class fractal nodes (Reference Manual p850-884).
//
// Fractal is Vue's Simple / Grainy / Variable Roughness / Fast Perlin fractal
// in one node, grouped the way the manual groups them: Noise, Scale,
// Fractal, Variation, Distortion, Filter, Output. TerrainFractal adds the
// landscape type (plain / ridges / billows / mixes), ridge smoothness and
// bump surge. Both give the second output every Vue fractal has: Rough
// areas, the local roughness, to drive material distribution. The maths is
// gpx/fractal_core.hpp; these declare parameters and run it per pixel.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/fractal_core.hpp"
#include "gpx/parallel.hpp"

namespace gpx {

namespace {

// the parameter groups every fractal shares, in the manual's order
void setup_fractal_common(Node &n, bool terrain) {
  n.add_in("envelope", DataType::Heightmap, true);
  n.add_in("distortion map", DataType::Heightmap, true);
  n.add_out("output");
  n.add_out("rough_areas");
  add_choice(n.attrs, "base", "Base noise",
             {"Perlin", "Value", "Cellular (F1)", "Cell edges", "Grainy"}, 0, "Noise")
      .tooltip = "The pattern repeated at every harmonic. Grainy keeps\n"
                 "detail at all frequencies (colour and bump work).";
  add_bool(n.attrs, "rotate", "With rotation", true, "Noise")
      .tooltip = "Rotate the noise between harmonics, so the lattice's\n"
                 "directions never line up.";
  add_bool(n.attrs, "double_noise", "Double noise", false, "Noise")
      .tooltip = "A second, offset noise multiplied in: richer variation,\n"
                 "about twice the cost.";
  add_float(n.attrs, "steepness", "Filter steepness", 1.f, 0.2f, 4.f, "Noise")
      .tooltip = "Contrast of the base noise itself.";
  add_seed(n.attrs, "seed", "Seed", 0, "Noise");
  add_float(n.attrs, "wavelength", "Wavelength", 0.25f, 0.005f, 4.f, "Scale")
      .tooltip = "Size of the largest feature, as a fraction of the tile.";
  add_vec2(n.attrs, "stretch", "Stretch X / Y", 1.f, 1.f, 0.1f, 10.f, "Scale");
  add_float(n.attrs, "stretch_damping", "Stretch damping", 0.5f, 0.f, 1.f, "Scale")
      .tooltip = "Less stretch on the finer harmonics, so the whole pattern\n"
                 "does not read as smeared.";
  add_int(n.attrs, "octaves", "Iterations", 8, 1, 16, "Fractal");
  add_float(n.attrs, "scale_ratio", "Scale ratio", 0.5f, 0.1f, 0.9f, "Fractal")
      .tooltip = "Wavelength ratio between iterations. 0.5 is classic;\n"
                 "above favours the large forms, below the fine detail.";
  add_float(n.attrs, "amp_ratio", "Amplitude ratio", 0.5f, 0.05f, 0.95f, "Fractal")
      .tooltip = "Amplitude ratio between iterations.";
  add_float(n.attrs, "roughness", "Roughness", 1.f, 0.f, 2.f, "Fractal")
      .tooltip = "Scales the amplitude ratio: more roughness, more detail.";
  add_float(n.attrs, "gain", "Gain", 1.f, 0.2f, 10.f, "Fractal")
      .tooltip = "Contrast of the result.";
  add_choice(n.attrs, "combine", "Combination mode",
             {"Add", "Blend", "Variable roughness", "Variable roughness (abs)",
              "Max", "Max (abs)", "Min", "Min (abs)", "Multiply"},
             0, "Fractal")
      .tooltip = "How the iterations are put together (manual p866-870).";
  add_float(n.attrs, "smooth_level", "Smooth level", 0.f, -1.f, 1.f, "Variation")
      .tooltip = "Altitude of least roughness; roughness grows with the\n"
                 "distance from it (Variable Roughness Fractal).";
  add_float(n.attrs, "influence", "Influence", 0.f, 0.f, 1.f, "Variation")
      .tooltip = "0 behaves exactly like a simple fractal.";
  add_float(n.attrs, "local_influence", "Local influence", 0.f, 0.f, 1.f, "Variation")
      .tooltip = "0: keyed on the first iteration's altitude. 1: on the\n"
                 "last iteration's, giving local patches of smoothness.";
  add_float(n.attrs, "variation_strength", "Variation strength", 0.f, 0.f, 1.f,
            "Variation")
      .tooltip = "Grainy fractal: how much the grain varies over the map.";
  add_float(n.attrs, "variation_roughness", "Variation roughness", 0.5f, 0.05f, 2.f,
            "Variation");
  add_float(n.attrs, "smooth_altitude", "Smooth area altitude", 0.f, -1.f, 1.f,
            "Variation");
  if (terrain) {
    add_choice(n.attrs, "landscape", "Noise / landscape type",
               {"Plain noise", "Ridges", "Billows", "Ridge mix",
                "Billow-ridge mix"},
               1, "Landscape");
    add_float(n.attrs, "blend", "Blend", 0.5f, 0.f, 1.f, "Landscape")
        .tooltip = "Mixed types only: weight of the second shape.";
    add_float(n.attrs, "ridge_smooth", "Ridge smoothness", 0.2f, 0.f, 1.f,
              "Landscape")
        .tooltip = "Rounding of the ridges / billows; not for plain noise.";
    add_float(n.attrs, "bump_surge", "Bump surge", 0.f, -1.f, 1.f, "Landscape")
        .tooltip = "Bumpy areas rise above (+) or sink below (-) the average.";
  }
  add_float(n.attrs, "distortion", "Distortion", 0.f, 0.f, 1.f, "Distortion")
      .tooltip = "Smears the pattern around, as if pushed by a random flow.";
  add_float(n.attrs, "distortion_scale", "Distortion scale", 1.f, 0.1f, 8.f,
            "Distortion");
  add_float(n.attrs, "distortion_map", "Distortion map strength", 0.f, 0.f, 1.f,
            "Distortion")
      .tooltip = "The 'distortion map' input, when wired, warps the\n"
                 "coordinates by this much.";
  add_choice(n.attrs, "profile", "Filter",
             {"None", "Terraces", "Soft clip", "S-curve", "Plateau", "Valleys"},
             0, "Filter")
      .tooltip = "A profile applied to the altitudes (Vue's filter curve).";
  add_float(n.attrs, "profile_steps", "Terrace steps", 6.f, 2.f, 40.f, "Filter");
  add_float(n.attrs, "creep_in", "Creep-in", 0.f, 0.f, 1.f, "Filter")
      .tooltip = "How much of the unfiltered signal is mixed back.";
  add_range(n.attrs, "filter_range", "Filter range", 0.f, 1.f, 0.f, 1.f, "Filter")
      .tooltip = "The part of the full range the filter acts on.";
  add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 4.f, "Output");
  add_float(n.attrs, "offset", "Offset", 0.f, -1.f, 1.f, "Output");
  add_float(n.attrs, "rough_ref", "Rough areas: ref. feature size", 0.f, 0.f, 1.f,
            "Output")
      .tooltip = "Harmonics finer than this (fraction of the tile) count\n"
                 "as roughness. 0 counts them all.";
  setup_post(n);
}

fractal::Params read_common(const Node &n, bool terrain) {
  fractal::Params P;
  P.base = n.attrs.get_choice("base");
  P.rotate = n.attrs.get_b("rotate", true);
  P.double_noise = n.attrs.get_b("double_noise");
  P.filter_steepness = n.attrs.get_f("steepness", 1.f);
  P.wavelength = n.attrs.get_f("wavelength", 0.25f);
  n.attrs.get_vec2("stretch", P.stretch_x, P.stretch_y);
  P.stretch_damping = n.attrs.get_f("stretch_damping", 0.5f);
  P.octaves = n.attrs.get_i("octaves", 8);
  P.scale_ratio = n.attrs.get_f("scale_ratio", 0.5f);
  P.amp_ratio = n.attrs.get_f("amp_ratio", 0.5f);
  P.roughness = n.attrs.get_f("roughness", 1.f);
  P.gain = n.attrs.get_f("gain", 1.f);
  P.combine = n.attrs.get_choice("combine");
  P.smooth_level = n.attrs.get_f("smooth_level", 0.f);
  P.influence = n.attrs.get_f("influence", 0.f);
  P.local_influence = n.attrs.get_f("local_influence", 0.f);
  P.variation_strength = n.attrs.get_f("variation_strength", 0.f);
  P.variation_roughness = n.attrs.get_f("variation_roughness", 0.5f);
  P.smooth_altitude = n.attrs.get_f("smooth_altitude", 0.f);
  if (terrain) {
    P.landscape = n.attrs.get_choice("landscape");
    P.blend = n.attrs.get_f("blend", 0.5f);
    P.ridge_smooth = n.attrs.get_f("ridge_smooth", 0.2f);
    P.bump_surge = n.attrs.get_f("bump_surge", 0.f);
  }
  P.distortion = n.attrs.get_f("distortion", 0.f);
  P.distortion_scale = n.attrs.get_f("distortion_scale", 1.f);
  P.profile = n.attrs.get_choice("profile");
  P.profile_steps = n.attrs.get_f("profile_steps", 6.f);
  P.creep_in = n.attrs.get_f("creep_in", 0.f);
  n.attrs.get_range("filter_range", P.filter_min, P.filter_max);
  P.amplitude = n.attrs.get_f("amplitude", 1.f);
  P.offset = n.attrs.get_f("offset", 0.f);
  P.rough_ref = n.attrs.get_f("rough_ref", 0.f);
  return P;
}

// run a fractal over the tile: altitude to `output`, roughness to
// `rough_areas`, envelope and distortion map honoured
void run_fractal(Node &n, const fractal::Params &P) {
  Heightmap &out = n.out_hmap("output");
  Heightmap &rough = n.out_hmap("rough_areas");
  const Heightmap *env = n.in_hmap("envelope");
  const Heightmap *dmap = n.in_hmap("distortion map");
  const float dstr = n.attrs.get_f("distortion_map", 0.f);
  const uint32_t seed = n.attrs.get_seed("seed");
  const int w = out.w, h = out.h;
  parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        float u = x / float(w), v = y / float(h);
        if (dmap && !dmap->empty() && dstr > 0.f) {
          float d = dmap->atc(x, y) - 0.5f;
          u += d * dstr * 0.5f;
          v -= d * dstr * 0.5f;
        }
        float r = 0.f;
        float a = fractal::eval(u, v, seed, P, &r);
        if (env && !env->empty()) a *= std::clamp(env->atc(x, y), 0.f, 1.f);
        out.at(x, y) = a;
        rough.at(x, y) = r;
      }
  });
  apply_post(n, out);
}

} // namespace

// NoiseFractal, not Fractal: that name belongs to the diamond-square /
// fault-line node in nodes_primitives.cpp, and the census forbids renames.
REGISTER_NODE(
    NoiseFractal, "Primitive",
    "Vue-class fractal: base noise over harmonics with stretch, combination modes, variable roughness, distortion and a filter profile; second output is the local roughness",
    [](Node &n) { setup_fractal_common(n, false); },
    [](Node &n) { run_fractal(n, read_common(n, false)); })

REGISTER_NODE(
    TerrainFractal, "Primitive",
    "Vue's Terrain Fractal: the fractal with a landscape type (plain, ridges, billows, mixes), ridge smoothness and bump surge; rough areas on the second output",
    [](Node &n) { setup_fractal_common(n, true); },
    [](Node &n) { run_fractal(n, read_common(n, true)); })

} // namespace gpx
