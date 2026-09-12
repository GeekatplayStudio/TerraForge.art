// Geekatplay TerraForge - a band of air, and as many of them as a sky needs.
//
// One fog is one band: a density, a height it lies at, and how quickly it
// thins above that. Real air is never one band. There is haze out to the
// horizon, a fog lying in the valley bottom that its ridges stand clear of, a
// brown layer over a town, a clear gap, and a sheet of mist above that catches
// the last of the sun. A single set of numbers can be any one of those and
// never two at once, which is why a scene built with one fog always looks
// like a scene with one fog in it.
//
// Every FogLayer node in the graph is a band, and the sky and every surface
// see all of them at once. They compose the way air does: each contributes an
// optical depth along the ray, the total is the sum - which is exact, since
// absorption through mixed media adds - and the colour is each layer's,
// weighted by how much of the total it accounts for. A thin haze in front of
// a thick fog therefore reads as the fog, and neither hides the other.
//
// Like CloudLayer, a node counts whether or not it is wired: the wire is the
// order they are listed in, not a switch.
#include "gpx/node_graph.hpp"

namespace gpx {

REGISTER_NODE(
    FogLayer, "Atmosphere", "A band of air: haze, fog or pollution at its own height",
    [](Node &n) {
      n.add_in("fog", DataType::Heightmap, true);
      n.add_out("fog");
      add_bool(n.attrs, "enabled", "Enabled", true, "Layer")
          .tooltip = "Turns the layer off without losing its settings.";
      add_choice(n.attrs, "type", "Type", {"Off", "Haze", "Fog", "Pollution"}, 2, "Layer")
          .tooltip = "What the air is. Haze is thin and blue and reaches to the\n"
                     "horizon; fog is dense and white and lies low; pollution is\n"
                     "fog with the blue taken out of it, so it browns what is\n"
                     "seen through it.";
      add_float(n.attrs, "density", "Density", 0.6f, 0.f, 6.f, "Layer")
          .tooltip = "How thick the air is. Distance does the rest: thin air\n"
                     "still closes a far horizon.";
      add_float(n.attrs, "level", "Height", 0.2f, -1.f, 4.f, "Layer")
          .tooltip = "The height the band sits at, in the same units as the\n"
                     "terrain: 0 is the ground, 1 the top of its range. A fog\n"
                     "at 0.15 fills the valleys and leaves the ridges standing\n"
                     "out of it.";
      add_float(n.attrs, "falloff", "Falloff", 6.f, 0.1f, 40.f, "Layer")
          .tooltip = "How sharply the band thins above its height. High values\n"
                     "give a fog with a definite top you can look down on;\n"
                     "low ones give a haze that fades out gradually.";
      add_float(n.attrs, "color_r", "Color R", 0.62f, 0.f, 1.f, "Color")
          .tooltip = "The red of the light this air scatters, linear rather\n"
                     "than sRGB.";
      add_float(n.attrs, "color_g", "Color G", 0.68f, 0.f, 1.f, "Color")
          .tooltip = "The green of the light this air scatters.";
      add_float(n.attrs, "color_b", "Color B", 0.76f, 0.f, 1.f, "Color")
          .tooltip = "The blue of the light this air scatters. More blue than\n"
                     "red reads as distance; the other way round reads as dust.";
      add_float(n.attrs, "sun_scatter", "Sun scattering", 0.5f, 0.f, 1.f, "Light")
          .tooltip = "How much the sun lights the air from within, so looking\n"
                     "toward it through the band glares and away from it does\n"
                     "not.";
      add_float(n.attrs, "albedo", "Albedo", 0.85f, 0.f, 1.f, "Light")
          .tooltip = "How much of the light this air scatters rather than\n"
                     "swallows. Water droplets scatter nearly all of it; smoke\n"
                     "absorbs, and darkens what is behind it.";
      add_float(n.attrs, "anisotropy", "Anisotropy", 0.55f, -0.95f, 0.95f, "Light")
          .tooltip = "Which way the air throws light. Positive scatters it\n"
                     "onward, so the sun glares through; 0 scatters evenly.";
      add_float(n.attrs, "drift", "Drift with the wind", 0.6f, 0.f, 2.f, "Wind")
          .tooltip = "How fast this band travels with the scene's wind, as a\n"
                     "share of the wind's own speed. Air near the ground is\n"
                     "held back by it; a high sheet runs with it.";
    },
    [](Node &n) {
      // The layer is settings, like CloudLayer: the studio reads it off the
      // node every evaluation (studio/scene_nodes.cpp). The pass-through
      // keeps a chain of them wireable in order.
      if (const Heightmap *in = n.in_hmap("fog")) n.out_hmap("fog") = *in;
    })

} // namespace gpx
